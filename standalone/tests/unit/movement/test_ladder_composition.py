from tests.support.ladder_composition import airborne_case
from tests.support.paths import AUTONOMY_DATA
import copy
import hashlib
import json
import os
import random
import time
import unittest

from parkour_solver.scene.ladders import ladder_columns, ladder_regions, climb_walls, entry_channels, exit_faces
from parkour_solver.movement.ladder.contacts import ceiling_sequence, with_contacts
from parkour_solver.movement.ladder.schedules import generate_ladder_schedules, trace_ladder_schedule
from parkour_solver.movement.ladder.episodes import compose_ladder_episodes
from parkour_solver.scene.normalize import prepare_scene
from parkour_solver.constraints.contacts import floor_checks
from parkour_solver.search.repair import solve_contact_branches; from parkour_solver.validation.vertical import validate_vertical_schedule
from parkour_solver.bridge.python_compiler import PythonCompiler
from parkour_solver.scene.route import prepare_route, schedule
from parkour_solver.physics.vertical import REST_CARRY, CLIMB_CARRY, f32
from parkour_solver.movement.ceiling_profiles import ceiling_options


ROOT = AUTONOMY_DATA


class LadderRegionTests(unittest.TestCase):
    def test_expired_generation_budget_retains_unknown_status(self):
        config=json.loads((ROOT / 'fixtures/ladder-schedule-generator.json').read_text())
        result=generate_ladder_schedules(config,deadline=time.monotonic()-1)
        self.assertEqual(result['termination'],'deadline')
        self.assertFalse(result['generationComplete'])
        self.assertEqual(result['schedules'],[])

    def test_horizontal_vertical_strips_merge_without_diagonal_or_hole_filling(self):
        cells = [[0,y,z] for y in range(3) for z in range(4)]
        regions = ladder_regions(cells)
        self.assertEqual(len(regions), 1)
        self.assertEqual((regions[0]['x'], regions[0]['maxX'], regions[0]['z'], regions[0]['maxZ']), (0,1,0,4))
        self.assertEqual(len(ladder_regions([[0,0,0], [1,0,1]])), 2)
        rng = random.Random(7398)
        for _ in range(200):
            cells = [[x,y,z] for x in range(3) for y in range(3) for z in range(3) if rng.random() < .6]
            regions = ladder_regions(cells)
            represented = [(x,y,z) for region in regions for r in region.get('volumes', [region])
                           for x in range(r['x'], r['maxX']) for y in range(r['minY'], r['maxY'])
                           for z in range(r['z'], r['maxZ'])]
            self.assertEqual(set(represented), {tuple(c) for c in cells})
            self.assertEqual(len(represented), len(set(represented)))

    def test_repeated_ladder_ceiling_events_regenerate_the_complete_schedule(self):
        config = json.loads((ROOT / 'fixtures/ladder-schedule-generator.json').read_text())
        s = next(s for s in generate_ladder_schedules(config)['schedules']
                 if s['climbWindow'] == [0,6] and not s['beginningStop'] and not s['endStop'] and not any(s['sneak']))
        col = {'id': 'a', 'x': 0, 'z': 0, 'minY': 0, 'maxY': 5}
        wall = climb_walls(col, [[1,0,-2,2,6,2]])[0]
        ceiling = [-2, 4., -2, 3, 5, 3]
        contacts = ceiling_sequence(s, ceiling_options([ceiling])[0])
        self.assertGreater(len(contacts), 1)
        changed = with_contacts(s, contacts, col, wall, 0, max_inputs=40)
        self.assertIsNotNone(changed)
        self.assertNotEqual(s['trace']['position'], changed['trace']['position'])
        for e in changed['contactEvents']:
            state = e['offset'] + 1
            if changed['ladderTicks'][state] and changed['hit'][state - 1]:
                self.assertEqual(changed['trace']['velocity'][state], CLIMB_CARRY)

    def test_generator_marks_unprobed_transfer_entries_without_claiming_validation(self):
        config = json.loads((ROOT / 'fixtures/ladder-schedule-generator.json').read_text())
        e = config['entries'][0]
        state = e.pop('firstValidatedState')
        e['firstCandidateState'] = state
        generated = generate_ladder_schedules(config)
        self.assertNotIn('firstValidatedState', generated['entryWindows'][0])
        self.assertEqual(generated['entryWindows'][0]['firstCandidateState'], state)
        self.assertTrue(generated['schedules'])


@unittest.skipUnless(os.environ.get('PKC_IPOPT_PYTHON'),
                     'Set PKC_IPOPT_PYTHON')
class CompositionEvidenceTests(unittest.TestCase):
    def test_saved_multiple_ladder_ceiling_transfer_and_contact_blockage_contracts(self):
        from parkour_solver.validation.scene import check_geometry
        from parkour_solver.constraints.avoidance import gate_timeline
        from parkour_solver.numerics import prepare, validate
        with PythonCompiler() as compiler:
            for name in ('two-ladders','ladder-ceiling','airborne-transfer','automatic-airborne-transfer','contact-blockage'):
                with self.subTest(name=name):
                    e = json.loads((ROOT / ('experiments/contact-combinations/' + name + '.json')).read_text())
                    if 'fixtureSha256' in e:
                        fixture = ROOT / 'experiments/contact-combinations' / e['fixture']
                        self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(), e['fixtureSha256'])
                    p = prepare(e['problem'])
                    c = validate(p, p['yaw'])
                    self.assertTrue(c['feasible'], c)
                    self.assertEqual(c['positions'], e['candidate']['positions'])
                    self.assertEqual(c['velocity'], e['candidate']['velocity'])
                    self.assertEqual(c['y'], e['candidate']['y'])
                    self.assertTrue(validate_vertical_schedule(e['route'], e['profiles'], e['plan'])['feasible'])
                    vertical = dict(vertical=p['vertical'], y=c['y'], vy=c['vy'], onGround=c['onGround'])
                    checked = check_geometry(e['geometry'],e['plan'],e['candidate'],vertical,compiler,5)
                    self.assertTrue(checked['feasible'],checked)
                    if name in ('airborne-transfer','automatic-airborne-transfer'):
                        self.assertEqual(len(e['route']['ladderEvents']),2)
                        self.assertFalse(any(c['onGround'][1:-1]))
                    if name == 'contact-blockage':
                        gates = gate_timeline(e['route']['passages'], e['plan']['events'])
                        self.assertEqual(gates[0]['firstState'], e['plan']['sceneContacts'][0]['state'])
                        self.assertEqual(len(gates), 2)
