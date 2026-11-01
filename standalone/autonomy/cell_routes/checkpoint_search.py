import hashlib
import math
import time

from standalone.autonomy.block_contacts import floor_checks, solve_contact_branches
from standalone.autonomy.block_search import prepare_scene
from standalone.autonomy.movement_composition import compile_program, empty_program, program_physics_key
from standalone.autonomy.movement_search import movement_settings, screen_program
from standalone.autonomy.ranking import candidate_rank
from standalone.autonomy.route import supports
from standalone.ipopt.model import integer, number
from .checkpoints import checkpoint, retain_prefix
from .decision_windows import DecisionPolicy, fair_programs, plan_windows, retain_candidates, score_features
from .geometry import decompose
from .local import CorridorAdapter, CountingCompiler, SearchWork
from .routes import corridor_for_points, enumerate_routes, landmarks


class ScopedWork:
    def __init__(self, parent, calls, deadline):
        self.parent = parent
        self.local = SearchWork(calls, deadline=deadline)

    @property
    def termination(self):
        return self.parent.termination or self.local.termination

    def check(self):
        self.local.check()
        self.parent.check()

    def proposal(self, problem):
        self.parent.proposal(problem)
        self.local.proposal(problem)

    def before_solve(self, problem):
        self.check()
        self.parent.before_solve(problem)
        self.local.before_solve(problem)

    def accepted(self, best):
        self.local.accepted(best)


def _motion_bound(scene, compiler, seconds):
    sample = compiler.compile(dict(scene, sharedMovement=True), dict(grounded=[True, True, False], jump=[False, True, False]), seconds)
    speed = max(math.hypot(scene['initial']['velocity'][0], scene['initial']['velocity'][2]),
                max(f*m/(1-f) for m, f in zip(sample['magnitude'], sample['friction'])))
    return dict(storedSpeedNorm=speed+1e-8, movementAfterSupportCheck=speed+max(sample['magnitude'])+1e-8,
                groundMagnitude=sample['magnitude'][0])


def evaluate_program(scene, program, compiler, adapter, work, motion_bound, *, checkpoint=None,
                     seconds=3, candidate_seconds=1, solver_calls=8, python_executable=None, cancel_event=None):
    complete = checkpoint is None
    compiled = compile_program(scene, program, require_complete=complete)
    route, profiles = compiled['route'], compiled['profiles']
    if checkpoint is not None:
        if program['profile']['airtime'] != checkpoint.tick:
            raise ValueError('Every compared program must end at the shared absolute tick')
        route['objective'] = dict(axis='X', sense='MAX')
        route['guideObjective'] = checkpoint.objective
        route['constraints'].extend(checkpoint.rows())
    scoped = ScopedWork(work, solver_calls, min(work.deadline or math.inf, time.monotonic()+seconds))
    owners = [b['sourceBlock'] for b in scene['blocks']]
    def candidate_filter(plan, candidate, vertical):
        verdict = adapter.candidate(plan, candidate, vertical, partial=not complete)
        if verdict['accepted'] and checkpoint is not None:
            staged = dict(candidate, **{key: vertical[key] for key in ('y', 'vy', 'onGround')})
            stage = checkpoint.stage(staged, adapter.gates)
            if not stage['accepted']:
                return dict(verdict, accepted=False, reason='checkpoint_not_after_turn', comparisonStage=stage)
        return verdict

    result = solve_contact_branches(route, profiles, floor_checks(scene, [scene['startBlock'], scene['goalBlock']], profiles),
        compiler, seconds=seconds, candidate_seconds=candidate_seconds, retries=0,
        max_branches=scene['maxContactBranches'], improve=True, rank_equal_cost=True, optimize_goal=True,
        python_executable=python_executable, cancel_event=cancel_event, geometry=scene['collisionScene'],
        motion_bound=motion_bound, max_blockages=scene['maxBlockagesPerObstacle'],
        max_horizontal=scene['maxHorizontalBlockagesPerObstacle'], max_vertical=scene['maxVerticalBlockagesPerObstacle'],
        max_passage_candidates=scene['maxPassageCandidates'], solid_blocks=owners, max_wall_hits=0,
        candidate_filter=candidate_filter, work=scoped)
    if result['best'] is not None:
        if checkpoint is not None:
            stage = checkpoint.stage(result['best']['candidate'], adapter.gates)
            result['comparisonStage'] = stage
            if not stage['accepted']:
                result['deferredCandidate'] = result['best']
                result['best'] = None
                result.update(status='unknown', termination='checkpoint_not_after_turn')
        else:
            work.accepted(result['best'])
    result['work'] = scoped.local.summary()
    return result


