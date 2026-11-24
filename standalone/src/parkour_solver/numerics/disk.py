"""Clarabel solve of the acceleration-disk relaxation of a threshold-0 request.

Each tick's horizontal acceleration ``|a[t]| == magnitude[t]`` becomes
``|a[t]| <= magnitude[t]``. Every other row is the IPOPT worker's: exact
dynamics, fixed starts, contact resets and EQ walls in the zero cone, contact
approach/reach and LE/GE walls as nonnegative rows. A primal-infeasible status
therefore excludes the full request; a feasible point is a relaxed candidate.
Every other status, and a limit hit twice, is also infeasible: the relaxation
never leaves a request undecided.
"""
import collections
import json
import math
import time

from parkour_solver.schema import SOLVER_MARGIN

MODEL_VERSION = 1
ALLOWANCE = 1e-9
CAP = 0.1
MAX_ITER = 200
# A run that hits a limit is solved once more with these; a second limit is infeasible.
RETRY_CAP = 0.5
RETRY_MAX_ITER = 500
TOL = 1e-9
INFEASIBILITY_TOL = 1e-8
ALARM_FRACTION = 0.1
SEED_TILT = 1e-4
MATERIAL_GAP = 1e-4

FEASIBLE = ('Solved', 'AlmostSolved')
LIMIT = ('MaxIterations', 'MaxTime')

STATS = dict(screen=collections.Counter(), candidate=collections.Counter())
_DECIDED = {}


def _native():
    """The native core under PKC_NATIVE_CORE, which solves the relaxation, keeps the decided-result cache and its
    counters (shared with the native gate's screens) and seeds IPOPT; None otherwise.

    The spec and the other small helpers stay here: converting a problem costs more than they do."""
    from parkour_solver import native_core
    return native_core.from_environment()


def spec(problem, extra_rows=(), objective=None, *, strict):
    """Canonical numerical content, or None outside the supported class.

    Screens (strict=False) drop rows the relaxation cannot express, which keeps
    them a relaxation; candidates (strict=True) must keep every row.
    """
    if problem.get('threshold', 0.) != 0 or any(problem.get('ladderTicks', [])) or 'inertiaPattern' in problem:
        return None
    n = problem['ticks']
    rows = []
    for r in list(problem.get('walls', []))+list(extra_rows):
        t, t2 = r.get('t1', r.get('offset')), r.get('t2')
        if (r.get('mode') in ('X', 'Z') and type(t) is int and 0 <= t <= n
                and (t2 is None or type(t2) is int and 0 <= t2 <= n)):
            rows.append((r['mode'], t, t2, r.get('op', 'PLUS'), r['cmp'], r['rhs']))
        elif strict:
            return None
    result = {k: problem[k] for k in ('ticks', 'position', 'velocity', 'magnitude', 'friction')}
    result.update(version=MODEL_VERSION, rows=sorted(set(rows), key=str), contacts=problem.get('contacts', []),
                  initialIntervals=problem.get('initialIntervals'))
    if problem.get('objective') is not None and problem.get('objectiveMode') != 'constant_zero':
        objective = problem['objective']
    elif objective is not None:
        objective = dict(objective, tick=n)
    if objective is not None:
        result['objective'] = {k: objective.get(k) for k in ('tick', 'axis', 'sense', 'customYaw', 'type')}
    return result


def stats():
    """STATS, or under PKC_NATIVE_CORE the native decided cache's counters, which every solve then counts."""
    native = _native()
    if native is None:
        return STATS
    return {kind: collections.Counter(counts) for kind, counts in native.disk_stats().items()}


