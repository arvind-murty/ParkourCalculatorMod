"""Answer an alarmed relaxed optimum with Clarabel first and IPOPT last (docs/reference/NUMERICAL_SOLVER.md#post-alarm-pipeline).

The relaxed optimum leaves a jump tick short, so it may be no route at full strength. A penalty push moves the relaxed
point toward full strength under the request's own rows; a point every tick of which is at most 1% short and that the
scalar validator accepts answers the request. Without one, a sector check may prove the request infeasible, reversal
cuts look for the other basin, and two IPOPT calls on a kept-alive worker come last. Every constant was measured on 371
alarmed requests (research/post-alarm/README.md).
"""
import heapq
import itertools
import math
import time

from parkour_solver.numerics import disk

G = 1e-2              # an answer this close to its node's bound stops the push and needs no probe
W0, RAMP, W_MAX, MOMENTUM = .3, 1.2, 1e3, .5
FULL = 1e-6           # a tick this little short is full strength
ACCEPT_SHORT = .01    # a relaxed answer's largest tick shortfall
PUSH_CAP = 10
QUADRANT_BUDGET = 7   # Clarabel solves of the sector check
QUADRANT_FULL = 1e-7  # a sector solution this little short ends the check open
LEVEL_STEP, LEVEL_PROBES, PROBE_CAP = 1e-2, 3, 5
RACE_WINDOW, RACE_CUT, RACE_STEPS, RACE_FINISH, RACE_FINISH_CAP = 6, .5, 5, 2, 30
IPOPT_CAP = .2        # seconds of one IPOPT call
POLISH_MU = 1e-6
TERMINATIONS = dict(push='post_alarm_push', probe='post_alarm_probe', race='post_alarm_race',
                    prodSeed='post_alarm_prod_seed', polish='post_alarm_polish')


class _Stop(Exception):
    """The deadline passed or the caller cancelled."""


class _Node:
    """A Clarabel problem the push steps re-solve with new linear costs: the request's rows plus cuts and a level row."""
    def __init__(self, pipeline, content, cuts=(), level=None, disk_point=None):
        self.feasible = False
        central, _ = pipeline.decide({k: v for k, v in content.items() if k != 'objective'}, cuts, level)
        if central['outcome'] != 'feasible':
            return
        self.central = central['controls']
        if disk_point is not None:
            # The request's own disk solve already gave the bound and its point (setup reuse).
            self.bound, self.disk = disk_point
            try:
                self.built = disk._build(content, disk.CAP, disk.MAX_ITER)
            except (ValueError, RuntimeError):
                return
        else:
            result, self.built = pipeline.decide(content, cuts, level)
            if result['outcome'] != 'feasible':
                return
            self.bound, self.disk = result['objective'], result['controls']
        self.feasible = True

    def step(self, pipeline, y, w):
        """Minimizes w*cost - sum_t y_t.u_t; the point whatever the status when finite (the validator judges it)."""
        import numpy as np
        pipeline.check()
        q = w*self.built['cost']
        for target, columns in zip(y, self.built['columns']):
            for value, column in zip(target, columns):
                q[column] -= value
        self.built['solver'].update(q=q)
        pipeline.clarabel += 1
        try:
            x = np.asarray(self.built['solver'].solve().x)
        except (ValueError, RuntimeError):
            return None
        controls = [[float(x[i]) for i in columns] for columns in self.built['columns']]
        return controls if all(math.isfinite(v) for c in controls for v in c) else None


