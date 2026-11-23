from tests.support.paths import AUTONOMY_DATA
import copy
import json
import os
import unittest

from parkour_solver.validation.vertical import validate_vertical_schedule
from parkour_solver.movement.profiles import ordinary_jump_profile
from parkour_solver.scene.normalize import prepare_scene
from parkour_solver.movement.ceiling_profiles import ceiling_options
from parkour_solver.bridge.python_compiler import PythonCompiler
from parkour_solver.scene.route import schedule
from parkour_solver.physics.vertical import REST_CARRY, f32
from tests.support.checkpoint_guidance import landing_programs


FIXTURE = AUTONOMY_DATA / 'fixtures/ceiling-bonk.json'


def underside(box):
    option, = ceiling_options([box])
    return option


def ceiling_scene(bottom):
    """A wide start block (top 1) under a ceiling box with the given bottom; the goal is out of one jump's reach, so a
    hit lands back on the start."""
    return prepare_scene(dict(version=1, sceneModel='collision_boxes', geometryBounds=[-5, -5, -5, 20, 10, 10],
        initial=dict(position=[.5, 1, .5], velocity=[0, -.0784000015258789, 0]), startBlock='start', goalBlock='goal',
        blocks=[dict(id='start', boxes=[[-2, 0, -2, 5, 1, 3]]), dict(id='ceiling', boxes=[[-2, bottom, -2, 5, bottom + 1, 3]]),
                dict(id='goal', boxes=[[15, 0, 0, 16, 1, 1]])]))


class CeilingProfileTests(unittest.TestCase):
    def test_existing_y_model_regenerates_single_hit_and_shorter_landing(self):
        for bottom in [3, 3.4, 3.9]:
            with self.subTest(bottom=bottom):
                scene = ceiling_scene(bottom)
                hits = [p for p in landing_programs(scene, 'ceiling', goal='start')
                        if any(e.get('role') == 'ceiling' for e in p['profile']['contactEvents'])]
                self.assertTrue(hits)
                profile = hits[0]['profile']
                hit = next(e['offset'] for e in profile['contactEvents'] if e.get('role') == 'ceiling')
                self.assertLess(profile['duration'], ordinary_jump_profile(1, 1)['duration'])
                self.assertEqual(profile['velocity'][hit + 1], REST_CARRY)
                self.assertLess(profile['position'][hit + 2], profile['position'][hit + 1])
                self.assertEqual(sum(c['direction'] > 0 for c in profile['contacts']), 1)
                plane = bottom - f32(1.8)
                self.assertTrue(all(y <= plane for y in profile['position'][:hit + 1]))
                self.assertTrue(all(y < plane for y in profile['position'][hit + 2:]))
        self.assertFalse(any(any(e.get('role') == 'ceiling' for e in p['profile']['contactEvents'])
                             for p in landing_programs(ceiling_scene(5), 'ceiling', goal='start')))


@unittest.skipUnless(os.environ.get('PKC_IPOPT_PYTHON'),
                     'Set PKC_IPOPT_PYTHON')
class CeilingContactIntegrationTests(unittest.TestCase):
    def test_geometry_discovers_bonk_and_the_hit_route_solves(self):
        from parkour_solver.constraints.contacts import floor_checks
        from parkour_solver.movement.program import compile_program
        from parkour_solver.scene.route import prepare_route
        from parkour_solver.search.repair import solve_contact_branches
        from parkour_solver.validation.scene import check_geometry
        scene = prepare_scene(json.loads(FIXTURE.read_text()))
        self.assertTrue(any(p['profile']['jump'][0] for p in landing_programs(scene, 'normal')))
        bonk = next(p for p in landing_programs(scene, 'ceiling')
                    if any(e.get('role') == 'ceiling' for e in p['profile']['contactEvents']))
        self.assertEqual(bonk['profile']['duration'], 3)
        compiled = compile_program(scene, bonk)
        with PythonCompiler() as compiler:
            result = solve_contact_branches(compiled['route'], compiled['profiles'],
                floor_checks(scene, ['start', 'goal'], compiled['profiles']), compiler, seconds=15, candidate_seconds=1,
                retries=0, geometry=scene['collisionScene'], motion_bound=dict(movementAfterSupportCheck=.8, storedSpeedNorm=.6),
                python_executable=os.environ['PKC_IPOPT_PYTHON'])
            self.assertEqual(result['status'], 'feasible', result)
            best = result['best']
            self.assertEqual(best['plan']['ticks'], 3)
            self.assertTrue(best['geometryCheck']['feasible'])
            self.assertTrue(all(best['candidate']['movement']['sprinting']))
            self.assertFalse(any(best['candidate']['movement']['horizontalCollision']))
            route = prepare_route(dict(compiled['route'], maxTotalRunTicks=2, runBounds=[[0, 2]]))
            plan = schedule(route, [2])
            self.assertEqual(min(c['state'] for c in plan['sceneContacts'] if c['axis'] == 1 and c['direction'] > 0), 3)
            self.assertTrue(validate_vertical_schedule(route, compiled['profiles'], plan)['feasible'])
            missing = copy.deepcopy(best['plan'])
            missing['sceneContacts'] = []
            vertical = {'vertical': best['vertical'], **{k: best['candidate'][k] for k in ('y', 'vy', 'onGround')}}
            self.assertFalse(check_geometry(scene['collisionScene'], missing, best['candidate'], vertical, compiler, 5)['feasible'])


