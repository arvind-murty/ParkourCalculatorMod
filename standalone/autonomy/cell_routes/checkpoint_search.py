import copy
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
from .checkpoints import checkpoint, retain_prefix, retain_unresolved, retain_decisions
from .decision_windows import DecisionPolicy, fair_programs, plan_windows, retain_candidates, score_features
from .geometry import decompose
from .local import CorridorAdapter, CountingCompiler, SearchWork
from .routes import corridor_for_points, enumerate_routes, landmarks
from .time_progress import clock_result, retain_two_goals
from .comparison_goals import goal_key, goal_spec, landing_satisfied, probe_spatial
from standalone.autonomy.solve_spec import solve_scope, solve_identity, validation_prefix, processing_key
from standalone.autonomy.reachability import reachable_envelopes
from standalone.autonomy.constraint_variants import ConstraintVariants


class ScopedWork:
    def __init__(self, parent, calls, deadline):
        self.parent = parent
        self.local = SearchWork(calls, deadline=deadline)

    @property
    def deadline(self):
        return min(self.local.deadline or math.inf, self.parent.deadline or math.inf)

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
                     seconds=3, candidate_seconds=1, solver_calls=8, python_executable=None, cancel_event=None,
                     partial_corridor=False, required_landing=None, scope=None, on_progress=None, retry_unresolved=True):
    complete = checkpoint is None
    compiled = compile_program(scene, program, require_complete=complete)
    route, profiles = compiled['route'], compiled['profiles']
    scope = solve_scope(scene, program, scope)
    scoped_adapter = adapter.scoped(scope) if hasattr(adapter, 'scoped') else adapter
    if checkpoint is not None:
        if program['profile']['airtime'] != checkpoint.tick:
            raise ValueError('Every compared program must end at the shared absolute tick')
        route['objective'] = dict(axis='X', sense='MAX')
        route['guideObjective'] = checkpoint.objective
        route['constraints'].extend(checkpoint.rows())
    scoped = ScopedWork(work, solver_calls, min(work.deadline or math.inf, time.monotonic()+seconds))
    owners = [b['sourceBlock'] for b in scene['blocks']]
    def candidate_filter(plan, candidate, vertical):
        p, c, v = validation_prefix(plan, candidate, vertical, scope['through'])
        verdict = scoped_adapter.candidate(p, c, v, partial=not complete or partial_corridor or scope['purpose'] == 'preliminary')
        if verdict['accepted'] and required_landing is not None and not landing_satisfied(scene,required_landing,vertical):
            return dict(verdict,accepted=False,reason='spatial_goal_not_satisfied')
        if verdict['accepted'] and checkpoint is not None and scope['purpose'] == 'complete':
            staged = dict(candidate, **{key: vertical[key] for key in ('y', 'vy', 'onGround')})
            stage = checkpoint.stage(staged, adapter.gates)
            if not stage['accepted']:
                return dict(verdict, accepted=False, reason='checkpoint_not_after_turn', comparisonStage=stage)
        return verdict

    from standalone.autonomy.route import schedule
    from standalone.autonomy.block_contacts import validate_vertical_schedule
    plan = schedule(route, [0])
    vertical = validate_vertical_schedule(route, profiles, plan)
    if not vertical['feasible']:
        return dict(status='unknown', termination='vertical_schedule', best=None, attempts=[], contactAttempts=[],
                    work=scoped.local.summary(), solveScope=scope, fullSceneValidated=False)
    plan['vertical'] = dict(vertical['vertical'], referencePosition=vertical['y'], referenceVelocity=vertical['vy'])
    coefficients = compiler.compile(route, plan, max(.001, min(seconds, candidate_seconds)))
    model = dict(coefficients, version=1, ticks=plan['ticks'], walls=plan['walls'], contacts=plan['contacts'],
        position=scene['initial']['position'][::2], velocity=scene['initial']['velocity'][::2],
        threshold=route.get('physicsThreshold', .005), objective=dict(route.get('guideObjective', route['objective']), tick=plan['ticks']))
    envelopes = reachable_envelopes(model, vertical['y'])
    from standalone.autonomy.solve_spec import digest
    envelopes['modelKey'] = digest(coefficients)
    from standalone.autonomy.search import initial_guess
    from standalone.ipopt import prepare, validate
    reference_yaw = initial_guess(route, plan, None)
    reference = validate(prepare(dict(model, yaw=reference_yaw)), reference_yaw)['positions']
    active = set(scope['solidIndices'])
    checks = [c for c in floor_checks(scene, [scene['startBlock'], scene['goalBlock']], profiles)
              if c['block']['solidIndex'] in active and c['offset'] < scope['through']]
    signature = checkpoint.required_signature if checkpoint is not None else (
        adapter.corridor.signature if hasattr(adapter, 'corridor') and not partial_corridor else ())
    identity = solve_identity(scene, program, route.get('guideObjective', route['objective']), scope,
        getattr(scoped_adapter, 'corridor', None).to_dict() if hasattr(scoped_adapter, 'corridor') else None, coefficients)
    variants = ConstraintVariants(route, profiles[0], plan, scene['collisionScene'], envelopes, scope,
        checks=checks, gates=scoped_adapter.gates, signature=signature,
        state=program.get('repairState', {}).get('constraintGeneration'), deadline=scoped.deadline, reference=reference)
    result = solve_contact_branches(route, profiles, checks,
        compiler, seconds=seconds, candidate_seconds=candidate_seconds, retries=0,
        max_branches=scene['maxContactBranches'], improve=True, rank_equal_cost=True, optimize_goal=True,
        python_executable=python_executable, cancel_event=cancel_event, geometry=scene['collisionScene'],
        motion_bound=motion_bound, max_blockages=scene['maxBlockagesPerObstacle'],
        max_horizontal=scene['maxHorizontalBlockagesPerObstacle'], max_vertical=scene['maxVerticalBlockagesPerObstacle'],
        max_passage_candidates=scene['maxPassageCandidates'], solid_blocks=owners, max_wall_hits=0,
        candidate_filter=candidate_filter, work=scoped, resume_state=program.get('repairState'),
        repair_scope=dict(solveIdentity=identity, scope=scope, assemblyVersion=4),
        validation_scope=scope, initial_variants=variants, on_progress=on_progress, retry_unresolved=retry_unresolved)
    if result['best'] is not None:
        if checkpoint is not None and scope['purpose'] == 'complete':
            stage = checkpoint.stage(result['best']['candidate'], adapter.gates)
            result['comparisonStage'] = stage
            if not stage['accepted']:
                result['deferredCandidate'] = result['best']
                result['best'] = None
                result.update(status='unknown', termination='checkpoint_not_after_turn')
        elif checkpoint is None and scope['purpose'] == 'complete':
            work.accepted(result['best'])
    result['work'] = scoped.local.summary()
    result.update(solveScope=scope, solveIdentity=identity,
                  fullSceneValidated=result['best'] is not None and scope['purpose'] == 'complete',
                  reachability=dict(excludedSolids=variants.exclusions, interactionGroups=len(variants.items)))
    if result['best'] is not None:
        result['best']['validationScope'] = scope
    return result


