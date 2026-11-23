import copy
from collections import Counter
import math
import time

from parkour_solver.constraints.contacts import floor_checks
from parkour_solver.search.repair import solve_contact_branches
from parkour_solver.scene.normalize import prepare_scene
from parkour_solver.schema import integer, number
from parkour_solver.bridge.counting import CountingCompiler; from parkour_solver.work import SearchWork
from parkour_solver.search.goals import landing_satisfied
from parkour_solver.search.identity import solve_scope, solve_identity, validation_prefix
from parkour_solver.reachability.intervals import motion_bound
from parkour_solver.constraints.variants import ConstraintVariants, check_generation


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


def segment_solids(scene, program, profile, through, gate):
    from parkour_solver.constraints.encounters import landing_segments
    from parkour_solver.search.anchors.policy import launch
    result = {}
    for begin, end in landing_segments(profile, through):
        found = launch(scene, program, begin, end)
        if found is None:
            continue
        table = gate.reachable_faces(found[1])
        if end-found[0] <= table['ticks']:
            result[begin] = sorted(table['solids'])
    return result


def evaluate_program(scene, program, compiler, adapter, work, motion_bound, *, seconds=3, candidate_seconds=1,
                     python_executable=None, cancel_event=None, required_landing=None, scope=None, on_progress=None,
                     retry_unresolved=True, first_witness=False, guide_objective=None, solve_policy=None,
                     climb_families=False, final_route=None):
    timings={}
    marked=time.monotonic()
    def mark(name):
        nonlocal marked
        now=time.monotonic();timings[name]=now-marked;marked=now
    complete = not getattr(adapter,'node_query',False)
    from parkour_solver.search.feasibility import policy_spec, outcome
    policy = policy_spec(solve_policy, first_witness=first_witness)
    from parkour_solver.reachability.service import InteractionGate
    service=getattr(adapter,'interaction_service',None) or InteractionGate(scene,compiler,seconds=max(.001,min(seconds,candidate_seconds)))
    prepared=service.prepare(program,scene=scene,require_complete=complete)
    if prepared is None:
        return dict(status='unknown',termination='vertical_schedule',best=None,attempts=[],contactAttempts=[],work=dict(solverCalls=0),fullSceneValidated=False)
    route,profiles=copy.copy(prepared['route']),prepared['profiles']
    route['constraints']=list(route.get('constraints',[]))
    mark('programCompilation')
    if guide_objective is not None:
        route['objective']=dict(axis='X',sense='MAX')
        route['guideObjective']=copy.deepcopy(guide_objective)
    scope = solve_scope(scene, program, scope)
    scoped_adapter = adapter.scoped(scope) if hasattr(adapter, 'scoped') else adapter
    if hasattr(scoped_adapter,'bind_program'):
        scoped_adapter = scoped_adapter.bind_program(program)
    scoped = ScopedWork(work, None, min(work.deadline or math.inf, time.monotonic()+seconds))
    owners = [b['sourceBlock'] for b in scene['blocks']]
    def candidate_filter(plan, candidate, vertical):
        p, c, v = validation_prefix(plan, candidate, vertical, scope['through'])
        # Anchor search always validates a partial corridor: its frames see only their own goal.
        verdict = scoped_adapter.candidate(p, c, v, partial=True)
        if verdict['accepted'] and required_landing is not None and not landing_satisfied(scene,required_landing,vertical):
            return dict(verdict,accepted=False,reason='spatial_goal_not_satisfied')
        return verdict

    from parkour_solver.scene.route import schedule
    plan = schedule(route, [0])
    vertical = prepared['vertical']
    if not vertical['feasible']:
        return dict(status='unknown', termination='vertical_schedule', best=None, attempts=[], contactAttempts=[],
                    work=scoped.local.summary(), solveScope=scope, fullSceneValidated=False)
    plan['vertical'] = dict(vertical['vertical'], referencePosition=vertical['y'], referenceVelocity=vertical['vy'])
    mark('scopeAndVerticalPreparation')
    analysis=service.analyze_model(route,plan,vertical,scope=scope)
    coefficients=analysis['coefficient']
    mark('coefficientCompilation')
    model = dict(coefficients, version=1, ticks=plan['ticks'], walls=plan['walls'], contacts=plan['contacts'],
        position=scene['initial']['position'][::2], velocity=scene['initial']['velocity'][::2],
        threshold=route.get('physicsThreshold', .005), vertical=vertical['vertical'], objective=dict(route.get('guideObjective', route['objective']), tick=plan['ticks']))
    envelopes=analysis['envelopes']
    mark('reachability')
    from parkour_solver.search.ordered_route import initial_guess
    from parkour_solver.numerics import prepare, validate
    reference_yaw = initial_guess(route, plan, None)
    reference = validate(prepare(dict(model, yaw=reference_yaw)), reference_yaw)['positions']
    active = set(scope['solidIndices'])
    walk_offs = program.get('walkOffs', [])
    # A carried walk-off face selection replaces the generic floor checks on its support at its tick.
    checks = [c for c in floor_checks(scene, [scene['startBlock'], scene['goalBlock']], profiles, envelopes=envelopes)
              if c['block']['solidIndex'] in active and c['offset'] < scope['through']
              and not any(c['offset'] == w['offset'] and c['block']['solidIndex'] in w['solids'] for w in walk_offs)]
    adapter_identity=getattr(scoped_adapter,'corridor',None).to_dict() if hasattr(scoped_adapter,'corridor') else None
    identity = solve_identity(scene, program, route.get('guideObjective', route['objective']), scope,
        dict(adapter=adapter_identity,policy=policy,nodeQuery=getattr(scoped_adapter,'node_query',False)), coefficients)
    saved_repair=program.get('repairState',{})
    saved_policy=(saved_repair.get('context',{}).get('scope') or {}).get('scope',{}).get('solvePolicy')
    repair_route = route if final_route is None else final_route
    # Under PKC_NATIVE_CORE the generator and the repair loop run natively for the routes they cover: no passages,
    # wall hits or ladder events.
    native = getattr(service, 'native', None)
    deadline = scoped.deadline
    if (native is not None and
            not any(r.get(k) for r in (route, repair_route) for k in ('passages', 'hitEvents', 'ladderEvents'))):
        variants = native.ConstraintVariants(route=route, profile=profiles[0], geometry=scene['collisionScene'],
            envelopes=envelopes, scope=scope, checks=list(checks),
            state=saved_repair.get('constraintGeneration') if saved_policy==policy else None, reference=reference,
            owners=owners, check=lambda: check_generation(cancel_event, scoped.check, deadline),
            segment_solids=segment_solids(scene, program, profiles[0], scope['through'], service),
            objective=route.get('guideObjective', route['objective']), settled=program.get('settledThrough', 0),
            obstacles=[], walk_offs=list(walk_offs))
    else:
        variants = ConstraintVariants(route, profiles[0], plan, scene['collisionScene'], envelopes, scope,
            segment_solids=segment_solids(scene, program, profiles[0], scope['through'], service), checks=checks, walk_offs=walk_offs,
            state=saved_repair.get('constraintGeneration') if saved_policy==policy else None, deadline=deadline, reference=reference,
            settled=program.get('settledThrough', 0),
            # Conflict-driven: obstacles join the generator only once a solve hits them.
            obstacles=[],
            owners=owners, budget=scoped.check, cancel_event=cancel_event,
            objective=route.get('guideObjective', route['objective']))
        native = None
    mark('eligibilityAndVariantConstruction')
    dispatch = None
    if policy['numerical'] == 'relaxed':
        from parkour_solver.numerics.disk import RelaxedDispatch
        dispatch = RelaxedDispatch(gate=service)
    # Finalization re-solves a relaxed witness's own branch route with IPOPT.
    result = solve_contact_branches(repair_route, profiles, checks,
        compiler, seconds=seconds, candidate_seconds=candidate_seconds, retries=0,
        improve=True, rank_equal_cost=policy['rankEqualCost'], optimize_goal=True,
        stop_on_feasible=policy['stopOnFeasible'], oracle_rejection=policy['oracleRejection'], climb_families=climb_families,
        python_executable=python_executable, cancel_event=cancel_event, geometry=scene['collisionScene'],
        motion_bound=motion_bound, solid_blocks=owners, max_wall_hits=0,
        candidate_filter=candidate_filter, work=scoped, resume_state=program.get('repairState'),
        repair_scope=dict(solveIdentity=identity, scope=scope, solvePolicy=policy, assemblyVersion=14),
        validation_scope=scope, initial_variants=variants if final_route is None else None, on_progress=on_progress,
        retry_unresolved=retry_unresolved, numerical=dispatch.solve if dispatch is not None else None,
        confirm=dispatch.confirmed if dispatch is not None else None, native=native)
    mark('physicalSearchAndValidation')
    generation = variants.state
    diagnostics=[d for a in result.get('attempts',[]) for d in a.get('diagnostics',[])]
    parent_work=work
    while not hasattr(parent_work,'counts'):
        parent_work=parent_work.parent
    counts=numerical_counts(diagnostics)
    for counter in (parent_work.counts,scoped.local.counts):
        counter.update(counts)
    result['timings']=dict(timings,**generation['timings'])
    if result['best'] is not None and scope['purpose'] == 'complete':
        work.accepted(result['best'])
    result['work'] = scoped.local.summary()
    result.update(solveScope=scope, solveIdentity=identity,
                  solvePolicy=policy,objectiveMode='configured', nodeFeasibility=outcome(result, identity, policy),
                  fullSceneValidated=result['best'] is not None and scope['purpose'] == 'complete',
                  reachability=dict(excludedSolids=generation['exclusions'], interactionGroups=len(generation['items'])))
    if result['best'] is not None:
        result['best']['validationScope'] = scope
    return result


