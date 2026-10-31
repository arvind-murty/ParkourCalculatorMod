import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import unittest

from standalone.ipopt import prepare, validate
from standalone.ipopt.ladder import CAP
from .backend import solve
from .geometry import disjunctions, satisfied, selected_rows


def problem(ticks=1, **values):
    return dict(dict(version=1, ticks=ticks, position=[0, 0], velocity=[0, 0],
        magnitude=[.1] * ticks, friction=[.91] * ticks, base=[0] * ticks, yaw=[0] * ticks,
        walls=[], objective=dict(axis='X', tick=ticks, sense='MAX'), threshold=.005), **values)


class GeometryTests(unittest.TestCase):
    def setUp(self):
        self.scene = dict(solids=[[0, 0, 0, 1, 3, 1]])
        self.groups = disjunctions(self.scene, [1, 1])

    def allowed(self, positions):
        return all(any(all(satisfied(row, positions) for row in option['rows'])
                       for option in group['options']) for group in self.groups)

    def test_x_resolves_before_z_and_endpoint_clearance_is_insufficient(self):
        self.assertFalse(self.allowed([[-.5, .5], [.5, 1.5]]))
        self.assertTrue(self.allowed([[.5, -.5], [1.5, .5]]))
        self.assertFalse(self.allowed([[-1, .5], [2, .5]]))

    def test_y_sweep_uses_pre_horizontal_state(self):
        scene = dict(solids=[[0, 3, 0, 1, 4, 1]])
        groups = disjunctions(scene, [1, 2])
        self.assertEqual(groups[0]['stage'], 'Y')
        self.assertTrue(all(row['t1'] == 0 for option in groups[0]['options'] for row in option['rows']))

    def test_floor_and_ceiling_tangency_do_not_create_interior_collision_constraints(self):
        from standalone.autonomy.obstacle_geometry import BODY_HEIGHT
        self.assertEqual(disjunctions(dict(solids=[[0, 0, 0, 1, 1, 1]]), [1, 1]), [])
        self.assertEqual(disjunctions(dict(solids=[[0, 1 + BODY_HEIGHT, 0, 1, 5, 1]]), [1, 1]), [])

    def test_selected_rows_preserve_stage_states_and_reject_bad_assignments(self):
        rows = selected_rows(self.groups, [0, 0, 0])
        self.assertEqual({r['t1'] for r in rows}, {0, 1})
        self.assertEqual(len(rows), 2)
        for indices in ([0], [4, 0, 0], [-1, 0, 0]):
            with self.assertRaises(ValueError):
                selected_rows(self.groups, indices)

    def test_clearance_guard_preserves_declared_contact_and_initial_tangency(self):
        from standalone.autonomy.route import HALF_WIDTH
        plane = -HALF_WIDTH
        groups = disjunctions(self.scene, [1, 1], contacts=[dict(state=1, axis=0, plane=plane)], initial=[plane, -.5])
        self.assertTrue(all(row['rhs'] == plane for group in groups for option in group['options']
                            if option['axis'] == 'X' and option['direction'] == -1 for row in option['rows']))
        self.assertTrue(any(row['rhs'] < plane for group in self.groups for option in group['options']
                            if option['axis'] == 'X' and option['direction'] == -1 for row in option['rows']))


@unittest.skipUnless(importlib.util.find_spec('pyscipopt'), 'Install spike requirements or add its isolated site directory to PYTHONPATH')
class MixedModelTests(unittest.TestCase):
    def accepted(self, p, **options):
        result = solve(dict(problem=p), seconds=3, **options)
        self.assertIn('yaw', result, result)
        checked = validate(prepare(p), result['yaw'])
        self.assertTrue(checked['feasible'], checked)
        for actual, expected in zip(result['positions'], checked['positions']):
            for value, reference in zip(actual, expected):
                self.assertAlmostEqual(value, reference, places=6)
        return result, checked

    def test_circle_dynamics_match_original_scalar_model_from_nonzero_world_origin(self):
        p = problem(3, position=[200, -30], velocity=[.02, -.04],
            walls=[dict(mode='X', t1=3, cmp='GE', rhs=200.1)])
        self.accepted(p)

    def test_inertia_threshold_first_input_matches_positive_negative_and_zero_branches(self):
        for value in (0, .0049, -.0049, .005, -.005):
            with self.subTest(velocity=value):
                self.accepted(problem(velocity=[value, 0]))

    def test_both_ladder_components_can_clamp_without_changing_scalar_acceptance(self):
        p = problem(magnitude=[.4], ladderTicks=[True, True],
            walls=[dict(mode=axis, t1=1, cmp='EQ', rhs=CAP) for axis in ('X', 'Z')])
        _, checked = self.accepted(p)
        for value in checked['positions'][1]:
            self.assertAlmostEqual(value, CAP, places=9)

    def test_fixed_wall_hit_reaches_plane_and_resets_only_blocked_velocity(self):
        p = problem(velocity=[.2, .03], contacts=[dict(state=1, axis=0, plane=.15, direction=1)])
        _, checked = self.accepted(p)
        self.assertEqual(checked['positions'][1][0], .15)
        self.assertEqual(checked['velocity'][1][0], 0)

    def test_disk_is_only_a_relaxation_and_cannot_certify_stationary_fixed_input(self):
        p = problem(walls=[dict(mode=axis, t1=1, cmp='EQ', rhs=0) for axis in ('X', 'Z')])
        circle = solve(dict(problem=p), seconds=2, control='circle')
        disk = solve(dict(problem=p), seconds=2, control='disk')
        self.assertNotIn('yaw', circle)
        self.assertEqual(circle['status'], 'infeasible')
        self.assertIn('yaw', disk)
        self.assertFalse(validate(prepare(p), disk['yaw'])['feasible'])

    def test_future_unsupported_facing_rows_fail_explicitly(self):
        p = problem(walls=[dict(mode='F', t1=0, cmp='GE', rhs=0)])
        with self.assertRaisesRegex(ValueError, 'free facing'):
            solve(dict(problem=p))

    def test_geometry_choices_are_actually_binary_and_enforced(self):
        group = dict(options=[dict(rows=[dict(mode='X', t1=1, cmp='LE', rhs=-.06)]),
                              dict(rows=[dict(mode='X', t1=1, cmp='GE', rhs=.04)])])
        result = solve(dict(problem=problem(), disjunctions=[group]), seconds=3)
        self.assertEqual(len(result['selectedFaces']), 1)
        self.assertTrue(all(satisfied(row, result['positions'], 1e-7) for row in result['selectedRows']))
        opposite = solve(dict(problem=problem(), disjunctions=[group]), seconds=3,
                         forbidden=[result['selectedFaces']])
        self.assertNotEqual(opposite['selectedFaces'], result['selectedFaces'])
        self.assertTrue(validate(prepare(problem()), opposite['yaw'])['feasible'])


