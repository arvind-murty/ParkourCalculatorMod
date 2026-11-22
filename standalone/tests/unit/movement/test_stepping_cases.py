from tests.support.stepping_cases import FIXTURES, fixture
from tests.support.paths import AUTONOMY_DATA
import copy
import itertools
import json
import os
import unittest

from parkour_solver.constraints.contacts import floor_checks
from parkour_solver.search.repair import solve_contact_branches; from parkour_solver.validation.vertical import validate_vertical_schedule
from parkour_solver.scene.normalize import prepare_scene
from parkour_solver.movement.cases import case_profile, corner_rows, entry_possible
from parkour_solver.bridge.python_compiler import PythonCompiler
from parkour_solver.movement.generation import generate_choices
from parkour_solver.movement.units import compile_units, overlay_program, profile_program, shifted_unit
from parkour_solver.validation.scene import check_geometry
from parkour_solver.scene.route import prepare_route, schedule
from parkour_solver.numerics import prepare
from parkour_solver.physics.vertical import REST_CARRY, f32, vertical_step


def generated(data):
    scene = prepare_scene(data['request'])
    by_id = {b['id']: b for b in scene['blocks']}
    choices = list(generate_choices('ceiling' if data['case'] == 'grind' else 'stepping',
        dict(scene=scene, source=by_id['start'], target=by_id['goal'], cases=[data['case']], maxInputs=40,
             interactionGate=None)))
    choice = choices[data['choice']]
    edge = dict(source='start', target='goal', profileKind=choice['profileKind'], profile=choice['profile'],
                entryFace=choice.get('entryFace'), exitFace=None)
    return scene, edge


class SteppingCaseTests(unittest.TestCase):
    def test_scalar_named_outcomes_and_rejected_controls(self):
        cases = [
            ('normal_step', 1, REST_CARRY, True, None, 1.5, {}, 1.5, True),
            ('slab_boost', 1, REST_CARRY, True, f32(.42), 1.5, {}, 1.5, False),
            ('blip', 1.2, -.4, False, None, 1.5, {'lowerHeight': 1, 'higherHeight': 1.5}, 1.5, True),
            ('blip_up', 1.6, -.8, False, None, 1.6, {'lowerHeight': 1, 'higherHeight': 1.5}, 1.6, True),
            ('corner_blip_up', 1.2, -.4, False, None, 1.2, {'lowerHeight': 1, 'higherHeight': 1.5}, 1.2, True),
            ('corner_jump_cancel', 1, REST_CARRY, True, f32(.42), 1, {'higherHeight': 1.5}, 1, False),
            ('grind', 1, REST_CARRY, True, f32(.42), 1, {'clearance': .1}, 1, False)]
        for kind, y, vy, ground, jump, plane, metadata, expected_y, expected_ground in cases:
            with self.subTest(case=kind):
                contact = dict(state=1, direction=-1, plane=plane, case=kind, **metadata)
                config = dict(threshold=.005, jumpVelocity=[jump], sneak=[False], contacts=[contact])
                actual_y, move, actual_ground, failures = vertical_step(config, 0, y, vy, False, ground, 0)
                self.assertEqual(failures, [])
                self.assertEqual((actual_y, move, actual_ground), (expected_y, 0, expected_ground))
                config['contacts'][0]['plane'] += 1
                self.assertTrue(vertical_step(config, 0, y, vy, False, ground, 0)[3])
        for height in [.1, .42, .5, f32(.6)]:
            p = case_profile(1, 1 + height, dict(case='normal_step', input=0, plane=1 + height),
                             finish_at=1, jump_at_start=False)
            self.assertEqual(p['position'], [1, 1 + height])
            self.assertEqual(p['jump'], [False])
        for height in [.60001, .75, 1.25]:
            self.assertIsNone(case_profile(1, 1 + height, dict(case='normal_step', input=0, plane=1 + height),
                                          finish_at=1, jump_at_start=False))

    def test_height_windows_and_free_hit_tick_are_required(self):
        for kind, y, vy, plane, metadata in [
            ('blip', 1.2, -.1, 1.5, {'lowerHeight': 1}),
            ('blip_up', 1.5, -.8, 1.5, {'lowerHeight': 1, 'higherHeight': 1.5}),
            ('corner_jump_cancel', 1, REST_CARRY, 1, {'higherHeight': 1 + f32(.42)}),
            ('grind', 1, REST_CARRY, 1, {'clearance': .42}),
            ('corner_blip_up', 1.2, -.4, 1.2, {'lowerHeight': 1, 'higherHeight': 1.9})]:
            jump = f32(.42) if kind in ('grind', 'corner_jump_cancel') else None
            config = dict(threshold=.005, jumpVelocity=[jump], sneak=[False],
                          contacts=[dict(state=1, direction=-1, plane=plane, case=kind, **metadata)])
            self.assertTrue(vertical_step(config, 0, y, vy, False, jump is not None, 0)[3], kind)

    def test_ladder_ownership_survives_typed_air_overlay(self):
        units = [dict(type='normal', start=0, end=2, jumpInputs=[0], groundedInputs=[0], sneakInputs=[]),
                 dict(type='ladder_climb', start=2, end=5, occupiedStates=[3, 5], hitInputs=[2, 3]),
                 dict(type='normal', start=5, end=8, jumpInputs=[], groundedInputs=[], sneakInputs=[])]
        templates = [dict(type='ceiling', start=1, end=2, case='ceiling_hit'),
                     dict(type='ceiling', start=3, end=4, case='ceiling_hit'),
                     dict(type='ceiling', start=6, end=7, case='ceiling_hit')]
        overlaid = overlay_program(units, templates)
        self.assertEqual([u['type'] for u in overlaid], ['normal', 'ceiling', 'ladder_climb', 'normal', 'ceiling', 'normal'])
        self.assertEqual(compile_units(8, overlaid)['hit'], [False, False, True, True, False, False, False, False])
        self.assertEqual(overlaid[0]['jumpInputs'], [0])
        with self.assertRaises(ValueError):
            overlay_program(units, templates + [templates[0]])

    def test_corner_constraints_have_x_approach_and_consecutive_z_escape(self):
        box = [.85, 1, 0, 1.15, 1.5, .55]
        for direction, escape in itertools.product((-1, 1), repeat=2):
            rows = corner_rows(box, 4, 5, direction, escape)
            self.assertEqual(rows[0]['mode'], 'X')
            self.assertEqual(rows[0]['t1'], 4)
            self.assertEqual(rows[1]['mode'], 'Z')
            self.assertEqual(rows[1]['t1'], 5)
            self.assertEqual(rows[0]['cmp'], 'LE' if direction > 0 else 'GE')
            self.assertEqual(rows[1]['cmp'], 'GE' if escape > 0 else 'LE')

