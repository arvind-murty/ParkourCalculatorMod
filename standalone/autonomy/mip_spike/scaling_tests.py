import importlib.util
import hashlib
import itertools
import json
import os
from pathlib import Path
import time
import unittest

from standalone.autonomy.compiler import JavaCompiler
from .joint_contract import compile_allocation, prepare_joint
from .joint_model import solve_horizon
from .joint_run import accept_joint
from .run import acceptance
from standalone.ipopt import prepare, validate
from .joint_tests import open_request
from .scaling import SceneObserver
from .scaling_cases import CASE_NAMES, allocation_count, scaling_request


class DomainCountTests(unittest.TestCase):
    def test_count_matches_small_exhaustive_domains(self):
        for bounds in ([[0, 2]] * 3, [[1, 3], [0, 1], [2, 4]]):
            for maximum in range(9):
                expected = sum(sum(v) <= maximum for v in itertools.product(*(range(a, b + 1) for a, b in bounds)))
                self.assertEqual(allocation_count(bounds, maximum), expected)


@unittest.skipUnless(importlib.util.find_spec('pyscipopt') and os.environ.get('PKC_RUN_TICKS_COMPILER'),
                     'Set the Java compiler config and optional SCIP dependency')
class ScalingInstrumentationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = JavaCompiler(json.loads(Path(os.environ['PKC_RUN_TICKS_COMPILER']).read_text()))
        cls.addClassCleanup(cls.compiler.close)

    def test_every_campaign_case_has_only_supported_movement_and_no_saved_schedule(self):
        for name in CASE_NAMES:
            with self.subTest(case=name):
                request, provenance = scaling_request(name)
                case = prepare_joint(request, self.compiler)
                self.assertIn(len(case['profiles']), (3, 5))
                self.assertGreater(allocation_count(case['route']['runBounds'], case['route']['maxTotalRunTicks']), 1)
                self.assertEqual(case['positionRows'], [])
                self.assertTrue(all(p['jump'] == [True] + [False] * (p['airtime'] - 1) for p in case['profiles']))
                self.assertFalse(provenance['recordedFacingsUsed'])
                self.assertFalse(provenance['suppliedAllocation'])

    def test_captured_scip_incumbent_is_reconstructible_and_scene_valid(self):
        case = prepare_joint(open_request(), self.compiler)
        compiled = compile_allocation(case, [0], self.compiler)
        master = solve_horizon(case, 12, seconds=3, optimize=True, capture_incumbents=True)
        self.assertTrue(master['incumbents'], master)
        first = master['incumbents'][0]
        self.assertGreaterEqual(first['availableSeconds'], master['buildSeconds'])
        self.assertLessEqual(first['availableSeconds'], master['elapsedSeconds'])
        accepted = accept_joint(case, first['candidate'], self.compiler, seconds=2)
        self.assertTrue(accepted['rawAccepted'])
        self.assertTrue(accepted['accepted'])
        self.assertEqual(accepted['problem']['position'], compiled['problem']['position'])

    def test_first_solution_observer_excludes_prefixes_and_requires_full_contract(self):
        class Compiler:
            def check_scene(self, *args, **kwargs):
                return dict(feasible=True)
        observer = SceneObserver(Compiler(), dict(minimumHorizon=3, route=dict(objective=dict(axis='X'))), time.monotonic())
        observer.check_scene({}, [[0, 1, 0]] * 2, [[0, 0, 0]], [True] * 2, 1)
        self.assertEqual(observer.curve, [])
        with self.assertRaisesRegex(ValueError, 'full velocity'):
            observer.check_scene({}, [[0, 1, 0]] * 4, [[0, 0, 0]] * 3, [True] * 4, 1)
        observer.check_scene({}, [[0, 1, 0]] * 4, [[0, 0, 0]] * 3, [True] * 4, 1, velocities=[[0, 0, 0]] * 4)
        self.assertEqual(len(observer.curve), 1)

    def test_all_saved_campaign_witnesses_and_first_scenes_revalidate(self):
        directory = Path(__file__).parents[1] / 'experiments/joint-scaling'
        manifest = json.loads((directory / 'manifest.json').read_text())
        measurements = json.loads((directory / 'measurements.json').read_text())['results']
        self.assertEqual(len(measurements), 24)
        self.assertEqual(len({(r['case'], r['repeat'], r['method']) for r in measurements}), 24)
        self.assertEqual(len(manifest['witnesses']), 18)
        self.assertEqual(len(manifest['supplementalWitnesses']), 3)
        self.assertEqual(len(json.loads((directory / 'supplemental/measurements.json').read_text())['results']), 4)
        for record in manifest['witnesses'] + manifest['supplementalWitnesses']:
            with self.subTest(witness=record['file']):
                self.assertEqual(hashlib.sha256((directory / record['file']).read_bytes()).hexdigest(), record['sha256'])
                self.assertEqual(hashlib.sha256((directory / record['case']).read_bytes()).hexdigest(), record['caseSha256'])
                evidence = json.loads((directory / record['file']).read_text())
                case = json.loads((directory / record['case']).read_text())
                problem = prepare(evidence['problem'])
                candidate = validate(problem, evidence['candidate']['yaw'])
                self.assertTrue(candidate['feasible'])
                for key in ('positions', 'velocity', 'y', 'vy', 'onGround'):
                    self.assertEqual(candidate[key], evidence['candidate'][key])
                self.assertEqual(candidate['positions'][0], case['route']['initial']['position'][::2])
                self.assertEqual(candidate['velocity'][0], case['route']['initial']['velocity'][::2])
                vertical = dict(y=candidate['y'], vy=candidate['vy'], onGround=candidate['onGround'], vertical=problem['vertical'])
                self.assertTrue(acceptance(dict(scene=case['scene'], plan=evidence['plan'], problem=problem, vertical=vertical),
                                           candidate, self.compiler, 3)['feasible'])
        for record in measurements:
            if record.get('firstSceneFile'):
                source = directory / record['firstSceneFile']
                self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), record['firstSceneSha256'])
                first = json.loads(source.read_text())
                self.assertTrue(self.compiler.check_scene(first['scene'], first['positions'], first['movement'],
                    first['ground'], 3, **first['contract'])['feasible'])


if __name__ == '__main__':
    unittest.main()