def solve(content, *, seconds, kind='candidate'):
    """Solve a spec to a feasible or infeasible verdict, cached process-wide by content."""
    native = _native()
    if native is not None:  # the native gate's screens share this cache; the module's limits are read here
        return native.disk_cached_solve(content, seconds, kind, CAP, MAX_ITER, RETRY_CAP, RETRY_MAX_ITER)
    encoded = json.dumps(content, sort_keys=True)
    stats = STATS[kind]
    if encoded in _DECIDED:
        stats['cacheHits'] += 1
        return dict(_DECIDED[encoded], cached=True)
    stats['solved'] += 1
    result, _ = decide(content, seconds)
    if result.get('retried'):
        stats['retried'] += 1
    stats[result['outcome']] += 1
    _DECIDED[encoded] = result
    while len(_DECIDED) > 1024:
        _DECIDED.pop(next(iter(_DECIDED)))
    return dict(result, cached=False)


def decide(content, seconds, cuts=(), level=None):
    """(verdict, built problem): one run, a limit retried once with higher limits, a second limit infeasible.

    `cuts` and `level` add rows (`_build`); the built problem is None after an exception.
    """
    result, built = _attempt(content, min(CAP, seconds), MAX_ITER, cuts, level)
    if result['outcome'] == 'limit':
        result, built = _attempt(content, min(RETRY_CAP, seconds), RETRY_MAX_ITER, cuts, level)
        result['retried'] = True
        if result['outcome'] == 'limit':
            result['outcome'] = 'infeasible'
    return result, built


def screen(model, extra_rows=(), objective=None):
    """Interval-screen refinement: None when not applicable, else a verdict."""
    content = spec(model, extra_rows, objective, strict=False)
    if content is None or not content['rows'] and not content['contacts']:
        return None
    result = solve(content, seconds=RETRY_CAP, kind='screen')
    checked = dict(outcome=result['outcome'], status=result['status'])
    if result['outcome'] == 'infeasible':
        checked['proof'] = dict(reason='disk_control_relaxation_infeasible', relaxation='acceleration_disk',
                                status=result['status'])
    return checked


def deficits(problem, controls):
    """Per-tick relative acceleration shortfall: (peak, values, tick of peak)."""
    values = [max(0., m-math.hypot(*c))/m if m > 0 else 0. for m, c in zip(problem['magnitude'], controls)]
    peak = max(values, default=0.)
    return peak, values, values.index(peak) if values else None


def ipopt_seed(yaw):
    """IPOPT facings from a relaxed point's disk directions, all turned by SEED_TILT radians.

    On a mirror-symmetric request Clarabel returns the symmetric optimum, so a
    short tick points exactly between its two full-strength completions. IPOPT
    iterates from a symmetric start stay symmetric and stop at a local
    infeasibility; a uniform turn breaks every mirror symmetry while staying in
    the same basin (research/disk-relaxation/README.md#confirmation-seed).
    """
    native = _native()
    if native is not None:
        return native.ipopt_seed(yaw)
    return [y+math.degrees(SEED_TILT) for y in yaw]


def alarm_ticks(problem, shortfall):
    """Jump ticks short by more than ALARM_FRACTION, where the relaxation can brake a push the full model must make.

    A jump input applies its whole sprint-jump push, so a relaxed optimum that uses only part of it can be
    impossible at full strength; every full-model infeasibility measured had a jump tick at least 28% short
    (research/disk-relaxation/README.md). Jumps come from the program's vertical schedule.
    """
    jumps = (problem.get('vertical') or {}).get('jumpVelocity') or [None]*problem['ticks']
    return [t for t, s in enumerate(shortfall) if s > ALARM_FRACTION and jumps[t] is not None]


