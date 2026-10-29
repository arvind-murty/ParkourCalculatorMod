import copy
import itertools
import json
import math
import os
from pathlib import Path
import unittest

from standalone.autonomy.contact_combinations import contact_combinations, merge_contacts
from standalone.autonomy.contact_profiles import contact_profile, generate_contact_profiles
from standalone.autonomy.compiler import JavaCompiler
from standalone.autonomy.ladder_schedules import trace_ladder_schedule
from standalone.autonomy.route import HALF_WIDTH, prepare_route, schedule
from standalone.autonomy.obstacle_geometry import BODY_HEIGHT
from standalone.ipopt import prepare, validate, solve
from standalone.ipopt.ladder import CLIMB_CARRY, REST_CARRY, f32


def event(axis, tick=0, direction=1, box=None, role='contact'):
    box = box or ([1, -2, -2, 2, 8, 2] if axis == 'X' else [-2, -2, 1, 2, 8, 2]
                  if axis == 'Z' else [-2, 3, -2, 2, 4, 2])
    a = 'XYZ'.index(axis)
    width = BODY_HEIGHT if axis == 'Y' else HALF_WIDTH
    plane = box[a] - width if direction > 0 else box[a + 3] + (0 if axis == 'Y' else width)
    return dict(leg=0, offset=tick, axis=axis, direction=direction, plane=plane, box=box, role=role)


class CombinationTests(unittest.TestCase):
    def test_every_same_tick_axis_subset_and_two_tick_pair_is_generated(self):
        pool = [event(a, t) for t in (0, 1) for a in 'XYZ']
        found = list(contact_combinations(pool, max_events=6, limit=10000))
        actual = {tuple((e['offset'], e['axis']) for e in c) for c in found}
        expected = {tuple((e['offset'], e['axis']) for e in c)
                    for size in range(1, 7) for c in itertools.combinations(pool, size)}
        self.assertEqual(actual, expected)
        self.assertEqual(len(actual), 63)

    def test_free_gap_is_independent_but_full_ladder_sequence_is_combined(self):
        pool = [event('X', 0), event('Y', 3), event('Z', 8)]
        self.assertEqual(len(list(contact_combinations(pool, limit=100))), 3)
        combined = list(contact_combinations(pool, limit=100, ladder_windows=[{'leg': 0, 'start': 0, 'end': 8}]))
        self.assertEqual(len(combined), 7)

    def test_different_blocks_and_normal_signs_do_not_filter_combination_types(self):
        for xs, zs in itertools.product((-1, 1), repeat=2):
            x, z = event('X', direction=xs), event('Z', direction=zs)
            x['blockId'], z['blockId'] = 'a', 'b'
            self.assertEqual(len(list(contact_combinations([x, z], limit=100))), 3)

    def test_conflicting_plane_rejected_and_compatible_duplicate_merged(self):
        c = {'state': 1, 'axis': 0, 'plane': .7, 'direction': 1}
        self.assertEqual(merge_contacts([c, c]), [c])
        for changed in (dict(c, plane=.8), dict(c, direction=-1)):
            with self.assertRaises(ValueError):
                merge_contacts([c, changed])
        self.assertEqual(len(list(contact_combinations([event('X'), event('X', direction=-1)], limit=100))), 2)

    def test_events_and_before_after_avoidance_follow_run_insertion(self):
        contacts = [event('X'), event('Z', 1), event('Y', 1)]
        for e in contacts:
            e['avoidBefore'] = [{'mode': 'Z', 't1': 0, 'cmp': 'GE', 'rhs': -.5}]
            e['avoidAfter'] = [{'mode': 'X', 't1': 2, 'cmp': 'LE', 'rhs': 2}]
        r = prepare_route({'version': 1, 'initial': {'position': [0, 1, 0], 'velocity': [0, REST_CARRY, 0]},
            'blocks': [{'bounds': [-1, 1, -1, 1], 'top': 1}, {'bounds': [0, 2, 0, 2], 'top': 1}],
            'airtimes': [4], 'sharedMovement': True, 'contactEvents': contacts})
        a, b = schedule(r, [0]), schedule(r, [3])
        self.assertEqual([c['state'] + 3 for c in a['sceneContacts']], [c['state'] for c in b['sceneContacts']])
        self.assertEqual([c['state'] + 3 for c in a['verticalContacts']], [c['state'] for c in b['verticalContacts']])
        x = [w['t1'] for w in a['walls'] if w['name'].startswith('contact:')]
        y = [w['t1'] for w in b['walls'] if w['name'].startswith('contact:')]
        self.assertEqual([t + 3 for t in x], y)
        self.assertEqual(schedule(r, [3], 'run')['sceneContacts'], [])
        self.assertFalse(any('reverse' in w['name'] for w in a['walls']))

    def test_contact_on_landing_input_and_actual_step_have_explicit_representation(self):
        contacts = [event('X', 3), event('Y', 3, -1, [-2, 0, -2, 2, 1, 2], 'landing')]
        r = prepare_route({'version': 1, 'initial': {'position': [0, 1, 0], 'velocity': [0, REST_CARRY, 0]},
            'blocks': [{'bounds': [-1, 1, -1, 1], 'top': 1}, {'bounds': [0, 2, 0, 2], 'top': 1}],
            'airtimes': [4], 'sharedMovement': True, 'contactEvents': contacts})
        self.assertEqual({c['state'] for c in schedule(r, [0])['sceneContacts']}, {4})
        changed = copy.deepcopy(r)
        changed['contactEvents'][0]['plane'] += 1
        with self.assertRaises(ValueError):
            prepare_route(changed)