class _Pipeline:
    def __init__(self, problem, relaxed_candidate, relaxed, seconds, python_executable, cancel_event):
        self.problem, self.relaxed_candidate, self.relaxed = problem, relaxed_candidate, relaxed
        self.python_executable, self.cancel_event = python_executable, cancel_event
        self.started = time.monotonic()
        self.deadline = self.started + seconds
        self.sense = 1 if problem['objective']['sense'] == 'MAX' else -1
        self.content = disk.spec(problem, strict=True)
        self.exact = dict(self.content, magnitude=[m-disk.ALLOWANCE for m in self.content['magnitude']])
        self.clarabel = self.ipopt = 0
        self.launched, self.ipopt_error, self.ipopt_status, self.phases = False, None, None, []
        # The best answer so far, which a deadline does not discard.
        self.best = None
        self.info = dict(method='none', acceptedAs=None, pushSteps=0, probes=0, reversalTick=None, raced=0,
                         quadrant=None)

    def check(self):
        if self.cancel_event is not None and self.cancel_event.is_set():
            raise _Stop('cancelled')
        if time.monotonic() >= self.deadline:
            raise _Stop('time_limit')

    def decide(self, content, cuts=(), level=None):
        self.check()
        result, built = disk.decide(content, self.deadline-time.monotonic(), cuts, level)
        self.clarabel += 1+result.get('retried', False)
        if result['status'] in disk.LIMIT:
            # A limit the deadline cut short is no verdict.
            self.check()
        return result, built

    def better(self, value, other):
        return other is None or self.sense*value > self.sense*other

    def accept(self, controls):
        """(kind, candidate) for an acceptable point: full strength passing `validate`, else every tick at most
        ACCEPT_SHORT short passing `validate_controls`."""
        from parkour_solver.validation.numerical import validate, validate_controls
        peak = disk.deficits(self.problem, controls)[0]
        if peak < FULL:
            candidate = validate(self.problem, facings(self.problem, controls))
            if candidate['feasible']:
                return 'full', candidate
        if peak <= ACCEPT_SHORT:
            candidate = validate_controls(self.problem, controls)
            if candidate['feasible']:
                candidate['model'] = 'relaxed_disk'
                return 'relaxed', candidate
        return None

    def push(self, node, start, cap):
        """(best accepted or None, last iterate, steps): the penalty push from `start`, stopped within G of the node's
        bound, after a gain under G/10 over 3 steps, or at the cap."""
        y, w, last_full, best, history, u, steps = start, W0, None, None, [], None, 0
        for _ in range(cap):
            point = node.step(self, y, w)
            if point is None:
                break
            u, steps = point, steps+1
            accepted = self.accept(u)
            if accepted is not None and (best is None or self.better(accepted[1]['objective'], best[1]['objective'])):
                best = accepted
            history.append(None if best is None else best[1]['objective'])
            if best is not None and self.sense*(node.bound-best[1]['objective']) <= G:
                break
            if (best is not None and len(history) > 3 and history[-4] is not None
                    and self.sense*(best[1]['objective']-history[-4]) < G/10):
                break
            full = disk.deficits(self.problem, u)[0] < FULL
            if last_full is not None:
                if full:
                    w = min(w*RAMP, W_MAX)
                    y = [[a+MOMENTUM*(a-b) for a, b in zip(c, d)] for c, d in zip(u, last_full)]
                    last_full = u
                else:
                    w, y = w/2, last_full
            else:
                if full:
                    last_full = u
                y = u
        return best, u, steps

    def quadrant(self):
        """'infeasible' when every full-strength completion is excluded, else 'open': branch on the worst tick's
        direction (4 quadrants, then halves), each sector relaxed to its chord, diving on the parent's peak shortfall."""
        free = {k: v for k, v in self.content.items() if k != 'objective'}
        magnitude = self.problem['magnitude']
        heap, counter, solves = [(0., 0, {})], itertools.count(1), 0
        while heap:
            if solves >= QUADRANT_BUDGET:
                return 'open'
            _, _, sectors = heapq.heappop(heap)
            result, _ = self.decide(free, sector_cuts(sectors, magnitude))
            solves += 1
            if result['outcome'] != 'feasible':
                continue
            peak, _, tick = disk.deficits(self.problem, result['controls'])
            if peak <= QUADRANT_FULL:
                return 'open'
            for child in sector_children(sectors, tick, result['controls'][tick]):
                heapq.heappush(heap, (peak, next(counter), child))
        return 'infeasible'

    def probes(self, plain, best):
        """Objective-cut probes: push inside objective >= best+LEVEL_STEP (MAX) while best is more than G below the
        bound."""
        while self.info['probes'] < LEVEL_PROBES and self.sense*(plain.bound-best[1]['objective']) > G:
            self.info['probes'] += 1
            level = (self.content['objective'], best[1]['objective']+self.sense*LEVEL_STEP)
            node = _Node(self, self.exact, level=level)
            if not node.feasible:
                break
            found, _, _ = self.push(node, node.central, PROBE_CAP)
            if found is None or not self.better(found[1]['objective'], best[1]['objective']):
                break
            best = self.best = found
            self.info.update(method='probe', acceptedAs=found[0])
        return best

    def race(self, plain):
        """Reversal race: each jump tick near an alarm tick turned against the disk point's direction, 5 push steps
        each, the best 2 finished."""
        from parkour_solver.validation.numerical import validate_controls
        ticks = self.problem['ticks']
        alarms = self.relaxed['alarmTicks']
        jumps = (self.problem.get('vertical') or {}).get('jumpVelocity') or [None]*ticks
        window = sorted({t for a in alarms for t in range(max(0, a-RACE_WINDOW), min(ticks, a+RACE_WINDOW+1))
                         if jumps[t] is not None})
        scored = []
        for t in window:
            direction = math.atan2(plain.disk[t][1], plain.disk[t][0])+math.pi
            cut = (t, math.cos(direction), math.sin(direction), RACE_CUT*self.problem['magnitude'][t])
            node = _Node(self, self.exact, [cut])
            if not node.feasible:
                continue
            found, last, _ = self.push(node, node.central, RACE_STEPS)
            self.info['raced'] += 1
            if found is not None:
                score = (1, self.sense*found[1]['objective'])
            elif last is not None:
                score = (0, self.sense*validate_controls(self.problem, last)['objective'])
            else:
                score = (0, -math.inf)
            scored.append((score, t, node))
        best = None
        for _, t, node in sorted(scored, key=lambda row: row[0], reverse=True)[:RACE_FINISH]:
            found, _, _ = self.push(node, node.central, RACE_FINISH_CAP)
            if found is not None and (best is None or self.better(found[1]['objective'], best[1]['objective'])):
                best = self.best = found
                self.info.update(method='race', acceptedAs=found[0], reversalTick=t)
        return best

    def ipopt_call(self, request):
        """One IPOPT call on the kept-alive worker, budget min(IPOPT_CAP, time left); the parent waits until the
        pipeline deadline for its last messages."""
        from parkour_solver.numerics.solver import solve as full_solve
        self.check()
        left = self.deadline-time.monotonic()
        budget = min(IPOPT_CAP, left)
        request = {k: v for k, v in request.items() if k != 'fallbackYaw'}
        result = full_solve(request, seconds=budget, grace=left-budget, stop_on_feasible=False,
                            python_executable=self.python_executable, cancel_event=self.cancel_event, keep_alive=True)
        self.ipopt += 1
        self.launched |= result['workerLaunched']
        self.ipopt_status = result['workerStatus']
        self.phases.extend(result['phases'])
        if result['status'] == 'cancelled':
            raise _Stop('cancelled')
        if result['status'] == 'feasible':
            return 'full', result['candidate']
        if result['termination'] == 'time_limit':
            raise _Stop('time_limit')
        if result['termination'] != 'worker_finished':
            self.ipopt_error = result['error'] or result['termination']
        return None

    def answer(self):
        """The best accepted (kind, candidate) or None, and the termination."""
        alarmed_point = (self.relaxed['objective'], self.relaxed_candidate['controls'])
        plain = _Node(self, self.exact, disk_point=alarmed_point)
        if not plain.feasible:
            return None, 'post_alarm_none'
        start = plain.central
        warm = self.problem.get('warmYaw')
        if warm:
            # A child starts its shared prefix from the parent's answer at full strength.
            start = [[m*math.cos(b+math.radians(y)), m*math.sin(b+math.radians(y))]
                     for m, b, y in zip(self.problem['magnitude'], self.problem['base'], warm)]+start[len(warm):]
        best, last, self.info['pushSteps'] = self.push(plain, start, PUSH_CAP)
        if best is not None:
            self.best = best
            self.info.update(method='push', acceptedAs=best[0])
            best = self.probes(plain, best)
            return best, TERMINATIONS[self.info['method']]
        self.info['quadrant'] = self.quadrant()
        if self.info['quadrant'] == 'infeasible':
            self.info['method'] = 'quadrant'
            return None, 'quadrant_infeasible'
        best = self.race(plain)
        if best is not None:
            return best, TERMINATIONS['race']
        best = self.ipopt_call(dict(self.problem, yaw=disk.ipopt_seed(self.relaxed_candidate['yaw'])))
        if best is not None:
            self.info.update(method='prodSeed', acceptedAs='full')
            return best, TERMINATIONS['prodSeed']
        if last is not None:
            best = self.ipopt_call(dict(self.problem, yaw=disk.ipopt_seed(facings(self.problem, last)),
                                        stateSeed=states(self.problem, last), ipoptMuInit=POLISH_MU))
            if best is not None:
                self.info.update(method='polish', acceptedAs='full')
                return best, TERMINATIONS['polish']
        return None, 'post_alarm_none'


