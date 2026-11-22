from tests.support.paths import AUTONOMY_DATA, RESEARCH_ROOT
import copy
import json
from pathlib import Path
import unittest

from parkour_solver.scene.normalize import prepare_scene
from parkour_solver.constraints.avoidance import blockage_counts, blockage_usage, compile_passages, gate_timeline, outside, passage_y_valid
from tests.support.obstacle_faces import faces
from parkour_solver.scene.geometry import BODY_HEIGHT, expanded_box
from parkour_solver.scene.route import HALF_WIDTH, prepare_route, schedule
from parkour_solver.search.ranking import passage_seed
from tests.support.block_coefficients import Coefficients
from parkour_solver.numerics import prepare


FIXTURES = AUTONOMY_DATA / 'fixtures'


def scene():
    return json.loads((FIXTURES / 'obstacle-wide-neo.json').read_text())


class ObstacleGeometryTests(unittest.TestCase):
    def test_collision_boxes_derive_supports_and_survive_normalization_without_mutation(self):
        request = scene()
        original = copy.deepcopy(request)
        result = prepare_scene(request)
        self.assertEqual(request, original)
        self.assertEqual(result, prepare_scene(result))
        self.assertEqual(result['collisionScene']['solids'][1], request['blocks'][1]['boxes'][0])
        self.assertEqual(result['blocks'][1]['top'], 4)
        self.assertEqual(result['blocks'][1]['solidIndex'], 1)

    def test_multipart_supports_remain_separate_with_their_actual_gap(self):
        request = scene()
        request['blocks'][1]['boxes'].append([5, 0, .25, 6, 4, .75])
        result = prepare_scene(request)
        parts = [b for b in result['blocks'] if b['sourceBlock'] == 'wall']
        self.assertEqual([b['id'] for b in parts], ['wall#0', 'wall#1'])
        self.assertLess(parts[0]['bounds'][1], parts[1]['bounds'][0])
        self.assertEqual(len(result['collisionScene']['solids']), 4)

    def test_geometry_requires_vertical_bounds_coverage_and_consistent_supports(self):
        for change in ('missing_bottom', 'outside', 'bad_support', 'unsupported_medium', 'incoming_hit'):
            with self.subTest(change=change):
                request = scene()
                if change == 'missing_bottom':
                    request['blocks'][1]['boxes'] = [[1, 2, 3, 4]]
                elif change == 'outside':
                    request['geometryBounds'][3] = 1
                elif change == 'incoming_hit':
                    request['initial']['previousHorizontalCollision'] = True
                else:
                    request = prepare_scene(request)
                    if change == 'bad_support':
                        request['blocks'][1]['top'] = 1
                    else:
                        request['collisionScene']['fluids'] = [[0, 0, 0]]
                with self.assertRaises(ValueError):
                    prepare_scene(request)

    def test_body_clearance_uses_the_head_and_the_actual_bottom(self):
        box = expanded_box([1, 0, 1, 2, 4, 2])
        self.assertEqual(box, [1 - HALF_WIDTH, -BODY_HEIGHT, 1 - HALF_WIDTH,
                               2 + HALF_WIDTH, 4, 2 + HALF_WIDTH])
        under = {'axis': 'Y', 'direction': -1, 'plane': box[1]}
        self.assertFalse(outside(under, [0, -1, 0]))
        self.assertTrue(outside(under, [0, -2, 0]))


class BlockageTests(unittest.TestCase):


    def test_all_six_orders_match_the_minecraft_corner_probe(self):
        evidence = json.loads((RESEARCH_ROOT / 'blockage-order/results.json').read_text())
        edge_faces = faces([0, 0, 0, 1, 1, 1])
        for case in evidence['cases']:
            with self.subTest(case=case):
                first, second = case['type'].split('->')
                chain = [next(f for f in edge_faces if f['axis'] == axis and f['direction'] == sign)
                         for axis, sign in [(first, case['firstSign']), (second, case['secondSign'])]]
                passage = {'leg': 0, 'solidIndex': 0, 'faces': chain, 'gates': [0]}
                event = {'runTicks': 0, 'jump': 0, 'landing': 1}
                rows, failures = compile_passages({'passages': [passage]}, [event])
                positions = [case['start'], case['end']]
                horizontal_ok = all((positions[r['t1']][0 if r['mode'] == 'X' else 2] >= r['rhs']
                                     if r['cmp'] == 'GE' else positions[r['t1']][0 if r['mode'] == 'X' else 2] <= r['rhs'])
                                    for r in rows)
                profile = {'duration': 1, 'position': [p[1] for p in positions]}
                self.assertEqual(not failures and horizontal_ok and passage_y_valid(passage, event, profile),
                                 case['directClippedAxis'] == 'none')


if __name__ == '__main__':
    unittest.main()
