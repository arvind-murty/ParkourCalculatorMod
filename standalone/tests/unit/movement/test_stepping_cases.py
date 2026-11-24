from tests.support.stepping_cases import case_programs, fixture, fixture_names, fixture_program
from tests.support.paths import AUTONOMY_DATA
import copy
import itertools
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
from parkour_solver.movement.program import compile_program
from parkour_solver.numerics import prepare
from parkour_solver.physics.vertical import REST_CARRY, f32, vertical_step


def solve(scene, program, compiler, seconds=10):
    compiled = compile_program(scene, program)
    return compiled, solve_contact_branches(compiled['route'], compiled['profiles'],
        floor_checks(scene, [scene['startBlock'], scene['goalBlock']], compiled['profiles']), compiler,
        seconds=seconds, candidate_seconds=1, retries=0, geometry=scene['collisionScene'],
        motion_bound=dict(movementAfterSupportCheck=.8, storedSpeedNorm=.6),
        python_executable=os.environ['PKC_IPOPT_PYTHON'])


class SteppingCaseTests(unittest.TestCase):
    def test_scalar_named_outcomes_and_rejected_controls(self):
        cases = [
            ('normal_step', 1, REST_CARRY, True, None, 1.5, {}, 1.5, True),
            ('slab_boost', 1, REST_CARRY, True, f32(.42), 1.5, {}, 1.5, False),
            ('blip', 1.2, -.4, False, None, 1.5, {'lowerHeight': 1, 'higherHeight': 1.5}, 1.5, True),
            ('blip_up', 1.6, -.8, False, None, 1.6, {'lowerHeight': 1, 'higherHeight': 1.5}, 1.6, True),
            ('corner_blip', 1.2, -.4, False, None, 1.2, {'lowerHeight': 1, 'higherHeight': 1.5}, 1.2, True),
            ('corner_blip_up', 1.2, -.4, False, None, 1.2, {'lowerHeight': 1, 'higherHeight': 1.125}, 1.2, True),
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
            ('corner_blip', 1.2, -.4, 1.2, {'lowerHeight': 1, 'higherHeight': 1.9}),
            ('corner_blip_up', 1.2, -.4, 1.2, {'lowerHeight': 1, 'higherHeight': 1.2})]:
            jump = f32(.42) if kind in ('grind', 'corner_jump_cancel') else None
            config = dict(threshold=.005, jumpVelocity=[jump], sneak=[False],
                          contacts=[dict(state=1, direction=-1, plane=plane, case=kind, **metadata)])
            self.assertTrue(vertical_step(config, 0, y, vy, False, jump is not None, 0)[3], kind)

    def test_a_profile_without_landing_phase_compiles_with_pre_landing_timing(self):
        scene, _, program = fixture_program('blip')
        profile = {k: v for k, v in program['profile'].items() if k != 'landingPhase'}
        compiled = compile_program(scene, dict(program, profile=profile))
        self.assertEqual(compiled['route']['landingPhases'], ['pre'])
        self.assertIn((profile['duration'] - 1, 1, 'landing'), schedule(compiled['route'], [0])['supportChecks'])

    def test_generated_units_retime_all_case_controls_constraints_and_scene_contacts(self):
        for name in fixture_names():
            data = fixture(name)
            scene, _, program = fixture_program(name)
            compiled = compile_program(scene, program)
            route = compiled['route']
            route.update(maxTotalRunTicks=2, runBounds=[[0, 2]])
            route = prepare_route(route)
            a, b = schedule(route, [0]), schedule(route, [2])
            profile = program['profile']
            self.assertEqual(a['ticks'], profile['duration'])
            self.assertEqual(b['grounded'][2:], profile['grounded'][:-1])
            self.assertEqual(b['jump'][2:], profile['jump'])
            self.assertEqual([shifted_unit(u, 2) for u in a['movementUnits']], b['movementUnits'][1:])
            self.assertEqual([dict(c, state=c['state'] + 2) for c in a['sceneContacts']], b['sceneContacts'])
            self.assertTrue(validate_vertical_schedule(route, compiled['profiles'], b)['feasible'], name)
            partial = schedule(route, [2], stage='run')
            self.assertEqual(partial['ticks'], 2)
            self.assertTrue(validate_vertical_schedule(route, compiled['profiles'], partial)['feasible'], name)
            typed = [u for u in a['movementUnits'] if u['type'] in ('stepping', 'ceiling')]
            self.assertEqual([u['case'] for u in typed], [data['case']])
            for row in profile.get('positionConstraints', []):
                self.assertIn(dict(row, t1=row['t1'] + 2), b['walls'])
            broken = copy.deepcopy(route)
            broken['verticalProfiles'][0]['generatedUnits'][0]['jumpInputs'] = [199]
            with self.assertRaises(ValueError):
                prepare_route(broken)

    def test_elevated_ground_hands_off_to_jump_or_ground_then_air(self):
        for name in ['blip_up', 'blip_up-jump', 'corner_blip', 'corner_blip_up']:
            _, _, program = fixture_program(name)
            p = program['profile']
            hit = next(e['anchors']['hit'] for e in p['caseEvents'] if e['case'] in ('blip_up', 'corner_blip', 'corner_blip_up'))
            self.assertEqual(p['position'][hit], p['position'][hit - 1])
            self.assertTrue(p['grounded'][hit])
            self.assertEqual(p['velocity'][hit], REST_CARRY)
            self.assertTrue(p['jump'][hit] if name.endswith('jump') else not p['jump'][hit])
            self.assertFalse(p['grounded'][hit + 1])
            self.assertAlmostEqual(p['position'][hit + 1] - p['position'][hit],
                                   f32(.42) if name.endswith('jump') else REST_CARRY)
            self.assertEqual(program['movementUnits'][-1]['type'], 'normal')

    def test_grind_and_corner_cancel_share_y_without_claiming_a_wall_hit(self):
        programs = [fixture_program(name)[2] for name in ['grind', 'corner_jump_cancel']]
        self.assertEqual(programs[0]['profile']['position'], programs[1]['profile']['position'])
        for p in programs:
            self.assertEqual(p['profile']['position'], [1, 1, 1])
            self.assertEqual(p['profile']['grounded'], [True, False, True])
            self.assertFalse(any(e['axis'] != 'Y' for e in p['profile'].get('contactEvents', [])))
        self.assertEqual(programs[0]['movementUnits'][0]['type'], 'ceiling')
        self.assertEqual(programs[1]['movementUnits'][0]['type'], 'stepping')

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

