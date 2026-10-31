import copy
import importlib.util
import itertools
import json
import math
import os
import random
from pathlib import Path
import unittest

from standalone.autonomy.compiler import JavaCompiler
from standalone.autonomy.blockages import intervals, outside, within_caps
from standalone.ipopt import validate
from standalone.ipopt.ladder import REST_CARRY
from .joint_contract import compile_allocation, prepare_joint
from .joint_model import departure_domains, solve_horizon
from .joint_passages import add_passages, extract_passages
from .joint_run import accept_joint
from .joint_bounds import movement_bounds


def open_request(*, two=False):
    blocks = [dict(id='start', boxes=[[-2, 0, -2, 2, 1, 2]])]
    if two:
        blocks.append(dict(id='middle', boxes=[[2.2, 0, -1, 5, 1, 1]]))
    blocks.append(dict(id='goal', boxes=[[6 if two else 2.2, 0, -1, 10 if two else 5, 1, 1]]))
    scene = dict(version=1, sceneModel='collision_boxes', geometryBounds=[-20, -5, -20, 30, 15, 20],
        initial=dict(position=[0, 1, 0], velocity=[0, REST_CARRY, 0], yaw=0),
        startBlock='start', goalBlock='goal', blocks=blocks, maxRunTicksPerJump=2, maxTotalRunTicks=2)
    return dict(scene=scene, route=['start', 'middle', 'goal'] if two else ['start', 'goal'])


@unittest.skipUnless(importlib.util.find_spec('pyscipopt'), 'Install the optional SCIP dependency')
class PassageModelTests(unittest.TestCase):
    def solve_path(self, path, *, caps=(2, 2, 3), boxes=None, physical=None):
        from pyscipopt import Model
        model = Model('passage_control')
        model.hideOutput()
        model.setRealParam('limits/time', 2)
        boxes = boxes or [[0, 0, 0, 1, 4, 1]]
        physical = physical or ['obstacle:' + str(i) for i in range(len(boxes))]
        case = dict(scene=dict(maxHorizontalBlockagesPerObstacle=caps[0], maxVerticalBlockagesPerObstacle=caps[1],
            maxBlockagesPerObstacle=caps[2], blocks=[dict(solidIndex=i, sourceBlock=physical[i]) for i in range(len(boxes))],
            collisionScene=dict(solids=boxes)),
            route=dict(initial=dict(position=path[0])))
        coordinates = [[model.addVar(f'p_{t}_{a}', lb=v, ub=v) for a, v in enumerate(row)] for t, row in enumerate(path)]
        bounds = [[(v, v) for v in row] for row in path]
        records = add_passages(model, coordinates, bounds, case, len(path) - 1)
        model.optimize()
        return extract_passages(model.getBestSol(), records, len(path) - 1)[0] if model.getNSols() else None

    def test_collision_order_and_swept_segments(self):
        self.assertIsNone(self.solve_path([[-.5, 1, .5], [.5, 1, 1.5]]))
        legal = self.solve_path([[.5, 1, -.5], [1.5, 1, .5]])
        self.assertIsNotNone(legal)
        self.assertEqual(legal[0]['blockageCounts']['horizontal'], 1)
        self.assertIsNone(self.solve_path([[-1, 1, .5], [2, 1, .5]]))

    def test_wide_neo_requires_two_horizontal_blockages_and_rejects_smaller_cap(self):
        path = [[-.5, 1, .5], [-.5, 1, 1.5], [.5, 1, 1.5], [1.5, 1, 1.5], [1.5, 1, .5]]
        selected = self.solve_path(path)
        self.assertIsNotNone(selected)
        self.assertEqual(selected[0]['blockageCounts']['horizontal'], 2)
        self.assertIsNone(self.solve_path(path, caps=(1, 2, 3)))
        for face, (begin, end) in zip(selected[0]['faces'], intervals(selected[0]['faces'], selected[0]['gates'], 0, 4)):
            self.assertTrue(all(outside(face, path[t]) for t in range(begin, end + 1)))

    def test_y_crossing_cannot_pass_through_a_ceiling(self):
        path = [[.5, -2, .5], [.5, 1, .5]]
        self.assertIsNone(self.solve_path(path))

    def test_passage_encoding_matches_every_one_tick_clearance_on_a_small_grid(self):
        from .geometry import disjunctions, satisfied
        points = list(itertools.product((-.5, .5, 1.5), (-2, 1, 5), (-.5, .5, 1.5)))
        for before, after in itertools.product(points, repeat=2):
            with self.subTest(before=before, after=after):
                groups = disjunctions(dict(solids=[[0, 0, 0, 1, 4, 1]]), [before[1], after[1]])
                xz = [[p[0], p[2]] for p in (before, after)]
                expected = all(any(all(satisfied(row, xz) for row in option['rows'])
                                   for option in group['options']) for group in groups)
                self.assertEqual(self.solve_path([before, after]) is not None, expected)

    def test_two_obstacles_can_have_interspersed_horizontal_and_vertical_gates(self):
        path = [[-.5, 1, .5], [-.5, 1, 1.5], [-.5, 2.5, 1.5], [.5, 2.5, 1.5], [1.5, 2.5, 1.5], [1.5, 1, .5]]
        selected = self.solve_path(path, boxes=[[0, 0, 0, 1, 10, 1], [0, 0, 1.3, .3, 2, 1.7]])
        self.assertIsNotNone(selected)
        self.assertEqual(selected[0]['blockageCounts']['horizontal'], 2)
        self.assertEqual(selected[1]['blockageCounts']['vertical'], 2)
        self.assertLessEqual(selected[0]['gates'][0], selected[1]['gates'][0])
        self.assertLess(selected[1]['gates'][0], selected[0]['gates'][-1])

    def test_caps_aggregate_collision_boxes_of_the_same_physical_block(self):
        path = [[-.5, 1, .5], [-.5, 1, 1.5], [.5, 1, 1.5], [1.5, 1, 1.5], [1.5, 1, .5]]
        boxes = [[0, 0, 0, 1, 4, 1], [0, 0, .1, 1, 4, 1]]
        self.assertIsNotNone(self.solve_path(path, boxes=boxes))
        self.assertIsNone(self.solve_path(path, boxes=boxes, physical=['same', 'same']))


