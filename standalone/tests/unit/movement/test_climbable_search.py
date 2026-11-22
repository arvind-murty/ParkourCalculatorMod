from tests.support.paths import AUTONOMY_DATA
import copy
import hashlib
import json
import os
import unittest
from pathlib import Path

from parkour_solver.scene.normalize import prepare_scene
from parkour_solver.validation.vertical import validate_vertical_schedule
from parkour_solver.movement.ladder.settings import ladder_settings
from parkour_solver.bridge.python_compiler import PythonCompiler
from parkour_solver.movement.ladder.events import compile_ladder_events
from parkour_solver.scene.ladders import climb_walls, entry_channels, ladder_columns, schedule_constraints
from parkour_solver.movement.ladder.schedules import generate_ladder_schedules
from parkour_solver.validation.scene import check_geometry
from parkour_solver.scene.route import prepare_route, schedule
from parkour_solver.numerics import prepare, validate


ROOT = AUTONOMY_DATA


class ClimbableContractTests(unittest.TestCase):
    def test_invalid_cells_settings_and_conflicting_metadata_are_rejected(self):
        r = json.loads((ROOT / 'fixtures/ladder-top.json').read_text())
        for value in ([[0, 1.5, 0]], [[True, 1, 0]], [[0, 1]], {}, None):
            changed = copy.deepcopy(r)
            changed['climbableCells'] = value
            with self.assertRaises(ValueError):
                prepare_scene(changed)
        with self.assertRaises(ValueError):
            ladder_settings({'ladderSearch': {'maxInteractions': 201}})
        self.assertEqual(ladder_settings({'ladderSearch': {'maxInteractions': 13}})['maxInteractions'], 13)
        scene = prepare_scene(r)
        scene['collisionScene']['ladders'] = [[9, 9, 9]]
        with self.assertRaises(ValueError):
            prepare_scene(scene)

    def test_ladder_events_retime_keys_hits_membership_and_y_together(self):
        request = json.loads((ROOT / 'fixtures/ladder-schedule-generator.json').read_text())
        s = next(s for s in generate_ladder_schedules(request)['schedules']
                 if s['climbWindow'] == [0, 6] and s['endStop'] and not s['beginningStop'] and not any(s['sneak']))
        column = {'x': 0, 'z': 0, 'minY': 0, 'maxY': 5}
        wall = climb_walls(column, [[1, 0, -1, 2, 5, 2]])[0]
        entry = entry_channels(column, wall)[0]
        event = {'leg': 0, 'schedule': s, 'column': column, 'wall': wall, 'entryFace': entry,
                 'exitFace': {'axis': 'Z', 'direction': 1, 'plane': 1}}
        r = prepare_route({'version': 1, 'initial': {'position': [-.5, 2, .5], 'velocity': [0, -.0784000015258789, 0]},
                           'blocks': [{'bounds': [-1, 0, 0, 1], 'top': 2}, {'bounds': [0, 1, 1, 2], 'top': 0}],
                           'airtimes': [s['ticks']], 'sharedMovement': True, 'ladderEvents': [event]})
        a, b = schedule(r, [0]), schedule(r, [2])
        self.assertEqual([c['state'] + 2 for c in a['contacts']], [c['state'] for c in b['contacts']])
        self.assertEqual(b['ladderTicks'][2:], a['ladderTicks'])
        self.assertEqual(b['sneak'][2:], a['sneak'])
        self.assertFalse(any(w['mode'] == 'F' for w in b['walls']))
        self.assertFalse(any('reverse' in w['name'] for w in b['walls']))

    def test_finite_wall_must_overlap_the_actual_climb_height(self):
        config = json.loads((ROOT / 'fixtures/ladder-schedule-generator.json').read_text())
        s = next(s for s in generate_ladder_schedules(config)['schedules'] if sum(s['hit']) >= 3)
        column = {'x': 0, 'z': 0, 'minY': 0, 'maxY': 5}
        wall = climb_walls(column, [[1, -1, -1, 2, .1, 2]])[0]
        with self.assertRaises(ValueError):
            schedule_constraints(s, column, wall, entry_channels(column, wall)[0], None)


class SavedLadderEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.compiler = PythonCompiler()
        self.addCleanup(self.compiler.close)

    def check(self, evidence):
        candidate, problem = evidence['candidate'], prepare(evidence['problem'])
        vertical = {'vertical': problem['vertical'], 'y': candidate['y'],
                    'vy': candidate['vy'], 'onGround': candidate['onGround']}
        return check_geometry(evidence['geometry'], evidence['plan'], candidate, vertical, self.compiler, 5)

    def test_saved_candidates_revalidate_scalar_y_and_whole_scene_contracts(self):
        for name in ('ladder-top', 'ladder-side-entry', 'ladder-side-landing', 'ladder-thin-top', 'ladder-prefix-suffix'):
            with self.subTest(name=name):
                path = ROOT / ('experiments/ladder-search/' + name + '.json')
                e = json.loads(path.read_text())
                self.assertEqual(hashlib.sha256((ROOT / ('fixtures/' + Path(e['fixture']).name)).read_bytes()).hexdigest(), e['fixtureSha256'])
                problem = prepare(e['problem'])
                candidate = validate(problem, problem['yaw'])
                self.assertTrue(candidate['feasible'], candidate)
                self.assertEqual(candidate['positions'], e['candidate']['positions'])
                self.assertEqual(candidate['velocity'], e['candidate']['velocity'])
                self.assertEqual(candidate['y'], e['candidate']['y'])
                self.assertTrue(validate_vertical_schedule(e['route'], e['profiles'], e['plan'])['feasible'])
                self.assertTrue(self.check(e)['feasible'])

    def test_shared_acceptance_rejects_missing_extra_and_mistimed_hits_occupancy_and_carry(self):
        source = json.loads((ROOT / 'experiments/ladder-search/ladder-top.json').read_text())
        mutations = []
        e = copy.deepcopy(source)
        e['plan']['sceneContacts'].pop(0)
        mutations.append(e)
        e = copy.deepcopy(source)
        hit = copy.deepcopy(e['plan']['sceneContacts'][0])
        hit['state'] -= 1
        e['plan']['sceneContacts'].insert(0, hit)
        mutations.append(e)
        e = copy.deepcopy(source)
        e['plan']['sceneContacts'][0]['state'] -= 1
        mutations.append(e)
        e = copy.deepcopy(source)
        e['plan']['ladderTicks'][source['summary']['entryState']] = False
        mutations.append(e)
        e = copy.deepcopy(source)
        state = e['plan']['sceneContacts'][0]['state']
        e['candidate']['vy'][state] = 0
        mutations.append(e)
        for index, changed in enumerate(mutations):
            with self.subTest(mutation=index):
                self.assertFalse(self.check(changed)['feasible'])

