import copy
import time

from parkour_solver.numerics.disk import ipopt_seed
from parkour_solver.reachability.service import InteractionGate
from parkour_solver.movement.program import boundary_state, empty_program, program_search_key
from parkour_solver.search.movement import iter_movement_programs, screen_program
from parkour_solver.reachability.viability import prefix_viability
from parkour_solver.search.identity import digest
from parkour_solver.spatial.checkpoints import retain_prefix
from parkour_solver.search.goals import goal_key, _landing_prefixes
from parkour_solver.search.deferrals import POLICY_VERSION
from parkour_solver.search.anchors.policy import (advantageous_candidates, advantageous_targets, launch, next_anchor,
                                                  semantic_settings, terminal_face)
from parkour_solver.constraints.variants import GENERATION_POLICY
from parkour_solver.search.comparison import comparison_label, compare_candidates, POLICY_VERSION as COMPARISON_POLICY_VERSION


def candidate_key(program):
    physical = {k:v for k,v in program.items() if k != 'repairState'}
    return digest(program_search_key(physical))


def decision_family(program, parents):
    start = min(p['profile']['duration'] for p in parents)
    p = program['profile']
    jump = next((t for t in range(start,p['duration']) if p['jump'][t] and p['grounded'][t]),None)
    events = [(e['offset'],e['axis'],e.get('role'),e['boxes']) for e in p.get('contactEvents',[])
        if e['offset']>=start and not (e['axis']=='Y' and p['grounded'][e['offset']] and not p['jump'][e['offset']])]
    events += [(e['input'],e['case'],e.get('boxes')) for e in p.get('caseEvents',[]) if e['input']>=start]
    first = min(events,key=lambda e:e[0]) if events else None
    return digest([jump,first])


def commit_arrival(program, best, *, source_program=None, keep_region_choices=False):
    best = copy.deepcopy(best)
    n = program['profile']['duration']
    source = source_program if source_program is not None else program
    def row_key(row):
        return tuple(row.get(k) for k in ('mode','t1','t2','op','cmp','rhs'))
    dropped = {row_key(r) for r, owner in zip(source['profile']['positionConstraints'],
        source['profile'].get('constraintOwners',[0]*len(source['profile']['positionConstraints'])))
        if owner>n and source.get('committedThrough',0)<owner}
    best['plan']['walls'] = [r for r in best['plan']['walls'] if r['t1']<=n and
        (r.get('t2') is None or r['t2']<=n) and row_key(r) not in dropped]
    result = retain_prefix(program, best)
    result['profile']['constraintOwners'] = program['profile'].get(
        'constraintOwners', [0]*len(program['profile']['positionConstraints'])) + [0]*(
            len(result['profile']['positionConstraints'])-len(program['profile']['positionConstraints']))
    result.pop('repairState', None)
    result['guideSeedYaw'] = result['guideSeedYaw'][:n]
    if not keep_region_choices:
        # The solved rectangle's rows are in the plan's walls, now retained position constraints.
        open_choices = [c for c in result['profile'].pop('regionChoices', []) if c['offset'] > n]
        if open_choices:
            result['profile']['regionChoices'] = open_choices
    return result