class RelaxedDispatch:
    """`find_route`'s numerical callable: Clarabel unless IPOPT must decide.

    IPOPT runs, with stock semantics, on requests outside the supported class.
    Every other request is solved relaxed; a relaxed point the scalar validator
    rejects is infeasible. `solve` returns
    the relaxed point with its alarm ticks; `confirmed` also re-solves an
    optimization request whose relaxed optimum leaves more than ALARM_FRACTION
    of a jump tick's acceleration unused (alarm_ticks): through numerics.post_alarm
    (Clarabel only), or with `post_alarm='ipopt'` with IPOPT. The repair
    loop solves with `solve` and confirms its kept candidates once, at its end
    (search/repair.py). `full_strength` answers every request at full strength,
    for the search's frame-end re-solve of a chosen witness (search/dfs.py).
    A gate with a native core (PKC_NATIVE_CORE) dispatches natively
    (numerics/disk.hpp) and keeps the confirmations in the core's cache.
    """
    def __init__(self, *, gate, post_alarm='pipeline'):
        if post_alarm not in ('ipopt', 'pipeline'):
            raise ValueError('post_alarm must be ipopt or pipeline')
        self.gate = gate
        self.post_alarm = post_alarm

    def solve(self, problem, **options):
        return self._dispatch(problem, mode='solve', **options)

    def confirmed(self, problem, **options):
        return self._dispatch(problem, mode='confirmed', **options)

    def full_strength(self, problem, **options):
        return self._dispatch(problem, mode='full_strength', **options)

    def _dispatch(self, problem, *, seconds, stop_on_feasible, python_executable=None, cancel_event=None,
                  on_incumbent=None, mode):
        from parkour_solver.numerics.solver import solve as full_solve
        from parkour_solver.validation.numerical import validate_controls
        core = getattr(self.gate, 'core', None)
        if core is not None:  # native node solves dispatch there too, so both share the core's confirmations
            return _native().relaxed_dispatch(core, problem, seconds=seconds, stop_on_feasible=stop_on_feasible,
                mode=mode, post_alarm=self.post_alarm, cancel_event=cancel_event, on_incumbent=on_incumbent)

        def full(reason, request=problem):
            result = full_solve(request, seconds=seconds, stop_on_feasible=stop_on_feasible,
                                python_executable=python_executable, cancel_event=cancel_event,
                                on_incumbent=on_incumbent)
            return dict(result, backend='ipopt', fallback=reason)

        content = spec(problem, strict=True)
        if content is None:
            return full('unsupported')
        started = time.monotonic()
        if cancel_event is not None and cancel_event.is_set():
            return _result(problem, started, 'cancelled', None, 'cancelled', None, None)
        solved = solve(content, seconds=seconds)
        relaxed = {k: solved[k] for k in ('status', 'outcome', 'iterations', 'cached')}
        if solved['outcome'] == 'infeasible':
            return _result(problem, started, 'unknown', None, 'disk_finished', solved['status'], relaxed)
        candidate = validate_controls(problem, solved['controls'])
        if not candidate['feasible']:
            # The replay only fails a solved point by numerical error; it is not worth an IPOPT call (user, 2026-11-23).
            return _result(problem, started, 'unknown', None, 'disk_rejected', solved['status'], relaxed)
        peak, shortfall, tick = deficits(problem, solved['controls'])
        alarmed = alarm_ticks(problem, shortfall)
        relaxed.update(peakRelativeDeficit=peak, peakTick=tick, alarmTicks=alarmed, objective=solved['objective'])
        candidate['model'] = 'relaxed_disk'
        if mode == 'full_strength':
            return self._frame_end(problem, candidate, relaxed, started, seconds=seconds,
                                   python_executable=python_executable, cancel_event=cancel_event,
                                   on_incumbent=on_incumbent)
        if mode == 'confirmed' and alarmed and not stop_on_feasible:
            return self.confirm(problem, candidate, relaxed, seconds=seconds, python_executable=python_executable,
                                cancel_event=cancel_event, on_incumbent=on_incumbent)
        if on_incumbent is not None:
            on_incumbent(json.loads(json.dumps(candidate)))
        return _result(problem, started, 'feasible', candidate, 'disk_finished', solved['status'], relaxed)

    def confirm(self, problem, relaxed_candidate, relaxed, *, seconds, python_executable, cancel_event, on_incumbent):
        """Re-solve an alarmed request with IPOPT, or answer it with the post-alarm pipeline (no IPOPT); the outcome
        governs it."""
        from parkour_solver.numerics.model import fingerprint
        from parkour_solver.numerics.solver import solve as full_solve
        key = ('confirmation', fingerprint(problem))
        cached = key in self.gate.cache
        if self.post_alarm == 'pipeline':
            from parkour_solver.numerics import post_alarm
            if cached:
                result = dict(self.gate.cache[key])
                result['confirmation'] = dict(result['confirmation'], cached=True, clarabelSolves=0)
            else:
                result = post_alarm.run(problem, relaxed_candidate, relaxed, seconds=seconds, cancel_event=cancel_event)
                # Decided: an answer, a sector certificate, or no answer after every step; a deadline is not.
                if result['status'] == 'feasible' or result['termination'] in ('quadrant_infeasible', 'post_alarm_none'):
                    self.gate.cache[key] = result
            if result['status'] == 'feasible' and on_incumbent is not None:
                on_incumbent(json.loads(json.dumps(result['candidate'])))
            return result
        if cached:
            result = dict(self.gate.cache[key], workerLaunched=False)
        else:
            # Assumption (measured, research/disk-relaxation/README.md): the disk
            # directions are the most accurate single-attempt seed. The request's
            # fallback stays.
            result = full_solve(dict(problem, yaw=ipopt_seed(relaxed_candidate['yaw'])), seconds=seconds,
                                stop_on_feasible=False, python_executable=python_executable, cancel_event=cancel_event)
            if result['status'] == 'feasible' or result['status'] == 'unknown' and result['termination'] == 'worker_finished':
                self.gate.cache[key] = result
        confirmation = dict(peak=relaxed['peakRelativeDeficit'], tick=relaxed['peakTick'], cached=cached,
                            diskObjective=relaxed_candidate['objective'], ipoptObjective=None, gap=None, material=False)
        if result['status'] == 'feasible':
            sense = 1 if problem['objective']['sense'] == 'MAX' else -1
            gap = sense*(relaxed_candidate['objective']-result['candidate']['objective'])
            confirmation.update(ipoptObjective=result['candidate']['objective'], gap=gap, material=gap > MATERIAL_GAP)
            if on_incumbent is not None:
                on_incumbent(json.loads(json.dumps(result['candidate'])))
        return dict(result, backend='ipopt', disk=relaxed, confirmation=confirmation)

    def _frame_end(self, problem, relaxed_candidate, relaxed, started, *, seconds, python_executable, cancel_event,
                   on_incumbent):
        """`full_strength`'s answer for a validated relaxed point, cached when decided like a confirmation."""
        from parkour_solver.numerics.model import fingerprint
        key = ('frameEnd', fingerprint(problem))
        if key in self.gate.cache:
            result = dict(self.gate.cache[key], workerLaunched=False)
            result['frameEnd'] = dict(result['frameEnd'], cached=True, ipoptCalls=0, clarabelSolves=0)
        else:
            result = _full_strength(problem, relaxed_candidate, relaxed, started, seconds=seconds,
                                    python_executable=python_executable, cancel_event=cancel_event)
            if result['status'] == 'feasible' or result['termination'] in ('quadrant_infeasible', 'worker_finished'):
                self.gate.cache[key] = result
        if result['status'] == 'feasible' and on_incumbent is not None:
            on_incumbent(json.loads(json.dumps(result['candidate'])))
        return result