def solve_services(adapter, program, work, service, numerical, *, seconds, cancel_event):
    """A native node solve's Python services (search/engine.hpp's hooks): the scoped work, the relaxed dispatch, the
    gate's analysis and face tables (served by its native core when it has one) and the adapter's candidate check
    (None without an adapter: the native search's own adapter checks candidates)."""
    scoped = ScopedWork(work, None, min(work.deadline or math.inf, time.monotonic()+seconds))
    deadline = scoped.deadline
    dispatch = None
    if numerical == 'relaxed':
        from parkour_solver.numerics.disk import RelaxedDispatch
        dispatch = RelaxedDispatch(gate=service)
    views = {}
    def candidate(plan, value, vertical, solve_scope):
        if 'view' not in views:  # the adapter scoped to the native solve's scope and bound to the program
            view = adapter.scoped(solve_scope) if hasattr(adapter, 'scoped') else adapter
            views['view'] = view.bind_program(program) if hasattr(view, 'bind_program') else view
        return views['view'].candidate(plan, value, vertical, partial=True)
    def reachable_faces(name):
        table = service.reachable_faces(name)
        return dict(ticks=table['ticks'], solids=sorted(table['solids']))
    def record_counts(counts):
        parent_work = work
        while not hasattr(parent_work, 'counts'):
            parent_work = parent_work.parent
        for counter in (parent_work.counts, scoped.local.counts):
            counter.update(counts)
    return dict(work=scoped, numerical=dispatch.solve if dispatch is not None else None,
        confirm=dispatch.confirmed if dispatch is not None else None,
        analyze_model=lambda route, plan, vertical, scope: service.analyze_model(route, plan, vertical, scope=scope),
        reachable_faces=reachable_faces, candidate=candidate if adapter is not None else None,
        check_generation=lambda: check_generation(cancel_event, scoped.check, deadline), record_counts=record_counts,
        accepted=work.accepted, work_summary=scoped.local.summary, seconds=seconds, gate=service.core,
        adapter_identity=adapter.corridor.to_dict() if hasattr(adapter, 'corridor') else None)