def run(problem, relaxed_candidate, relaxed, *, seconds, python_executable, cancel_event):
    """A `numerics.solver.solve`-shaped result for an alarmed request with `confirmation`; `relaxed` is the
    dispatch's disk record including the disk solve's own `objective` (the bound) and `alarmTicks`."""
    pipeline = _Pipeline(problem, relaxed_candidate, relaxed, seconds, python_executable, cancel_event)
    best, termination, cancelled = None, None, False
    try:
        best, termination = pipeline.answer()
    except _Stop as stop:
        cancelled = str(stop) == 'cancelled'
        best = pipeline.best
        termination = TERMINATIONS[pipeline.info['method']] if best is not None else str(stop)
    if cancelled:
        best = None
    status = 'cancelled' if cancelled else 'feasible' if best is not None else 'unknown'
    result = disk._result(problem, pipeline.started, status, None if best is None else best[1], termination,
                          relaxed['status'], relaxed)
    ipopt_answer = pipeline.info['method'] in ('prodSeed', 'polish')
    result.update(backend='ipopt' if ipopt_answer else 'clarabel', workerLaunched=pipeline.launched,
                  error=pipeline.ipopt_error, phases=pipeline.phases)
    if ipopt_answer or best is None and pipeline.ipopt:
        result['workerStatus'] = pipeline.ipopt_status
    confirmation = dict(peak=relaxed['peakRelativeDeficit'], tick=relaxed['peakTick'], cached=False,
                        diskObjective=relaxed_candidate['objective'], ipoptObjective=None, gap=None, material=False,
                        **pipeline.info, ipoptCalls=pipeline.ipopt, clarabelSolves=pipeline.clarabel)
    if best is not None:
        gap = pipeline.sense*(relaxed_candidate['objective']-best[1]['objective'])
        confirmation.update(ipoptObjective=best[1]['objective'], gap=gap, material=gap > disk.MATERIAL_GAP)
    result['confirmation'] = confirmation
    return result