def search_goals(scene, compiler, adapter, work, motion, *, cfg, accepted_program, program_filter, roots,
                 resume_state, retry_unknown, deadline, started, candidate_seconds, score_seconds, batch_size,
                 python_executable, cancel_event, on_progress, minimum_run_pruning, numerical_backend='ipopt',
                 finalize=False, _owned_state=False):
    """Depth-first anchor search: one frame per encounter group, over the adapter's lazy contact-event goals."""
    from parkour_solver.search.engine import evaluate_program
    from parkour_solver.validation.witness import replay_witness

    seed = empty_program(scene['initial'], threshold=0.)
    seed['terminalSupport'] = scene['startBlock']
    roots = copy.deepcopy(roots if roots is not None else
        resume_state.get('initialRoots', [seed]) if resume_state else [seed])
    if not roots or any(p['initial'] != seed['initial'] or p.get('threshold', .005) != 0. for p in roots):
        raise ValueError('DFS roots must preserve original state and zero inertia thresholds')
    if any(not accepted_program(p) for p in roots):
        raise ValueError('DFS roots violate the decision domain')
    schema = 8
    context = digest(dict(version=schema, kind='ordered_goal_dfs', movementGenerationPolicy=12,constraintGenerationPolicy=GENERATION_POLICY,
        scene={k:v for k,v in scene.items() if k!='movementSearch'},
        domain={k:v for k,v in cfg.items() if k not in ('maxPrefixes','maxQueuedPrefixes','maxChoicesPerType','expansionsPerProposal')},
        roots=[program_search_key(p) for p in roots], goals=[], terminal=None, corridor=adapter.corridor.to_dict(),
        filter=getattr(program_filter, 'checkpoint_identity', None),
        physics=motion,
        eventStagePolicy=4,anchorPolicy=semantic_settings(adapter.anchor_policy),guidePlan=adapter.guide_plan['id'],
        minimumRunPolicy=POLICY_VERSION if minimum_run_pruning else None,comparisonPolicy=COMPARISON_POLICY_VERSION,
        numericalBackend=numerical_backend,routeStagePolicy=2,routeCommitPolicy=1,routeProposalPolicy=1))
    if resume_state is not None and (resume_state.get('version') != schema or
            resume_state.get('kind') != 'ordered_goal_dfs' or resume_state.get('context') != context):
        raise ValueError('Incompatible DFS continuation; old checkpoint queues require explicit reconstruction')
    state = (resume_state if _owned_state else copy.deepcopy(resume_state)) if resume_state is not None else dict(version=schema, kind='ordered_goal_dfs',
        context=context, initialRoots=roots, stack=[], suspended=[], refinements=[], frames=[], transitions=[],
        windows=[], finalAttempts=[], best=None, cumulativeCalls=0, session=0, expanded=0, nextFrame=0)
    state.setdefault('goalQueue', [])
    state.setdefault('goalOwners', {})
    state.setdefault('discovery', {})
    state.setdefault('hardLatches', {})
    state.setdefault('latchRevision', 0)
    state.setdefault('nodeResults', {})
    state.setdefault('observations', dict(enabled=adapter.anchor_policy['observability'], decisions={}))
    state['observations']['enabled']=adapter.anchor_policy['observability']
    if not finalize:
        state['session'] += 1
    prior_calls, initial_expanded = state['cumulativeCalls'], state['expanded']
    if resume_state and not _owned_state and state['best'] is not None and not replay_witness(scene,state['best'],compiler)['passed']:
        raise ValueError('Saved DFS incumbent no longer passes independent replay')
    gate = InteractionGate(scene, compiler, seconds=candidate_seconds)
    adapter.interaction_service=gate
    gate.cache = adapter.interaction_cache
    gate.metrics = adapter.interaction_metrics
    state.setdefault('timeBoundExclusions',[])
    state.setdefault('minimumRunExclusions',[])
    excluded_runs={(r['frame'],r['candidate']) for r in state['minimumRunExclusions']}
    viability_counts = dict(checked=0, excluded=0)
    termination = 'exhausted'

    def publish():
        state['cumulativeCalls'] = prior_calls+work.counts['solverCalls']
        if on_progress:
            on_progress(state)

    def begin_repair(program,extra_targets=()):
        state['cumulativeCalls']=prior_calls+work.counts['solverCalls']
        begin=getattr(on_progress,'begin_repair',None)
        if begin is not None:begin(state,program,extra_targets)
        else:publish()

    def repair_progress():
        state['cumulativeCalls']=prior_calls+work.counts['solverCalls']
        callback=getattr(on_progress,'repair_progress',None)
        if callback is not None:callback(state)
        else:publish()

    def transition(kind, frame, **fields):
        state['transitions'].append(dict(kind=kind, frame=frame['id'], depth=frame['index'], session=state['session'], elapsedSeconds=time.monotonic()-started, **fields))
        # A typed arrival's tried flag and child execution owner are one
        # checkpoint transaction. The child's open/backtrack publishes both.
        if kind!='choose':
            publish()

    stage_views = {}

    def frame(index, parents, parent=None, cursor=None):
        cursor = cursor if cursor is not None else adapter.initial_cursor(parents)
        owner = digest(dict(parents=[candidate_key(p) for p in parents], cursor=cursor,
            physics=motion, domain=adapter.data['domainId']))
        if owner in state['goalOwners']:
            return None
        discovery = dict(owner=owner,index=index,parents=copy.deepcopy(parents),parent=parent,
            cursor=cursor,continuation=None,exhausted=False)
        state['discovery'][owner] = discovery
        state['goalOwners'][owner] = []
        if parent is not None:
            state['pendingGoalOwner'] = owner
        expand_discovery(discovery)
        ready = next((f for f in state['goalQueue'] if f['owner']==owner),None)
        if ready is not None:
            f = ready
            state['goalQueue'].remove(f)
            state['stack'].append(f)
            if state.get('pendingGoalOwner') == owner:
                state.pop('pendingGoalOwner')
            transition('open',f,goal=f['goal'])
            return f
        publish()
        return None

    def make_frame(index, parents, parent, goal, **fields):
        f = dict(id=state['nextFrame'], index=index, parents=copy.deepcopy(parents), parent=parent,
            goal=goal, phase='generate', generation={}, ready=[], records=[], inFlight=None,
            sourceExhausted=False, batches=0, **fields)
        state['nextFrame'] += 1
        return f

    def expand_discovery(discovery):
        settings=getattr(adapter,'discovery_settings',{})
        used=state.setdefault('discoveryWork',dict(expansions=0,materializations=0))
        requests, continuation, report=adapter.next_goals(discovery['cursor'],discovery['parents'],
            discovery['continuation'],budget=dict(check=work.check,
                expansions=max(0,settings.get('maxGraphExpansions',5000)-used['expansions']),
                materializations=max(0,settings.get('maxGuideMaterializations',50)-used['materializations'])))
        used['expansions']+=report['expansions'];used['materializations']+=report['materializations']
        discovery.update(continuation=continuation,exhausted=continuation['exhausted'])
        for request in requests:
            if request.get('localSetupAlias'):
                # Current-support hops/runs remain in the movement source for
                # every next goal. They do not need an extra comparison frame.
                state.setdefault('localSetupAliases',[]).append(dict(owner=discovery['owner'],request=request['id']))
                continue
            f=make_frame(discovery['index'],discovery['parents'],discovery['parent'],request['goal'],
                goalRequest=request,semanticCursor=copy.deepcopy(discovery['cursor']),owner=discovery['owner'])
            state['goalOwners'][discovery['owner']].append(f['id'])
            state['goalQueue'].append(f)

    def stage_adapter(f):
        key=f['id']
        if key not in stage_views:
            stage_views[key] = adapter.for_goal(f['goalRequest'],f['semanticCursor'])
            f['guide'] = stage_views[key].guide
            f['scoreContextKey'] = stage_views[key].score_context
        return stage_views[key]

    def guide_objective(f):
        """The stage's progress objective; a final frame keeps the scene objective (None)."""
        if final_frame(f):
            return None
        return stage_adapter(f).objective

    def span_info(f):
        view=stage_adapter(f)
        return view.cursor['anchorIndex'],view.cursor['spanStart']

    def priority(program,f,identity=''):
        from parkour_solver.search.anchors.policy import priority as span_priority
        return span_priority(program,span_info(f)[1],program.get('priorityTurnaround',False),identity)

    def record_priority(record,f):
        return priority(record.get('child',record['program']),f,record['id'])

    # A frame searches one encounter group (pairs first..last); per-pair rules use
    # the program's own pair, capped at the group's last pair.
    def group_span(f):
        cursor=stage_adapter(f).cursor
        return cursor['anchorIndex'],cursor['groupEnd'],cursor['spanStart']

    def pair_key(k):
        names=adapter.guide_plan['anchors']
        return names[k]+'→'+names[k+1]

    def visited(program):
        from parkour_solver.search.anchors.policy import audit
        return audit(scene,program,adapter.anchor_policy['anchors'])

    def pair_of(program,f):
        return min(visited(program)['reached'],group_span(f)[1])

    def group_latched(f):
        first,last,_=group_span(f)
        return all(pair_key(k) in state['hardLatches'] for k in range(first,last+1))

    def pair_transfer(program,f,k,arrivals,*,complete):
        from parkour_solver.search.anchors.policy import transfer_structure
        first,last,start=group_span(f)
        names=adapter.guide_plan['anchors']
        return transfer_structure(scene,program,start if k==first else arrivals['ticks'][k-1],names[k],names[k+1],
                                  complete=complete,until_arrival=k<last)

    def hard_bound(program,f):
        first,last,_=group_span(f)
        if not any(pair_key(k) in state['hardLatches'] for k in range(first,last+1)):return None
        arrivals=visited(program)
        completed=program.get('terminalSupport') in f['goal'].get('blocks',[f['goal']['block']])
        for k in range(first,min(arrivals['reached'],last)+1):
            if pair_key(k) not in state['hardLatches']:continue
            transfer=pair_transfer(program,f,k,arrivals,complete=completed if k==last else k<arrivals['reached'])
            if not transfer['valid']:
                return dict(reason='global_single_jump_latch',span=pair_key(k),revision=state['latchRevision'],
                            transfer=transfer)
        return None

    def score_key(program,f):
        return digest([candidate_key(program),stage_adapter(f).score_context])

    def guide_leg(decision, f):
        """Unit horizontal direction of the decision's current guide leg; None past the last point or when it is zero."""
        from parkour_solver.spatial.anchor_guides import unit
        k, points = pair_of(decision, f), adapter.guide_plan['points']
        return unit(points[k+1][0]-points[k][0], points[k+1][2]-points[k][2]) if k+1 < len(points) else None

    def program_faces(program):
        """The merged top faces (ContactOption) a grounded program stands on at its feet: those named by the face it
        last visited (landing contacts and case events, so a blip-up without a terminalSupport counts); one, unless
        solids through it cut it into pieces. None on a ladder tick."""
        from parkour_solver.movement.contact_combinations import face_name, solid_owners, top_faces
        found = launch(scene, program)
        if found is None:
            return []
        owners = solid_owners(scene)
        return [o for o in top_faces(scene)
                if o.plane == program['profile']['position'][-1] and face_name(scene, o, owners) == found[1]]

    def launch_face(decision):
        """The merged top face an ordinary decision stands on: of its own, the nearest to its validated position; None
        when its feet are on none of them."""
        import math
        x, z = decision['decisionPoint']
        return min(program_faces(decision), default=None,
                   key=lambda o: min(math.hypot(max(r[0]-x, 0, x-r[2]), max(r[1]-z, 0, z-r[3])) for r in o.regions))

    def lands_on_launch_face(decision, landing):
        """Whether a landing is on the decision's launch face; a support cut into several faces never counts."""
        face = launch_face(decision)
        return face is not None and [o.surface for o in program_faces(landing)] == [face.surface]

    def same_face_fit(decision, landing, f):
        """same_face_fit for a jump from an ordinary decision landing back on its launch face (O11), with the face's
        name; None when there is no leg or no physics to measure it with."""
        from parkour_solver.movement.contact_combinations import face_name
        from parkour_solver.search.anchors.policy import same_face_fit as fit
        leg, analysis = guide_leg(decision, f), gate.analyze(landing)
        if leg is None or analysis is None:
            return None
        face = launch_face(decision)
        start, end, model = decision['profile']['duration'], landing['profile']['duration'], analysis['model']
        result = fit(decision['decisionPoint'], decision['decisionVelocity'], leg, face.regions,
                     model['magnitude'][start:end], model['friction'][start:end])
        return dict(result, face=face_name(scene, face))

    def configure(ctx, f):
        stage_adapter(f).context(ctx)
        gate.context(ctx)
        ctx['launchFace'] = (launch(scene, ctx['program']) or (None, None))[1]
        ctx['regionItems'] = True
        ctx['frameId'] = f['id']
        decision = ctx.get('decision')
        if decision is not None and not decision.get('decisionTurnaround', False):
            # An ordinary decision generates no move its guide leg's direction class disallows (O10).
            from parkour_solver.spatial.anchor_guides import leg_class
            direction = guide_leg(decision, f)
            if direction is not None:
                ctx['forward'] = dict(point=decision['decisionPoint'], allowed=leg_class(direction)[1],
                                      margin=adapter.anchor_policy['turnaround']['spatialMargin'])

    def time_bound(program,f=None):
        if state['best'] is None:return None
        label=dict(T=program['profile']['duration'])
        ctx=dict(fixedTerminal=False)
        decision=compare_candidates(label,context=ctx,incumbent=state['best'])
        if decision['action']=='exclude_bound':
            return dict(reason=decision['reason'],elapsedInputs=label['T'],incumbentTick=decision['incumbentT'])
        if f is not None:
            from parkour_solver.search.bounds.remaining import remaining_bound
            span=pair_of(program,f)
            from parkour_solver.search.bounds.flight import transfer_bounds
            records=state.setdefault('transferTimeBounds',{})
            transfer_bounds(scene,adapter.guide_plan,motion,gate,saved=records,threshold=0.,check=work.check)
            proof=remaining_bound(adapter.guide_plan,span,program,motion,
                                  transfers=records,hard_latches=state['hardLatches'])
            label['remainingFinishBound']=dict(ticks=proof['remainingTicks'],proofIds=proof.get('proofIds',[]))
            decision=compare_candidates(label,context=ctx,incumbent=state['best'])
            if decision['action']=='exclude_bound':
                return dict(proof,reason=decision['reason'],elapsedInputs=label['T'],incumbentTick=decision['incumbentT'])
        return None

    def tick_label(program,f, *, identity=None, validated=False):
        span,start=span_info(f)
        if 'comparisonContextId' not in f:
            f['comparisonContextId']=digest(dict(roots=[candidate_key(p) for p in f['parents']],
                span=span,start=start,goal=f['goal'],score=stage_adapter(f).score_context))
        complete=program.get('terminalSupport') in f['goal'].get('blocks',[f['goal']['block']]) and program['profile']['grounded'][-1]
        return comparison_label(program,start=start,mode=program.get('priorityTurnaround',False),
            identity=identity or candidate_key(program),context=dict(comparisonContext=f['comparisonContextId'],
                basis='anchor_arrival' if complete else 'open_prefix',scope=program.get('solveScope'),
                milestone=f['goal']['block'],landingPhase=program['profile'].get('landingPhase','pre')),
            evidence=dict(validated=validated))

    def run_bound(program, f):
        if not minimum_run_pruning or f is None:return None
        proof=None
        if (program.get('restoredRun') or {}).get('frame')==f['id']:return None
        from parkour_solver.search.deferrals import assess
        context=dict(purpose='hard_anchor',backend='automatic_anchor',localDeferralEnabled=True,
            hardLatched=group_latched(f),comparisonContextId=f.get('comparisonContextId'))
        # Incomplete prefixes never inherit an arrival's future counts.
        if not context['hardLatched'] or program.get('terminalSupport') not in f['goal'].get('blocks',[f['goal']['block']]):return None
        label=tick_label(program,f)
        token=[f['id'],f.get('tickPeerRevision',0),state['latchRevision'],label['id'],label['mode']]
        counters=state.setdefault('comparisonWork',dict(assessments=0,cacheHits=0,peerChecks=0))
        if program.get('comparisonCacheToken')==token:
            decision=program['comparisonDecision'];counters['cacheHits']+=1
        else:
            decision=assess(label,f.get('tickPeers',{}),context)
            counters['assessments']+=1;counters['peerChecks']+=decision.get('peerChecks',0)
            program['comparisonCacheToken']=token
        program['comparisonDecision']=decision
        state.setdefault('comparisonDecisions',{})[str(f['id'])+':'+label['id']]=decision
        if decision['action']=='defer_preparation':
            proof=dict(reason='higher_ticks_without_structural_advantage',heuristic=True,comparison=decision)
        if proof is not None:
            identity=candidate_key(program);key=(f['id'],identity)
            if key not in excluded_runs:
                excluded_runs.add(key)
                state['minimumRunExclusions'].append(dict(frame=f['id'],candidate=identity,proof=proof,
                    program={k:copy.deepcopy(v) for k,v in program.items()},span=span_info(f)[0],
                    decision=program.get('decisionNode'),isTurnaround=program.get('priorityTurnaround',False),
                    latchRevision=state['latchRevision']))
        return proof

    def eligible(program, f=None):
        if reject_time(program,f) is not None:
            return False
        return (not hard_bound(program,f) and run_bound(program,f) is None and accepted_program(program) and
                stage_adapter(f).program_allowed(program))

    def reject_time(program,f):
        proof=time_bound(program,f)
        if proof is not None:
            state['timeBoundExclusions'].append(dict(id=candidate_key(program),proof=proof))
        return proof

    def screen(program, f=None):
        bounded=program
        if f is not None and program.get('ownedEndpoint')!=program['profile']['duration']:
            arrivals=[p for b in f['goal'].get('blocks',[f['goal']['block']])
                      for p in _landing_prefixes(scene,program,b)]
            if arrivals:bounded=min(arrivals,key=lambda p:p['profile']['duration'])
        bound=time_bound(bounded,f) or hard_bound(bounded,f) or run_bound(bounded,f)
        if bound is not None:
            return bound
        g=f['goal']
        required=[g['block']] if g['kind']=='landing' and bounded['terminalSupport'] not in g.get('blocks',[g['block']]) else []
        proof = prefix_viability(scene, bounded, required_contacts=required, terminal_tick=cfg['maxInputs'],
            enabled_types=cfg['types'])
        viability_counts['checked'] += 1
        if proof['dead']:
            viability_counts['excluded'] += 1
            return proof
        # Check the full proposal, including events beyond a diagnostic projection.
        return screen_program(scene,program,compiler,
            max(.001,min(candidate_seconds,deadline-time.monotonic())),gate=gate,
            objective=None if f is None else stage_adapter(f).program_objective(program))

    def numerical_pending(record):
        repair = record.get('program', {}).get('repairState', {})
        return bool(repair.get('pending') or repair.get('inFlight') or
            repair.get('constraintGeneration') and not repair['constraintGeneration'].get('exhausted'))

    def final_frame(f):
        return f['goalRequest']['terminal']

    def evaluate_node(program, nodes, unused, f):
        from parkour_solver.search.feasibility import solve_due
        from parkour_solver.spatial.anchor_guides import classify
        from parkour_solver.observability.turns import record as observe, replay
        active=stage_adapter(f)
        span,start=pair_of(program,f),span_info(f)[1]
        identity=program['decisionNode']
        depth=program['decisionDepth']
        if not solve_due(program,adapter.anchor_policy['solveEveryDecisionLevels']):
            return dict(status='untested',program=program)
        saved=nodes.get(identity)
        if saved and saved['status']=='oracle_rejected':
            return dict(status='oracle_rejected',program=program)
        if saved and saved['status']=='feasible':
            # The saved program carries physical rows and a free-yaw seed.
            return dict(status='feasible',program=copy.deepcopy(saved['program']),
                        alternatives=copy.deepcopy(saved.get('alternatives',[])))
        if saved and saved.get('repairState') is not None:
            program['repairState']=saved['repairState']
        # Climb every side family and keep the best, as at a group end: families are compared here.
        policy=dict(purpose='node',stopOnFeasible=False,rankEqualCost=True,oracleRejection=True,numerical=numerical_backend)
        t=time.monotonic()
        entry=dict(status='suspended',parent=program.get('decisionParent'),repairState=program.get('repairState'))
        nodes[identity]=entry
        begin_repair(program,(entry,))
        def progress(repair):
            program['repairState']=repair
            entry['repairState']=repair
            repair_progress()
        node_view=active.for_node(program)
        result=evaluate_program(scene,program,compiler,node_view,work,motion,
            seconds=max(.001,min(score_seconds,deadline-time.monotonic())),candidate_seconds=candidate_seconds,
            python_executable=python_executable,cancel_event=cancel_event,
            guide_objective=node_view.objective,first_witness=True,
            solve_policy=policy,climb_families=True,on_progress=progress if on_progress else None,retry_unresolved=retry_unknown)
        if not on_progress and result.get('repairState') is not None:
            # Nothing saves progress, so the repair published nothing: leave its final state where progress would.
            program['repairState']=entry['repairState']=result['repairState']
        query=result.get('nodeFeasibility',dict(status='feasible' if result['best'] else 'suspended',model=identity))
        repair=result.get('repairState') or {}
        if query['status']=='feasible' and (repair.get('pending') or repair.get('inFlight') or repair.get('repairSources') or
                                            (repair.get('constraintGeneration') or {}).get('exhausted') is False):
            # An interrupted climb has a best but has not decided the node: resume it exactly first.
            query=dict(query,status='suspended')
        status=query['status']
        observe(state['observations'],adapter.guide_plan,program,span=span,start=start,
                model=query['model'],status=status,elapsed=time.monotonic()-t,metrics=result['work'],
                service=dict(latchRevision=state['latchRevision'],hardLatched=pair_key(span) in state['hardLatches'],
                    runDeferralEnabled=minimum_run_pruning and not program.get('priorityTurnaround',False) and group_latched(f),
                    tickDeferralEnabled=minimum_run_pruning and group_latched(f),comparisonPolicy=COMPARISON_POLICY_VERSION))
        alternatives=[]
        if status=='feasible':
            from parkour_solver.scene.route import row_tick
            from parkour_solver.constraints.encounters import settled_through
            from parkour_solver.search.repair import node_alternatives
            settled=max(settled_through(program['profile'],program['profile']['duration']),program.get('settledThrough',0))
            def branch_program(best):
                # Retain selected physical walls; never endpoint/velocity/yaw equalities.
                # A decision check makes an intermediate choice for this branch: the exact rows of every
                # stretch settled (closed) at the node stay, so later checks never re-decide them; the open
                # stretch's rows go.
                witness=copy.deepcopy(best)
                events=witness['plan']['events']
                unsettled={(r['mode'],t,r['cmp'],r['rhs']) for r in witness.get('alternativeRows',[]) if r['leg']<len(events)
                           for t in [row_tick(events[r['leg']],r)] if t>=settled}
                witness['plan']['walls']=[w for w in witness['plan']['walls'] if (w['mode'],w['t1'],w['cmp'],w['rhs']) not in unsettled]
                kept=commit_arrival(program,witness,keep_region_choices=True)
                regions=[c for c in kept['profile'].pop('regionChoices',[]) if c['offset']>=settled]
                if regions:
                    kept['profile']['regionChoices']=regions
                kept['settledThrough']=settled
                for k in ('decisionNode','decisionParent','decisionDepth','priorityTurnaround','restoredRun'):
                    if k in program:kept[k]=program[k]
                kept['lastSolvedDepth']=depth
                point=best['candidate']['positions'][-1]
                kept.update(decisionPoint=point,decisionVelocity=best['candidate']['velocity'][-1],decisionPointTick=program['profile']['duration'],
                    decisionPointSource='validated_node',decisionTurnaround=classify(adapter.guide_plan,span,point)['isTurnaround'])
                return kept
            updated=branch_program(result['best'])
            # Other branch choices and close switch timings wait, best first, until this branch dies.
            repair=result.get('repairState',{})
            known={candidate_key(updated)}
            for variant in node_alternatives(repair.get('acceptedVariants',[]),repair.get('incumbent'),
                                             result['best']['route']['objective']):
                held=branch_program(variant['best'])
                key=candidate_key(held)
                if key in known:continue
                known.add(key)
                held.update(decisionNode=digest([identity,'alternative',len(alternatives)]),alternativeOf=identity)
                alternatives.append(held)
            ancestor=program.get('decisionParent')
            while ancestor:
                event=state['observations']['decisions'].get(ancestor)
                if event is None:break
                tick=event['pointTick']
                replay(state['observations'],adapter.guide_plan,ancestor,event['span'],
                       result['best']['candidate']['positions'][tick],query['model'])
                ancestor=event['parent']
            program=updated
        elif result.get('repairState') is not None:
            program['repairState']=result['repairState']
        if (program.get('restoredRun') or {}).get('frame')==f['id'] and status in ('oracle_rejected','exhausted'):
            f['recoveryFailure']=dict(reason='restored_internal_preparation_failed',candidate=identity,model=query.get('model'))
        nodes[identity]=dict(status=status,parent=program.get('decisionParent'),query=query,
            depth=depth,lastSolvedDepth=program.get('lastSolvedDepth',0),program=copy.deepcopy(program),
            **(dict(alternatives=copy.deepcopy(alternatives)) if alternatives else {}),
            # The committed program drops its repair; the solve report reads the node's alternatives from it.
            **(dict(repairState=repair) if status=='feasible' else {}))
        state['nodeResults'][identity]=dict(query,parent=program.get('decisionParent'),depth=depth)
        publish()
        from parkour_solver.search.feasibility import unresolved_only
        return dict(status=status,program=program,alternatives=alternatives,
                    deferUnknown=status=='suspended' and unresolved_only(program.get('repairState',{})))

    def generate(f):
        target_scene = dict(scene,goalBlock=f['goal']['block'])
        report = f['generation']
        before = report.get('continuation',{}).get('expanded',0)
        settings = dict(cfg,maxPrefixes=max(0,cfg['maxPrefixes']-(state['expanded']-initial_expanded)))
        from parkour_solver.spatial.anchor_guides import classify
        span,_=span_info(f)
        for root in f['parents']:
            point=root.get('decisionPoint',scene['initial']['position'][::2])
            root.setdefault('decisionPoint',point)
            root.setdefault('decisionVelocity',scene['initial']['velocity'][::2])
            root.setdefault('decisionPointTick',root['profile']['duration'])
            root.setdefault('decisionPointSource','original_initial' if not root['profile']['duration'] else 'validated_anchor')
            root['decisionTurnaround']=classify(adapter.guide_plan,span,point)['isTurnaround']
        anchors,task=adapter.anchor_policy['anchors'],adapter.data['task']['block']
        tables={}
        def candidates(p):
            key=(terminal_face(scene,p),p['profile']['position'][-1],next_anchor(scene,p,anchors,task))
            if key not in tables:
                tables[key]=advantageous_candidates(scene,gate.reachable_faces,*key,settings['types'],0.)
            return tables[key]
        # Outside turnarounds a grounded decision runs only the minimum ticks an advantageous jump needs.
        run_policy=dict(ordinary=lambda p:boundary_state(p)['onGround'] and not p.get('decisionTurnaround',False),
            targets=lambda base,child:advantageous_targets(scene,base,child,next_anchor(scene,base,anchors,task)),
            candidates=candidates,next=lambda p:next_anchor(scene,p,anchors,task),face=lambda p:terminal_face(scene,p),
            back=lands_on_launch_face,fits=lambda decision,landing:same_face_fit(decision,landing,f))
        node_options=dict(node_evaluator=lambda p,n,x:evaluate_node(p,n,x,f),
            priority_key=lambda p,identity:priority(p,f,identity),
            live_programs=lambda:live_programs(f),run_policy=run_policy,
            policy_context=dict(anchor=semantic_settings(adapter.anchor_policy),span=span,guide=adapter.guide_plan['id'],score=stage_adapter(f).score_context))
        source = iter_movement_programs(target_scene,settings=settings,report=report,initial_programs=f['parents'],
            **node_options,
            continuation=report.get('continuation'),
            threshold=0., deadline=deadline,cancel_event=cancel_event,work=work,screen=lambda p:screen(p,f),
            context_adapter=lambda ctx:configure(ctx,f),program_filter=lambda p:eligible(p,f),
            stop_supports=f['goal'].get('blocks'))
        seen = {r.get('candidateKey',r['id']) for r in f['records']} | {candidate_key(p) for p in f['ready']}
        try:
            while len(f['ready']) < batch_size:
                try:
                    p = next(source)
                except StopIteration:
                    f['sourceExhausted'] = report.get('continuation',{}).get('exhausted',False)
                    break
                identity = candidate_key(p)
                if identity not in seen:
                    f['ready'].append(p)
                    seen.add(identity)
                    # Score the iterator's first ready arrival immediately.
                    # Remaining proposals stay owned by its saved cursor.
                    break
        finally:
            source.close()
            state['expanded'] += report.get('continuation',{}).get('expanded',0)-before
        f['ready'].sort(key=lambda p:priority(p,f))
        f['batches'] += 1
        if not f['ready'] and not f['sourceExhausted']:
            raise TimeoutError(report.get('termination','generation_suspended'))
        f['phase'] = 'score'
        transition('generated_batch',f,count=len(f['ready']),exhausted=f['sourceExhausted'])

    def evaluation_args(f, program):
        """evaluate_program's frame-dependent arguments, shared by scoring and finalization."""
        landing = program['terminalSupport'] if program['terminalSupport'] in f['goal'].get('blocks',[]) else f['goal']['block']
        policy=dict(purpose='terminal' if final_frame(f) else 'anchor',stopOnFeasible=False,rankEqualCost=True,
                    oracleRejection=True,numerical=numerical_backend)
        return dict(scene=dict(scene,goalBlock=landing),adapter=stage_adapter(f),required_landing=landing,
            first_witness=not final_frame(f),guide_objective=guide_objective(f),solve_policy=policy,climb_families=True)

    def finalize_best():
        """One IPOPT re-solve of the best relaxed finish through the repair loop."""
        from parkour_solver.search.ranking import candidate_rank
        source = digest(state['best'])
        saved = state.get('finalization')
        if saved is not None and saved['source'] == source and saved['stage'] == 'complete':
            return
        record = state['finalization'] = dict(version=1, source=source, stage='running', attempts=[], best=None)
        publish()
        frames = {f['id']:f for group in ('frames','suspended','refinements','stack') for f in state.get(group,[])}
        # Every final-frame witness is a complete relaxed finish; a chosen
        # variant can be the incumbent without a finalAttempts record.
        candidates = sorted(((f,r) for f in frames.values() if final_frame(f) for r in f['records'] if r.get('witness')),
                            key=lambda item: candidate_rank(item[1]['witness'],scene['objective']))
        for f, r in candidates:
            if time.monotonic() >= deadline or cancel_event is not None and cancel_event.is_set():
                break
            witness = r['witness']
            program = {k:v for k,v in r['program'].items() if k!='repairState'}
            began = time.monotonic()
            if 'model' not in witness['candidate']:
                best, child, attempt = witness, r['child'], dict(status='physical', termination=None, ipoptCalls=0)
            else:
                args = evaluation_args(f,program)
                args['solve_policy'] = dict(purpose='terminal',stopOnFeasible=False,rankEqualCost=False,
                                            oracleRejection=True,numerical='ipopt')
                result = evaluate_program(args.pop('scene'),program,compiler,args.pop('adapter'),work,motion,
                    seconds=max(.001,deadline-time.monotonic()),candidate_seconds=candidate_seconds,
                    python_executable=python_executable,cancel_event=cancel_event,retry_unresolved=retry_unknown,
                    final_route=dict(witness['route'],warmYaw=ipopt_seed(witness['candidate']['yaw'])),**args)
                best = result['best']
                if best is not None and not result.get('fullSceneValidated'):
                    best = None
                child = commit_arrival(program,best,source_program=program) if best is not None else None
                attempt = dict(status='feasible' if best is not None else result['status'],
                               termination=result['termination'],ipoptCalls=result['work'].get('workerLaunches',0))
            record['attempts'].append(dict(attempt,frame=f['id'],record=r['id'],seconds=time.monotonic()-began))
            if best is not None:
                published = copy.deepcopy(best)
                published.update(movementProgram=child,blockIds=[scene['startBlock'],scene['goalBlock']])
                record['best'] = published
                break
            publish()
        if record['best'] is not None:
            record['stage'] = 'complete'
        elif cancel_event is None or not cancel_event.is_set():
            record['stage'] = 'failed'
        publish()

    def score(f, program, old=None):
        source_program = program
        identity = old['id'] if old else f['inFlight']['id'] if f['inFlight'] is not None else score_key(program,f)
        work.counts['terminalEvaluations' if final_frame(f) else 'spatialGoalEvaluations'] += 1
        f['inFlight'] = dict(id=identity,program=program)
        transition('dispatch',f,candidate=identity,goal=f['goal'],inputs=program['profile']['duration'])
        def progress(repair):
            f['inFlight']['program']['repairState'] = repair
            repair_progress()
        active_adapter = stage_adapter(f)
        begin_repair(f['inFlight']['program'])
        args = evaluation_args(f,program)
        result = evaluate_program(args.pop('scene'),program,compiler,args.pop('adapter'),work,motion,
            seconds=max(.001,min(score_seconds,deadline-time.monotonic())),candidate_seconds=candidate_seconds,
            python_executable=python_executable,cancel_event=cancel_event,
            on_progress=progress if on_progress else None,retry_unresolved=retry_unknown,**args)
        if not on_progress and result.get('repairState') is not None:
            f['inFlight']['program']['repairState'] = result['repairState']
        from parkour_solver.observability.turns import record as observe, replay
        span,start=pair_of(program,f),span_info(f)[1]
        observe(state['observations'],adapter.guide_plan,program,span=span,start=start,
            model=result.get('solveIdentity',identity),status=result.get('nodeFeasibility',{}).get('status',result['status']),kind='anchor',
            metrics=result['work'],elapsed=sum(result.get('timings',{}).values()),
            service=dict(latchRevision=state['latchRevision'],hardLatched=pair_key(span) in state['hardLatches'],
                runDeferralEnabled=minimum_run_pruning and not program.get('priorityTurnaround',False) and group_latched(f),
                tickDeferralEnabled=minimum_run_pruning and group_latched(f),comparisonPolicy=COMPARISON_POLICY_VERSION))
        if result['best'] is not None:
            ancestor=program.get('decisionParent')
            while ancestor:
                event=state['observations']['decisions'].get(ancestor)
                if event is None:break
                replay(state['observations'],adapter.guide_plan,ancestor,event['span'],
                    result['best']['candidate']['positions'][event['pointTick']],result.get('solveIdentity',identity))
                ancestor=event['parent']
        node_id=program.get('decisionNode') or identity
        state['nodeResults'][node_id]=dict(result.get('nodeFeasibility',{}),
            parent=program.get('decisionParent'),depth=program.get('decisionDepth',0),kind='terminal' if final_frame(f) else 'anchor')
        p = copy.deepcopy(program)
        if result.get('repairState') is not None:
            p['repairState'] = result['repairState']
        record = dict(id=identity,program=p,status=result['status'],termination=result['termination'],
            nodeFeasibility=result.get('nodeFeasibility'),objectiveMode='configured',
            arrival=program['profile']['duration'] if result['best'] is not None else None,
            value=result['best']['candidate']['objective'] if result['best'] else None,
            family=decision_family(program,f['parents']),work=result['work'],goal=f['goal'],goalKey=goal_key(scene,f['goal'],active_adapter.corridor) if f['goal'] else None,
            evaluations=(old.get('evaluations',[]) if old else [])+[dict(session=state['session'],work=result['work'])],
            tried=old.get('tried',False) if old else False,solveScope=result.get('solveScope'),solveIdentity=result.get('solveIdentity'),
            numericalAttempts=result.get('repairState',{}).get('attemptLog',result.get('attempts',[])),
            contactAttempts=result.get('repairState',{}).get('contactLog',result.get('contactAttempts',[])))
        record['candidateKey'] = candidate_key(program)
        if ((program.get('restoredRun') or {}).get('frame')==f['id'] and
                (record.get('nodeFeasibility') or {}).get('status')=='oracle_rejected'):
            f['recoveryFailure']=dict(reason='restored_preparation_oracle_rejected',candidate=identity)
        record['scoreContextKey'] = active_adapter.score_context
        record['guideId'] = active_adapter.guide['id']
        record['ticks'] = program['profile']['duration']
        record['complete'] = final_frame(f)
        if result['best'] is not None:
            best = result['best']
            record['witness'] = best
            succeed(f,program)
            record['child'] = commit_arrival(program,best,source_program=source_program)
            record['child']['settledThrough'] = record['child']['profile']['duration']
            record['child']['searchParent'] = identity
            for k in ('decisionNode','decisionParent','decisionDepth','restoredRun'):
                if k in program:record['child'][k]=program[k]
            record['child'].update(decisionPoint=best['candidate']['positions'][-1],decisionVelocity=best['candidate']['velocity'][-1],
                decisionPointTick=program['profile']['duration'],decisionPointSource='validated_anchor',
                lastSolvedDepth=program.get('decisionDepth',0))
            record['child'].pop('solveScope',None)
        elif old and old.get('witness'):
            for key in ('witness','child','arrival','value'):
                record[key] = old[key]
        if old and old.get('child') is not None and record.get('child') is not None and candidate_key(old['child'])!=candidate_key(record['child']):
            record['tried'] = False
        if old:
            record['refinementSession'] = old.get('refinementSession')
            f['records'][f['records'].index(old)] = record
        else:
            f['records'].append(record)
        # Anchor legs keep only their optimized best; close losers wait in repairState as fallbacks.
        final_validated = result.get('fullSceneValidated',True)
        if final_frame(f) and not final_validated and record.get('witness'):
            record['guidanceWitness'] = record.pop('witness')
            record.pop('child',None)
            record.update(status='unknown',termination='preliminary_scope_not_a_terminal_witness')
        if record.get('witness') and final_validated:
            first,last,_=group_span(f)
            arrivals=visited(program)
            transfers={pair_key(k):pair_transfer(program,f,k,arrivals,complete=True) for k in range(first,last+1)}
            record['transferStructure']=transfers[pair_key(last)]
            if last>first:
                record['earlierTransfers']={k:v for k,v in transfers.items() if k!=pair_key(last)}
            for key,transfer in transfers.items():
                if transfer['valid'] and key not in state['hardLatches']:
                    state['latchRevision']+=1
                    state['hardLatches'][key]=dict(version=transfer['version'],revision=state['latchRevision'],candidate=identity,
                        frame=f['id'],model=result.get('solveIdentity'),inputs=program['profile']['duration'],
                        transferStructure=transfer)
                    transition('hard_jump_latched',f,span=key,revision=state['latchRevision'])
        if minimum_run_pruning and record.get('witness') and final_validated:
            from parkour_solver.search.deferrals import remember_peer
            if all(t['valid'] for t in [record.get('transferStructure',{'valid':False}),*record.get('earlierTransfers',{}).values()]) and remember_peer(
                    f.setdefault('tickPeers',{}),tick_label(record['child'],f,identity=identity,validated=True)):
                f['tickPeerRevision']=f.get('tickPeerRevision',0)+1
        if record.get('witness') and final_validated and final_frame(f):
            from parkour_solver.search.ranking import candidate_rank
            candidate = copy.deepcopy(record['witness'])
            candidate.update(movementProgram=record['child'],blockIds=[scene['startBlock'],scene['goalBlock']])
            if state['best'] is None or candidate_rank(candidate,scene['objective']) < candidate_rank(state['best'],scene['objective']):
                state['best'] = candidate
                state.setdefault('firstFullSolutionSeconds',time.monotonic()-started)
                work.accepted(candidate)
                transition('incumbent',f,candidate=identity,objective=candidate['candidate']['objective'])
            existing = next((r for r in state['finalAttempts'] if r['id']==identity),None)
            if existing is not None:
                state['finalAttempts'][state['finalAttempts'].index(existing)] = record
            else:
                state['finalAttempts'].append(record)
        f['inFlight'] = None
        transition('scored',f,candidate=identity,status=record['status'],arrival=record['arrival'])
        if result['status'] in ('error','cancelled'):
            raise TimeoutError(result['status'])
        return record

    def refine(f, pending):
        pending['refinementSession'] = state['session']
        preparation=run_bound(pending['program'],f)
        if preparation is not None:
            pending['preparationDeferral']=preparation
            return
        proof=reject_time(pending['program'],f)
        if proof is not None:
            pending['refinementTimeExclusion']=proof
            transition('candidate_exclusion',f,candidate=pending['id'],proof=proof,refinement=True)
            return
        score(f,pending['program'],pending)

    def variant_record(f, record, variant):
        source_program = {k:v for k,v in record['program'].items() if k!='repairState'}
        best = copy.deepcopy(variant['best'])
        if record.get('solveScope') is not None:
            best['validationScope'] = record['solveScope']
        at = source_program['profile']['duration']
        child = commit_arrival(source_program,best,source_program=source_program)
        child['settledThrough'] = at
        child.update(decisionPoint=best['candidate']['positions'][at],decisionVelocity=best['candidate']['velocity'][at],decisionPointTick=at,
            decisionPointSource='validated_anchor',lastSolvedDepth=source_program.get('decisionDepth',0))
        key = candidate_key(child)
        if key in {candidate_key(r['child']) for r in f['records'] if r.get('child')}:
            return None
        alternate = dict(record,id=digest([record['id'],key]),program=copy.deepcopy(child),child=child,
            witness=best,tried=False,variantOf=record['id'],arrival=at,
            value=best['candidate']['objective'])
        alternate.pop('restoredFallbacks',None)
        return alternate

    def restore_fallback(f, recovery):
        for candidate in f.get('failedCandidates',[]):
            original = next((r for r in f['records'] if r['id']==candidate),None)
            while original is not None and original.get('variantOf'):
                original = next((r for r in f['records'] if r['id']==original['variantOf']),None)
            if original is None:
                continue
            fallbacks = original['program'].get('repairState',{}).get('fallbackVariants',[])
            while original.get('restoredFallbacks',0) < len(fallbacks):
                variant = fallbacks[original.get('restoredFallbacks',0)]
                original['restoredFallbacks'] = original.get('restoredFallbacks',0)+1
                alternate = variant_record(f,original,variant)
                if alternate is not None:
                    f['records'].append(alternate)
                    transition('restore_alternative',f,candidate=alternate['id'],recovery=recovery)
                    return True
        return False

    def close_frame(f, *, deferred=False):
        state['stack'].pop()
        old = next((v for v in state['frames'] if v['id']==f['id']),None)
        if old is not None:
            state['frames'].remove(old)
        state['frames'].append(f)
        if not f.get('validatedFinish') and not deferred:
            rejected=any((r.get('nodeFeasibility') or {}).get('status')=='oracle_rejected' for r in f['records'])
            if rejected or f.get('recoveryFailure'):
                parent=next((v for v in state['stack'] if f['parent'] and v['id']==f['parent']['frame']),None)
                if parent is not None:
                    parent['recoveryFailure']=dict(reason='required_descendant_oracle_rejected',frame=f['id'])
                    failed=parent.setdefault('failedCandidates',[])
                    if f['parent']['candidate'] not in failed:
                        failed.append(f['parent']['candidate'])
        if not deferred and (any(numerical_pending(r) or r.get('program',{}).get('repairState',{}).get('unresolved') for r in f['records']) or
                             f['generation'].get('continuation',{}).get('deferredNodes')):
            state['suspended'].append(f)
        transition('backtrack',f,parent=f['parent'])

    def support_episodes_for_child(record, frame):
        from parkour_solver.spatial.event_routes import observed_route
        return observed_route(adapter.data,record['child']['profile'],record['witness'],
                              after=stage_adapter(frame).request['afterTick'])

    def live_programs(f):
        # Frame programs outside the frontier that can still produce a group-end arrival.
        return f['ready']+([f['inFlight']['program']] if f['inFlight'] is not None else [])+[
            r['program'] for r in f['records'] if not r.get('witness') and numerical_pending(r)]

    def succeed(f, program):
        # A validated group-end arrival: its decision node and every ancestor keep their held alternatives.
        continuation=f['generation'].get('continuation')
        if continuation is None or 'succeededNodes' not in continuation:return
        done=set(continuation['succeededNodes']);node=program.get('decisionNode')
        while node and node not in done:
            continuation['succeededNodes'].append(node);done.add(node)
            node=continuation['decisionParents'].get(node)

    def restore_alternatives(f):
        from parkour_solver.search.frontier import restore_alternatives as restore
        continuation=f['generation'].get('continuation')
        if continuation is None or 'alternatives' not in continuation or not restore(continuation,live_programs(f)):
            return False
        continuation['exhausted']=False
        f['sourceExhausted']=False
        return True

    def retry_deferred_nodes(f):
        from parkour_solver.search.feasibility import restore_unknown_nodes
        source=f['generation'].get('continuation',{})
        if restore_unknown_nodes(source,retry_unknown):
            f['sourceExhausted']=False

    if finalize:
        finalize_best()
        return dict(status='feasible' if state['finalization']['best'] else 'unknown',termination='finalization',
                    best=state['finalization']['best'],searchState=state,work=work.summary())
    state['discoveryWork'] = dict(expansions=0,materializations=0)
    if not state['stack'] and state['nextFrame']==0 and not state.get('goalOwners'):
        frame(0,roots)
    for discovery in state['discovery'].values():
        if not discovery['exhausted']:
            expand_discovery(discovery)
    ready = next((f for f in state['goalQueue'] if f['owner']==state.get('pendingGoalOwner')),None)
    if ready is not None:
        state['goalQueue'].remove(ready)
        state['stack'].append(ready)
        state.pop('pendingGoalOwner')
    resumable = next((f for f in reversed(state['suspended']) if retry_unknown or any(numerical_pending(r) for r in f['records'])),None)
    if not state['stack'] and resumable is not None:
        f = resumable
        state['suspended'].remove(f)
        retry_deferred_nodes(f)
        f['retrySession'] = state['session']
        for r in f['records']:
            if retry_unknown and not r.get('witness'):
                f['ready'].append(r['program'])
        f['phase'] = 'score'
        state['stack'].append(f)
    resumable_ids = {f['id'] for f in state['suspended'] if retry_unknown or any(numerical_pending(r) for r in f['records'])}
    try:
        while state['stack'] or state['refinements'] or resumable_ids or state['goalQueue']:
            work.check()
            if not state['stack']:
                if state['goalQueue']:
                    state['stack'].append(state['goalQueue'].pop(0))
                    continue
                if state['refinements']:
                    f = state['refinements'].pop(0)
                else:
                    f = next(f for f in state['suspended'] if f['id'] in resumable_ids)
                    resumable_ids.remove(f['id'])
                    state['suspended'].remove(f)
                    if retry_unknown:
                        known = {candidate_key(p) for p in f['ready']}
                        for r in f['records']:
                            if not r.get('witness') and r['id'] not in known:
                                f['ready'].append(r['program'])
                retry_deferred_nodes(f)
                f['phase'] = 'score' if f['ready'] else 'generate' if not f['sourceExhausted'] else 'select'
                state['stack'].append(f)
            if cancel_event is not None and cancel_event.is_set():
                raise TimeoutError('cancelled')
            f = state['stack'][-1]
            # An uninterrupted run would have finished a solve the cancellation cut off; resume it
            # before any other work so a continued search repeats that run exactly.
            interrupted = next((r for r in f['records'] if r.get('termination')=='cancelled' and numerical_pending(r) and
                r.get('refinementSession')!=state['session']),None)
            if interrupted is not None:
                refine(f,interrupted)
                continue
            if f['phase']=='generate':
                generate(f)
            if f['phase']=='score':
                while f['ready'] or f['inFlight'] is not None:
                    if f['inFlight'] is not None:
                        work.check()
                        item = f['inFlight']
                        program = item['program']
                        previous = next((r for r in f['records'] if r['id']==item['id']),None)
                    else:
                        covered = {r['family'] for r in f['records'] if r.get('witness') and not r['tried']}
                        program = next((p for p in f['ready'] if decision_family(p,f['parents']) not in covered),None)
                        if program is None:
                            break
                        measured = [r for r in f['records'] if r.get('witness') and not r['tried']]
                        initial_domain = getattr(program_filter,'initial_jump_inputs',None) if f['index']==0 else None
                        observed = {next((t for t,j in enumerate(r['program']['profile']['jump'])
                            if j and r['program']['profile']['grounded'][t]),None) for r in measured}
                        compared_roots = initial_domain is None or set(initial_domain)<=observed
                        if measured and compared_roots and priority(program,f)[:3] > record_priority(min(measured,key=lambda r:record_priority(r,f)),f)[:3]:
                            break
                        work.check()
                        f['ready'].remove(program)
                        previous = next((r for r in f['records'] if r.get('candidateKey',r['id'])==candidate_key(program)),None)
                    if not eligible(program,f):
                        f['inFlight']=None
                        transition('candidate_exclusion',f,candidate=candidate_key(program))
                        continue
                    score(f,program,previous)
                f['phase'] = 'select'
            options = [r for r in f['records'] if r.get('witness') and not r['tried'] and not r.get('preparationDeferral')]
            if options:
                chosen = min(options,key=lambda r:record_priority(r,f))
                chosen['tried'] = True
                preparation=run_bound(chosen['child'],f)
                structural=hard_bound(chosen['child'],f)
                if preparation or structural:
                    chosen['preparationDeferral']=preparation or structural
                    transition('candidate_exclusion',f,candidate=chosen['id'],proof=preparation or structural)
                    continue
                proof=reject_time(chosen['child'],f)
                if proof is not None:
                    chosen['timeBoundExclusion']=proof
                    transition('candidate_exclusion',f,candidate=chosen['id'],proof=proof)
                    continue
                event=state['observations']['decisions'].get(chosen['program'].get('decisionNode'))
                if event is not None:event['selected']=event.get('selected',0)+1
                transition('choose',f,candidate=chosen['id'],arrival=chosen['arrival'],key=list(record_priority(chosen,f)))
                if final_frame(f):
                    for ancestor in state['stack']:
                        ancestor['validatedFinish']=True
                    candidate = copy.deepcopy(chosen['witness'])
                    candidate.update(movementProgram=chosen['child'],blockIds=[scene['startBlock'],scene['goalBlock']])
                    from parkour_solver.search.ranking import candidate_rank
                    if state['best'] is None or candidate_rank(candidate,scene['objective']) < candidate_rank(state['best'],scene['objective']):
                        state['best'] = candidate
                        work.accepted(candidate)
                        transition('incumbent',f,candidate=chosen['id'],objective=candidate['candidate']['objective'])
                    # Save local refinements, but return to actual siblings first.
                    if final_frame(f):
                        deferred = bool(f['ready'] or not f['sourceExhausted'] or any(numerical_pending(r) for r in f['records']))
                        if deferred:
                            state['refinements'].append(f)
                        close_frame(f,deferred=deferred)
                    continue
                window = dict(checkpoint=dict(tick=chosen['arrival']),goal=f['goal'],frame=f['id'],
                    candidates=copy.deepcopy(f['records']),retained=[r['id'] for r in options],
                    deferred=[candidate_key(p) for p in f['ready']],
                    scored=len(options),unknown=sum(not r.get('witness') for r in f['records']),heuristicDrops=0,
                    generation=copy.deepcopy(f['generation']),retention=dict(policy='saved_dfs_siblings_no_cap_eviction'))
                state['windows'].append(window)
                observed=support_episodes_for_child(chosen,f)
                cursor=adapter.advance(f['semanticCursor'],chosen['child'],observed,f['goalRequest'])
                frame(f['index']+1,[chosen['child']],parent=dict(frame=f['id'],candidate=chosen['id']),cursor=cursor)
                continue
            if f['ready']:
                f['phase'] = 'score'
                continue
            pending = next((r for r in f['records'] if not r.get('refinementTimeExclusion') and not r.get('preparationDeferral') and numerical_pending(r) and
                r.get('refinementSession')!=state['session']),None)
            if pending is not None:
                refine(f,pending)
                continue
            if not f['sourceExhausted']:
                f['phase'] = 'generate'
                continue
            if restore_alternatives(f):
                transition('restore_node_alternative',f)
                f['phase'] = 'generate'
                continue
            if f.get('recoveryFailure') and not f.get('validatedFinish'):
                recovery=f.pop('recoveryFailure')
                if restore_fallback(f,recovery):
                    f['phase']='select'
                    continue
                deferred=[r for r in state['minimumRunExclusions'] if r['frame']==f['id'] and
                          not r.get('restored') and not r.get('retired')]
                cached=False
                for saved in sorted(deferred,key=lambda r:priority(r['program'],f,r['candidate'])):
                    p=copy.deepcopy(saved['program']);p['restoredRun']=dict(frame=f['id'],candidate=saved['candidate'])
                    excluded=time_bound(p,f) or hard_bound(p,f)
                    if excluded is not None:
                        saved.update(retired=True,restorationExclusion=excluded)
                        continue
                    saved.update(restored=True,recovery=recovery)
                    prior=next((r for r in f['records'] if r.get('witness') and
                        candidate_key(r.get('child',r['program']))==saved['candidate']),None)
                    if prior is not None:
                        prior['program']['restoredRun']=p['restoredRun']
                        prior['child']['restoredRun']=p['restoredRun']
                        prior.pop('preparationDeferral',None);prior['tried']=False;cached=True
                    elif p['terminalSupport'] in f['goal'].get('blocks',[f['goal']['block']]):
                        f['ready'].append(p)
                    else:
                        continuation=f['generation'].get('continuation')
                        if continuation is not None:
                            continuation['pending'].append(p);continuation['exhausted']=False
                            f['sourceExhausted']=False
                    transition('restore_run',f,candidate=saved['candidate'],recovery=recovery,cachedWitness=cached)
                    break
                if cached or f['ready'] or not f['sourceExhausted']:
                    f['phase']='select' if cached else 'score' if f['ready'] else 'generate'
                    continue
            close_frame(f)
        if not state['stack'] and state['suspended']:
            termination = 'numerical_unknown'
    except TimeoutError as error:
        termination = work.termination or str(error)
    publish()
    if any(not d['exhausted'] for d in state['discovery'].values()) and termination=='exhausted':
        termination='discovery_work_limit'
    generations = [f['generation'] for f in state['frames']+state['stack']+state.get('goalQueue',[])]
    return dict(status='feasible' if state['best'] else 'cancelled' if termination=='cancelled' else
        'error' if termination=='error' else 'unknown',termination=termination,best=state['best'],
        windows=state['windows'],finalAttempts=state['finalAttempts'],corridor=adapter.corridor.to_dict(),
        routeGeneration=None,adapter=adapter.summary(),movementGeneration=generations,
        expandedPrefixes=state['expanded']-initial_expanded,totalExpandedPrefixes=state['expanded'],
        work=work.summary(),compiler=compiler.summary(),elapsedSeconds=time.monotonic()-started,threshold=0.,
        order='goal_arrival_depth_first',comparisonPolicy='goal_arrival_dfs',objectiveOptimalityProven=False,minimumTicksProven=False,searchState=state,
        resume=dict(exact=state['session']>1,session=state['session']),interactionGate=gate.summary(),viability=viability_counts,
        minimumRunPruning=dict(enabled=minimum_run_pruning,version=POLICY_VERSION,excluded=len(state['minimumRunExclusions']),
            scope='protected_hard_anchor_ticks',
            comparisonPolicy=COMPARISON_POLICY_VERSION,certifiedDominance=False))