def numerical_counts(diagnostics):
    """Work counters for numerical requests; a Clarabel answer is never a worker or a seed."""
    counts = Counter()
    for d in diagnostics:
        ipopt = d.get('backend', 'ipopt') == 'ipopt'
        counts['workerLaunches'] += d.get('workerLaunched') is True
        confirmation = d.get('confirmation')
        counts['feasibleSeedReturns'] += (ipopt and confirmation is None and d.get('workerLaunched') is False
                                          and d.get('status') == 'feasible')
        counts['conicCandidates'] += not ipopt and d.get('status') == 'feasible'
        if confirmation is not None:
            counts['confirmations'] += 1
            counts['confirmationIpoptCalls'] += not confirmation['cached']
            counts['materialGaps'] += confirmation['material']
        if d.get('fallback'):
            counts['relaxedFallbacks'] += 1
            counts['relaxedFallback:'+d['fallback']] += 1
    return counts


def find_checkpoint_route(request, compiler, *, seconds=30, candidate_seconds=1, max_solver_calls=128,
                          max_candidates=24, python_executable=None, cancel_event=None, initial_programs=None,
                          program_filter=None, resume_state=None, on_progress=None, retry_unknown=False,
                          minimum_run_pruning=True, task_goal=None, discovery_settings=None, anchor_search=None):
    """Anchor search for a physical landing task over the scene's automatically discovered contact events."""
    if type(minimum_run_pruning) is not bool:raise ValueError('minimum_run_pruning must be boolean')
    started = time.monotonic()
    for name, value in [('seconds', seconds), ('candidate_seconds', candidate_seconds)]:
        if number(value, name) <= 0:
            raise ValueError(name+' must be positive')
    for name, value in [('max_candidates', max_candidates), ('max_solver_calls', max_solver_calls)]:
        integer(value, name, 1, 100000)
    if request.get('maxWallHits', 0):
        raise ValueError('Intentional wall-hit experiments are excluded')
    scene = prepare_scene(dict(request, maxWallHits=0))
    if scene['sceneModel'] != 'collision_boxes' or scene.get('climbableCells') or scene['maxWallHits']:
        raise ValueError('Checkpoint search requires non-ladder collision geometry with maxWallHits=0')
    if any(k not in ('normal', 'ceiling', 'stepping') for k in request.get('movementSearch', {}).get('types', [])):
        raise ValueError('Select only normal, ceiling and stepping movement types')
    work, counted = SearchWork(max_solver_calls, deadline=started+seconds), CountingCompiler(compiler)
    def unfinished(reason):
        return dict(status='cancelled' if reason == 'cancelled' else 'unknown', termination=reason, best=None,
            windows=[], finalAttempts=[], routeGeneration=None, expandedPrefixes=0, movementGeneration=[],
            work=work.summary(), compiler=counted.summary(), elapsedSeconds=time.monotonic()-started, threshold=0.,
            objectiveOptimalityProven=False, minimumTicksProven=False)
    if cancel_event is not None and cancel_event.is_set():
        return unfinished('cancelled')
    from parkour_solver.search.events.engine import search_event_routes
    try:
        return search_event_routes(request, compiler, task_goal=task_goal, discovery=discovery_settings,
            anchor_search=anchor_search, seconds=seconds, candidate_seconds=candidate_seconds,
            max_solver_calls=max_solver_calls, max_candidates=max_candidates, python_executable=python_executable,
            cancel_event=cancel_event, initial_programs=initial_programs, program_filter=program_filter,
            resume_state=resume_state, on_progress=on_progress, retry_unknown=retry_unknown,
            minimum_run_pruning=minimum_run_pruning)
    except TimeoutError as error:
        return unfinished(str(error))
