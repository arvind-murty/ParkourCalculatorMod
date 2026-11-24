"""Answer an alarmed relaxed optimum with Clarabel alone (docs/reference/NUMERICAL_SOLVER.md#post-alarm-pipeline).

The relaxed optimum leaves a jump tick short, so it may be no route at full strength. A penalty push moves the relaxed
point toward full strength under the request's own rows; a full-strength point that the scalar validator accepts
answers the request, and the first such answer ends the search. Without one, a sector check
may prove the request infeasible, reversal cuts look for a route that turns one tick around, and a push whose objective
weight decays comes last. Every constant was measured on 371 study and 482 Real11 alarmed requests
(research/post-alarm/README.md).
"""
import heapq
import itertools
import math
import time

from parkour_solver.numerics import disk

G = 1e-2              # an answer this close to its node's bound stops the push and needs no probe
W0, RAMP, W_MAX, MOMENTUM = .3, 1.2, 1e3, .5
FULL = 1e-6           # a tick this little short is full strength
PUSH_CAP = 10
QUADRANT_BUDGET = 7   # Clarabel solves of the sector check
QUADRANT_FULL = 1e-7  # a sector solution this little short ends the check open
LEVEL_STEP, LEVEL_PROBES, PROBE_CAP = 1e-2, 3, 5
RACE_WINDOW, RACE_CUT, RACE_STEPS, RACE_FINISH_CAP = 6, .5, 5, 30
SHORT = 1e-3          # a tick this short is a reversal candidate whatever its kind
DECAY_CAP = 30
TERMINATIONS = dict(push='post_alarm_push', probe='post_alarm_probe', race='post_alarm_race',
                    decay='post_alarm_decay')


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
    def __init__(self, problem, relaxed_candidate, relaxed, seconds, cancel_event):
        self.problem, self.relaxed_candidate, self.relaxed = problem, relaxed_candidate, relaxed
        self.cancel_event = cancel_event
        self.started = time.monotonic()
        self.deadline = self.started + seconds
        self.sense = 1 if problem['objective']['sense'] == 'MAX' else -1
        self.content = disk.spec(problem, strict=True)
        self.exact = dict(self.content, magnitude=[m-disk.ALLOWANCE for m in self.content['magnitude']])
        self.clarabel = 0
        # The best answer so far, which a deadline does not discard.
        self.best = None
        self.info = dict(method='none', pushSteps=0, probes=0, reversalTick=None, raced=0, quadrant=None)

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
        """The validated candidate of a full-strength point, else None."""
        from parkour_solver.validation.numerical import validate
        if disk.deficits(self.problem, controls)[0] >= FULL:
            return None
        candidate = validate(self.problem, facings(self.problem, controls))
        return candidate if candidate['feasible'] else None

    def push(self, node, start, cap, decaying=False, state=None):
        """(best accepted or None, last iterate, steps, state): the penalty push from `start`, or continuing an earlier
        push's `state`, stopped within G of the node's bound, after a gain under G/10 over 3 steps, or at the cap.
        `decaying` halves w after every step short of full strength until the first full-strength one: at W0 the
        objective can hold ticks short indefinitely."""
        y, w, last_full, best, history = state or (start, W0, None, None, [])
        history, u, steps = list(history), None, 0
        for _ in range(cap):
            point = node.step(self, y, w)
            if point is None:
                break
            u, steps = point, steps+1
            accepted = self.accept(u)
            if accepted is not None and (best is None or self.better(accepted['objective'], best['objective'])):
                best = accepted
            history.append(None if best is None else best['objective'])
            if best is not None and self.sense*(node.bound-best['objective']) <= G:
                break
            if (best is not None and len(history) > 3 and history[-4] is not None
                    and self.sense*(best['objective']-history[-4]) < G/10):
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
                elif decaying:
                    w = w/2
                y = u
        return best, u, steps, (y, w, last_full, best, history)

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
        bound. The level row holds the objective, so the probe push decays."""
        while self.info['probes'] < LEVEL_PROBES and self.sense*(plain.bound-best['objective']) > G:
            self.info['probes'] += 1
            level = (self.content['objective'], best['objective']+self.sense*LEVEL_STEP)
            node = _Node(self, self.exact, level=level)
            if not node.feasible:
                break
            found, _, _, _ = self.push(node, node.central, PROBE_CAP, decaying=True)
            if found is None or not self.better(found['objective'], best['objective']):
                break
            best = self.best = found
            self.info['method'] = 'probe'
        return best

    def race(self, plain):
        """Reversal race: each jump tick near an alarm tick, and the disk point's most short tick whatever its kind,
        turned against the disk point's direction. Candidates are screened with RACE_STEPS push steps in order of
        their cut node's bound until no remaining bound can beat the answer; only when no screen answers does the
        best-bound candidate's push go on, to RACE_FINISH_CAP steps."""
        ticks = self.problem['ticks']
        alarms = self.relaxed['alarmTicks']
        jumps = (self.problem.get('vertical') or {}).get('jumpVelocity') or [None]*ticks
        candidates = {t for a in alarms for t in range(max(0, a-RACE_WINDOW), min(ticks, a+RACE_WINDOW+1))
                      if jumps[t] is not None}
        shortfall = disk.deficits(self.problem, plain.disk)[1]
        top = max(range(ticks), key=shortfall.__getitem__)
        if shortfall[top] >= SHORT:
            candidates.add(top)
        nodes = []
        for t in sorted(candidates):
            direction = math.atan2(plain.disk[t][1], plain.disk[t][0])+math.pi
            cut = (t, math.cos(direction), math.sin(direction), RACE_CUT*self.problem['magnitude'][t])
            node = _Node(self, self.exact, [cut])
            if node.feasible:
                nodes.append((t, node))
        nodes.sort(key=lambda row: -self.sense*row[1].bound)
        best, first = None, None
        for i, (t, node) in enumerate(nodes):
            found, _, _, state = self.push(node, node.central, RACE_STEPS)
            self.info['raced'] += 1
            first = first or state
            if found is not None and (best is None or self.better(found['objective'], best['objective'])):
                best = self.best = found
                self.info.update(method='race', reversalTick=t)
            if best is not None and (i+1 == len(nodes) or self.sense*(best['objective']-nodes[i+1][1].bound) >= 0):
                break
        if best is None and nodes:
            t, node = nodes[0]
            found, _, _, _ = self.push(node, None, RACE_FINISH_CAP-RACE_STEPS, state=first)
            if found is not None:
                best = self.best = found
                self.info.update(method='race', reversalTick=t)
        return best

    def answer(self):
        """The best accepted candidate or None, and the termination."""
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
        best, _, self.info['pushSteps'], _ = self.push(plain, start, PUSH_CAP)
        if best is not None:
            self.best = best
            self.info['method'] = 'push'
            best = self.probes(plain, best)
            return best, TERMINATIONS[self.info['method']]
        self.info['quadrant'] = self.quadrant()
        if self.info['quadrant'] == 'infeasible':
            self.info['method'] = 'quadrant'
            return None, 'quadrant_infeasible'
        best = self.race(plain)
        if best is not None:
            # The race answer is held below its cut node's bound; the probes are not.
            best = self.probes(plain, best)
            return best, TERMINATIONS[self.info['method']]
        # A restart, not the first push's state: continuing a stalled push answers fewer requests.
        best, _, _, _ = self.push(plain, plain.central, DECAY_CAP, decaying=True)
        if best is not None:
            self.best = best
            self.info['method'] = 'decay'
            return best, TERMINATIONS['decay']
        return None, 'post_alarm_none'


