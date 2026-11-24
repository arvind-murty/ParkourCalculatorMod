import copy
import unittest
from unittest.mock import patch

from tools.profiling.runtime import CopyProxy, RuntimeProfile, instrument
from tools.profiling.bookkeeping import BookkeepingProfile, instrument as instrument_bookkeeping


class RuntimeProfileTests(unittest.TestCase):
    def test_nested_timings_are_exclusive_and_exception_safe(self):
        ticks = iter([0.,1.,3.,5.])
        p = RuntimeProfile(clock=lambda:next(ticks))
        with self.assertRaisesRegex(ValueError, 'stop'):
            with p.region('outer'):
                with p.region('inner'):
                    raise ValueError('stop')
        self.assertEqual(p.buckets['outer']['inclusiveSeconds'],5.)
        self.assertEqual(p.buckets['outer']['selfSeconds'],3.)
        self.assertEqual(p.buckets['inner']['selfSeconds'],2.)
        self.assertEqual(p.stack,[])

    def test_copy_proxy_preserves_aliases_cycles_and_original_library(self):
        original = copy.deepcopy
        p = RuntimeProfile();proxy = CopyProxy(p,'sample')
        shared = [];source = [shared,shared];shared.append(source)
        result = proxy.deepcopy(source)
        self.assertIs(result[0],result[1])
        self.assertIs(result[0][0],result)
        self.assertIsNot(result,source)
        self.assertIs(copy.deepcopy,original)
        self.assertEqual(sum(v['calls'] for v in p.buckets.values()),1)

    def test_generator_measures_only_active_next_and_closes(self):
        closed = []
        p = RuntimeProfile()
        def source():
            try:
                yield 1
                yield 2
            finally:
                closed.append(True)
        g = p.generator('choices',source)()
        self.assertEqual(next(g),1)
        self.assertEqual(p.stack,[])
        g.close()
        self.assertEqual(closed,[True])
        self.assertEqual(p.buckets['choices.next']['calls'],1)

    def test_scoped_instrumentation_restores_module_references_on_failure(self):
        from parkour_solver.search import frontier as movement_frontier
        from parkour_solver.search import dfs as depth_first
        before = (depth_first.copy,depth_first.search_goals,movement_frontier.generate_choices)
        with self.assertRaisesRegex(RuntimeError,'stop'):
            with instrument(RuntimeProfile()):
                self.assertIsNot(depth_first.copy,before[0])
                raise RuntimeError('stop')
        self.assertEqual((depth_first.copy,depth_first.search_goals,movement_frontier.generate_choices),before)

    def test_session_with_no_numerical_calls_may_omit_count(self):
        from parkour_solver.search import dfs as depth_first
        p = RuntimeProfile()
        empty = dict(work={},expandedPrefixes=0,termination='deadline')
        with patch.object(depth_first,'search_goals',return_value=empty):
            with instrument(p):
                result = depth_first.search_goals(resume_state=None,deadline=0.)
        self.assertIs(result,empty)
        self.assertEqual(p.sessions[0]['calls'],0)
        self.assertEqual(p.sessions[0]['expanded'],0)
        self.assertIn('dfs.session',p.sessions[0]['buckets'])