def _key(program):
    return hashlib.sha256(program_physics_key(program).encode()).hexdigest()


def find_checkpoint_route(request, compiler, *, checkpoints=None, corridor=None, spatial_points=None, seconds=30,
                          candidate_seconds=1, score_seconds=3, max_solver_calls=128,
                          max_candidates=24, beam_width=4, unknown_slots=1, score_call_limit=8,
                          final_call_limit=24, order='guided', python_executable=None, cancel_event=None,
                          opportunities=None, cluster_gap=2, objective_gap=None, fair_parents=False,
                          initial_programs=None):
    started = time.monotonic()
    for name, value in [('seconds', seconds), ('candidate_seconds', candidate_seconds), ('score_seconds', score_seconds)]:
        if number(value, name) <= 0:
            raise ValueError(name+' must be positive')
    for name, value in [('max_candidates', max_candidates), ('beam_width', beam_width),
                        ('score_call_limit', score_call_limit), ('final_call_limit', final_call_limit), ('max_solver_calls', max_solver_calls)]:
        integer(value, name, 1, 100000)
    integer(unknown_slots, 'unknown_slots', 0, beam_width)
    if order not in ('guided', 'generation'):
        raise ValueError('Ordering must be guided or generation')
    if type(fair_parents) is not bool:
        raise ValueError('fair_parents must be boolean')
    if objective_gap is not None and number(objective_gap,'objective_gap') < 0:
        raise ValueError('objective_gap must be nonnegative')
    if opportunities is not None and checkpoints is not None:
        raise ValueError('Supply opportunities or checkpoint ticks, not both')
    if request.get('maxWallHits', 0):
        raise ValueError('Intentional wall-hit experiments are excluded')
    scene = prepare_scene(dict(request, maxWallHits=0))
    if scene['sceneModel'] != 'collision_boxes' or scene.get('climbableCells') or scene['maxWallHits']:
        raise ValueError('Checkpoint search requires non-ladder collision geometry with maxWallHits=0')
    cfg = movement_settings(scene)
    if any(k not in ('normal', 'ceiling', 'stepping') for k in request.get('movementSearch', {}).get('types', [])):
        raise ValueError('Select only normal, ceiling and stepping movement types')
    cfg['types'] = [k for k in cfg['types'] if k in ('normal', 'ceiling', 'stepping')]
    deadline = started+seconds
    work, counted = SearchWork(max_solver_calls, deadline=deadline), CountingCompiler(compiler)
    def unfinished(reason):
        return dict(status='cancelled' if reason == 'cancelled' else 'unknown', termination=reason, best=None,
            windows=[], finalAttempts=[], routeGeneration=None, corridor=None, expandedPrefixes=0,
            movementGeneration=[], work=work.summary(), compiler=counted.summary(),
            elapsedSeconds=time.monotonic()-started, order=order, threshold=0., checkpoints=[],
            objectiveOptimalityProven=False, minimumTicksProven=False)
    if cancel_event is not None and cancel_event.is_set():
        return unfinished('cancelled')
    try:
        d, gates = decompose(scene, deadline=deadline), landmarks(scene)
    except TimeoutError:
        return unfinished('deadline')
    route_report = None
    if corridor is None:
        if spatial_points is not None:
            goal = next(b for b in scene['blocks'] if b['id'] == scene['goalBlock'])
            if list(spatial_points[0]) != list(scene['initial']['position']):
                raise ValueError('The spatial guide must start at the original initial position')
            last = spatial_points[-1]
            if abs(last[1]-goal['top']) > 1e-7 or not supports(goal,last[0],last[2]):
                raise ValueError('The spatial guide must end in the goal landing region')
            corridor = corridor_for_points(d, spatial_points, gates)
        else:
            routes, route_report = enumerate_routes(scene, d, max_routes=1, max_expansions=1000, deadline=deadline)
            if not routes:
                result = unfinished('no_spatial_route')
                result['routeGeneration'] = route_report
                return result
            corridor = routes[0]
    window_plan = plan_windows(opportunities,gap=cluster_gap,max_inputs=cfg['maxInputs']) if opportunities is not None else None
    policy = DecisionPolicy(window_plan,scene) if window_plan is not None else None
    specs = window_plan['checkpoints'] if window_plan is not None else checkpoints or []
    plans = [checkpoint(corridor, gates, spec) for spec in specs]
    if any(a.tick >= b.tick or a.segment > b.segment for a, b in zip(plans, plans[1:])):
        raise ValueError('Checkpoints must increase in absolute tick and ordered route segment')
    if plans and plans[-1].tick > cfg['maxInputs']:
        raise ValueError('Checkpoint exceeds movement horizon')
    adapter = CorridorAdapter(scene, corridor, gates, decomposition=d)
    try:
        work.check()
        motion = _motion_bound(scene, counted, max(.001,min(score_seconds,deadline-time.monotonic())))
    except TimeoutError:
        return unfinished('deadline')
    seed = empty_program(scene['initial'], threshold=0.)
    seed['terminalSupport'] = scene['startBlock']
    frontier = [seed] if initial_programs is None else list(initial_programs)
    if not frontier or any(p['initial']!=seed['initial'] or p.get('threshold',.005)!=0. for p in frontier):
        raise ValueError('Initial programs must preserve the original state and zero thresholds')
    if policy is not None and any(not policy(p) for p in frontier):
        raise ValueError('Initial programs violate the opportunity ranges')
    windows, best, final_attempts, generation_reports = [], None, [], []
    expanded = 0
    termination = 'checkpoint_search_finished'
    def screen(program):
        profile = program['profile']
        if profile['position'][1] < scene['initial']['position'][1]-1e-7:
            for block in scene['blocks']:
                if abs(block['top']-scene['initial']['position'][1]) < 1e-7 and supports(
                        block, scene['initial']['position'][0], scene['initial']['position'][2]):
                    return dict(reason='unselected_initial_floor')
        return screen_program(scene, program, counted, max(.001, min(candidate_seconds, deadline-time.monotonic())))
    try:
        for target in plans+[None]:
            work.check()
            if expanded >= cfg['maxPrefixes']:
                termination = 'prefix_limit'
                break
            if not frontier:
                termination = 'goal_found' if best else 'no_retained_prefix'
                break
            report, batch, solved, unknown, seen = {}, [], [], [], set()
            local_cfg = dict(cfg, maxPrefixes=cfg['maxPrefixes']-expanded)
            generator = fair_programs(scene,frontier,cfg=local_cfg,fair=fair_parents,
                deadline=deadline, cancel_event=cancel_event,report=report, context_adapter=adapter.context, work=work,
                screen=screen,
                checkpoint_tick=target.tick if target else None, threshold=0., program_filter=policy)
            capped = False
            for program in generator:
                try:
                    work.check()
                except TimeoutError:
                    termination = work.termination or 'deadline'
                    break
                if cancel_event is not None and cancel_event.is_set():
                    termination = 'cancelled'
                    break
                identity = _key(program)
                if identity in seen:
                    continue
                seen.add(identity)
                if len(batch) >= max_candidates:
                    capped = True
                    break
                complete = program['terminalSupport'] == scene['goalBlock'] and program['profile']['grounded'][-1]
                if best is not None and program['profile']['airtime'] > best['plan']['ticks']:
                    continue
                actual_target = None if complete else target
                if actual_target is None and not complete:
                    raise RuntimeError('Final generation emitted an open prefix')
                remaining = deadline-time.monotonic()
                result = evaluate_program(scene, program, counted, adapter, work, motion, checkpoint=actual_target,
                    seconds=max(.001, min(score_seconds, remaining)), candidate_seconds=candidate_seconds,
                    solver_calls=final_call_limit if complete else score_call_limit,
                    python_executable=python_executable, cancel_event=cancel_event)
                record = dict(id=identity, ticks=program['profile']['airtime'], complete=complete,
                    methods=[u.get('case', u['type']) for u in program['movementUnits']], status=result['status'],
                    termination=result['termination'], value=result['best']['candidate']['objective'] if result['best'] else None,
                    work=result['work'], comparisonStage=result.get('comparisonStage'))
                record['parent'] = program.get('searchParent')
                if policy is not None:
                    record['decisionAudit'] = policy.audit(program)
                    if not complete:
                        record['program'] = program
                if result['best'] is not None:
                    record['witness'] = result['best']
                    if not complete:
                        record['features'] = score_features(result['best'],actual_target,
                            goal_box=policy.goal_box if policy is not None else None)
                if result.get('error'):
                    record['error'] = result['error']
                batch.append(record)
                if result['best'] is not None:
                    if complete:
                        candidate = result['best']
                        candidate.update(movementProgram=program, blockIds=[scene['startBlock'], scene['goalBlock']])
                        if best is None or candidate_rank(candidate, scene['objective']) < candidate_rank(best, scene['objective']):
                            best = candidate
                            report['maxAcceptedTicks'] = best['plan']['ticks']
                    else:
                        solved.append((record, retain_prefix(program, result['best'])))
                elif not complete:
                    unknown.append((record, program))
                if result['status'] in ('error', 'cancelled'):
                    termination = result['status']
                    break
            generator.close()
            if cancel_event is not None and cancel_event.is_set():
                termination = 'cancelled'
            expanded += report.get('expandedPrefixes', 0)
            generation_reports.append(report)
            if capped:
                report['termination'] = 'candidate_limit'
            if target is None:
                final_attempts = batch
                if termination not in ('error','cancelled','deadline','numerical_call_limit'):
                    termination = report.get('termination', 'search_finished')
                break
            retained, retention = retain_candidates(solved,unknown,cap=beam_width,unknown_slots=unknown_slots,
                                                   epsilon=objective_gap,order=order)
            frontier = [p for _, p in retained]
            windows.append(dict(checkpoint=target.to_dict(), generation=report, candidates=batch,
                retained=[r['id'] for r, _ in retained], scored=len(solved), unknown=len(unknown),
                heuristicDrops=max(0, len(solved)+len(unknown)-len(retained)), order=order,retention=retention))
            if termination in ('error', 'cancelled', 'deadline', 'numerical_call_limit'):
                break
    except TimeoutError:
        termination = work.termination or 'deadline'
    return dict(status='feasible' if best else 'cancelled' if termination == 'cancelled' else 'error' if termination == 'error' else 'unknown',
        termination=termination, best=best, windows=windows, finalAttempts=final_attempts,
        corridor=corridor.to_dict(), routeGeneration=route_report, adapter=adapter.summary(),
        movementGeneration=generation_reports, expandedPrefixes=expanded,
        work=work.summary(), compiler=counted.summary(), elapsedSeconds=time.monotonic()-started,
        order=order, threshold=0., checkpoints=[p.to_dict() for p in plans],
        decisionPlan=window_plan,objectiveGap=objective_gap,fairParents=fair_parents,
        objectiveOptimalityProven=False, minimumTicksProven=False)
