import copy
import gzip
import json
import os
import random
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from parkour_solver import native_core
from parkour_solver.io.checkpoint_store import CheckpointStore, read_report
from parkour_solver.io.search_checkpoint import SearchCheckpoint
import tests.support.repair_resume as repair_tests


class StoreCase(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name)/'report.json.gz'


class CheckpointStoreTests(StoreCase):
    def test_lossless_sharing_without_mutable_aliases(self):
        shared = dict(array=[[.1,-2.,3.]]*200, literal=['r','not-a-reference'], escaped={'format':'pkc-checkpoint-store'})
        report = dict(a=shared,b=copy.deepcopy(shared),special=['d',[['r','fake']]],unicode='⛏',empty={},number=123)
        with CheckpointStore(self.path) as store:
            first = store.save(report)
            size = store.database.stat().st_size
            again = store.save(report)
            self.assertGreater(first['newObjects'],0)
            self.assertEqual(again['newObjects'],0)
            self.assertEqual(size,store.database.stat().st_size)
        restored = read_report(self.path)
        self.assertEqual(restored,report)
        restored['a']['array'][0][0] = 999
        self.assertEqual(restored['b'],shared)
        self.assertEqual(restored['a']['array'][1][0],.1)

    def test_changes_are_detected_across_saves_and_reopening(self):
        report = dict(queue=[dict(rows=[0.]*1000)],seen=['x'*3000])
        with CheckpointStore(self.path) as store:
            store.save(report)
            report['queue'][0]['rows'][0] = 42.
            store.save(report)
        self.assertEqual(read_report(self.path),report)
        with CheckpointStore(self.path) as store:
            self.assertEqual(store.save(report)['newObjects'],0)

    def test_failed_manifest_publication_preserves_previous_committed_root(self):
        with CheckpointStore(self.path) as store:
            store.save(dict(accepted='old'))
            # The manifest's temporary file cannot be written: the publication fails after its objects commit.
            temporary=self.path.with_name(self.path.name+'.tmp')
            temporary.mkdir()
            with self.assertRaises(OSError):
                store.save(dict(accepted='new'))
            temporary.rmdir()
            self.assertEqual(read_report(self.path),dict(accepted='old'))
            store.save(dict(accepted='new'))
        self.assertEqual(read_report(self.path),dict(accepted='new'))

    def test_legacy_and_companion_errors(self):
        for name in ('old.json','old.json.gz'):
            path = self.path.with_name(name)
            data = json.dumps(dict(searchState={'version':5})).encode()
            path.write_bytes(gzip.compress(data) if path.suffix=='.gz' else data)
            self.assertEqual(read_report(path),dict(searchState={'version':5}))
        with CheckpointStore(self.path) as store:
            store.save(dict(pending=[1,2]))
            database = store.database
        manifest = json.loads(gzip.decompress(self.path.read_bytes()))
        manifest['storeId'] = 'wrong'
        self.path.write_bytes(gzip.compress(json.dumps(manifest).encode()))
        with self.assertRaisesRegex(ValueError,'does not match'):
            read_report(self.path)
        database.unlink()
        with self.assertRaisesRegex(ValueError,'Missing checkpoint companion'):
            read_report(self.path)

    def test_patch_and_compaction_keep_needed_roots(self):
        with CheckpointStore(self.path) as store:
            base, _, _ = store.archive(dict(unchanged={'log':['x'*4000]},candidate=None))
            store.patch(base,[(['candidate'],dict(objective=1.))])
            store.compact()
            self.assertEqual(read_report(self.path)['candidate'],dict(objective=1.))
            store.patch(base,[(['candidate'],dict(objective=2.))])
            self.assertEqual(read_report(self.path)['candidate'],dict(objective=2.))
            store.save(dict(final=True))
            store.compact()
            self.assertEqual(read_report(self.path),dict(final=True))
            self.assertLess(len(store.known),5)

    def test_archive_reports_every_alias_path_of_its_targets(self):
        def walk(node, target):
            # Independent reference: every key path at which `target` occurs, without descending into it.
            if node is target:
                return [()]
            if not isinstance(node, (dict,list,tuple)):
                return []
            children = node.items() if isinstance(node,dict) else enumerate(node)
            return [(key,)+tail for key,child in children for tail in walk(child,target)]

        rng = random.Random(7)
        def build(pool, depth):
            kind = rng.random()
            if depth == 0 or kind < .25:
                return rng.choice([1,'x','y'*3000,None,.5]) if pool == [] or rng.random() < .6 else rng.choice(pool)
            if kind < .5:
                # Small scalar rows take the store's flat JSON path.
                value = dict(status='s',repairState=None) if rng.random() < .5 else [1.,2.]
            elif kind < .75:
                value = {f'k{i}':build(pool,depth-1) for i in range(rng.randint(1,4))}
            else:
                value = [build(pool,depth-1) for _ in range(rng.randint(1,4))]
                value = tuple(value) if rng.random() < .2 else value
            pool.append(value)
            return value

        found = 0
        with CheckpointStore(self.path) as store:
            for _ in range(300):
                pool = []
                report = dict(root=build(pool,5),again=build(pool,4))
                targets = [rng.choice(pool) for _ in range(3)] if pool else []
                _, _, paths = store.archive(report,targets)
                self.assertEqual(paths,[walk(report,t) for t in targets])
                found += sum(len(p) > 1 for p in paths)
        self.assertGreater(found,20)