@unittest.skipUnless(os.environ.get('PKC_IPOPT_PYTHON'),
                     'Set PKC_IPOPT_PYTHON')
class ExposureTests(unittest.TestCase):
    def sides(self, solids, case):
        from parkour_solver.movement.stepping import open_stepping_choices
        from parkour_solver.physics.vertical import REST_CARRY
        scene = dict(collisionScene=dict(solids=solids),
                     blocks=[dict(id='b' + str(i), sourceBlock='b' + str(i), solidIndex=i, top=b[4]) for i, b in enumerate(solids)])
        state = dict(position=1., velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)
        found = set()
        for choice in open_stepping_choices(dict(scene=scene, state=state, maxInputs=10, threshold=0., cases=[case],
                                                   interactionGate=None)):
            event, = choice['profile']['caseEvents']
            if event['boxes'] == [solids[1]]:
                found.add(tuple(sorted((r['mode'], r['cmp']) for r in choice['profile']['positionConstraints']
                                       if r['name'] in ('case:outside', 'case:cross_x'))))
        return found

    def test_a_side_flush_against_a_neighbour_offers_no_step(self):
        start, target, wall = [-2, 0, -2, 2, 1, 4], [2, 0, 0, 3, 1.5, 2], [2, 0, 2, 3, 1.5, 3]
        open_sides = self.sides([start, target], 'normal_step')
        flush = self.sides([start, target, wall], 'normal_step')
        self.assertIn((('Z', 'GE'),), open_sides)
        self.assertEqual(flush, open_sides - {(('Z', 'GE'),)})

    def test_a_departed_block_gets_no_feet_case_and_no_reachability_query(self):
        from parkour_solver.movement.ceiling_generation import open_ceiling_choices
        from parkour_solver.movement.program import boundary_state, empty_program
        from parkour_solver.movement.stepping import open_stepping_choices

        class Gate:
            def __init__(self):
                self.owners = []

            def query(self, context, bounds, tick, *, label, jumping, phases):
                self.owners.append(label['owner'])
                return dict(ruledOut=False)

            def walk_off(self, context, landing_boxes, *, label):
                self.owners.append(label['owner'])
                return True

        def run(name, block, reject):
            scene = prepare_scene(fixture(name)['request'])
            program = empty_program(scene['initial'], threshold=0.)
            gate = Gate()
            context = dict(mode='open', scene=scene, program=program, state=boundary_state(program), maxInputs=20,
                           threshold=0., interactionGate=gate)
            if reject:
                context['feetAllowed'] = lambda b: b['id'] != block
            solid = next(scene['collisionScene']['solids'][b['solidIndex']] for b in scene['blocks'] if b['id'] == block)
            touched = {event['case'] for choice in open_stepping_choices(context)
                       for event in choice['profile'].get('caseEvents', [])
                       if solid in event.get('boxes', []) + [event.get('lowerBox')]}
            return touched, gate.owners

        for name, block, cases in [
                ('blip', 'goal', {'normal_step', 'slab_boost', 'corner_jump_cancel', 'blip', 'corner_blip_up'}),
                ('blip_up', 'higher', {'blip_up'}), ('blip_up', 'goal', {'blip_up'}),
                ('corner_blip_up', 'higher', {'blip', 'corner_blip_up'})]:
            self.assertEqual(run(name, block, False)[0], cases)
            touched, owners = run(name, block, True)
            self.assertEqual(touched, set())
            self.assertNotIn(block, owners)
        scene = prepare_scene(fixture('grind')['request'])
        program = empty_program(scene['initial'], threshold=0.)
        grinds = list(open_ceiling_choices(dict(mode='open', scene=scene, program=program, state=boundary_state(program),
                                                maxInputs=20, threshold=0., feetAllowed=lambda block: False,
                                                interactionGate=None)))
        self.assertEqual({e['case'] for c in grinds for e in c['profile']['caseEvents']}, {'grind'})

    def test_a_flush_seam_is_not_a_corner(self):
        start, target, wall = [-2, 0, -2, 2, 1, 4], [2, 0, 0, 3, 1.5, 2], [2, 0, 2, 3, 1.5, 3]
        open_corners = self.sides([start, target], 'corner_jump_cancel')
        seam = self.sides([start, target, wall], 'corner_jump_cancel')
        self.assertTrue(seam < open_corners)
        self.assertFalse(any(('Z', 'GE') in rows for rows in seam))