class BookkeepingProfileTests(unittest.TestCase):
    def test_alias_imported_during_context_is_restored(self):
        import sys
        from types import ModuleType
        from parkour_solver.search import identity
        original = identity.digest
        late = ModuleType('parkour_solver._profiling_late_import')
        with patch.dict(sys.modules, {late.__name__: late}):
            with instrument_bookkeeping(BookkeepingProfile(RuntimeProfile())):
                late.digest = identity.digest
                self.assertIsNot(late.digest, original)
            self.assertIs(late.digest, original)

    def test_instrumented_preparation_matches_unwrapped_model_and_cache_hits(self):
        from tests.support.checkpoint_guidance import flat_scene
        from tests.support.minimum_runs import flight
        from tests.support.proposal_analysis import Coefficients
        from parkour_solver.reachability.service import InteractionGate
        scene = flat_scene(); program = flight(scene, 0)
        expected = InteractionGate(scene, Coefficients()).analyze(program)
        compiler = Coefficients(); gate = InteractionGate(scene, compiler)
        profile = BookkeepingProfile(RuntimeProfile(), sample_every=1)
        with instrument_bookkeeping(profile):
            first = gate.analyze(program)
            self.assertIs(first, gate.analyze(program))
        self.assertEqual(first, expected)
        self.assertEqual(compiler.calls, 1)
        # An analysis hit is keyed by its preparation, so it never asks for the coefficients again.
        for kind, counts in (('preparation', (2,1,1)), ('coefficient', (1,0,1)), ('analysis', (2,1,1))):
            row = profile.snapshot()['caches'][kind]
            self.assertEqual((row['calls'], row['hits'], row['misses']), counts)

    def test_digest_sampling_is_bounded_and_preserves_canonical_output(self):
        from parkour_solver.canonical import digest
        profile = BookkeepingProfile(RuntimeProfile(), sample_every=2, sample_limit=3)
        wrapped = profile.digest(digest)
        value = dict(z=[-0., 1.1234567890123457], a={'owner': 'retained'})
        expected = digest(value)
        results = [wrapped(value) for _ in range(20)]
        self.assertEqual(results, [expected]*20)
        row = next(iter(profile.snapshot()['digests'].values()))
        self.assertEqual((row['calls'], row['samples'], row['sampledDistinct'], row['sampledRepeated']), (20,3,1,2))

    def test_hooks_restore_aliases_and_copy_behavior_after_exception(self):
        from parkour_solver.search import identity, engine
        from parkour_solver.reachability.service import InteractionGate
        from parkour_solver.constraints import variants
        before = (identity.digest, variants.digest, engine.copy, InteractionGate.prepare)
        shared = []; source = [shared, shared]; shared.append(source)
        p = BookkeepingProfile(RuntimeProfile(), sample_every=1)
        with self.assertRaisesRegex(RuntimeError, 'stop'):
            with instrument_bookkeeping(p):
                result = engine.copy.deepcopy(source)
                self.assertIs(result[0], result[1])
                self.assertIs(result[0][0], result)
                self.assertEqual(identity.digest({'x': 1}), variants.digest({'x': 1}))
                raise RuntimeError('stop')
        self.assertEqual((identity.digest, variants.digest, engine.copy, InteractionGate.prepare), before)
        self.assertEqual(sum(r['calls'] for r in p.copies.values()), 1)

    def test_cache_measurement_preserves_invalidation_and_failed_build(self):
        from parkour_solver.reachability.service import InteractionGate
        gate = InteractionGate({}, None)
        p = BookkeepingProfile(RuntimeProfile(), distinct_limit=1)
        original = InteractionGate._cached
        def fail():
            raise TimeoutError('interrupted build')
        with instrument_bookkeeping(p):
            first = gate._cached(('test', 'one'), lambda: ['value'], 'test')
            self.assertIs(gate._cached(('test', 'one'), fail, 'test'), first)
            with self.assertRaises(TimeoutError):
                gate._cached(('test', 'two'), fail, 'test')
        self.assertIs(InteractionGate._cached, original)
        self.assertNotIn(('test', 'two'), gate.cache)
        row = p.snapshot()['caches']['test']
        self.assertEqual((row['calls'], row['hits'], row['misses']), (3,1,2))
        self.assertTrue(row['keyTrackingTruncated'])

    def test_copy_allocation_probe_does_not_reuse_or_mutate_supplied_memo(self):
        p = BookkeepingProfile(RuntimeProfile(), allocation_samples=1)
        memo = {}; original = [{'x': list(range(50000))}]
        result = p.copy(copy.deepcopy, 'site', original, memo)
        self.assertIs(memo[id(original)], result)
        self.assertEqual(result, original)
        self.assertEqual(len(p.copies['site']['allocations']), 1)
        import tracemalloc
        self.assertFalse(tracemalloc.is_tracing())

    def test_durable_timing_adapter_preserves_callback_protocol(self):
        from tools.profiling.run import DurableProgress
        from unittest.mock import Mock
        checkpoint = Mock()
        runtime = RuntimeProfile()
        adapter = DurableProgress(checkpoint, runtime)
        state, program, extra = {}, {}, [{}]
        adapter(state, program, extra_targets=extra)
        adapter.begin_repair(state, program, extra_targets=extra)
        adapter.repair_progress(state)
        checkpoint.assert_called_once_with(state, program, extra_targets=extra)
        checkpoint.begin_repair.assert_called_once_with(state, program, extra_targets=extra)
        checkpoint.repair_progress.assert_called_once_with(state)
        self.assertEqual(len(runtime.buckets), 3)

    def test_stop_report_links_unknown_requests_to_live_routes(self):
        from tools.profiling.trace import counted_as, request_owners, stop_report
        def record(name, status, node, attempts, unresolved=()):
            return dict(id=name, status=status, nodeFeasibility=dict(status=node),
                        program=dict(repairState=dict(attemptLog=[dict(problemHash=h) for h in attempts],
                                                      unresolved=list(unresolved), pending=[])))
        rejected = dict(id=1, goal=dict(block='b3'), generation={}, records=[record('a','unknown','oracle_rejected',['h1'])])
        finish = dict(id=4, goal=dict(block='b7'), generation=dict(continuation=dict()),
                      records=[record('b','feasible','feasible',['h2','h3'],[dict(score=-1.)])])
        stale = dict(finish, records=[record('b','feasible','feasible',['h2'])])
        state = dict(dfs=dict(frames=[rejected, stale], stack=[], suspended=[finish]))
        owners = request_owners(state)
        requests = [dict(index=1, fingerprint='h1', status='unknown', workerStatus='Infeasible_Problem_Detected'),
                    dict(index=2, fingerprint='h3', status='unknown', workerStatus='Infeasible_Problem_Detected'),
                    dict(index=3, fingerprint='h4', status='feasible'),
                    dict(index=4, fingerprint='h5', status='unknown', backend='clarabel', workerStatus='NumericalError')]
        for request in requests:
            request['owners'] = owners.get(request['fingerprint'], [])
        report = stop_report(state, requests)
        self.assertEqual(report['suspendedFrames'], [dict(frame=4, goal='b7', records=1, unresolvedBranches=1,
                                                          pendingBranches=0)])
        self.assertEqual([r['record'] for r in report['oracleRejectedRoutes']], ['a'])
        self.assertEqual([(r['index'], [o['record'] for o in r['owners']]) for r in report['unknownRequests']],
                         [(1, ['a']), (2, ['b']), (4, [])])
        self.assertEqual(requests[2]['owners'], [])
        # Every unsolved Clarabel request is an infeasible verdict, whatever its status.
        self.assertEqual(counted_as(report['unknownRequests'][2]), 'infeasible (disk relaxation)')

    def test_alternatives_without_a_solution_name_the_check_that_proved_them_infeasible(self):
        from tools.profiling.trace import alternative_outcome, repair_trace, solve_text
        certificate = dict(reason='disk_control_relaxation_infeasible')
        repair = dict(attemptLog=[dict(problemHash='p', status='blocked', certificate=certificate),
                                  dict(problemHash='i', status='unknown'), dict(problemHash='r', status='unknown')],
                      completed=[dict(branch='[[], [], []]', status='unknown', proven=True, certificates=[certificate],
                                      problemHashes=['p']),
                                 dict(branch='[[], [], []]', status='unknown', oracleRejected=True, problemHashes=['i']),
                                 dict(branch='[[], [], []]', status='unknown', problemHashes=['r'])])
        requests = dict(i=dict(index=8, workerStatus='Infeasible_Problem_Detected', seconds=.37),
                        r=dict(index=9, workerStatus='Maximum_CpuTime_Exceeded'))
        proven, local, kept = repair_trace(repair)['alternatives']
        self.assertEqual([alternative_outcome(a, requests) for a in (proven, local, kept)],
                         ['infeasible (disk control relaxation)', 'infeasible (IPOPT local)', 'unknown'])
        self.assertEqual(solve_text(proven['solves'][0], requests), 'infeasible (disk control relaxation)')
        self.assertEqual(solve_text(local['solves'][0], requests),
                         'request 8 infeasible (IPOPT local), IPOPT Infeasible_Problem_Detected, 0.37 s')

    def test_generator_rejections_are_grouped_per_frame_with_their_innermost_proof(self):
        from tools.profiling.trace import exclusion_lines, generator_exclusions
        far = dict(ruledOut=True, reason='all_interaction_alternatives_excluded', alternatives=[dict(
            ruledOut=True, reason='interaction_distance_unreachable', tick=t, phase='Y', gap=4.2, travelUpperBound=2.5)
            for t in (3,)])
        joint = dict(reason='required_interaction_unreachable', input=9, proof=dict(
            ruledOut=True, reason='interval_reach_contradiction', state=9, axis=0))
        result = dict(effectiveRequest=dict(blocks=[dict(id='b4', boxes=[[0, 0, 0, 1, 1, 1]])]), interactionGate=dict(exclusions=[
            dict(owner='b1', role='landing', frame=0, horizontal=[1., 2., 3., 4.], input=t, proof=far) for t in (3, 4, 6)] + [
            dict(reason='joint_prerequisites_unreachable', frame=2, input=5, proof=joint,
                 cases=[dict(case='blip', boxes=[[0, 0, 0, 1, 1, 1]])])]))
        frames = generator_exclusions(result)
        landing, = frames[0]
        self.assertEqual((landing['count'], landing['ticks'], landing['area']),
                         (3, [3, 4, 6], 'X [1.000, 3.000] × Z [2.000, 4.000]'))
        self.assertIn('| B1 landing | X [1.000, 3.000] × Z [2.000, 4.000] | t3-4, t6 | distance reachability | 3 | '
                      'at tick 3 the nearest point is 4.200 blocks away, travel at most 2.500 |', exclusion_lines(frames[0]))
        self.assertIn('| B4 blip (joint prerequisites) | - | t9 | required contact reachability | 1 | empty X interval at state 9 |',
                      exclusion_lines(frames[2]))

    def test_route_trace_links_alternatives_solves_and_inherited_rows(self):
        import json
        from tools.profiling.trace import route_trace, side as label
        def side(axis, direction, plane):
            bound = dict(axis=axis, direction=direction, plane=plane, owners=[0])
            return dict(key=[[axis, direction, plane]], bounds=[bound], owners=[0], region=[None]*4,
                        axis=axis, direction=direction, plane=plane)
        corner = dict(side('X', 1, 1.), axis='XZ', bounds=[side('X', 1, 1.)['bounds'][0], side('Z', 1, 2.)['bounds'][0]])
        self.assertEqual(label(corner), 'X+Z+')
        landing = dict(mode='X', t1=3, cmp='GE', rhs=1., name='normal:landing')
        avoid = dict(mode='Z', t1=2, cmp='LE', rhs=-2., name='avoidance')
        def record(name, key, rows, repair=None, witness=None):
            return dict(id=name, candidateKey=key, status='feasible', ticks=4, nodeFeasibility=dict(status='feasible'),
                        witness=witness, program=dict(profile=dict(positionConstraints=rows, jump=[True, False, False, False],
                            contactEvents=[dict(offset=3, axis='Y', direction=-1, boxes=[[0, 0, 0, 1, 1, 1]], role='landing')]),
                            guideSeedYaw=[1.],
                                                      repairState=repair or {}))
        branch = json.dumps([[[0, 2, 'Z', 'LE', -2.]], [], []])
        later = json.dumps([[[0, 3, 'Z', 'LE', -2.]], [], []])
        point = dict(status='settled', held=[], pending=0)
        repair = dict(completed=[dict(branch=branch, status='feasible', validationStatus='passed', problemHashes=['h1']),
                                 dict(branch=later, status='feasible', validationStatus='accepted', problemHashes=[])],
                      families=dict(f=dict(order=['[2]', '[3]', '[4]'], source=None, done=True, points={
                          '[2]': dict(point, switches=[2], rank=[4, 2.], objective=-2., keys=[branch]),
                          '[3]': dict(point, switches=[3], rank=[4, 2.1], objective=-2.1, keys=[later]),
                          '[4]': dict(point, switches=[4], status='held', rank=None, objective=None, held=[{}], keys=['k4'])})),
                      interactionChoices={branch: [dict(blockId='b6', faces=[side('Z', -1, -2.), side('X', -1, 0.)])]},
                      fallbackVariants=[dict(branch=later, best={})],
                      incumbent=dict(rows=[dict(leg=0, offset=2, mode='Z', cmp='LE', rhs=-2.)], passages=[], hits=[]),
                      attemptLog=[dict(status='feasible', problemHash='h1')],
                      constraintGeneration=dict(items=[dict(owner='b6', clusterId=0, members=[0], names=['b6'], begin=0, end=2),
                                                       dict(contactCheck={})],
                          metrics=dict(excludedFaces=2), encounterLog=[dict(clusterId=0, members=[0], names=['b6'], owner='b6',
                              segment=[0, 2], possiblePhases=[[1, 'X']], sides=[side('X', -1, 0.), side('X', 1, 1.)],
                              coveredPhases=[[3, [0]]], excludedFaces=[[4, 1]], verticalClear=[], uncertain=[])],
                          droppedOptions=[dict(clusterId=0, members=[0], segment=[0, 2], reason='dominated', rows=[avoid])],
                          screenExclusions=[dict(rowsHash='r', proof=dict(ruledOut=True, state=2, axis=1,
                              reason='interval_reach_contradiction'), item=0, optionRows=[avoid], passage=None)]))
        parent = record('p', 'k0', [landing], repair, dict(problemHash='h1', candidate=dict(objective=-2.), plan=dict(ticks=4, walls=[
            dict(avoid, t2=2), dict(mode='F', t1=0, cmp='GE', rhs=0.), dict(landing, name='checkpoint_probe:x')])))
        sibling = record('s', 'k0', [landing], witness=dict(problemHash='h1', candidate=dict(objective=-2.), plan=dict(ticks=4)))
        child = record('c', 'k1', [landing, dict(avoid, t2=2)])
        state = dict(dfs=dict(frames=[dict(id=0, goal=dict(block='b2'), parent=None, records=[parent, sibling]),
                                      dict(id=1, goal=dict(block='b3'), parent=dict(frame=0, candidate='p'), records=[child])]))
        routes = {r['record']: r for r in route_trace(state, dict(blocks=[dict(id='b2', boxes=[[0, 0, 0, 1, 1, 1]])]))}
        trace = routes['p']['repair']
        self.assertEqual([e['owner'] for e in trace['encounters']], ['b6'])
        self.assertEqual(trace['contactChecks'], 1)
        self.assertEqual({k: trace['screenRejections'][0][k] for k in ('reason', 'encounter', 'segment', 'optionRows')},
                         dict(reason='interval_reach_contradiction', encounter='b6', segment=[0, 2], optionRows=[avoid]))
        encounter = trace['preparedEncounters'][0]
        self.assertEqual((encounter['coveredPhases'], encounter['excludedFaces'], trace['droppedOptions'][0]['reason']),
                         ([dict(phase=[1, 'Y'], certainFaces=['X-'])], [dict(phase=[1, 'X'], face='X+')], 'dominated'))
        self.assertEqual(trace['alternatives'][0]['solves'], [dict(problemHash='h1', status='feasible', reason=None)])
        self.assertEqual((routes['s']['repairOwner'], routes['s']['repair']), ('p', None))
        self.assertEqual(routes['s']['acceptedAlternative'], [[0, 2, 'Z', 'LE', -2.]])
        self.assertEqual((routes['c']['parent'], routes['c']['inheritedRows'], routes['c']['seededFacings']),
                         ('p', [dict(avoid, t2=2)], True))
        from tools.profiling.trace import render, stop_report
        requests = [dict(index=1, fingerprint='h1', status='feasible', workerStatus='Solve_Succeeded', seconds=.25)]
        relaxation = dict(screen=dict(solved=5, cacheHits=4, infeasible=1), candidate=dict(solved=7))
        report = dict(result=dict(best=None, termination='exhausted', elapsedSeconds=1., work=dict(solverCalls=1, workerLaunches=1),
                                  expandedPrefixes=2, searchState=state, interactionGate=dict(relaxation=relaxation)))
        markdown = render(report, requests, list(routes.values()), stop_report(state, requests), {})
        self.assertIn('Disk-relaxation pre-checks: 5 solved (4 more from cache), 1 proved infeasible.', markdown)
        self.assertIn('- Frame 0 → B2: 2 routes (2 feasible)\n  - Frame 1 → B3: 1 route (1 feasible), from route 0.1 (lands B2 t3)', markdown)
        self.assertIn('#### Movement: jumps at t0; lands B2 t3; no ceiling hit', markdown)
        self.assertIn('| 0.2 | feasible | 4 | -2.000 | no obstacle in reach | Z ≤ -2.000 at t2 |', markdown)
        self.assertIn('| 1 | B6 sides t0-2 | Z ≤ -2.000 at t2 | interval reachability | empty Z interval at state 2 |', markdown)
        self.assertIn('| 1 | Z ≤ -2.000 at t2 | feasible | passed | request 1 feasible, IPOPT Solve_Succeeded, 0.25 s |', markdown)
        self.assertIn('Solver time for this movement: 0.25 s over 1 request.', markdown)
        self.assertIn('| B6: Z- then X- | input 2 (#1: -2.000); input 3 (#2: -2.100) | input 2 (#1: -2.000) | 1 |', markdown)
        self.assertIn('Kept: alternative #1; fallbacks: alternative #2.', markdown)
        self.assertIn('| b6 | 0-2 | 1X | 1 | X+ at 1X | none | none |', markdown)
        from tools.profiling.trace import rows_text
        self.assertEqual(rows_text([dict(mode='X', t1=1, cmp='GE', rhs=1.000001)], [('B6', 'X', 'GE', 1.), ('B7', 'X', 'GE', 1.)]),
                         'B6/B7: X ≥ 1.000 at t1')

    def test_node_tree_describes_each_node_and_groups_what_its_expansion_pruned(self):
        from tools.profiling.trace import node_trace, render, stop_report
        low, high, lid = [0, 0, 0, 1, 1, 1], [2, 0, 0, 3, 1, 1], [0, 3, 0, 1, 4, 1]
        def land(t, box):
            return dict(offset=t, axis='Y', direction=-1, boxes=[box], role='landing')
        def node(parent, status, end, jumps, events, cases=(), rows=(), repair=None):
            profile = dict(jump=[t in jumps for t in range(end)], contactEvents=events, caseEvents=list(cases),
                           positionConstraints=list(rows))
            return dict(status=status, parent=parent, depth=1, program=dict(movementUnits=[dict(end=end)], profile=profile),
                        **(dict(repairState=repair) if repair else {}))
        repair = dict(completed=[dict(branch='[[], [], []]', status='feasible', validationStatus='accepted', problemHashes=['h1'])],
                      attemptLog=[dict(status='feasible', problemHash='h1')])
        grind = (dict(case='grind', input=7, boxes=[lid]),)
        outside = (dict(mode='X', t1=7, cmp='LE', rhs=-.3, name='grind:outside'),)
        ground = [land(0, low), land(1, low), land(2, low), land(5, high), land(6, high)]
        nodes = dict(a=node('root', 'feasible', 6, {3}, ground[:4], repair=repair),
                     b=node('a', 'feasible', 8, {3, 7}, ground, grind, outside),
                     c=node('reserve', 'exhausted', 9, {3, 7}, ground+[land(8, high)], grind, outside))
        # A node built from a held reserve belongs to the node holding it, as does a child queued from the reserve.
        continuation = dict(nodes=nodes, decisionParents=dict(reserve='b'),
                            pending=[dict(needsNodeSolve=True, decisionParent='reserve')], exclusions=[
            dict(node='a', proof=dict(reason='search_policy')), dict(node='root', proof=dict(reason='disk_control_relaxation_infeasible'))],
            minimumRunCandidates=[dict(node='root', key='["ceiling", "b3"]', k=1, total=9, N=12, outcome='kept', nodes=['a']),
                                  dict(node='a', key='["step", "b2"]', k=None, total=None, N=None, outcome='unreached', nodes=[])],
            # Node a's held side variants: one restored behind its slack forward b, one promoted, one still waiting.
            sideVariants=[dict(node='a', child='s1', case='slab_boost', face='b2', side='X-', forwards=[['b', 'Z-']],
                               outcomes=dict(b='slack'), alternative='held'),
                          dict(node='a', child='s2', case='normal_step', face='b2', side='X+', forwards=[['b', 'Z-'], ['x', 'X-']],
                               outcomes=dict(b='tight', x='failed')),
                          dict(node='a', child='s3', case='slab_boost', face='b2', side='X+', forwards=[['b', 'Z-']])],
            alternatives=dict(held=dict(programs=[dict(decisionNode='s1')], restored=['s1'], next=1, watch=['b'], side=['b'])))
        frame = dict(id=3, goal=dict(block='b2'), parents=[dict(decisionNode='root')], goalRequest=dict(afterTick=0), records=[],
                     generation=dict(generated=9, screenedPrefixes=1, policyRejections=2, totalExpandedPrefixes=2,
                                     continuation=continuation))
        state = dict(dfs=dict(stack=[frame]))
        scene = dict(blocks=[dict(id='b1', boxes=[low]), dict(id='b2', boxes=[high]), dict(id='b3', boxes=[lid, [0, 4, 0, 1, 5, 1]])])
        trees = node_trace(state, scene)
        requests = [dict(index=4, fingerprint='h1', status='feasible', workerStatus='Solved', backend='clarabel', seconds=.01)]
        report = dict(result=dict(best=None, termination='deadline', elapsedSeconds=1., work=dict(solverCalls=1, workerLaunches=0),
                                  expandedPrefixes=2, searchState=state, effectiveRequest=scene))
        markdown = render(report, requests, [], stop_report(state, requests), trees)
        self.assertIn('| Block | X | Y | Z |\n|---|---|---|---|\n| B1 | [0, 1] | [0, 1] | [0, 1] |\n'
                      '| B2 | [2, 3] | [0, 1] | [0, 1] |\n| B3#0 | [0, 1] | [3, 4] | [0, 1] |\n| B3#1 | [0, 1] | [4, 5] | [0, 1] |', markdown)
        self.assertIn('Decision nodes solved: 3 (2 feasible, 1 no alternative survived (backtracks)). Prefixes generated: 9; '
                      'ruled out by the screens: 1; rejected by the search policy: 2 (1 when generated, 1 when taken from the '
                      'queue); expanded: 2; left unexpanded: 1.', markdown)
        # t6 follows t5's landing input, so it is a run tick; node 3.3 starts on the grind's landing input.
        self.assertIn('- node 3.1 (feasible, t0-5): runs 3 ticks on B1 (t0-2), jumps t3, lands on B2 t5\n'
                      '  - node 3.2 (feasible, t6-7): runs 1 tick on B2 (t6), jumps t7, grinds under B3#0 t7 from its X- '
                      'side\n    - node 3.3 (exhausted, t8-8): lands on B2 t8', markdown)
        self.assertIn('Solved children: node 3.1.\nChildren queued but not yet solved: 0.\nChildren pruned when generated: 1 disk '
                      'control relaxation.\nMinimum run ticks: ceiling jump under B3 after 1 run tick, landing after 9 inputs '
                      '(bound 12) → node 3.1.', markdown)
        self.assertIn('Solved children: node 3.3.\nChildren queued but not yet solved: 1.', markdown)
        self.assertIn('| 1 | no avoidance rows | feasible | accepted | request 4 feasible, Clarabel Solved, 0.01 s |', markdown)
        self.assertIn('Solved children: node 3.2.\nChildren queued but not yet solved: 0.\nChildren pruned when generated: 1 '
                      'search policy (time, hard-anchor latch '
                      'or minimum-run bound).\nMinimum run ticks: no run count below the bound passed the screens: step-up '
                      'onto B2.\n\nSide variants:\n\n- slab boost onto B2 from its X- side: reserved behind node 3.2 (its Z- '
                      "side row was slack); restored: node 3.2's branch died.\n- normal step onto B2 from its X+ side: promoted: "
                      "node 3.2's Z- side row was tight, an unsolved node failed.\n\n", markdown)

    def test_case_steps_name_their_faces_and_wall_hits_their_face(self):
        from tools.profiling.trace import steps
        lower, higher, low = [0, 6, 0, 1, 6.5, 1], [0, 6, 1, 1, 7, 2], [0, 0, 0, 1, 6, 1]
        names = {tuple(lower): 'b13a', tuple(higher): 'b13b', tuple(low): 'b12'}
        def row(name, t, mode, cmp):
            return dict(name=name, t1=t, mode=mode, cmp=cmp, rhs=0.)
        def text(cases, rows, events=()):
            return steps(dict(jump=[False]*4, caseEvents=cases, positionConstraints=rows, contactEvents=list(events)), 0, names)
        blip = dict(case='blip', input=2, boxes=[higher], lowerBoxes=[lower])
        self.assertEqual(text([blip], [row('blip:wall_avoid', 2, 'Z', 'LE')]),
                         'lands on B13A and blips onto B13B t2 from its Z- side')
        # A wall hit names the face it meets: moving +Z meets the Z- face.
        hit = dict(offset=1, axis='Z', direction=1, boxes=[higher], role='contact')
        self.assertEqual(text([], [], [hit]), "hits B13B's Z- face t1")
        slab = dict(case='slab_boost', input=1, boxes=[higher])
        self.assertEqual(text([slab], [row('case:outside', 1, 'X', 'GE')]), 'slab boost onto B13B t1 from its X+ side')
        self.assertEqual(text([slab], [row('case:outside', 1, 'X', 'GE'), row('ceiling_clear:b10+b9a', 1, 'X', 'GE')]),
                         'slab boost onto B13B t1 from its X+ side, keeping to the X+ side of ceiling B10+B9A')
        # A corner case passes the corner (X side before it, Z side after) and ends beside the block.
        corner = dict(case='corner_blip_up', input=2, boxes=[higher], lowerBoxes=[low])
        self.assertEqual(text([corner], [row('case:outside', 2, 'X', 'LE'), row('case:outside', 3, 'Z', 'LE'),
                                         row('case:cross_x', 3, 'X', 'GE')]),
                         "lands on B12 and corner blips up past B13B's X-Z- corner t2")

    def test_contact_region_items_and_chosen_rectangles_are_named(self):
        import json
        from tools.profiling.trace import family_sides, item_label, repair_trace
        options = [[dict(leg=0, offset=6, mode=m, cmp=c, rhs=v, name='contact:region') for m, c, v in rows]
                   for rows in ([('X', 'GE', 728.7), ('X', 'LE', 729.7), ('Z', 'GE', -1095.3), ('Z', 'LE', -1092.7)],
                                [('X', 'GE', 729.7), ('X', 'LE', 730.3), ('Z', 'GE', -1093.7), ('Z', 'LE', -1092.7)])]
        item = dict(rows=options, role='contact_region', surface='surface:b7', owner='b7')
        self.assertEqual(item_label(item), ('b7 region', [6, 6]))
        trace = repair_trace(dict(constraintGeneration=dict(items=[item])))
        self.assertEqual(trace['contactRegions'], [dict(ticks=[6], owner='b7', surface='surface:b7', options=options)])
        from parkour_solver.search.repair_state import branch_key
        family = dict(order=[0], points=[dict(keys=[branch_key(options[1], [], [])])])
        self.assertEqual(family_sides({}, family, [item]), 'B7: X [729.700, 730.300] × Z [-1093.700, -1092.700]')