@unittest.skipUnless(os.environ.get('PKC_IPOPT_PYTHON'),
                     'Set PKC_IPOPT_PYTHON')
class SteppingNumericalTests(unittest.TestCase):
    def test_fresh_original_state_solves_for_every_case(self):
        with PythonCompiler() as compiler:
            for name in fixture_names():
                with self.subTest(fixture=name):
                    data = fixture(name)
                    scene, _, program = fixture_program(name)
                    _, result = solve(scene, program, compiler)
                    self.assertEqual(result['status'], 'feasible', result)
                    best = result['best']
                    self.assertTrue(best['geometryCheck']['feasible'])
                    self.assertEqual(best['plan']['ticks'], program['profile']['duration'])
                    vertical = dict(vertical=best['vertical'], **{k: best['candidate'][k] for k in ('y', 'vy', 'onGround')})
                    missing = copy.deepcopy(best['plan'])
                    missing['sceneContacts'] = [c for c in missing['sceneContacts'] if not c.get('movementCase')]
                    self.assertFalse(check_geometry(scene['collisionScene'], missing, best['candidate'], vertical, compiler, 3)['feasible'])
                    if data['case'] in ('grind', 'corner_jump_cancel'):
                        self.assertFalse(any(best['candidate']['movement']['horizontalCollision']))
                    if data['case'] in ('blip_up', 'corner_blip', 'corner_blip_up'):
                        hit = next(e['anchors']['hit'] for e in program['profile']['caseEvents'] if e['case'] == data['case'])
                        self.assertEqual(best['candidate']['y'][hit], best['candidate']['y'][hit - 1])
                        self.assertTrue(best['candidate']['onGround'][hit])

    def test_z_facing_corner_controls_do_not_inherit_x_facing_vertical_cancellation(self):
        def swap_box(box):
            return [box[2], box[1], box[0], box[5], box[4], box[3]]
        with PythonCompiler() as compiler:
            for name in ['corner_jump_cancel', 'corner_blip', 'corner_blip_up']:
                scene, _, program = fixture_program(name)
                _, result = solve(scene, program, compiler, seconds=8)
                self.assertEqual(result['status'], 'feasible', result)
                best = result['best']
                candidate = copy.deepcopy(best['candidate'])
                for key in ('positions', 'velocity', 'rawMovement'):
                    candidate[key] = [row[::-1] for row in candidate[key]]
                plan = copy.deepcopy(best['plan'])
                for contact in plan['sceneContacts']:
                    contact['boxes'] = [swap_box(box) for box in contact['boxes']]
                    if contact['axis'] in (0, 2):
                        contact['axis'] = 2 - contact['axis']
                geometry = copy.deepcopy(scene['collisionScene'])
                geometry['solids'] = [swap_box(box) for box in geometry['solids']]
                geometry['bounds'] = swap_box(geometry['bounds'])
                vertical = dict(vertical=best['vertical'], **{k: best['candidate'][k] for k in ('y', 'vy', 'onGround')})
                self.assertFalse(check_geometry(geometry, plan, candidate, vertical, compiler, 3)['feasible'])

    def test_normal_step_supports_both_horizontal_axes(self):
        data = fixture('normal_step')
        request = data['request']
        request['initial']['position'] = request['initial']['position'][::-1]
        request['initial']['velocity'] = request['initial']['velocity'][::-1]
        for block in request['blocks']:
            block['boxes'] = [[b[2], b[1], b[0], b[5], b[4], b[3]] for b in block['boxes']]
        scene, found = case_programs(data)
        with PythonCompiler() as compiler:
            _, result = solve(scene, found[0][1], compiler)
        self.assertEqual(result['status'], 'feasible', result)
        self.assertEqual(result['best']['plan']['ticks'], 1)
        self.assertTrue(result['best']['geometryCheck']['feasible'])

    def test_grind_continues_through_normal_air_to_a_lower_floor(self):
        data = fixture('grind')
        request = data['request']
        request['blocks'][0]['boxes'][0][3] = .55
        request['blocks'][-1]['boxes'][0][4] = .5
        scene, found = case_programs(data)
        with PythonCompiler() as compiler:
            _, result = solve(scene, found[0][1], compiler, seconds=8)
        self.assertEqual(result['status'], 'feasible', result)
        self.assertTrue(result['best']['geometryCheck']['feasible'])
        self.assertEqual(result['best']['candidate']['y'][:2], [1, 1])
        self.assertEqual(result['best']['candidate']['y'][-1], .5)
        self.assertGreater(result['best']['plan']['ticks'], 2)

    def test_open_generation_covers_the_catalogue_and_allows_composite_height_gain(self):
        for name in ['blip', 'blip_up', 'corner_blip', 'corner_blip_up', 'corner_jump_cancel', 'grind']:
            self.assertTrue(case_programs(fixture(name))[1], name)
        data = fixture('blip_up')
        data['request']['blocks'] = [dict(id='start', boxes=[[-2, 1, -1, 1, 2, 1]]),
            dict(id='lower', boxes=[[2, 2, -1, 3, 2.5, 1]]),
            dict(id='higher', boxes=[[3, 2.5, -1, 3.125, 2.625, 1]]),
            dict(id='goal', boxes=[[5, 2, -1, 6, 3.5, 1]])]
        scene = prepare_scene(data['request'])
        from parkour_solver.movement.program import boundary_state, empty_program
        root = empty_program(scene['initial'], threshold=0.)
        cases = {e['case'] for c in generate_choices('stepping', dict(mode='open', scene=scene, program=root,
                 state=boundary_state(root), maxInputs=40, threshold=0., interactionGate=None))
                 for e in c['profile'].get('caseEvents', [])}
        self.assertIn('blip_up', cases)

    def test_open_generation_offers_steps_and_jumps_onto_a_step_up(self):
        data = fixture('normal_step')
        scene = prepare_scene(data['request'])
        from parkour_solver.movement.program import boundary_state, empty_program
        root = empty_program(scene['initial'], threshold=0.)
        context = dict(mode='open', scene=scene, program=root, state=boundary_state(root), maxInputs=40, threshold=0.,
                       interactionGate=None)
        cases = {e['case'] for c in generate_choices('stepping', context) for e in c['profile'].get('caseEvents', [])}
        self.assertTrue({'normal_step', 'slab_boost'} <= cases, cases)
        self.assertTrue(any(c.get('terminalSupport') == 'goal' and c['profile']['jump'][0]
                            for c in generate_choices('normal', context)))
        with PythonCompiler() as compiler:
            _, result = solve(scene, fixture_program('normal_step')[2], compiler)
        self.assertEqual(result['status'], 'feasible', result)
        self.assertEqual(result['best']['plan']['ticks'], 1)