def _full_strength(problem, relaxed_candidate, relaxed, started, *, seconds, python_executable, cancel_event):
    """The relaxed point itself when it is full strength, else the post-alarm pipeline's answer, then one IPOPT polish
    from the answer's exact facings (the SEED_TILT turn moves validated points out of IPOPT's tolerance), keeping the
    better; without an answer or a sector certificate, IPOPT from the turned disk facings
    (research/post-alarm/README.md#full-strength-acceptance-and-frame-end-re-solves)."""
    from parkour_solver.numerics import post_alarm
    from parkour_solver.numerics.solver import solve as full_solve
    from parkour_solver.validation.numerical import validate
    record = dict(method='direct', pushObjective=None, ipoptObjective=None, ipoptCalls=0, clarabelSolves=0,
                  cached=False)
    answer, termination = None, 'disk_finished'
    if deficits(problem, relaxed_candidate['controls'])[0] < post_alarm.FULL:
        checked = validate(problem, post_alarm.facings(problem, relaxed_candidate['controls']))
        answer = checked if checked['feasible'] else None
    if answer is None:
        pushed = post_alarm.run(problem, relaxed_candidate, relaxed, seconds=max(.001, started+seconds-time.monotonic()),
                                cancel_event=cancel_event)
        record.update(method=pushed['confirmation']['method'], clarabelSolves=pushed['confirmation']['clarabelSolves'])
        if pushed['status'] == 'cancelled' or pushed['termination'] == 'quadrant_infeasible':
            return dict(pushed, frameEnd=record)
        answer, termination = pushed['candidate'], pushed['termination']
    if answer is not None:
        record['pushObjective'] = answer['objective']
        request = {k: v for k, v in dict(problem, yaw=answer['yaw']).items() if k != 'fallbackYaw'}
    else:
        request = dict(problem, yaw=ipopt_seed(relaxed_candidate['yaw']))
    ipopt = full_solve(request, seconds=max(.001, started+seconds-time.monotonic()), stop_on_feasible=False,
                       python_executable=python_executable, cancel_event=cancel_event)
    record['ipoptCalls'] = int(ipopt.get('workerLaunched') is True)
    if ipopt['status'] == 'feasible':
        record['ipoptObjective'] = ipopt['candidate']['objective']
    sense = 1 if problem['objective']['sense'] == 'MAX' else -1
    if answer is None or ipopt['status'] == 'cancelled' or (
            ipopt['status'] == 'feasible' and sense*ipopt['candidate']['objective'] > sense*answer['objective']):
        return dict(ipopt, backend='ipopt', disk=relaxed, frameEnd=record)
    result = _result(problem, started, 'feasible', answer, termination, relaxed['status'], relaxed)
    return dict(result, workerLaunched=ipopt.get('workerLaunched'), frameEnd=record)