class EnumerationProfileTests(unittest.TestCase):
    def test_repeated_contexts_share_report_rows_without_losing_counts(self):
        from parkour_solver.constraints.variants import ConstraintVariants
        from tools.profiling.enumeration import EnumerationProfile,instrumented_option
        profile=EnumerationProfile(); function=instrumented_option(ConstraintVariants.option,profile)
        first=None
        for _ in range(2):
            source=self.source();cursor={}
            while function(source,source.items[0],cursor) is not None:pass
            snapshot=profile.snapshot()
            if first is None:
                first=(len(profile.rows),snapshot['totals']['visits'])
        self.assertEqual(len(profile.rows),first[0])
        self.assertEqual(snapshot['totals']['visits'],first[1]*2)
        self.assertTrue(snapshot['exitsPartitionVisits'])

    def test_enumeration_hooks_restore_after_exception_without_global_probe(self):
        from contextlib import nullcontext
        from parkour_solver.constraints.variants import ConstraintVariants
        from parkour_solver.search import engine
        from tools.profiling.enumeration import EnumerationProfile,instrument
        original, evaluate = ConstraintVariants.option, engine.evaluate_program
        with self.assertRaisesRegex(RuntimeError,'stop'):
            with instrument(EnumerationProfile(),nullcontext()):
                self.assertIsNot(ConstraintVariants.option,original)
                raise RuntimeError('stop')
        self.assertIs(ConstraintVariants.option,original)
        self.assertIs(engine.evaluate_program,evaluate)
        self.assertNotIn('_enumeration_probe',original.__globals__)

    @staticmethod
    def source(state=None):
        from parkour_solver.constraints.variants import ConstraintVariants
        from parkour_solver.reachability.intervals import reachable_envelopes
        model=dict(ticks=4,position=[-1.,0.],velocity=[0.,0.],magnitude=[1.]*4,friction=[.9]*4,walls=[])
        return ConstraintVariants(dict(passageCaps=dict(horizontal=2,vertical=2,total=3),
            contactEvents=[]),dict(position=[1.]*5),
            dict(events=[dict(jump=0,runTicks=0)]),dict(solids=[[0.,0.,-.2,1.,3.,.2]]),
            reachable_envelopes(model,[1.]*5),dict(through=4,solidIndices=[0]),state=state)

    def test_option_observations_preserve_outputs_cursors_and_exit_partition(self):
        from parkour_solver.constraints.variants import ConstraintVariants
        from tools.profiling.enumeration import EnumerationProfile,instrumented_option
        original=ConstraintVariants.option
        def consume(source, function):
            cursor={};values=[]
            while True:
                value=function(source,source.items[0],cursor)
                if value is None:break
                values.append(value)
            return values,cursor
        expected=consume(self.source(),original)
        p=EnumerationProfile()
        actual=consume(self.source(),instrumented_option(original,p))
        self.assertEqual(actual,expected)
        summary=p.snapshot()
        self.assertTrue(summary['exitsPartitionVisits'])
        self.assertGreater(summary['totals']['visits'],len(actual[0]))
        self.assertEqual(summary['totals']['returned'],len(actual[0]))

    def test_json_interruption_cursor_is_identical_with_observations(self):
        import json
        from parkour_solver.constraints.variants import ConstraintVariants
        from tools.profiling.enumeration import EnumerationProfile,instrumented_option
        def partial(function):
            source=self.source();source.prepare_item(source.items[0]);cursor={};count=0;values=[]
            def check():
                nonlocal count
                count+=1
                if count>25:raise TimeoutError('fixed work')
            source.check=check
            with self.assertRaises(TimeoutError):
                while True:
                    value=function(source,source.items[0],cursor)
                    if value is None:break
                    values.append(value)
            return values,json.loads(json.dumps(cursor)),json.loads(json.dumps(source.items[0]))
        expected=partial(ConstraintVariants.option)
        p=EnumerationProfile()
        self.assertEqual(partial(instrumented_option(ConstraintVariants.option,p)),expected)
        self.assertTrue(p.snapshot()['exitsPartitionVisits'])

    def test_source_change_is_rejected_instead_of_silent_miscount(self):
        from tools.profiling.enumeration import EnumerationProfile,instrumented_option
        with self.assertRaisesRegex(RuntimeError,'source changed'):
            instrumented_option(self.source,EnumerationProfile())