class SearchCheckpointTests(StoreCase):
    def test_nested_internal_node_repair_patches_program_cache_and_calls(self):
        program=dict(repairState=None)
        entry=dict(status='suspended',repairState=None)
        frame=dict(inFlight=None,generation=dict(nodeInFlight=program,nodes={'node':entry}))
        dfs=dict(stack=[frame],windows=[],cumulativeCalls=0)
        outer=dict(kind='event_route_search',dfs=dfs,best=None)
        with CheckpointStore(self.path) as store:
            writer=SearchCheckpoint(store,{})
            writer.begin_repair(outer,program,(entry,))
            for i in range(2):
                repair=dict(pending=[i],attemptLog=[i])
                program['repairState']=repair;entry['repairState']=repair
                dfs['cumulativeCalls']+=1
                with patch.object(SearchCheckpoint,'__call__',side_effect=AssertionError('full checkpoint')):
                    writer.repair_progress(outer)
                self.assertEqual(read_report(self.path)['searchState'],outer)
            result=dict(searchState=outer,windows=[],best=None)
            writer.finish(result,[])
            self.assertEqual(read_report(self.path)['result'],result)

    def state(self):
        program = dict(repairState=dict(pending=[1],seen=[],attemptLog=[],contactLog=[]))
        frame = dict(id=3,inFlight=dict(program=program),records=[dict(program=program)])
        # Refinement frames and their programs can have multiple aliases.
        return dict(stack=[frame],frames=[frame],windows=[dict(candidates=[dict(id='past')])],
                    cumulativeCalls=1,transitions=[dict(kind='dispatch')])

    def test_repair_patch_is_full_state_equivalent_including_aliases(self):
        state = self.state()
        metadata = dict(source='test',fixture={})
        with CheckpointStore(self.path) as store:
            writer = SearchCheckpoint(store,metadata)
            writer(state)
            for i in range(3):
                state['stack'][-1]['inFlight']['program']['repairState'] = dict(pending=[2+i],completed=[i],attemptLog=[i])
                state['cumulativeCalls'] += 1
                # A repair publication must not walk/archive the outer tree.
                with patch.object(SearchCheckpoint,'__call__',side_effect=AssertionError('full save')):
                    writer.repair_progress(state)
                self.assertEqual(read_report(self.path)['searchState'],state)
            result = dict(searchState=state,windows=state['windows'],finalAttempts=[],best=None)
            writer.finish(result,[dict(passed=True)])
        restored = read_report(self.path)
        self.assertEqual(restored['result'],result)
        self.assertEqual(restored['windows'],state['windows'])
        self.assertEqual(restored['validation'],[dict(passed=True)])

    def test_completed_repair_is_not_resolved_after_persistent_resume(self):
        helper = repair_tests.RepairHarness()
        first = helper.invoke(repair_tests.route(),lambda *a,**k:repair_tests.outcome(),lambda *a:dict(accepted=True))
        state = self.state()
        with CheckpointStore(self.path) as store:
            writer = SearchCheckpoint(store,{})
            writer(state)
            state['stack'][-1]['inFlight']['program']['repairState'] = first['repairState']
            writer.repair_progress(state)
        restored = read_report(self.path)['searchState']['stack'][-1]['inFlight']['program']['repairState']
        second = helper.invoke(repair_tests.route(),lambda *a,**k:self.fail('Completed request restarted'),lambda *a:dict(accepted=True),
                               resume_state=restored)
        self.assertEqual(second['status'],'feasible')

        self.assertEqual(second['attempts'],[])

    def test_interrupted_repair_keeps_pending_branch_and_audit(self):
        helper = repair_tests.RepairHarness()
        state = self.state()
        with CheckpointStore(self.path) as store:
            writer = SearchCheckpoint(store,{})
            writer(state)
            def progress(repair):
                state['stack'][-1]['inFlight']['program']['repairState'] = repair
                writer.repair_progress(state)
            def interrupted(*args,**kwargs):
                raise RuntimeError('interrupted worker')
            with self.assertRaisesRegex(RuntimeError,'interrupted worker'):
                helper.invoke(repair_tests.route(),interrupted,lambda *a:dict(accepted=True),on_progress=progress)
        restored = read_report(self.path)['searchState']
        self.assertEqual(restored,state)
        repair = restored['stack'][-1]['inFlight']['program']['repairState']
        self.assertIsNotNone(repair['inFlight'])
        second = helper.invoke(repair_tests.route(),lambda *a,**k:repair_tests.outcome(),lambda *a:dict(accepted=True),resume_state=repair)
        self.assertEqual(second['status'],'feasible')