def _key(program):
    return hashlib.sha256(program_physics_key(program).encode()).hexdigest()


def find_checkpoint_route(request, compiler, *, checkpoints=None, corridor=None, spatial_points=None, seconds=30,
                          candidate_seconds=1, score_seconds=3, max_solver_calls=128,
                          max_candidates=24, beam_width=4, unknown_slots=1, score_call_limit=8,
                          final_call_limit=24, order='guided', python_executable=None, cancel_event=None,
                          opportunities=None, cluster_gap=2, objective_gap=None, fair_parents=False,
                          initial_programs=None, comparison_policy='time_progress', spatial_goal=None,
                          spatial_seconds=1.5, spatial_calls=4, spatial_candidates=4, spatial_prefixes=64,
                          time_margin=1, progress_margin=None, rescue_passes=1, terminal_checkpoint=None, program_filter=None,
                          on_window=None, spatial_goals=None, program_priority=None, resume_state=None, on_progress=None,
                          retry_unknown=False):
    integer(rescue_passes,'rescue passes',0,100)
    if comparison_policy not in ('time_progress','objective_band','unpruned'):
        raise ValueError('Unknown comparison policy')
    if number(spatial_seconds,'spatial comparison seconds') <= 0:
        raise ValueError('Spatial comparison seconds must be positive')
    for name,value in [('spatial_calls',spatial_calls),('spatial_candidates',spatial_candidates),('spatial_prefixes',spatial_prefixes)]:
        integer(value,name,1,100000)
    if number(time_margin,'time margin')<0:
        raise ValueError('Time margin must be nonnegative')
    progress_margin = progress_margin if progress_margin is not None else objective_gap if objective_gap is not None else .25
    if number(progress_margin,'progress margin')<0:
        raise ValueError('Progress margin must be nonnegative')
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
    spatial_goal = goal_spec(scene,spatial_goal)
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
            objectiveOptimalityProven=False, minimumTicksProven=False, comparisonPolicy=comparison_policy)
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
            if terminal_checkpoint is None and (abs(last[1]-goal['top']) > 1e-7 or not supports(goal,last[0],last[2])):
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
    def accepted_program(program):
        from standalone.autonomy.solve_spec import motion_record
        movement = motion_record(program)
        if movement is not None and any(name in request and movement[field] > request[name]
                for name, field in [('maxTotalRunTicks', 'runs'), ('maxJumps', 'jumps')]):
            return False
        return (policy is None or policy(program)) and (program_filter is None or program_filter(program))
    specs = window_plan['checkpoints'] if window_plan is not None else checkpoints or []
    plans = [checkpoint(corridor, gates, spec) for spec in specs]
    if spatial_goals is not None and len(spatial_goals)!=len(plans):
        raise ValueError('Supply one shared spatial goal per comparison checkpoint')
    goals_by_tick={p.tick:goal_spec(scene,g) for p,g in zip(plans,spatial_goals or [])}
    terminal_plan=checkpoint(corridor,gates,terminal_checkpoint) if terminal_checkpoint is not None else None
    if terminal_plan is not None and (terminal_plan.tick>cfg['maxInputs'] or any(p.tick>=terminal_plan.tick for p in plans)):
        raise ValueError('Terminal clock must follow every comparison checkpoint and fit the movement horizon')
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
    from standalone.autonomy.solve_spec import digest
    from standalone.autonomy.prefix_viability import prefix_viability
    seed = empty_program(scene['initial'], threshold=0.)
    seed['terminalSupport'] = scene['startBlock']
    roots = [seed] if initial_programs is None else list(initial_programs)
    if resume_state is not None and initial_programs is None:
        roots = resume_state.get('initialRoots', roots)
    if not roots or any(p['initial'] != seed['initial'] or p.get('threshold', .005) != 0. for p in roots):
        raise ValueError('Initial programs must preserve the original state and zero thresholds')
    if any(not accepted_program(p) for p in roots):
        raise ValueError('Initial programs violate the opportunity ranges')
    completed = min(p['profile']['airtime'] for p in roots)
    plans = [p for p in plans if p.tick > completed]
    stages = plans+[terminal_plan]
    semantic_scene = {k: v for k, v in scene.items() if k != 'movementSearch'}
    generation_domain = {k: v for k, v in cfg.items() if k not in
        ('maxPrefixes', 'maxQueuedPrefixes', 'maxChoicesPerType', 'expansionsPerProposal')}
    from standalone.autonomy.route import HALF_WIDTH
    from standalone.autonomy.obstacle_geometry import BODY_HEIGHT
    context = digest(dict(version=4, scene=semantic_scene, generation=generation_domain,
        physics=dict(body=[HALF_WIDTH,BODY_HEIGHT],sample=motion),
        initialRoots=[program_physics_key(p) for p in roots], stages=[p.to_dict() if p else None for p in stages],
        corridor=corridor.to_dict(), windows=window_plan, goals=goals_by_tick, spatialGoal=spatial_goal,
        margins=[time_margin, progress_margin], policy=comparison_policy,
        filter=getattr(program_filter, 'checkpoint_identity', None),
        explicitLimits={k: request[k] for k in ('maxJumps','maxTotalRunTicks') if k in request}))
    if resume_state is not None and (resume_state.get('version') != 1 or resume_state.get('context') != context):
        raise ValueError('Incompatible checkpoint search continuation')
    state = copy.deepcopy(resume_state) if resume_state is not None else dict(version=1, context=context,
        initialRoots=roots, stage=0, frontier=roots, current=None, windows=[], finalAttempts=[], best=None,
        generationReports=[], parkedCandidates=[], unevaluatedCandidates=[], cumulativeCalls=0, session=0)
    state['session'] += 1
    if resume_state is not None and state['best'] is not None and 'route' in state['best']:
        from .checkpoint_experiment import replay_witness
        if not replay_witness(scene,state['best'],counted)['passed']:
            raise ValueError('Saved incumbent no longer replays under the current physics')
    prior_calls = state['cumulativeCalls']
    initial_expanded = sum(g.get('totalExpandedPrefixes', g.get('expandedPrefixes', 0)) for g in state['generationReports'])
    if state['current'] is not None:
        initial_expanded += state['current']['generation'].get('continuation', {}).get('expanded', 0)
    viability_cache, viability_counts = {}, dict(checked=0, excluded=0)
    termination = 'checkpoint_search_finished'
    generator = None

    def total_expanded():
        value = sum(g.get('totalExpandedPrefixes', g.get('expandedPrefixes', 0)) for g in state['generationReports'])
        if state['current'] is not None:
            value += state['current']['generation'].get('continuation', {}).get('expanded', 0)
        return value

    def spatial_expanded():
        records = [r for window in state['windows'] for r in window['candidates']]
        records.extend(state['current']['records'] if state['current'] is not None else state['finalAttempts'])
        return sum(r.get('spatial_probe', {}).get('measurement', {}).get('generation', {}).get('expandedPrefixes', 0)
                   for r in records)

    initial_spatial_expanded = spatial_expanded()

    def publish():
        state['cumulativeCalls'] = prior_calls+work.counts['solverCalls']
        if on_progress is not None:
            on_progress(state)

    def physical_program(program):
        return {k: copy.deepcopy(v) for k, v in program.items()
                if k not in ('repairState', 'guideSeedYaw') and not k.startswith('_')}

    def outstanding(program):
        if hasattr(program_filter, 'remaining'):
            return program_filter.remaining(program)
        if terminal_plan is None:
            p = program['profile']
            return [] if p['grounded'][-1] and program['terminalSupport'] == scene['goalBlock'] else [scene['goalBlock']]
        return None

    def viability(program):
        required = outstanding(program)
        if required is None:
            return dict(dead=False, reason='outstanding_requirements_unspecified')
        key = digest(dict(boundary={k: program['profile'][k][-1] for k in ('position', 'velocity', 'grounded')},
            tick=program['profile']['airtime'], required=required, types=cfg['types']))
        if key not in viability_cache:
            viability_cache[key] = prefix_viability(scene, program, required_contacts=required,
                terminal_tick=terminal_plan.tick if terminal_plan is not None else cfg['maxInputs'], enabled_types=cfg['types'])
        return viability_cache[key]

    def screen(program):
        checked = viability(program)
        viability_counts['checked'] += 1
        if checked['dead']:
            viability_counts['excluded'] += 1
            program['viability'] = checked
            return checked
        profile = program['profile']
        if profile['position'][1] < scene['initial']['position'][1]-1e-7:
            for block in scene['blocks']:
                if abs(block['top']-scene['initial']['position'][1]) < 1e-7 and supports(
                        block, scene['initial']['position'][0], scene['initial']['position'][2]):
                    return dict(reason='unselected_initial_floor')
        return screen_program(scene, program, counted, max(.001, min(candidate_seconds, deadline-time.monotonic())))

    try:
        while state['stage'] < len(stages):
            work.check()
            target = stages[state['stage']]
            final_clock = terminal_plan is not None and target is terminal_plan
            window_goal = goals_by_tick.get(target.tick, spatial_goal) if target is not None else spatial_goal
            if state['current'] is None:
                if not state['frontier']:
                    termination = 'goal_found' if state['best'] else 'no_retained_prefix'
                    break
                state['current'] = dict(roots=state['frontier'], records=[], generation={}, numerical=[], ready=[], inFlight=None)
            current = state['current']
            current.setdefault('ready', [])
            report = current['generation']
            if resume_state is not None:
                for p in current['numerical']+current['ready']+([current['inFlight']] if current['inFlight'] else []):
                    repair = p.get('repairState', {})
                    prior_profiles = repair.get('context', {}).get('profiles', [])
                    if (repair.get('context', {}).get('scope') or {}).get('solveIdentity') and prior_profiles and prior_profiles[0]['airtime'] != p['profile']['airtime']:
                        p.pop('repairState', None)
                    if retry_unknown:
                        p.pop('_continuedNumerical', None)
                    else:
                        p['_continuedNumerical'] = True
                queued = {_key(p) for p in current['numerical']}
                if current['inFlight'] is not None:
                    queued.add(_key(current['inFlight']))
                for r in current['records']:
                    p = r.get('program')
                    repair = p.get('repairState', {}) if p else {}
                    if retry_unknown and p and repair.get('unresolved') and r['id'] not in queued:
                        current['numerical'].append(copy.deepcopy(p))
                resume_state = None
            local_cfg = dict(cfg, maxPrefixes=max(1, cfg['maxPrefixes']-(total_expanded()-initial_expanded)))
            generator = fair_programs(scene, current['roots'], cfg=local_cfg, fair=fair_parents, report=report,
                deadline=deadline, cancel_event=cancel_event, context_adapter=adapter.context, work=work,
                screen=screen, checkpoint_tick=target.tick if target else None, threshold=0.,
                program_filter=accepted_program, stop_on_goal=terminal_plan is None,
                persistent=True, continuation=report.get('continuation'))
            records = {r['id']: r for r in current['records']}
            source_done = report.get('continuation', {}).get('exhausted', False)
            while True:
                work.check()
                if cancel_event is not None and cancel_event.is_set():
                    raise TimeoutError('cancelled')
                if current['inFlight'] is not None:
                    program = current['inFlight']
                else:
                    while len(current['ready']) < max_candidates and not source_done:
                        try:
                            new_program = next(generator)
                        except StopIteration:
                            source_done = True
                            if report.get('termination') in ('deadline', 'cancelled', 'numerical_call_limit'):
                                termination = report['termination']
                                break
                            if report.get('termination') == 'prefix_limit':
                                termination = 'prefix_limit'
                        else:
                            if _key(new_program) not in records:
                                current['ready'].append(new_program)
                    if work.termination or termination in ('deadline', 'cancelled', 'numerical_call_limit'):
                        break
                    available = current['ready']+current['numerical']
                    if not available:
                        if source_done:
                            break
                        continue
                    program = min(available, key=lambda p: processing_key(p, _key(p)))
                    (current['ready'] if program in current['ready'] else current['numerical']).remove(program)
                    current['inFlight'] = program
                publish()
                identity = _key(program)
                complete = final_clock or terminal_plan is None and program['terminalSupport'] == scene['goalBlock'] and program['profile']['grounded'][-1]
                actual_target = target if final_clock else None if complete else target
                if actual_target is None and not complete:
                    raise RuntimeError('Final generation emitted an open prefix')
                checked = viability(program)
                if checked['dead']:
                    viability_counts['excluded'] += 1
                    state['unevaluatedCandidates'].append(dict(id=identity, window=state['stage'], program=physical_program(program),
                        selection='provably_dead', proof=checked))
                    current['inFlight'] = None
                    publish()
                    continue
                def repair_progress(repair):
                    current['inFlight']['repairState'] = repair
                    publish()
                remaining = deadline-time.monotonic()
                result = evaluate_program(scene, program, counted, adapter, work, motion, checkpoint=actual_target,
                    seconds=max(.001, min(score_seconds, remaining)), candidate_seconds=candidate_seconds,
                    solver_calls=final_call_limit if complete else score_call_limit,
                    python_executable=python_executable, cancel_event=cancel_event,
                    required_landing=scene['goalBlock'] if complete and not final_clock and comparison_policy != 'objective_band' else None,
                    on_progress=repair_progress, retry_unresolved=not program.get('_continuedNumerical', False))
                previous = records.get(identity)
                record = dict(id=identity, ticks=program['profile']['airtime'], complete=complete,
                    methods=[u.get('case', u['type']) for u in program['movementUnits']], status=result['status'],
                    termination=result['termination'], value=result['best']['candidate']['objective'] if result['best'] else None,
                    work=result['work'], comparisonStage=result.get('comparisonStage'), parent=program.get('searchParent'),
                    decision_program=physical_program(program), spatial_results=copy.deepcopy(previous.get('spatial_results', {})) if previous else {},
                    clock_results={}, solveScope=result.get('solveScope'), solveIdentity=result.get('solveIdentity'), viability=checked)
                if policy is not None:
                    record['decisionAudit'] = policy.audit(program)
                record['evaluations'] = (previous.get('evaluations', []) if previous else [])+[
                    dict(session=state['session'], work=result['work'], termination=result['termination'],
                         retryUnresolved=not program.get('_continuedNumerical', False))]
                if target is not None:
                    clock_key, measurement = clock_result(target, result)
                    record['clock_results'][clock_key] = measurement
                    record['required_clocks'] = [clock_key]
                spatial = None
                if not previous and not final_clock and comparison_policy != 'objective_band' and target is not None and (not complete or
                        window_goal != dict(kind='landing', block=scene['goalBlock'])):
                    try:
                        work.check()
                        spatial = probe_spatial(scene, program, counted, adapter, work, motion, spec=window_goal,
                            seconds=max(.001, min(spatial_seconds, deadline-time.monotonic())),
                            candidate_seconds=candidate_seconds, solver_calls=spatial_calls, max_candidates=spatial_candidates,
                            max_prefixes=spatial_prefixes, python_executable=python_executable,
                            cancel_event=cancel_event, program_filter=accepted_program)
                    except TimeoutError:
                        pass
                if spatial is not None:
                    record['spatial_results'][spatial['key']] = spatial['measurement']
                    record['spatial_probe'] = spatial
                elif complete and not final_clock and result['best'] is not None and window_goal == dict(kind='landing', block=scene['goalBlock']):
                    skey = goal_key(scene, window_goal, corridor)
                    record['spatial_results'][skey] = dict(status='feasible', ticks=program['profile']['airtime'])
                if previous:
                    for name in ('spatial_probe', 'required_spatial'):
                        if name in previous:
                            record[name] = previous[name]
                if target is not None:
                    record['required_spatial'] = [goal_key(scene, window_goal, corridor)]
                record['program'] = retain_decisions(program, result,
                    facing_guess=spatial['best']['candidate']['yaw'] if spatial is not None and spatial['best'] is not None else None)
                record['program'].pop('_continuedNumerical', None)
                if result.get('repairState') is not None:
                    record['numericalAttempts'] = result['repairState'].get('attemptLog', result.get('attempts', []))
                    record['contactAttempts'] = result['repairState'].get('contactLog', result.get('contactAttempts', []))
                if result['best'] is not None:
                    record['witness'] = result['best']
                    if not complete:
                        record['features'] = score_features(result['best'], actual_target,
                            goal_box=policy.goal_box if policy is not None else None)
                    if complete and result.get('fullSceneValidated', True):
                        candidate = copy.deepcopy(result['best'])
                        candidate.update(movementProgram=physical_program(program), blockIds=[scene['startBlock'], scene['goalBlock']])
                        if state['best'] is None or candidate_rank(candidate, scene['objective']) < candidate_rank(state['best'], scene['objective']):
                            state['best'] = candidate
                            work.accepted(candidate)
                if previous:
                    current['records'][current['records'].index(previous)] = record
                else:
                    current['records'].append(record)
                records[identity] = record
                repair = result.get('repairState', {})
                if repair.get('pending') or repair.get('inFlight') or repair.get('constraintGeneration') and not repair['constraintGeneration'].get('exhausted'):
                    pending = copy.deepcopy(record['program'])
                    pending['_continuedNumerical'] = True
                    current['numerical'].append(pending)
                current['inFlight'] = None
                report['evaluatedBatches'] = (len(current['records'])+max_candidates-1)//max_candidates
                publish()
                if result['status'] in ('error', 'cancelled'):
                    termination = result['status']
                    break
            generator.close()
            if termination in ('error', 'cancelled', 'deadline', 'numerical_call_limit', 'prefix_limit') or work.termination:
                termination = work.termination or termination
                break
            batch = current['records']
            if target is None or final_clock:
                state['finalAttempts'] = batch
                termination = 'numerical_unknown' if any(r.get('program', {}).get('repairState', {}).get('unresolved') for r in batch) else report.get('termination', 'exhausted')
                if termination != 'numerical_unknown':
                    state['generationReports'].append(copy.deepcopy(report))
                    state['current'] = None
                    state['stage'] += 1
                publish()
                break
            pairs = [(r, r['program']) for r in batch]
            if comparison_policy == 'objective_band':
                solved = [(r, p) for r, p in pairs if r.get('witness')]
                unknown = [(r, p) for r, p in pairs if not r.get('witness')]
                retained, retention = retain_candidates(solved, unknown, cap=beam_width, unknown_slots=unknown_slots,
                    epsilon=objective_gap, order=order)
                deferred = [(r, p) for r, p in pairs if r['id'] not in {v['id'] for v, _ in retained}]
            else:
                retained, retention = retain_two_goals(pairs, cap=beam_width, unknown_slots=unknown_slots,
                    time_margin=time_margin, progress_margin=progress_margin, prune=comparison_policy != 'unpruned')
                deferred = [(r, p) for r, p in pairs if r['id'] in retention.get('deferred', [])]
                retention.update(outsideBand=0, scoredCapDrops=0, unknownCapDrops=0)
            frontier = sorted(retained+deferred, key=lambda pair: processing_key(pair[1], pair[0]['id']))
            selected = {r['id'] for r, _ in frontier}
            for r, p in pairs:
                if r['id'] not in selected:
                    state['parkedCandidates'].append(dict(id=r['id'], window=state['stage'], program=p, selection='markedly_worse'))
            window = dict(checkpoint=target.to_dict(), generation=copy.deepcopy(report), candidates=batch,
                retained=[r['id'] for r, _ in retained], deferred=[r['id'] for r, _ in deferred],
                scored=sum(bool(r.get('witness')) for r in batch), unknown=sum(not r.get('witness') for r in batch),
                heuristicDrops=len(pairs)-len(frontier), order=order, retention=retention)
            state['windows'].append(window)
            state['frontier'] = [p for _, p in frontier]
            state['generationReports'].append(copy.deepcopy(report))
            state['current'] = None
            state['stage'] += 1
            publish()
            if on_window is not None:
                on_window(window)
    except TimeoutError as error:
        termination = work.termination or str(error)
    finally:
        if generator is not None:
            generator.close()
    if state['current'] is not None and state['stage'] == len(stages)-1:
        state['finalAttempts'] = state['current']['records']
    publish()
    current_report = [state['current']['generation']] if state['current'] is not None else []
    return dict(status='feasible' if state['best'] is not None else 'cancelled' if termination == 'cancelled' else 'error' if termination == 'error' else 'unknown',
        termination=termination, best=state['best'], windows=state['windows'], finalAttempts=state['finalAttempts'],
        corridor=corridor.to_dict(), routeGeneration=route_report, adapter=adapter.summary(),
        movementGeneration=state['generationReports']+current_report, expandedPrefixes=total_expanded()-initial_expanded,
        spatialExpandedPrefixes=spatial_expanded()-initial_spatial_expanded,
        totalExpandedPrefixes=total_expanded()+spatial_expanded(), work=work.summary(), compiler=counted.summary(),
        elapsedSeconds=time.monotonic()-started, order=order, threshold=0., checkpoints=[p.to_dict() for p in plans],
        decisionPlan=window_plan, objectiveGap=objective_gap, fairParents=fair_parents, comparisonPolicy=comparison_policy,
        spatialGoal=spatial_goal, parkedCandidates=state['parkedCandidates'], rescues=[],
        unevaluatedCandidates=state['unevaluatedCandidates'], comparisonMargins=dict(time=time_margin, progress=progress_margin),
        objectiveOptimalityProven=False, minimumTicksProven=False,
        terminalCheckpoint=terminal_plan.to_dict() if terminal_plan is not None else None,
        spatialGoalsByCheckpoint=goals_by_tick, goalType='fixed_absolute_tick_objective' if terminal_plan is not None else 'landing_time',
        searchState=state, resume=dict(exact=state['session'] > 1, session=state['session']), viability=viability_counts)
