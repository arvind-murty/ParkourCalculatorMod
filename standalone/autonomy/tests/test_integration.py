import copy
import json
import os
from pathlib import Path
import unittest

from standalone.autonomy import find_route, prepare_route, schedule
from standalone.autonomy.compiler import JavaCompiler
from standalone.autonomy.route import supports


CONFIG = os.environ.get('PKC_RUN_TICKS_COMPILER')
RUNTIME = os.environ.get('PKC_IPOPT_PYTHON')
FIXTURES = Path(__file__).parents[1] / 'fixtures'


@unittest.skipUnless(CONFIG and RUNTIME, 'Set PKC_RUN_TICKS_COMPILER and PKC_IPOPT_PYTHON')
class IntegrationTests(unittest.TestCase):
    def test_java_compiles_ground_jump_and_air_with_the_existing_model(self):
        r = prepare_route(json.loads((FIXTURES / 'one-required-run-tick.json').read_text()))
        plan = schedule(r, [1])
        with JavaCompiler(json.loads(Path(CONFIG).read_text())) as c:
            coefficients = c.compile(r, plan, 3)
        self.assertEqual(coefficients['friction'][0], coefficients['friction'][1])
        self.assertLess(coefficients['friction'][0], coefficients['friction'][2])
        self.assertGreater(coefficients['magnitude'][1], coefficients['magnitude'][0])
        self.assertGreater(coefficients['magnitude'][0], coefficients['magnitude'][2])

    def test_java_reuses_the_mod_footprint_deriver(self):
        import math
        r = prepare_route(json.loads((FIXTURES / 'one-required-run-tick.json').read_text()))
        with JavaCompiler(json.loads(Path(CONFIG).read_text())) as c:
            footprints = c.derive_footprints(r, 3)
            r['blocks'][1] = {'centerBounds': [4, 4.5, 0, 1], 'top': 1}
            supplied = c.derive_footprints(r, 3)
        lo, hi, zlo, zhi = footprints[0]
        self.assertTrue(supports(r['blocks'][0], lo, .5))
        self.assertFalse(supports(r['blocks'][0], math.nextafter(lo, -math.inf), .5))
        self.assertTrue(supports(r['blocks'][0], hi, .5))
        self.assertFalse(supports(r['blocks'][0], math.nextafter(hi, math.inf), .5))
        self.assertEqual(supplied[1], [4, 4.5, 0, 1])

    def test_known_routes_zero_one_and_backtracking(self):
        for name, expected in [('two-jumps.json', [0, 0]), ('one-required-run-tick.json', [1]),
                               ('backtrack.json', [2, 0]), ('turning-two-jumps.json', [0, 0])]:
            with self.subTest(name=name):
                request = json.loads((FIXTURES / name).read_text())
                original = copy.deepcopy(request)
                with JavaCompiler(json.loads(Path(CONFIG).read_text())) as c:
                    result = find_route(request, c, seconds=25, candidate_seconds=3,
                                        retries=0, python_executable=RUNTIME)
                self.assertEqual(result['status'], 'feasible', result)
                self.assertEqual(result['best']['allocation'], expected, result)
                self.assertEqual(request, original)
                best = result['best']
                for state, block, kind in best['plan']['supportChecks']:
                    self.assertTrue(supports(request['blocks'][block], *best['candidate']['positions'][state]))
                if name == 'backtrack.json':
                    self.assertGreaterEqual(result['backtracks'], 2)
                if name == 'turning-two-jumps.json':
                    self.assertGreater(max(best['candidate']['yaw']) - min(best['candidate']['yaw']), 30)
                print(name, best['allocation'], round(result['elapsedSeconds'], 3), 's')

    def test_linkcraft_section_keeps_walkoff_and_finds_without_recorded_facings(self):
        request = json.loads((FIXTURES / 'linkcraft11-j5-j9.json').read_text())
        self.assertFalse(request['source']['recordedFacingsUsed'])
        self.assertEqual(request['initial']['yaw'], 0)
        self.assertEqual(request['constraints'], [])
        self.assertFalse(request['source']['authoredScalarRulesUsed'])
        self.assertEqual(request['source']['recordedAllocation'], [0, 0, 0, 0, 0, 1])
        with JavaCompiler(json.loads(Path(CONFIG).read_text())) as compiler:
            result = find_route(request, compiler, seconds=30, candidate_seconds=3,
                                retries=1, python_executable=RUNTIME)
            self.assertEqual(result['status'], 'feasible', result)
            best = result['best']
            self.assertEqual(best['allocation'], [0, 0, 0, 0, 0, 0])
            self.assertEqual(best['plan']['ticks'], 24)
            event = best['plan']['events'][1]
            self.assertEqual(event['kind'], 'walk_off')
            departure = event['jump']
            self.assertEqual(best['plan']['grounded'][departure:departure + 4], [True, False, False, False])
            self.assertFalse(any(best['plan']['jump'][departure:departure + 4]))
            self.assertFalse(supports(request['blocks'][1], *best['candidate']['positions'][departure]))
            coefficients = compiler.compile(prepare_route(request), best['plan'], 3)
            self.assertLess(coefficients['magnitude'][departure], .14)
            self.assertGreater(coefficients['magnitude'][0], .32)
        self.assertEqual(result['validationLevel'], 'continuous_horizontal_declared_regions_with_supplied_airtimes')
        self.assertEqual(result['gameReplay'], 'not_run')
        print('Linkcraft j5-j9', best['allocation'], round(result['elapsedSeconds'], 3), 's')


if __name__ == '__main__':
    unittest.main()