def facings(problem, controls):
    """Facings (degrees) of acceleration vectors."""
    return [math.degrees(math.atan2(c[1], c[0])-b) for c, b in zip(controls, problem['base'])]


def states(problem, controls):
    """Exact-dynamics positions and velocities under acceleration vectors, in numerics.worker.forward's frame."""
    ticks = problem['ticks']
    positions, velocities = [[0., 0.]], [list(problem['velocity'])]
    contacts = {(c['state'], c['axis']): c for c in problem['contacts']}
    for t in range(ticks):
        move = [v+a for v, a in zip(velocities[t], controls[t])]
        position = [p+m for p, m in zip(positions[t], move)]
        velocity = [problem['friction'][t]*m for m in move]
        for axis in range(2):
            contact = contacts.get((t+1, axis))
            if contact:
                position[axis] = contact['plane']-problem['position'][axis]
                velocity[axis] = 0.
        positions.append(position)
        velocities.append(velocity)
    return dict(positions=positions, velocities=velocities)


def sector_cuts(sectors, magnitude):
    """Each sector (lo, hi) of tick t as its chord row: a.(cos mid, sin mid) >= m_t cos(width/2)."""
    return [(t, math.cos((lo+hi)/2), math.sin((lo+hi)/2), magnitude[t]*math.cos((hi-lo)/2))
            for t, (lo, hi) in sectors.items()]


def sector_children(sectors, t, controls):
    """Tick t's first split: 4 quadrants with the point's direction at one's centre; later splits halve."""
    if t not in sectors:
        angle = math.atan2(controls[1], controls[0])
        return [{**sectors, t: (angle-math.pi/4+k*math.pi/2, angle+math.pi/4+k*math.pi/2)} for k in range(4)]
    lo, hi = sectors[t]
    return [{**sectors, t: (lo, (lo+hi)/2)}, {**sectors, t: ((lo+hi)/2, hi)}]
