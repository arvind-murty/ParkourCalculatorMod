from tests.support.paths import AUTONOMY_DATA
import copy
import json
import os
import unittest

from parkour_solver.api import find_route, prepare_route, schedule
from parkour_solver.scene.normalize import prepare_scene
from parkour_solver.bridge.python_compiler import PythonCompiler
from parkour_solver.scene.route import initial_guess, supports
from parkour_solver.validation.vertical import validate_vertical_schedule
from parkour_solver.validation.scene import check_geometry
from parkour_solver.numerics import prepare, validate


RUNTIME = os.environ.get('PKC_IPOPT_PYTHON')
FIXTURES = AUTONOMY_DATA / 'fixtures'


def anchor_route(name, compiler, **changes):
    """The anchor search's result on an obstacle fixture, with intentional wall hits and ceiling hits off as the old
    obstacle tests ran them."""
    from parkour_solver.search.engine import find_checkpoint_route
    request = json.loads((FIXTURES / name).read_text())
    request.update(ceilingHits=False, maxWallHits=0)
    for change in changes.values():
        change(request)
    return find_checkpoint_route(request, compiler, anchor_search={}, seconds=60, python_executable=RUNTIME)

@unittest.skipUnless(RUNTIME, 'Set PKC_IPOPT_PYTHON')
class IntegrationTests(unittest.TestCase):
    def test_shared_scene_bridge_detects_swept_walls_and_releases_below_the_bottom(self):
        geometry = {'version': 1, 'profile': 'minecraft-1.8.9', 'bounds': [-5, -5, -5, 10, 10, 10],
                    'solids': [[1, 0, -3, 2, 5, 3]], 'stepHeight': .6}
        with PythonCompiler() as compiler:
            blocked = compiler.check_scene(geometry, [[.5, 1, .5], [3.5, 1, .5]],
                                           [[3, 0, 0]], [False, False], 2)
            clear = compiler.check_scene(geometry, [[.5, -2, .5], [3.5, -2, .5]],
                                         [[3, 0, 0]], [False, False], 2)
        self.assertFalse(blocked['feasible'])
        self.assertEqual(blocked['clips'][0]['axis'], 0)
        self.assertEqual(blocked['clips'][0]['box'], geometry['solids'][0])
        self.assertTrue(clear['feasible'], clear)

    def test_java_compiles_ground_jump_and_air_with_the_existing_model(self):
        r = prepare_route(json.loads((FIXTURES / 'one-required-run-tick.json').read_text()))
        plan = schedule(r, [1])
        with PythonCompiler() as c:
            coefficients = c.compile(r, plan, 3)
        self.assertEqual(coefficients['friction'][0], coefficients['friction'][1])
        self.assertLess(coefficients['friction'][0], coefficients['friction'][2])
        self.assertGreater(coefficients['magnitude'][1], coefficients['magnitude'][0])
        self.assertGreater(coefficients['magnitude'][0], coefficients['magnitude'][2])

    def test_java_reuses_the_mod_footprint_deriver(self):
        import math
        r = prepare_route(json.loads((FIXTURES / 'one-required-run-tick.json').read_text()))
        with PythonCompiler() as c:
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
                with PythonCompiler() as c:
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

    def test_linkcraft_section_keeps_runoff_and_finds_without_recorded_facings(self):
        request = json.loads((FIXTURES / 'linkcraft11-j5-j9.json').read_text())
        self.assertFalse(request['source']['recordedFacingsUsed'])
        self.assertEqual(request['initial']['yaw'], 0)
        self.assertEqual(request['constraints'], [])
        self.assertFalse(request['source']['authoredScalarRulesUsed'])
        self.assertEqual(request['source']['recordedAllocation'], [0, 0, 0, 0, 0, 1])
        with PythonCompiler() as compiler:
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


    def test_wall_and_ceiling_the_player_cannot_pass_between_are_one_obstacle(self):
        # The wall and the overhanging ceiling are 0.14 apart: one cluster the route passes as a whole.
        with PythonCompiler() as compiler:
            result = anchor_route('obstacle-interleaved.json', compiler)
        self.assertEqual(result['status'], 'feasible', result['termination'])
        self.assertEqual(result['best']['plan']['ticks'], 12)
        self.assertTrue(result['best']['geometryCheck']['feasible'])

    def test_full_obstacle_geometry_finds_cross_and_wide_neos_and_checks_both_models(self):
        def wide_goal(request):
            # The historical full-width goal also permits a cross landing at its edge. Require a real return from the side.
            target = next(b for b in request['blocks'] if b['id'] == request['goalBlock'])
            target['boxes'][0][2], target['boxes'][0][5] = .4, .6
            request['initial']['velocity'][0] = .1
        with PythonCompiler() as compiler:
            for fixture, changes in [('obstacle-cross-neo.json', {}), ('obstacle-wide-neo.json', dict(goal=wide_goal))]:
                with self.subTest(fixture=fixture):
                    result = anchor_route(fixture, compiler, **changes)
                    self.assertEqual(result['status'], 'feasible', result['termination'])
                    best = result['best']
                    self.assertEqual(best['plan']['ticks'], 12)
                    self.assertTrue(best['geometryCheck']['feasible'])
                    scene = prepare_scene(result['effectiveRequest'])
                    coefficients = compiler.compile(best['route'], best['plan'], 2)
                    problem = prepare(dict(coefficients, version=1, ticks=best['plan']['ticks'],
                                           position=scene['initial']['position'][::2],
                                           velocity=scene['initial']['velocity'][::2], yaw=best['candidate']['yaw'],
                                           walls=best['plan']['walls'], vertical=best['vertical'],
                                           objective={'axis': 'X', 'sense': 'MAX', 'tick': best['plan']['ticks']}))
                    self.assertTrue(validate(problem, best['candidate']['yaw'])['feasible'])
                    vertical = validate_vertical_schedule(best['route'], best['profiles'], best['plan'])
                    self.assertTrue(check_geometry(scene['collisionScene'], best['plan'], best['candidate'], vertical,
                                                   compiler, 3)['feasible'])
                    self.assertFalse(any(w['mode'] == 'F' for w in problem['walls']))

    def test_ceiling_can_be_passed_under_and_a_sealed_wall_is_never_accepted(self):
        with PythonCompiler() as compiler:
            under = anchor_route('obstacle-under-ceiling.json', compiler)
            blocked = anchor_route('obstacle-blocked.json', compiler)
        self.assertEqual(under['status'], 'feasible', under['termination'])
        self.assertTrue(under['best']['geometryCheck']['feasible'])
        self.assertEqual(under['best']['plan']['ticks'], 12)
        self.assertEqual(blocked['status'], 'unknown')
        self.assertIsNone(blocked['best'])


if __name__ == '__main__':
    unittest.main()