class ExposureTests(unittest.TestCase):
    def sides(self, solids, case, **context):
        from parkour_solver.movement.stepping import open_stepping_choices
        from parkour_solver.physics.vertical import REST_CARRY
        scene = dict(collisionScene=dict(solids=solids),
                     blocks=[dict(id='b' + str(i), sourceBlock='b' + str(i), solidIndex=i, top=b[4]) for i, b in enumerate(solids)])
        state = dict(position=1., velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)
        found = []
        for choice in open_stepping_choices(dict(dict(scene=scene, state=state, maxInputs=10, threshold=0., cases=[case],
                                                      interactionGate=None, program=dict(profile=dict(duration=0))), **context)):
            event, = choice['profile']['caseEvents']
            if solids[1] in event['boxes']:
                found.append(tuple(sorted((r['mode'], r['cmp'], round(r['rhs'], 6)) for r in choice['profile']['positionConstraints']
                                          if r['name'] in ('case:outside', 'case:cross_x'))))
        return found

    def test_a_step_or_slab_boost_takes_off_from_outside_a_low_ceiling(self):
        from parkour_solver.movement.stepping import open_stepping_choices
        from parkour_solver.physics.vertical import REST_CARRY
        from parkour_solver.scene.static_collision import StaticScene
        start, target = [-2, 0, -2, 2, 1, 4], [2, 0, 0, 3, 1.5, 2]
        def clears(solids, case):
            scene = dict(collisionScene=dict(solids=solids), blocks=[dict(id='b' + str(i), sourceBlock='b' + str(i),
                         solidIndex=i, top=b[4]) for i, b in enumerate(solids)])
            state = dict(position=1., velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)
            return [sorted((r['mode'], r['cmp'], round(r['rhs'], 6)) for r in c['profile']['positionConstraints']
                           if r['name'].startswith('ceiling_clear:'))
                    for c in open_stepping_choices(dict(scene=scene, state=state, maxInputs=10, threshold=0., cases=[case],
                                                        interactionGate=None, program=dict(profile=dict(duration=0))))]
        # The ceiling's underside at 3 leaves feet 1.2 of room, below the 1.5 top.
        low = [1, 3, 0, 2, 4, 1]
        sides = [[('X', 'GE', 2.3001)], [('X', 'LE', .6999)], [('Z', 'GE', 1.3001)], [('Z', 'LE', -.3001)]]
        for case in ('normal_step', 'slab_boost', 'corner_jump_cancel'):
            found = clears([start, target, low], case)
            self.assertTrue(found, case)
            # Every choice keeps to one side of the ceiling; the sides beside the target's approach are offered.
            self.assertTrue(all(rows in sides for rows in found), case)
            self.assertLessEqual({tuple(sides[1]), tuple(sides[2])}, {tuple(rows) for rows in found}, case)
            # An underside at 3.3 leaves room for the 1.5 top: no clearance.
            self.assertEqual({tuple(rows) for rows in clears([start, target, [1, 3.3, 0, 2, 4, 1]], case)}, {()}, case)
        # Minecraft's step rises from the take-off box: under the ceiling a slab boost stops short of the top and a corner
        # jump cancel stays blocked at the X face; beside the ceiling both work.
        scene = StaticScene(dict(version=1, profile='minecraft-1.8.9', bounds=[-5, -1, -5, 6, 6, 6], solids=[start, target, low]))
        for z, top in ((.5, False), (1.5, True)):
            move = scene.move(scene.start_box((1.6, 1., z)), (.3, .42, 0.), True, False)
            self.assertEqual(move.position[1] == 1.5, top, z)
        corner = StaticScene(dict(version=1, profile='minecraft-1.8.9', bounds=[-5, -1, -5, 6, 6, 6],
                                  solids=[start, [2, 0, 0, 3, 1.5, 1], [1, 3, 0, 2, 4, 2]]))
        cleared = StaticScene(dict(version=1, profile='minecraft-1.8.9', bounds=[-5, -1, -5, 6, 6, 6],
                                   solids=[start, [2, 0, 0, 3, 1.5, 1], [1, 3.3, 0, 2, 4, 2]]))
        for scene, cancelled in ((corner, False), (cleared, True)):
            move = scene.move(scene.start_box((1.6, 1., 1.1)), (.3, .42, .25), True, False)
            self.assertEqual((move.stepped, move.position[1] == 1.), (cancelled, cancelled))

    def test_a_side_flush_against_a_neighbour_offers_no_step(self):
        # wall is flush with target's Z+ side at its height: one merged face, whose seam is no side.
        start, target, wall = [-2, 0, -2, 2, 1, 4], [2, 0, 0, 3, 1.5, 2], [2, 0, 2, 3, 1.5, 3]
        open_sides = self.sides([start, target], 'normal_step')
        flush = self.sides([start, target, wall], 'normal_step')
        self.assertIn((('Z', 'GE', 2.3001),), open_sides)
        # The merged face's sides once each: target's X sides stand for wall's too, and its Z+ side is wall's far one.
        self.assertEqual(sorted(flush), sorted([s for s in open_sides if s != (('Z', 'GE', 2.3001),)]+[(('Z', 'GE', 3.3001),)]))

    def test_an_ordinary_decision_steps_and_corners_in_only_by_allowed_moves(self):
        from parkour_solver.spatial.anchor_guides import leg_class
        start, target = [-2, 0, -2, 2, 1, 4], [2, 0, 0, 3, 1.5, 2]
        plus_x = dict(point=[0., 1.], allowed=leg_class((1., 0.))[1], margin=.3)
        diagonal = dict(plus_x, allowed=leg_class((.6, .8))[1])
        for case in ('normal_step', 'slab_boost'):
            steps = self.sides([start, target], case)
            self.assertEqual(len(steps), 4, case)
            # +X: entering across the X+ side moves -X; the Z sides are sideways. +X+Z also drops the Z+ side.
            self.assertEqual(sorted(self.sides([start, target], case, forward=plus_x)),
                             sorted(s for s in steps if s != (('X', 'GE', 3.3001),)), case)
            self.assertEqual(sorted(self.sides([start, target], case, forward=diagonal)),
                             sorted(s for s in steps if s not in ((('X', 'GE', 3.3001),), (('Z', 'GE', 2.3001),))), case)
            # The target's top ends at x 3.3, 1.7 behind a decision point at x 5.
            self.assertEqual(self.sides([start, target], case, forward=dict(plus_x, point=[5., 1.])), [], case)
        corners = self.sides([start, target], 'corner_jump_cancel', forward=plus_x)
        self.assertEqual(len(corners), 2)
        self.assertTrue(all(('X', 'LE', 1.6999) in corner for corner in corners))

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

            def query_many(self, context, bounds, tick, **kwargs):
                return [self.query(context, box, tick, **kwargs) for box in bounds]

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
                context['feetAllowed'] = lambda name: name != block
            solid = next(scene['collisionScene']['solids'][b['solidIndex']] for b in scene['blocks'] if b['id'] == block)
            touched = {event['case'] for choice in open_stepping_choices(context)
                       for event in choice['profile'].get('caseEvents', [])
                       if solid in event.get('boxes', []) + event.get('lowerBoxes', [])}
            return touched, gate.owners

        for name, block, cases in [
                ('blip', 'goal', {'normal_step', 'slab_boost', 'corner_jump_cancel', 'blip', 'corner_blip'}),
                # The blip-up's higher block lands 0.1 above its top, so its exposed corners give corner blip-ups too.
                ('blip_up', 'higher', {'blip_up', 'corner_blip_up'}), ('blip_up', 'goal', {'blip_up', 'corner_blip_up'}),
                ('corner_blip', 'higher', {'blip', 'corner_blip'}),
                ('corner_blip_up', 'higher', {'blip_up', 'corner_blip_up'})]:
            self.assertEqual(run(name, block, False)[0], cases)
            touched, owners = run(name, block, True)
            self.assertEqual(touched, set())
            self.assertNotIn(block, owners)
        scene = prepare_scene(fixture('grind')['request'])
        program = empty_program(scene['initial'], threshold=0.)
        grinds = list(open_ceiling_choices(dict(mode='open', scene=scene, program=program, state=boundary_state(program),
                                                maxInputs=20, threshold=0., feetAllowed=lambda name: False,
                                                interactionGate=None)))
        self.assertEqual({e['case'] for c in grinds for e in c['profile']['caseEvents']}, {'grind'})

    def test_a_flush_seam_is_not_a_corner(self):
        # wall is flush with target's Z+ side: the merged face's corners are its outline's, none at the seam.
        start, target, wall = [-2, 0, -2, 2, 1, 4], [2, 0, 0, 3, 1.5, 2], [2, 0, 2, 3, 1.5, 3]
        open_corners = self.sides([start, target], 'corner_jump_cancel')
        seam = self.sides([start, target, wall], 'corner_jump_cancel')
        rows = lambda found: {row for corner in found for row in corner}
        self.assertEqual((len(open_corners), len(seam)), (4, 4))
        self.assertIn(('Z', 'GE', 2.3001), rows(open_corners))
        self.assertNotIn(('Z', 'GE', 2.3001), rows(seam))
        self.assertIn(('Z', 'GE', 3.3001), rows(seam))

    def test_corner_rows_span_the_merged_side(self):
        # target and wall share their X- side; the Z tangent rows at target's -X-Z corner cover both, as one side.
        target, wall = [2, 0, 0, 3, 1.5, 2], [2, 0, 2, 3, 1.5, 3]
        tangent = lambda rows: sorted((r['cmp'], round(r['rhs'], 6)) for r in rows if r['name'] == 'case:finite' and r['mode'] == 'Z')
        self.assertEqual(tangent(corner_rows(target, 0, 1, 1, -1)), [('GE', -0.2999), ('LE', 2.2999)])
        self.assertEqual(tangent(corner_rows(target, 0, 1, 1, -1, [target, wall])), [('GE', -0.2999), ('LE', 3.2999)])
        self.assertEqual(corner_rows(target, 0, 1, 1, -1, [target]), corner_rows(target, 0, 1, 1, -1))

    def test_a_grind_under_a_merged_underside_enters_each_side_once(self):
        from parkour_solver.movement.ceiling_generation import grind_options
        # Two ceiling boxes make one underside; their Z sides share planes, so each is one entry, not one per box.
        solids = [[-3, 0, -3, 6, 1, 3], [0, 2.9, -2, 2, 4, 2], [2, 2.9, -2, 4, 4, 2]]
        scene = dict(collisionScene=dict(solids=solids), blocks=[dict(id=name, sourceBlock=name, solidIndex=i, top=b[4])
                     for i, (name, b) in enumerate(zip(['floor', 'c1', 'c2'], solids))])
        rows = [(r['mode'], r['cmp'], round(r['rhs'], 6)) for _, constraints, _ in
                grind_options(dict(scene=scene, interactionGate=None), 1.) for r in constraints if r['name'] == 'grind:outside']
        self.assertEqual(sorted(rows), [('X', 'GE', 4.3001), ('X', 'LE', -0.3001), ('Z', 'GE', 2.3001), ('Z', 'LE', -2.3001)])