class OpenCeilingTests(unittest.TestCase):
    def choices(self, solids, **context):
        from parkour_solver.movement.ceiling_generation import open_ceiling_choices
        scene = dict(collisionScene=dict(solids=solids), ceilingHits=True,
                     blocks=[dict(id='c' + str(i), solidIndex=i) for i in range(len(solids))])
        state = dict(position=1., velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)
        return [c for c in open_ceiling_choices(dict(dict(scene=scene, state=state, maxInputs=20, threshold=0., interactionGate=None), **context))
                if c['profile']['contactEvents']]

    def test_an_ordinary_decision_offers_nothing_behind_it_and_no_grind_entered_against_its_leg(self):
        import math
        from parkour_solver.movement.ceiling_generation import open_ceiling_choices
        from parkour_solver.spatial.anchor_guides import leg_class
        leg = lambda degrees: leg_class((math.cos(math.radians(degrees)), math.sin(math.radians(degrees))))[1]
        def offered(**context):
            scene = dict(collisionScene=dict(solids=[[0, 3, 0, 1, 4, 1]]), ceilingHits=True, blocks=[dict(id='c0', solidIndex=0)])
            state = dict(position=1., velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)
            kinds = []
            for c in open_ceiling_choices(dict(scene=scene, state=state, maxInputs=20, threshold=0., interactionGate=None,
                                               program=dict(profile=dict(duration=0)), **context)):
                entry = [r['mode']+('+' if r['cmp'] == 'GE' else '-') for r in c['profile']['positionConstraints']
                         if r.get('name') == 'grind:outside']
                kinds.append('grind '+entry[0] if entry else 'ceiling')
            return sorted(kinds)
        self.assertEqual(offered(), ['ceiling', 'grind X+', 'grind X-', 'grind Z+', 'grind Z-'])
        # The leg runs +Z: a grind entered from Z+ moves backwards; X entries move sideways.
        ahead = dict(point=[.5, .5], allowed=leg(90), margin=.3)
        self.assertEqual(offered(forward=ahead), ['ceiling', 'grind X+', 'grind X-', 'grind Z-'])
        # The ceiling's player-centre region ends at z 1.3, 0.7 behind a decision point at z 2.
        self.assertEqual(offered(forward=dict(ahead, point=[.5, 2.])), [])
        self.assertEqual(offered(forward=dict(ahead, point=[.5, 1.5])), ['ceiling', 'grind X+', 'grind X-', 'grind Z-'])
        # 10 degrees off +Z is still the +Z class.
        self.assertEqual(offered(forward=dict(ahead, allowed=leg(80))), ['ceiling', 'grind X+', 'grind X-', 'grind Z-'])
        # 20 degrees off +X: only -X is back, so a ceiling 1.7 blocks -Z of the decision point is sideways and stays.
        self.assertEqual(offered(forward=dict(point=[.5, 3.], allowed=leg(20), margin=.3)),
                         ['ceiling', 'grind X-', 'grind Z+', 'grind Z-'])
        # A +X+Z diagonal allows only +X and +Z: both facing entries go, and 0.5 back on Z is behind, 0.2 is not.
        diagonal = dict(point=[.5, 1.5], allowed=leg(45), margin=.3)
        self.assertEqual(offered(forward=diagonal), ['ceiling', 'grind X-', 'grind Z-'])
        self.assertEqual(offered(forward=dict(diagonal, point=[.5, 1.8])), [])

    def test_touching_coplanar_ceilings_are_one_choice_with_both_members(self):
        boxes = [[0, 3, 0, 1, 4, 1], [1, 3, 0, 2, 4, 1]]
        choice, = self.choices(boxes)
        event, = choice['profile']['contactEvents']
        self.assertEqual((event['boxes'], len(event['regions'])), (boxes, 1))
        self.assertEqual(event['blockId'], 'c0+c1')

    def test_l_shaped_ceiling_is_a_region_item_only_inside_the_walk(self):
        ceiling, pillar = [0, 3, 0, 2, 4, 2], [2, 0, 1, 3, 6, 2]
        walk, = self.choices([ceiling, pillar], regionItems=True)
        event, = walk['profile']['contactEvents']
        self.assertEqual((event['boxes'], len(event['regions'])), ([ceiling], 2))
        entry, = walk['profile']['regionChoices']
        self.assertEqual((entry['offset'], entry['owner'], entry['surface']), (event['offset'], 'c0', event['surface']))
        self.assertEqual(len(entry['options']), 2)
        self.assertTrue(all(r['offset'] == event['offset'] and r['leg'] == 0 for rows in entry['options'] for r in rows))
        legacy, = self.choices([ceiling, pillar])
        self.assertNotIn('regionChoices', legacy['profile'])
        self.assertEqual(legacy['profile']['contactEvents'], walk['profile']['contactEvents'])