@unittest.skipUnless(importlib.util.find_spec('pyscipopt') and os.environ.get('PKC_RUN_TICKS_COMPILER'),
                     'Set the Java compiler config and optional SCIP dependency')
class JointScheduleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = JavaCompiler(json.loads(Path(os.environ['PKC_RUN_TICKS_COMPILER']).read_text()))
        cls.addClassCleanup(cls.compiler.close)

    def prepare(self, request=None):
        return prepare_joint(request or open_request(), self.compiler)

    def assert_accepted(self, case, master):
        self.assertIn('yaw', master, master)
        checked = accept_joint(case, master, self.compiler, seconds=2)
        self.assertTrue(checked['accepted'], checked)
        self.assertTrue(checked['rawAccepted'], checked)
        self.assertLess(max(checked['errors'].values()), 1e-7)
        self.assertEqual(checked['candidate']['positions'][0], case['route']['initial']['position'][::2])
        self.assertEqual(checked['candidate']['velocity'][0], case['route']['initial']['velocity'][::2])
        self.assertTrue(within_caps(master['selectedPassages'], {'horizontal': 2, 'vertical': 2, 'total': 3}))
        return checked

    def test_departure_domains_match_all_small_allocations_without_generating_them_in_model(self):
        case = self.prepare(open_request(two=True))
        for total in range(3):
            horizon = 24 + total
            possible = [v for v in itertools.product(range(3), repeat=2) if sum(v) == total]
            expected = [{v[0] for v in possible}, {12 + sum(v) for v in possible}]
            self.assertEqual([set(v) for v in departure_domains(case, horizon)], expected)
        self.assertEqual(departure_domains(case, 27), [])

    def test_bounds_cover_random_original_trajectories_for_every_small_allocation(self):
        case = self.prepare(open_request(two=True))
        generator = random.Random(47)
        for allocation in ([0, 0], [1, 0], [0, 1], [1, 1], [2, 0], [0, 2]):
            compiled = compile_allocation(case, allocation, self.compiler)
            n = compiled['plan']['ticks']
            limits, phases = movement_bounds(case, departure_domains(case, n), n)
            for _ in range(12):
                raw = validate(compiled['problem'], [generator.uniform(-180, 180) for t in range(n)])
                for tick, (point, speed) in enumerate(zip(raw['positions'], raw['velocity'])):
                    self.assertLessEqual(math.hypot(*point), limits[tick][0] + 1e-10)
                    self.assertLessEqual(math.hypot(*speed), limits[tick][1] + 1e-10)
                for event in compiled['plan']['events']:
                    trace = phases[event['leg'], event['jump']]
                    for k, (radius, speed) in enumerate(trace):
                        self.assertLessEqual(math.hypot(*raw['positions'][event['jump'] + k]), radius + 1e-10)
                        self.assertLessEqual(math.hypot(*raw['velocity'][event['jump'] + k]), speed + 1e-10)

    def test_every_small_allocation_uses_the_shared_input_history_and_original_dynamics(self):
        case = self.prepare(open_request(two=True))
        for allocation in ([0, 0], [1, 0], [0, 1], [1, 1], [2, 0], [0, 2]):
            with self.subTest(allocation=allocation):
                compiled = compile_allocation(case, allocation, self.compiler)
                yaw = [-math.degrees(v) for v in compiled['problem']['base']]
                master = solve_horizon(case, compiled['plan']['ticks'], seconds=3, allocation=allocation, fixed_yaw=yaw)
                self.assertEqual(master.get('allocation'), allocation, master)
                self.assert_accepted(case, master)

    def test_joint_timing_must_put_the_one_run_tick_before_the_first_jump(self):
        request = open_request(two=True)
        request['scene']['blocks'][1]['boxes'] = [[3.4, 0, -1, 4.4, 1, 1]]
        request['scene']['blocks'][2]['boxes'] = [[7.2, 0, -1, 8.2, 1, 1]]
        case = self.prepare(request)
        master = solve_horizon(case, 25, seconds=8)
        self.assertEqual(master.get('allocation'), [1, 0], master)
        self.assert_accepted(case, master)
        self.assertEqual(master['completeAllocationsGenerated'], 0)
        impossible = solve_horizon(case, 25, seconds=3, allocation=[0, 1])
        self.assertNotIn('yaw', impossible)
        self.assertEqual(impossible['status'], 'infeasible')

    def test_nonzero_origin_and_initial_inertia_are_preserved(self):
        request = open_request()
        delta = [100, 0, -30]
        request['scene']['geometryBounds'] = [v + delta[a % 3] for a, v in enumerate(request['scene']['geometryBounds'])]
        request['scene']['initial']['position'] = [v + d for v, d in zip(request['scene']['initial']['position'], delta)]
        request['scene']['initial']['velocity'] = [.0049, REST_CARRY, -.005]
        for block in request['scene']['blocks']:
            block['boxes'] = [[v + delta[a % 3] for a, v in enumerate(box)] for box in block['boxes']]
        case = self.prepare(request)
        compiled = compile_allocation(case, [1], self.compiler)
        yaw = [-math.degrees(v) for v in compiled['problem']['base']]
        self.assert_accepted(case, solve_horizon(case, 13, seconds=3, allocation=[1], fixed_yaw=yaw))

    def test_different_heights_retime_y_and_use_the_corresponding_air_phase(self):
        request = open_request(two=True)
        request['scene']['blocks'][1]['boxes'] = [[1.7, 0, -1, 4, 1.5, 1]]
        request['scene']['blocks'][2]['boxes'] = [[4, -1, -1, 10, .5, 1]]
        case = self.prepare(request)
        self.assertNotEqual(case['route']['airtimes'][0], case['route']['airtimes'][1])
        compiled = compile_allocation(case, [1, 0], self.compiler)
        master = solve_horizon(case, compiled['plan']['ticks'], seconds=3, allocation=[1, 0],
                               fixed_yaw=[-math.degrees(v) for v in compiled['problem']['base']])
        checked = self.assert_accepted(case, master)
        self.assertEqual(checked['candidate']['y'], compiled['vertical']['y'])
        self.assertEqual(checked['candidate']['vy'], compiled['vertical']['vy'])

    def test_last_grounded_movement_can_leave_support_before_the_jump(self):
        from standalone.autonomy.route import supports
        request = open_request()
        request['scene']['blocks'][0]['boxes'] = [[-1, 0, -1, 1, 1, 1]]
        request['scene']['blocks'][1]['boxes'] = [[4.5, 0, -1, 6.5, 1, 1]]
        request['scene']['initial']['position'][0] = 1.25
        request['scene']['initial']['velocity'][0] = .1
        case = self.prepare(request)
        compiled = compile_allocation(case, [1], self.compiler)
        master = solve_horizon(case, 13, seconds=3, allocation=[1],
                               fixed_yaw=[-math.degrees(v) for v in compiled['problem']['base']])
        checked = self.assert_accepted(case, master)
        self.assertFalse(supports(case['route']['blocks'][0], *checked['candidate']['positions'][1], kind='run'))
        self.assertTrue(checked['candidate']['onGround'][1])
        self.assertTrue(compiled['plan']['jump'][1])
        two = compile_allocation(case, [2], self.compiler)
        blocked = solve_horizon(case, 14, seconds=3, allocation=[2],
                                fixed_yaw=[-math.degrees(v) for v in two['problem']['base']])
        self.assertEqual(blocked['status'], 'infeasible')

    def test_unplanned_earlier_landing_is_prevented(self):
        request = open_request()
        request['scene']['blocks'][1]['boxes'] = [[2, -1, -1, 5, 0, 1]]
        request['scene']['blocks'].append(dict(id='earlier-floor', boxes=[[1, -1, -1, 3.5, .5, 1]]))
        case = self.prepare(request)
        compiled = compile_allocation(case, [0], self.compiler)
        blocked = solve_horizon(case, compiled['plan']['ticks'], seconds=3, allocation=[0],
                                fixed_yaw=[-math.degrees(v) for v in compiled['problem']['base']])
        self.assertEqual(blocked['status'], 'infeasible')

    def test_two_jump_obstacle_choices_and_run_allocation_are_joint(self):
        request = json.loads((Path(__file__).parent / 'fixtures/two-jump-wide-neo.json').read_text())
        case = self.prepare(request)
        master = solve_horizon(case, 26, seconds=8)
        checked = self.assert_accepted(case, master)
        self.assertEqual(sum(master['allocation']), 2)
        self.assertGreater(master['allocation'][0], 0)
        wall = next(p for p in master['selectedPassages'] if p['blockId'] == 'wall')
        self.assertEqual(wall['blockageCounts']['horizontal'], 2)
        self.assertEqual(len(wall['gates']), 2)
        self.assertTrue(checked['candidate']['onGround'][master['departures'][1]])

    def test_unselected_ceiling_hit_is_prevented_inside_the_model(self):
        request = open_request()
        request['scene']['blocks'].append(dict(id='ceiling', boxes=[[-1, 3, -1, 1, 4, 1]]))
        case = self.prepare(request)
        master = solve_horizon(case, 12, seconds=3)
        self.assertEqual(master['status'], 'infeasible')
        self.assertNotIn('yaw', master)

    def test_repeated_support_has_two_distinct_jump_occurrences(self):
        request = open_request(two=True)
        request['scene']['blocks'] = [dict(id='start', boxes=[[-5, 0, -2, 5, 1, 2]]),
                                      dict(id='goal', boxes=[[6, 0, -1, 9, 1, 1]])]
        request['route'] = ['start', 'start', 'goal']
        case = self.prepare(request)
        compiled = compile_allocation(case, [0, 0], self.compiler)
        master = solve_horizon(case, 24, seconds=3, allocation=[0, 0],
                               fixed_yaw=[-math.degrees(v) for v in compiled['problem']['base']])
        checked = self.assert_accepted(case, master)
        self.assertEqual(master['departures'], [0, 12])
        self.assertTrue(checked['candidate']['onGround'][12])

    def test_unsupported_resume_ladders_and_facing_constraints_fail_explicitly(self):
        request = open_request()
        request['positionRows'] = [dict(mode='F', t1=0, cmp='GE', rhs=0)]
        with self.assertRaisesRegex(ValueError, 'X/Z'):
            self.prepare(request)
        request = open_request()
        request['scene']['initial']['velocity'][1] = -.2
        with self.assertRaisesRegex(ValueError, 'resting'):
            self.prepare(request)


if __name__ == '__main__':
    unittest.main()
