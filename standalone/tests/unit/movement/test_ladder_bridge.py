from tests.support.paths import MOVEMENT_FIXTURES
import copy
import json
import unittest

from parkour_solver.bridge.python_compiler import PythonCompiler
from parkour_solver.scene.ladders import climb_walls, entry_channels, ladder_columns
from parkour_solver.movement.ladder.schedules import generate_ladder_schedules
from parkour_solver.scene.route import HALF_WIDTH


class LadderGeometryTests(unittest.TestCase):
    def test_columns_preserve_holes_and_signed_front_side_channels(self):
        columns = ladder_columns([[0, 0, 0], [0, 1, 0], [0, 3, 0], [1, 1, 0]])
        self.assertEqual([(c['minY'], c['maxY']) for c in columns], [(0, 2), (3, 4), (1, 2)])
        walls = climb_walls(columns[0], [[1, 0, 0, 2, 4, 1]])
        self.assertEqual(len(walls), 1)
        self.assertEqual([(c['family'], c['axis'], c['direction']) for c in entry_channels(columns[0], walls[0])],
                         [('front', 'X', -1), ('side', 'Z', -1), ('side', 'Z', 1)])

class LadderBridgeTests(unittest.TestCase):
    def setUp(self):
        self.compiler = PythonCompiler()
        self.addCleanup(self.compiler.close)

    def test_shared_compiler_realizes_sneak_and_repeated_wall_sprint_loss(self):
        route = {'sharedMovement': True, 'inputs': {'forward': .98, 'strafe': 0, 'sprint': True,
                                                   'incomingSprint': True, 'force45': True}}
        plan = {'grounded': [False] * 4, 'jump': [False] * 4, 'sneak': [False, True, False, False],
                'ladderTicks': [True] * 5, 'sceneContacts': [{'state': s, 'axis': 0} for s in (1, 2, 3)]}
        result = self.compiler.compile(route, plan, 5)
        self.assertAlmostEqual(result['movement']['moveForward'][1], .3 * .98, places=7)
        self.assertEqual(result['movement']['sprinting'], [True, False, False, False])
        self.assertEqual(result['movement']['airSprinting'], [True, True, False, False])

    def test_scene_checks_occupancy_sneak_and_actual_climb_velocity(self):
        plane = 1 - HALF_WIDTH
        box = [1, 0, -1, 2, 5, 2]
        scene = {'version': 1, 'profile': 'minecraft-1.8.9', 'bounds': [-5, -5, -5, 5, 10, 5],
                 'solids': [box], 'ladders': [[0, 1, 0], [0, 2, 0]]}
        positions = [[.6, 2, .5], [plane, 2, .5]]
        contacts = [{'state': 1, 'axis': 0, 'plane': plane, 'direction': 1, 'boxes': [box]}]
        contract = {'ladderTicks': [True, True], 'sneak': [True],
                    'velocities': [[0, 0, 0], [0, (.2 - .08) * .9800000190734863, 0]], 'friction': [.91]}
        checked = self.compiler.check_scene(scene, positions, [[.15, 0, 0]], [False, False], 5,
                                             contacts=contacts, **contract)
        self.assertTrue(checked['feasible'], checked)
        wrong = copy.deepcopy(contract)
        wrong['velocities'][1][1] = 0
        self.assertFalse(self.compiler.check_scene(scene, positions, [[.15, 0, 0]], [False, False], 5,
                                                 contacts=contacts, **wrong)['feasible'])
        wrong = copy.deepcopy(contract)
        wrong['ladderTicks'][1] = False
        self.assertFalse(self.compiler.check_scene(scene, positions, [[.15, 0, 0]], [False, False], 5,
                                                 contacts=contacts, **wrong)['feasible'])
        self.assertFalse(self.compiler.check_scene(scene, positions, [[.3, 0, 0]], [False, False], 5,
                                                 contacts=contacts, **contract)['feasible'])

    def test_coplanar_climb_parts_require_the_declared_face_plane_and_overlap(self):
        lower, upper = [1, 0, -1, 2, 2, 2], [1, 2, -1, 2, 4, 2]
        scene = {'version': 1, 'profile': 'minecraft-1.8.9', 'bounds': [-5, -5, -5, 5, 10, 5],
                 'solids': [lower, upper]}
        plane = 1 - HALF_WIDTH
        positions, movement = [[.6, 1.8, .5], [plane, 1.8, .5]], [[.15, 0, 0]]
        contact = {'state': 1, 'axis': 0, 'plane': plane, 'direction': 1, 'boxes': [upper]}
        checked = self.compiler.check_scene(scene, positions, movement, [False, False], 5, contacts=[contact])
        self.assertTrue(checked['feasible'], checked)
        for wrong in ([2, 2, -1, 3, 4, 2], [1, 4, -1, 2, 5, 2], [1, 2, 3, 2, 4, 4]):
            with self.subTest(box=wrong):
                checked = self.compiler.check_scene(scene, positions, movement, [False, False], 5,
                                                     contacts=[dict(contact, boxes=[wrong])])
                self.assertFalse(checked['feasible'])