@unittest.skipUnless(os.environ.get('PKC_IPOPT_PYTHON'),
                     'Set PKC_IPOPT_PYTHON for native persistence checks')
class NativeCheckpointTests(StoreCase):
    def test_nested_anchor_and_internal_node_repair_publications_match_live_state(self):
        from parkour_solver.bridge.python_compiler import PythonCompiler
        from parkour_solver.search.engine import find_checkpoint_route
        from parkour_solver.search.domain import InitialJumpDomain
        from tests.support.checkpoint_guidance import flat_scene
        scene=flat_scene();scene['movementSearch'].update(maxInputs=28,maxPrefixes=64)
        checked=[];owners=[];mirrored=[];testcase=self
        core=native_core.from_environment()
        mirror=self.path.with_name('native.json.gz')

        def root(path):
            return json.loads(gzip.decompress(path.read_bytes()))['root']

        class CheckedWriter(SearchCheckpoint):
            """Saves the published states in Python, which reads them back live; under the native core each snapshot
            also goes to a native saver on a second store, which must publish the same roots."""
            saver=outer=None
            progressing=False
            def native_saver(self,outer):
                return None  # the snapshots come to the hooks below
            def mirror(self,state,call):
                dfs=state['dfs']
                if core is None or not hasattr(dfs,'repair'):
                    return
                if self.saver is None or dict(state,dfs=None)!=self.outer:  # each search's own event state
                    self.outer=dict(state,dfs=None)
                    self.saver=core.SearchSaver(mirrored_store,{},self.outer,list(self.saver.windows) if self.saver else [])
                getattr(self.saver,call)(dfs)
                testcase.assertEqual(root(mirror),root(testcase.path))
                mirrored.append(call)
            def __call__(self,state,program=None,extra_targets=()):
                super().__call__(state,program,extra_targets)
                if not self.progressing:  # a progress that saves in full is mirrored as progress
                    self.mirror(state,'save')
            def begin_repair(self,state,program,extra_targets=()):
                super().begin_repair(state,program,extra_targets)
                dfs=state['dfs']  # a native snapshot names its repair targets itself
                owners.append(bool(dfs.repair['targets'] if hasattr(dfs,'repair') else extra_targets))
            def repair_progress(self,state):
                self.progressing=True
                super().repair_progress(state)
                self.progressing=False
                self.mirror(state,'progress')
                dfs=state['dfs']  # a native search publishes snapshots, converted on request
                live=dict(state,dfs=dfs.to_python()) if hasattr(dfs,'to_python') else state
                testcase.assertEqual(read_report(testcase.path)['searchState'],json.loads(json.dumps(live)))
                checked.append(True)
        mirrored_store=core.CheckpointStore(str(mirror)) if core is not None else None
        with PythonCompiler() as compiler, CheckpointStore(self.path) as store:
            writer=CheckedWriter(store,{})
            result=find_checkpoint_route(scene,compiler,anchor_search={},seconds=8,
                max_solver_calls=20,program_filter=InitialJumpDomain([0],max_inputs=28),
                python_executable=os.environ['PKC_IPOPT_PYTHON'],on_progress=writer)
            writer.finish(result,[])
            self.assertEqual(read_report(self.path)['result'],json.loads(json.dumps(result)))
        self.assertTrue(checked)
        self.assertIn(True,owners,'No internal-node checkpoint was exercised')
        if core is not None:
            self.assertIn('progress',mirrored)

if __name__ == '__main__':
    unittest.main()