class ParityTests(unittest.TestCase):
    @staticmethod
    def result(objective, transitions, ticks=64, passed=True):
        return dict(validation=[dict(passed=passed)],result=dict(status='feasible',termination='exhausted',
            best=dict(plan=dict(ticks=ticks),candidate=dict(objective=objective)),
            searchState=dict(kind='event_route_search',dfs=dict(transitions=transitions))))

    def test_comparators_reject_defects_and_tolerance_only_relaxes_finite_floats(self):
        from tools.profiling.parity import KERNEL,SOLVE,compare,self_test
        self.assertEqual(self_test(**KERNEL)['rejected'],self_test(**{'rel_tol':None,**SOLVE})['rejected'])
        self.assertEqual(self_test(rel_tol=1e-9,abs_tol=None)['rejected'],
                         ['int_vs_float','large_float','missing_key','order','signed_zero','verdict'])
        self.assertAlmostEqual(compare(dict(a=[1.,2.]),dict(a=[1.,2.+1e-12]),rel_tol=1e-9),1e-12,delta=1e-15)
        for a,b in (([float('inf')],[1e308]),([True],[1]),(['x'],['y'])):
            with self.assertRaises(AssertionError):
                compare(a,b,rel_tol=1.)
        self.assertEqual(compare([float('nan')],[float('nan')]),0.)
        self.assertEqual(compare([1,0.],[1.,0.],numeric_types=False),0.)
        for b in ([True],['1'],[-0.]):
            with self.assertRaises(AssertionError):
                compare([1 if b!=[-0.] else 0.],b,numeric_types=False)
        with self.assertRaisesRegex(ValueError,'tolerance'):
            self_test(rel_tol=None,abs_tol=None)

    def test_route_comparison_ignores_timestamps_and_in_tolerant_mode_digests(self):
        from tools.profiling.parity import compare_routes,transition_signature
        d1,d2='a'*64,'b'*64
        event=lambda digest,elapsed:dict(kind='expand',depth=1,candidate=digest,elapsedSeconds=elapsed)
        python=self.result(-1093.2381026602418,[event(d1,0.5),event(d1,1.)])
        native=self.result(-1093.2381026602418,[event(d1,0.7),event(d1,2.)])
        self.assertEqual(transition_signature(python['result']),transition_signature(native['result']))
        self.assertTrue(compare_routes(python,native)['matches'])
        drifted=self.result(-1093.2381026602418*(1+1e-12),[event(d2,0.),event(d2,0.)])
        exact=compare_routes(python,drifted)
        self.assertEqual(([d['field'] for d in exact['differences']],exact['firstTransitionDifference']),
                         (['objective','transitions'],0))
        self.assertTrue(compare_routes(python,drifted,rel_tol=1e-9)['matches'])
        diverged=self.result(-1093.2381026602418,[event(d1,0.),dict(kind='backtrack',depth=0,elapsedSeconds=0.)])
        self.assertEqual(compare_routes(python,diverged,rel_tol=1e-9)['firstTransitionDifference'],1)
        self.assertEqual([d['field'] for d in compare_routes(python,self.result(-1.,[],ticks=65))['differences']],
                         ['ticks','objective','transitions'])
        unvalidated=self.result(-1093.2381026602418,[event(d1,0.),event(d1,0.)],passed=False)
        self.assertEqual([d['field'] for d in compare_routes(python,unvalidated,rel_tol=1e-9)['differences']],['validation'])

    def test_replacements_cover_imported_aliases_and_restore_them(self):
        from parkour_solver.reachability import intervals,service
        from tools.profiling.parity import load_oracle,replacements
        original=intervals.reachable_envelopes
        oracle=load_oracle(intervals)
        self.assertIsNot(oracle.reachable_envelopes,original)
        replacement=lambda *a,**k:None
        with replacements(dict(reachable_envelopes=original),dict(reachable_envelopes=replacement)):
            self.assertIs(service.reachable_envelopes,replacement)
            self.assertIs(intervals.reachable_envelopes,replacement)
        self.assertIs(service.reachable_envelopes,original)


if __name__ == '__main__':
    unittest.main()