def _result(problem, started, status, candidate, termination, solver_status, relaxed):
    """A `numerics.solver.solve`-shaped result for an answer Clarabel decided."""
    from parkour_solver.numerics.model import fingerprint
    from parkour_solver.schema import REPLAY_TOLERANCE
    return {'version': 1, 'status': status, 'feasible': candidate is not None, 'candidate': candidate,
            'objectiveMode': problem['objectiveMode'], 'problemHash': fingerprint(problem),
            'elapsedSeconds': time.monotonic()-started,
            'firstFeasibleSeconds': time.monotonic()-started if candidate is not None else None,
            'problem': {'ticks': problem['ticks'], 'constraints': len(problem['walls']),
                        'contacts': len(problem['contacts']), 'threshold': problem['threshold'],
                        'objective': problem['objective'], 'source': problem.get('source')},
            'termination': termination, 'workerStatus': solver_status, 'workerLaunched': False, 'error': None,
            'tolerance': REPLAY_TOLERANCE, 'phases': [], 'backend': 'clarabel', 'disk': relaxed,
            'diagnostics': {'rejectedCandidates': 0, 'lastRejection': None, 'stderr': []}}


def _attempt(content, time_limit, max_iter, cuts=(), level=None):
    try:
        built = _build(content, time_limit, max_iter, cuts, level)
        return dict(_run(built), assemblySeconds=built['assemblySeconds'], setupSeconds=built['setupSeconds']), built
    except (ValueError, RuntimeError) as error:
        return dict(status='exception', outcome='infeasible', controls=None, objective=None,
                    iterations=0, message=repr(error)), None


