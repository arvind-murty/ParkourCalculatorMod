import copy
import hashlib
import math
import time

from standalone.autonomy.block_contacts import floor_checks, solve_contact_branches
from standalone.autonomy.block_search import prepare_scene
from standalone.autonomy.movement_composition import compile_program, empty_program, program_search_key
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
                     partial_corridor=False, required_landing=None):
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
        verdict = adapter.candidate(plan, candidate, vertical, partial=not complete or partial_corridor)
        if verdict['accepted'] and required_landing is not None and not landing_satisfied(scene,required_landing,vertical):
            return dict(verdict,accepted=False,reason='spatial_goal_not_satisfied')
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
        candidate_filter=candidate_filter, work=scoped, resume_state=program.get('repairState'),
        repair_scope=(dict(corridor=adapter.corridor.to_dict(), gates=adapter.gates) if hasattr(adapter, 'corridor') else None))
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
    return hashlib.sha256(program_search_key(program).encode()).hexdigest()


def find_checkpoint_route(request, compiler, *, checkpoints=None, corridor=None, spatial_points=None, seconds=30,
                          candidate_seconds=1, score_seconds=3, max_solver_calls=128,
                          max_candidates=24, beam_width=4, unknown_slots=1, score_call_limit=8,
                          final_call_limit=24, order='guided', python_executable=None, cancel_event=None,
                          opportunities=None, cluster_gap=2, objective_gap=None, fair_parents=False,
                          initial_programs=None, comparison_policy='time_progress', spatial_goal=None,
                          spatial_seconds=1.5, spatial_calls=4, spatial_candidates=4, spatial_prefixes=64,
                          time_margin=1, progress_margin=None, rescue_passes=1, terminal_checkpoint=None, program_filter=None,
                          on_window=None):
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
        return (policy is None or policy(program)) and (program_filter is None or program_filter(program))
    specs = window_plan['checkpoints'] if window_plan is not None else checkpoints or []
    plans = [checkpoint(corridor, gates, spec) for spec in specs]
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
    seed = empty_program(scene['initial'], threshold=0.)
    seed['terminalSupport'] = scene['startBlock']
    frontier = [seed] if initial_programs is None else list(initial_programs)
    if not frontier or any(p['initial']!=seed['initial'] or p.get('threshold',.005)!=0. for p in frontier):
        raise ValueError('Initial programs must preserve the original state and zero thresholds')
    if any(not accepted_program(p) for p in frontier):
        raise ValueError('Initial programs violate the opportunity ranges')
    if initial_programs:
        completed=min(p['profile']['airtime'] for p in initial_programs)
        plans=[p for p in plans if p.tick>completed]
    windows, best, final_attempts, generation_reports, parked, unevaluated = [], None, [], [], [], []
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
        for target in plans+[terminal_plan]:
            final_clock=terminal_plan is not None and target is terminal_plan
            work.check()
            if expanded >= cfg['maxPrefixes']:
                termination = 'prefix_limit'
                break
            if not frontier:
                termination = 'goal_found' if best else 'no_retained_prefix'
                break
            report, batch, solved, unknown, all_candidates, seen = {}, [], [], [], [], set()
            local_cfg = dict(cfg, maxPrefixes=cfg['maxPrefixes']-expanded)
            generator = fair_programs(scene,frontier,cfg=local_cfg,fair=fair_parents,
                deadline=deadline, cancel_event=cancel_event,report=report, context_adapter=adapter.context, work=work,
                screen=screen,
                checkpoint_tick=target.tick if target else None, threshold=0., program_filter=accepted_program,
                stop_on_goal=terminal_plan is None)
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
                    unevaluated.append(dict(id=identity,window=len(windows),selection='candidate_resource_limit',
                                            program=copy.deepcopy(program)))
                    capped = True
                    break
                complete = final_clock or terminal_plan is None and program['terminalSupport'] == scene['goalBlock'] and program['profile']['grounded'][-1]
                if terminal_plan is None and comparison_policy!='unpruned' and best is not None and program['profile']['airtime'] > best['plan']['ticks']:
                    unevaluated.append(dict(id=identity,window=len(windows),selection='elapsed_goal_cutoff',
                                            program=copy.deepcopy(program)))
                    continue
                actual_target = target if final_clock else None if complete else target
                if actual_target is None and not complete:
                    raise RuntimeError('Final generation emitted an open prefix')
                remaining = deadline-time.monotonic()
                result = evaluate_program(scene, program, counted, adapter, work, motion, checkpoint=actual_target,
                    seconds=max(.001, min(score_seconds, remaining)), candidate_seconds=candidate_seconds,
                    solver_calls=final_call_limit if complete else score_call_limit,
                    python_executable=python_executable, cancel_event=cancel_event,
                    required_landing=scene['goalBlock'] if complete and not final_clock and comparison_policy!='objective_band' else None)
                spatial = None
                if not final_clock and comparison_policy!='objective_band' and target is not None and (not complete or
                        spatial_goal!=dict(kind='landing',block=scene['goalBlock'])):
                    try:
                        work.check()
                        spatial = probe_spatial(scene,program,counted,adapter,work,motion,spec=spatial_goal,
                            seconds=max(.001,min(spatial_seconds,deadline-time.monotonic())),
                            candidate_seconds=candidate_seconds,solver_calls=spatial_calls,
                            max_candidates=spatial_candidates,max_prefixes=spatial_prefixes,
                            python_executable=python_executable,cancel_event=cancel_event,program_filter=accepted_program)
                    except TimeoutError:
                        pass
                record = dict(id=identity, ticks=program['profile']['airtime'], complete=complete,
                    methods=[u.get('case', u['type']) for u in program['movementUnits']], status=result['status'],
                    termination=result['termination'], value=result['best']['candidate']['objective'] if result['best'] else None,
                    work=result['work'], comparisonStage=result.get('comparisonStage'))
                record['parent'] = program.get('searchParent')
                record['decision_program'] = copy.deepcopy(program)
                record['spatial_results'],record['clock_results'] = {},{}
                if target is not None:
                    clock_key,measurement=clock_result(target,result)
                    record['clock_results'][clock_key]=measurement
                    record['required_clocks']=[clock_key]
                if spatial is not None:
                    record['spatial_results'][spatial['key']]=spatial['measurement']
                    record['spatial_probe']=spatial
                elif complete and not final_clock and result['best'] is not None and spatial_goal==dict(kind='landing',block=scene['goalBlock']):
                    record['spatial_results'][goal_key(scene,spatial_goal,corridor)]=dict(
                        goal=spatial_goal,status='feasible',ticks=result['best']['plan']['ticks'],minimumProven=False,
                        interpretation='elapsed_time_to_shared_spatial_goal_not_clock_objective')
                spatial_key=goal_key(scene,spatial_goal,corridor)
                record['required_spatial']=[spatial_key]
                if spatial_key not in record['spatial_results']:
                    record['spatial_results'][spatial_key]=dict(goal=spatial_goal,status='unknown',ticks=None)
                if terminal_plan is None and spatial is not None and spatial['best'] is not None and spatial['finalGoal']:
                    completion=spatial['best']
                    completion.update(movementProgram=spatial['program'],blockIds=[scene['startBlock'],scene['goalBlock']])
                    if best is None or candidate_rank(completion,scene['objective'])<candidate_rank(best,scene['objective']):
                        best=completion
                        work.accepted(best)
                        if comparison_policy!='unpruned':report['maxAcceptedTicks']=best['plan']['ticks']
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
                            if terminal_plan is None and comparison_policy!='unpruned':report['maxAcceptedTicks'] = best['plan']['ticks']
                    else:
                        solved.append((record, retain_prefix(program, result['best'])))
                elif not complete:
                    record['repairResume'] = result.get('repairResume')
                    pending = retain_unresolved(program, result.get('repairState'))
                    record['program'] = pending
                    unknown.append((record, pending))
                if not complete and comparison_policy!='objective_band':
                    guess=spatial['best']['candidate']['yaw'] if spatial is not None and spatial['best'] is not None else None
                    carried=retain_decisions(program,result,facing_guess=guess)
                    all_candidates.append((record,carried))
                    record['program']=carried
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
            if target is None or final_clock:
                final_attempts = batch
                if termination not in ('error','cancelled','deadline','numerical_call_limit'):
                    termination = report.get('termination', 'search_finished')
                break
            if comparison_policy=='objective_band':
                retained, retention = retain_candidates(solved,unknown,cap=beam_width,unknown_slots=unknown_slots,
                                                       epsilon=objective_gap,order=order)
            else:
                retained,retention=retain_two_goals(all_candidates,cap=beam_width,unknown_slots=unknown_slots,
                    time_margin=time_margin,progress_margin=progress_margin,prune=comparison_policy!='unpruned')
                retention.update(outsideBand=0,scoredCapDrops=retention['resourceDrops'],unknownCapDrops=0)
            selected={r['id'] for r,_ in retained}
            if comparison_policy!='objective_band':
                for r,p in all_candidates:
                    if r['id'] not in selected:
                        parked.append(dict(id=r['id'],window=len(windows),program=p,
                            selection=retention['decisions'][r['id']]['reason']))
            frontier = [p for _, p in retained]
            windows.append(dict(checkpoint=target.to_dict(), generation=report, candidates=batch,
                retained=[r['id'] for r, _ in retained], scored=len(solved), unknown=len(unknown),
                heuristicDrops=max(0, len(solved)+len(unknown)-len(retained)), order=order,retention=retention))
            if on_window is not None:on_window(windows[-1])
            if termination in ('error', 'cancelled', 'deadline', 'numerical_call_limit'):
                break
    except TimeoutError:
        termination = work.termination or 'deadline'
    rescues=[]
    if best is None and comparison_policy=='time_progress' and rescue_passes and parked:
        for item in parked[:rescue_passes]:
            remaining=deadline-time.monotonic()
            calls=max_solver_calls-work.counts['solverCalls']
            prefix_budget=cfg['maxPrefixes']-expanded
            if remaining<=0 or calls<=0 or prefix_budget<=0 or cancel_event is not None and cancel_event.is_set():
                break
            rescue_started=time.monotonic()
            rescue_request=copy.deepcopy(request)
            rescue_request.setdefault('movementSearch',{})['maxPrefixes']=prefix_budget
            rescued=find_checkpoint_route(rescue_request,counted,checkpoints=checkpoints,opportunities=opportunities,
                corridor=corridor,seconds=remaining,candidate_seconds=candidate_seconds,score_seconds=score_seconds,
                max_solver_calls=calls,max_candidates=max_candidates,beam_width=beam_width,unknown_slots=unknown_slots,
                score_call_limit=score_call_limit,final_call_limit=final_call_limit,order=order,
                python_executable=python_executable,cancel_event=cancel_event,cluster_gap=cluster_gap,
                objective_gap=objective_gap,fair_parents=fair_parents,initial_programs=[item['program']],
                comparison_policy=comparison_policy,spatial_goal=spatial_goal,spatial_seconds=spatial_seconds,
                spatial_calls=spatial_calls,spatial_candidates=spatial_candidates,spatial_prefixes=spatial_prefixes,
                time_margin=time_margin,progress_margin=progress_margin,rescue_passes=0,terminal_checkpoint=terminal_plan,
                program_filter=program_filter,on_window=on_window)
            rescues.append(dict(candidate=item['id'],selection=item['selection'],result=rescued))
            for counter in ('solverCalls','numericalProposals','acceptedBranches'):
                work.counts[counter]+=rescued['work'].get(counter,0)
            if work.first_accepted is None and rescued['work'].get('firstAcceptedSeconds') is not None:
                work.first_accepted=rescue_started-work.started+rescued['work']['firstAcceptedSeconds']
            expanded+=rescued.get('expandedPrefixes',0)
            if rescued['best'] is not None:
                best=rescued['best']
                termination='rescue_found'
                break
    spatial_expanded=sum(c.get('spatial_probe',{}).get('measurement',{}).get('generation',{}).get('expandedPrefixes',0)
        for window in windows for c in window['candidates'])
    spatial_expanded+=sum(r['result'].get('spatialExpandedPrefixes',0) for r in rescues)
    return dict(status='feasible' if best else 'cancelled' if termination == 'cancelled' else 'error' if termination == 'error' else 'unknown',
        termination=termination, best=best, windows=windows, finalAttempts=final_attempts,
        corridor=corridor.to_dict(), routeGeneration=route_report, adapter=adapter.summary(),
        movementGeneration=generation_reports, expandedPrefixes=expanded,spatialExpandedPrefixes=spatial_expanded,
        totalExpandedPrefixes=expanded+spatial_expanded,
        work=work.summary(), compiler=counted.summary(), elapsedSeconds=time.monotonic()-started,
        order=order, threshold=0., checkpoints=[p.to_dict() for p in plans],
        decisionPlan=window_plan,objectiveGap=objective_gap,fairParents=fair_parents,
        comparisonPolicy=comparison_policy,spatialGoal=spatial_goal,parkedCandidates=parked,rescues=rescues,unevaluatedCandidates=unevaluated,
        comparisonMargins=dict(time=time_margin,progress=progress_margin),
        objectiveOptimalityProven=False, minimumTicksProven=False,
        terminalCheckpoint=terminal_plan.to_dict() if terminal_plan is not None else None,
        goalType='fixed_absolute_tick_objective' if terminal_plan is not None else 'landing_time')