@unittest.skipUnless(os.environ.get('PKC_RUN_TICKS_COMPILER'), 'Set PKC_RUN_TICKS_COMPILER for shared-scene evidence checks')
class SavedSceneTests(unittest.TestCase):
    def setUp(self):
        from standalone.autonomy.compiler import JavaCompiler
        self.compiler = JavaCompiler(json.loads(Path(os.environ['PKC_RUN_TICKS_COMPILER']).read_text()))
        self.addCleanup(self.compiler.close)
        self.directory = Path(__file__).parents[1] / 'experiments/mixed-integer-spike'

    def test_every_saved_witness_replays_original_and_selected_constraints_and_finite_scene(self):
        from .cases import CASES
        from .run import acceptance
        manifest = json.loads((self.directory / 'manifest.json').read_text())
        self.assertEqual(set(manifest['cases']), set(CASES))
        self.assertEqual(len(manifest['witnesses']), 16)
        for name in manifest['witnesses']:
            with self.subTest(witness=name):
                evidence = json.loads((self.directory / name).read_text())
                source = self.directory / evidence['case']
                self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), evidence['caseSha256'])
                case = json.loads(source.read_text())
                p = copy.deepcopy(case['problem'])
                p['walls'].extend(evidence['selectedRows'])
                checked = validate(prepare(p), evidence['candidate']['yaw'])
                self.assertTrue(checked['feasible'])
                for key in ('positions', 'velocity', 'y', 'vy', 'onGround'):
                    self.assertEqual(checked[key], evidence['candidate'][key])
                self.assertTrue(acceptance(case, checked, self.compiler, 5)['feasible'])
                self.assertEqual(checked['positions'][0], case['problem']['position'])
                self.assertEqual(checked['velocity'][0], case['problem']['velocity'])
                self.assertEqual(checked['y'], case['vertical']['y'])
                if case['name'] == 'repeat-support':
                    self.assertEqual([t for t, jump in enumerate(case['plan']['jump']) if jump], [0, 12])
                    self.assertTrue(checked['onGround'][12])
                self.assertFalse(evidence['globalOptimalityClaimed'])

    def test_saved_contacts_and_membership_are_required_by_independent_scene_acceptance(self):
        from standalone.autonomy.obstacle_geometry import check_geometry
        for case_name, remove_ceiling in [('wall-reset', False), ('ladder-end-stop', True)]:
            evidence = json.loads((self.directory / (case_name + '-circle.witness.json')).read_text())
            case = json.loads((self.directory / evidence['case']).read_text())
            plan = copy.deepcopy(case['plan'])
            selected = next(c for c in plan['sceneContacts'] if (c['axis'] == 1 and c['direction'] == 1) == remove_ceiling)
            plan['sceneContacts'].remove(selected)
            self.assertFalse(check_geometry(case['scene']['collisionScene'], plan, evidence['candidate'],
                                           case['vertical'], self.compiler, 5)['feasible'])
            if remove_ceiling:
                plan = copy.deepcopy(case['plan'])
                plan['ladderTicks'][0] = False
                self.assertFalse(check_geometry(case['scene']['collisionScene'], plan, evidence['candidate'],
                                               case['vertical'], self.compiler, 5)['feasible'])


if __name__ == '__main__':
    unittest.main()