def _build(content, time_limit, max_iter, cuts=(), level=None):
    """The Clarabel problem of a spec, built and set up but not solved.

    `cuts`: rows `nx*a_x[t] + nz*a_z[t] >= rhs` as (t, nx, nz, rhs). `level`: (objective, value), the row objective >=
    value (MAX) or <= value (MIN); it is separate from the content's own objective so a solve without one keeps it.
    """
    import clarabel
    import numpy as np
    from scipy.sparse import coo_matrix, csc_matrix
    started = time.perf_counter()
    n = content['ticks']
    width = 3*n+2

    def pos(axis, t): return axis*width+t
    def vel(axis, t): return axis*width+n+1+t
    def control(axis, t): return axis*width+2*(n+1)+t
    equalities, inequalities = ([], [], [], []), ([], [], [], [])  # rows, columns, values, rhs

    def row(block, entries, rhs):
        index = len(block[3])
        for column, value in entries.items():
            block[0].append(index); block[1].append(column); block[2].append(value)
        block[3].append(rhs)

    contacts = {(c['state'], c['axis']): c for c in content['contacts']}
    initial = content['initialIntervals']
    for axis in range(2):
        if initial is None:
            row(equalities, {pos(axis, 0): 1.}, 0.)
            row(equalities, {vel(axis, 0): 1.}, content['velocity'][axis])
        else:
            # Interval starts are absolute; positions are offsets from `position`.
            origin = content['position'][axis]
            row(inequalities, {pos(axis, 0): -1.}, origin-initial[0][axis])
            row(inequalities, {pos(axis, 0): 1.}, initial[1][axis]-origin)
            row(inequalities, {vel(axis, 0): -1.}, -initial[2][axis])
            row(inequalities, {vel(axis, 0): 1.}, initial[3][axis])
        for t in range(n):
            contact = contacts.get((t+1, axis))
            if contact is None:
                row(equalities, {pos(axis, t+1): 1., pos(axis, t): -1., vel(axis, t): -1., control(axis, t): -1.}, 0.)
                friction = content['friction'][t]
                row(equalities, {vel(axis, t+1): 1., vel(axis, t): -friction, control(axis, t): -friction}, 0.)
            else:
                plane, d = contact['plane']-content['position'][axis], float(contact['direction'])
                row(equalities, {pos(axis, t+1): 1.}, plane)
                row(equalities, {vel(axis, t+1): 1.}, 0.)
                row(inequalities, {pos(axis, t): d}, d*plane)
                row(inequalities, {pos(axis, t): -d, vel(axis, t): -d, control(axis, t): -d}, -d*plane-SOLVER_MARGIN)
    for mode, t, t2, op, cmp, rhs in content['rows']:
        axis = 0 if mode == 'X' else 1
        entries, offset = {pos(axis, t): 1.}, content['position'][axis]
        if t2 is not None:
            sign = -1. if op == 'MINUS' else 1.
            entries[pos(axis, t2)] = entries.get(pos(axis, t2), 0.)+sign
            offset += sign*content['position'][axis]
        if cmp == 'EQ':
            row(equalities, entries, rhs-offset)
        elif cmp == 'LE':
            row(inequalities, entries, rhs-offset)
        else:
            row(inequalities, {k: -v for k, v in entries.items()}, offset-rhs)
    for t, nx, nz, bound in cuts:
        row(inequalities, {control(0, t): -nx, control(1, t): -nz}, -bound)
    if level is not None:
        objective, value = level
        weights, origin, sign = _cost(dict(content, objective=objective), width, pos)
        # The objective is origin+sign*weights@x.
        if objective['sense'] == 'MAX':
            row(inequalities, {i: -sign*float(w) for i, w in enumerate(weights) if w}, origin-value)
        else:
            row(inequalities, {i: sign*float(w) for i, w in enumerate(weights) if w}, value-origin)
    disks = ([], [], [], [])
    for t, magnitude in enumerate(content['magnitude']):
        disks[3].extend([magnitude+ALLOWANCE, 0., 0.])
        for axis in range(2):
            disks[0].append(3*t+1+axis); disks[1].append(control(axis, t)); disks[2].append(-1.)
    blocks = [equalities, inequalities, disks]
    offsets = [sum(len(b[3]) for b in blocks[:i]) for i in range(len(blocks))]
    matrix = coo_matrix((np.concatenate([np.asarray(b[2], dtype=float) for b in blocks]),
                         (np.concatenate([np.asarray(b[0], dtype=int)+o for b, o in zip(blocks, offsets)]),
                          np.concatenate([np.asarray(b[1], dtype=int) for b in blocks]))),
                        shape=(sum(len(b[3]) for b in blocks), 2*width)).tocsc()
    rhs = np.concatenate([np.asarray(b[3], dtype=float) for b in blocks])
    cost, offset, sense = _cost(content, width, pos)
    cones = [cone(len(block[3])) for cone, block in ((clarabel.ZeroConeT, equalities),
                                                     (clarabel.NonnegativeConeT, inequalities)) if block[3]]
    cones += [clarabel.SecondOrderConeT(3) for _ in range(n)]
    settings = clarabel.DefaultSettings()
    settings.verbose = False
    settings.max_threads = 1
    settings.direct_solve_method = 'qdldl'
    settings.time_limit = time_limit
    settings.max_iter = max_iter
    settings.tol_feas = settings.tol_gap_abs = settings.tol_gap_rel = TOL
    settings.tol_infeas_abs = settings.tol_infeas_rel = INFEASIBILITY_TOL
    # Same verdicts and search decisions, faster solves (research/clarabel-refinement/README.md).
    settings.iterative_refinement_enable = False
    assembled = time.perf_counter()
    solver = clarabel.DefaultSolver(csc_matrix((2*width, 2*width)), cost, matrix, rhs, cones, settings)
    ready = time.perf_counter()
    return dict(solver=solver, cost=cost, offset=offset, sense=sense,
                columns=[[control(axis, t) for axis in range(2)] for t in range(n)],
                assemblySeconds=assembled-started, setupSeconds=ready-assembled)


