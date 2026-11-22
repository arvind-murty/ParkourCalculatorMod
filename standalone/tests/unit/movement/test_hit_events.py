from tests.support.paths import AUTONOMY_DATA
import copy
import json
import os
import unittest

from parkour_solver.movement.profiles import ordinary_jump_profile
from parkour_solver.bridge.python_compiler import PythonCompiler
from parkour_solver.constraints.hit_events import wall_hit_options
from parkour_solver.scene.route import prepare_route, schedule


FIXTURE = AUTONOMY_DATA / 'fixtures/wall-reversal.json'


class HitEventTests(unittest.TestCase):
    def test_relative_hit_and_all_avoidance_rows_follow_inserted_runs(self):
        hit = {'leg': 0, 'offset': 2, 'axis': 'X', 'direction': 1,
               'plane': 1.699999988079071, 'box': [2, 0, -1, 3, 5, 2], 'solidIndex': 2,
               'avoidOffsets': list(range(13))}
        route = prepare_route({'version': 1, 'initial': {'position': [.5, 1, .5], 'velocity': [0, 0, 0]},
                               'blocks': [{'bounds': [0, 1, 0, 1], 'top': 1},
                                          {'bounds': [0, 1, 2, 3], 'top': 1}],
                               'airtimes': [12], 'sharedMovement': True, 'hitEvents': [hit]})
        first, later = schedule(route, [0]), schedule(route, [3])
        self.assertEqual(first['contacts'][0]['state'], 3)
        self.assertEqual(later['contacts'][0]['state'], 6)
        self.assertEqual({r['t1'] for r in later['walls'] if r['name'].endswith(':avoid')}, set(range(16)))
        self.assertEqual(next(r['t1'] for r in later['walls'] if r['name'].endswith(':reverse')), 7)
        self.assertEqual(schedule(route, [3], 'run')['contacts'], [])
        invalid = copy.deepcopy(route)
        invalid['hitEvents'][0]['offset'] = 11
        with self.assertRaises(ValueError):
            prepare_route(invalid)

    def test_generation_preserves_fixed_height_and_reports_candidate_limit(self):
        route = {'blocks': [{'bounds': [0, 1, 0, 1]}, {'bounds': [0, 1, 2, 3]}]}
        profile = ordinary_jump_profile(1, 1)
        geometry = {'solids': [[2, 0, -3, 3, 5, 5]]}
        result = wall_hit_options(route, [profile], geometry, ['wall'],
                                  {'storedSpeedNorm': 1, 'groundMagnitude': .3}, 2)
        self.assertEqual(len(result['options']), 2)
        self.assertGreater(result['candidateDrops'], 0)
        self.assertEqual(profile['duration'], 12)
        self.assertTrue(all(h['blockId'] == 'wall' for h in result['options']))