class MixedVerticalTests(unittest.TestCase):
    def test_same_tick_ceiling_climb_refresh_and_no_climb_give_different_carry(self):
        initial = {'position': 1.98, 'velocity': CLIMB_CARRY}
        ceiling = [{'state': 1, 'plane': 2, 'direction': 1}]
        active = trace_ladder_schedule(initial, [True] * 4, [False] * 3, [True, False, False], contacts=ceiling)
        stopped = trace_ladder_schedule(initial, [True] * 4, [False] * 3, [False] * 3, contacts=ceiling)
        self.assertEqual(active['position'][1], stopped['position'][1])
        self.assertEqual(active['velocity'][1], CLIMB_CARRY)
        self.assertEqual(stopped['velocity'][1], REST_CARRY)
        self.assertGreater(active['position'][2], stopped['position'][2])

    def test_repeated_ceiling_climb_sequence_and_final_omission(self):
        initial = {'position': 2., 'velocity': CLIMB_CARRY}
        ceilings = [{'state': state, 'plane': 2, 'direction': 1} for state in range(1, 6)]
        active = trace_ladder_schedule(initial, [True] * 7, [False] * 6, [True] * 5 + [False], contacts=ceilings)
        stopped = trace_ladder_schedule(initial, [True] * 7, [False] * 6, [True] * 4 + [False] * 2, contacts=ceilings)
        self.assertEqual(active['position'][:6], [2.] * 6)
        self.assertEqual(active['velocity'][5], CLIMB_CARRY)
        self.assertEqual(stopped['velocity'][5], REST_CARRY)
        self.assertLess(stopped['position'][6], 2.)

    def test_no_ladder_refresh_on_exit_despite_same_horizontal_hit(self):
        for ladder in ([True, False, False], [False, True, True]):
            trace = trace_ladder_schedule({'position': 2, 'velocity': CLIMB_CARRY}, ladder, [False] * 2,
                [True, False], contacts=[{'state': 1, 'plane': 2.05, 'direction': 1}])
            self.assertEqual(trace['velocity'][1], CLIMB_CARRY if ladder[1] else REST_CARRY)

    def test_profile_regenerates_airtime_for_wall_and_ceiling_combinations(self):
        h = event('X', box=[1, 0, -4, 2, 6, 4])
        c = event('Y', box=[-2, 3, -2, 3, 4, 3])
        profile = contact_profile(1, 1, [h, c], max_inputs=30)
        self.assertEqual(profile['airtime'], 3)
        self.assertEqual(profile['velocity'][1], REST_CARRY)
        self.assertEqual({e['axis'] for e in profile['contactEvents']}, {'X', 'Y'})
        generated = generate_contact_profiles(1, 1, [h, c, event('Z', 1)], max_inputs=30, limit=100)
        self.assertTrue(any({e['axis'] for e in p['contactEvents']} == {'X', 'Y', 'Z'} for p in generated['profiles']))