class SideVariantTests(unittest.TestCase):
    """Each side-variants fixture's sideVariant meta names the forward sides, the sides held behind them ('reserved',
    and 'normal' sides of a move that still has a forward) and the untagged ones ('normal' without a forward)."""
    def choices(self, name):
        import json
        from parkour_solver.movement.generation import generate_choices
        from parkour_solver.movement.program import boundary_state, empty_program
        from parkour_solver.spatial.anchor_guides import leg_class
        raw = json.loads((AUTONOMY_DATA / 'fixtures/side-variants' / (name + '.json')).read_text())
        meta, scene = raw['sideVariant'], prepare_scene(raw['request'])
        program = empty_program(scene['initial'], threshold=0.)
        point = meta.get('decisionPoint', scene['initial']['position'][::2])
        context = dict(mode='open', scene=scene, program=program, state=boundary_state(program), maxInputs=40,
                       maxEntryInputs=40, threshold=0., interactionGate=None, decision=dict(decisionPoint=point),
                       forward=dict(point=point, allowed=leg_class(meta['leg'])[1], margin=.3))
        return meta, list(generate_choices('stepping', context))

    @staticmethod
    def side(choice):
        row, = [r for r in choice['profile']['positionConstraints'] if r['name'] == 'case:outside']
        return row['mode'] + ('-' if row['cmp'] == 'LE' else '+')

    def test_every_fixture_tags_its_forward_and_held_sides(self):
        names = sorted(p.stem for p in (AUTONOMY_DATA / 'fixtures/side-variants').glob('*.json'))
        self.assertEqual(len(names), 18)
        for name in names:
            meta, choices = self.choices(name)
            for case in ('normal_step', 'slab_boost'):
                expected = dict(meta, **meta.get('byCase', {}).get(case, {}))
                found = {}
                for choice in choices:
                    if choice['profile']['caseEvents'][-1]['case'] != case:
                        continue
                    tag = choice.get('sideVariant')
                    found.setdefault(self.side(choice), set()).add(None if tag is None else tag['forward'])
                # forward-behind's forward side is dropped by O10, so it has no forward choice.
                forward = {s for s in expected['forward'] if s in found}
                held = set(expected['reserved']) | (set(expected.get('normal', [])) if forward else set())
                untagged = set() if forward else set(expected.get('normal', []))
                if len(forward | held) == 1:
                    # single-side: a move entered from one side only is not tagged.
                    forward, held, untagged = set(), set(), forward | held
                self.assertEqual(found, {**{s: {True} for s in forward}, **{s: {False} for s in held},
                                         **{s: {None} for s in untagged}}, (name, case))
            for choice in choices:
                if choice['profile']['caseEvents'][-1]['case'] not in ('normal_step', 'slab_boost'):
                    self.assertNotIn('sideVariant', choice, name)

    def test_moves_key_on_the_rows_besides_the_side(self):
        # forward-ceiling: each of the three ceiling-clearance sets is its own move with a Z- forward.
        _, choices = self.choices('forward-ceiling')
        moves = {}
        for choice in choices:
            if choice['profile']['caseEvents'][-1]['case'] == 'slab_boost':
                tag = choice['sideVariant']
                moves.setdefault(tag['key'], {})[self.side(choice)] = tag['forward']
        self.assertEqual(list(moves.values()), [{'X-': False, 'X+': False, 'Z-': True}] * 3)
        # offset-members (targetA x 0..0.5 z 4..6, targetB x 0.5..1 z 5..7, decision point (0.5, 3)): one merged face,
        # each member's exposed sides in one move, both Z- sides forward.
        _, choices = self.choices('offset-members')
        rows = {}
        for choice in choices:
            if choice['profile']['caseEvents'][-1]['case'] == 'normal_step':
                tag = choice['sideVariant']
                rows.setdefault(tag['key'], set()).add((self.side(choice), round(tag['row']['rhs'], 6), tag['forward']))
        self.assertEqual(list(rows.values()), [{('X-', -0.3001, False), ('X-', 0.1999, False), ('X+', 0.8001, False),
                                                ('X+', 1.3001, False), ('Z-', 3.6999, True), ('Z-', 4.6999, True)}])
