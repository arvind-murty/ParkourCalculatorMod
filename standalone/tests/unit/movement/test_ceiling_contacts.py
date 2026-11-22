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


FIXTURE = AUTONOMY_DATA / 'fixtures/ceiling-bonk.json'


def underside(box):
    option, = ceiling_options([box])
    return option


class OpenCeilingTests(unittest.TestCase):
    def choices(self, solids, **context):
        from parkour_solver.movement.ceiling_generation import open_ceiling_choices
        scene = dict(collisionScene=dict(solids=solids), ceilingHits=True,
                     blocks=[dict(id='c' + str(i), solidIndex=i) for i in range(len(solids))])
        state = dict(position=1., velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)
        return [c for c in open_ceiling_choices(dict(dict(scene=scene, state=state, maxInputs=20, threshold=0., interactionGate=None), **context))
                if c['profile']['contactEvents']]

    def test_an_ordinary_decision_offers_nothing_behind_it_and_no_grind_entered_against_its_leg(self):
        from parkour_solver.movement.ceiling_generation import open_ceiling_choices
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
        ahead = dict(point=[.5, .5], direction=(0., 1.), margin=.3)
        self.assertEqual(offered(forward=ahead), ['ceiling', 'grind X+', 'grind X-', 'grind Z-'])
        # The ceiling's player-centre region ends at z 1.3, 0.7 behind a decision point at z 2.
        self.assertEqual(offered(forward=dict(ahead, point=[.5, 2.])), [])
        self.assertEqual(offered(forward=dict(ahead, point=[.5, 1.5])), ['ceiling', 'grind X+', 'grind X-', 'grind Z-'])

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