@unittest.skipUnless(os.environ.get('PKC_RUN_TICKS_COMPILER'), 'Set PKC_RUN_TICKS_COMPILER')
class CombinationSceneTests(unittest.TestCase):
    def setUp(self):
        self.compiler = JavaCompiler(json.loads(Path(os.environ['PKC_RUN_TICKS_COMPILER']).read_text()))
        self.addCleanup(self.compiler.close)

    def test_all_same_tick_axis_combinations_and_signs_against_shared_scene(self):
        for vertical, xhit, zhit, xs, zs in itertools.product(('none', 'ceiling', 'floor'), (False, True),
                                                           (False, True), (-1, 1), (-1, 1)):
            with self.subTest(vertical=vertical, x=xhit, z=zhit, xs=xs, zs=zs):
                p = [.45 * xs, 2., .45 * zs]
                displacement = [.2 * xs, .12 if vertical != 'floor' else -.12, .2 * zs]
                next_p = [p[a] + displacement[a] for a in range(3)]
                solids, contacts = [], []
                for axis, hit, sign in [('X', xhit, xs), ('Z', zhit, zs)]:
                    if hit:
                        box = [.85, -2, -3, 1.85, 8, 3] if axis == 'X' else [-3, -2, .85, 3, 8, 1.85]
                        a = 'XYZ'.index(axis)
                        if sign < 0:
                            box[a], box[a + 3] = -box[a + 3], -box[a]
                        e = event(axis, direction=sign, box=box)
                        next_p[a] = e['plane']
                        solids.append(box)
                        contacts.append({'state': 1, 'axis': a, 'direction': sign, 'plane': e['plane'], 'box': box})
                if vertical != 'none':
                    box = [-3, 2.05 + BODY_HEIGHT, -3, 3, 6, 3] if vertical == 'ceiling' else [-3, 0, -3, 3, 1.95, 3]
                    e = event('Y', direction=1 if vertical == 'ceiling' else -1, box=box)
                    next_p[1] = e['plane']
                    solids.append(box)
                    contacts.append({'state': 1, 'axis': 1, 'direction': e['direction'], 'plane': e['plane'], 'box': box})
                scene = {'version': 1, 'profile': 'minecraft-1.8.9', 'bounds': [-8, -5, -8, 8, 12, 8], 'solids': solids, 'stepHeight': 0}
                checked = self.compiler.check_scene(scene, [p, next_p], [displacement], [False, vertical == 'floor'], 5, contacts=contacts)
                self.assertTrue(checked['feasible'], checked)
                for index in range(len(contacts)):
                    missing = contacts[:index] + contacts[index + 1:]
                    if contacts[index]['axis'] != 1 or vertical == 'ceiling':
                        self.assertFalse(self.compiler.check_scene(scene, [p, next_p], [displacement],
                            [False, vertical == 'floor'], 5, contacts=missing)['feasible'])

    def test_selected_step_is_checked_as_step_not_a_fictitious_downward_clip(self):
        floor, stair = [-2, 0, -2, 1, 1, 2], [1, 0, -2, 3, 1.5, 2]
        scene = {'version': 1, 'profile': 'minecraft-1.8.9', 'bounds': [-8, -5, -8, 8, 12, 8], 'solids': [floor, stair]}
        selected = {'state': 1, 'axis': 1, 'direction': -1, 'plane': 1.5, 'box': stair, 'step': True}
        checked = self.compiler.check_scene(scene, [[.5, 1, 0], [1., 1.5, 0]], [[.5, REST_CARRY, 0]], [True, True], 5, contacts=[selected])
        self.assertTrue(checked['feasible'], checked)
        selected['plane'] = 1.4
        self.assertFalse(self.compiler.check_scene(scene, [[.5, 1, 0], [1., 1.5, 0]], [[.5, REST_CARRY, 0]],
                                                  [True, True], 5, contacts=[selected])['feasible'])