def _run(built):
    """Solves a built problem (again after `solver.update`) and decodes its verdict."""
    import numpy as np
    started = time.perf_counter()
    solution = built['solver'].solve()
    finished = time.perf_counter()
    status = str(solution.status)
    x = np.asarray(solution.x)
    controls = objective = None
    if status in FEASIBLE and np.isfinite(x).all():
        outcome = 'feasible'
        controls = [[float(x[i]) for i in tick] for tick in built['columns']]
        objective = built['offset']+built['sense']*float(built['cost']@x) if built['sense'] else None
    else:
        # Every status but a limit is a verdict: no undecided result reaches IPOPT (user, 2026-11-22).
        outcome = 'limit' if status in LIMIT else 'infeasible'
    return dict(status=status, outcome=outcome, controls=controls, objective=objective,
                iterations=int(solution.iterations), solveSeconds=finished-started)


def _cost(content, width, pos):
    """Minimized cost vector, the world offset and sign recovering the score."""
    import numpy as np
    cost = np.zeros(2*width)
    objective = content.get('objective')
    if objective is None or not 0 <= objective['tick'] <= content['ticks']:
        return cost, 0., 0.
    sign = -1. if objective['sense'] == 'MAX' else 1.
    tick = objective['tick']
    origin = content['position']
    if objective.get('customYaw') is not None:
        angle = math.radians(objective['customYaw'])
        weights = (-math.sin(angle), math.cos(angle))
        motion = objective.get('type') == 'MOTION' and tick > 0
        offset = 0. if motion else weights[0]*origin[0]+weights[1]*origin[1]
        for axis, weight in enumerate(weights):
            cost[pos(axis, tick)] += sign*weight
            if motion:
                cost[pos(axis, tick-1)] -= sign*weight
    else:
        axis = 0 if objective['axis'] == 'X' else 1
        cost[pos(axis, tick)] = sign
        offset = origin[axis]
    # cost@x is sign*(score-offset); the minimizer's sign is its own inverse.
    return cost, offset, sign