def run(problem, relaxed_candidate, relaxed, *, seconds, cancel_event):
    """A `numerics.solver.solve`-shaped result for an alarmed request with `confirmation`; `relaxed` is the
    dispatch's disk record including the disk solve's own `objective` (the bound) and `alarmTicks`."""
    pipeline = _Pipeline(problem, relaxed_candidate, relaxed, seconds, cancel_event)
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
    result = disk._result(problem, pipeline.started, status, best, termination,
                          relaxed['status'], relaxed)
    # search.engine.numerical_counts counts a confirmation without ipoptCalls as one IPOPT call.
    confirmation = dict(peak=relaxed['peakRelativeDeficit'], tick=relaxed['peakTick'], cached=False,
                        diskObjective=relaxed_candidate['objective'], ipoptObjective=None, gap=None, material=False,
                        **pipeline.info, ipoptCalls=0, clarabelSolves=pipeline.clarabel)
    if best is not None:
        gap = pipeline.sense*(relaxed_candidate['objective']-best['objective'])
        confirmation.update(ipoptObjective=best['objective'], gap=gap, material=gap > disk.MATERIAL_GAP)
    result['confirmation'] = confirmation
    return result


def facings(problem, controls):
    """Facings (degrees) of acceleration vectors."""
    return [math.degrees(math.atan2(c[1], c[0])-b) for c, b in zip(controls, problem['base'])]


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
