import copy
import unittest

from standalone.autonomy.movement_composition import append_choice, boundary_state, empty_program
from standalone.autonomy.movement_generation import generate_choices
from standalone.autonomy.open_movement import event_choice, named_choice
from standalone.ipopt.ladder import REST_CARRY, f32


def initial(y=1):
    return dict(position=y, velocity=REST_CARRY, onGround=True, previousHorizontalCollision=False)


class GenericCompositionTests(unittest.TestCase):
    def test_repeated_supported_units_do_not_stop_replay_at_first_landing(self):
        program = empty_program(initial())
        for t in range(7):
            context = dict(state=boundary_state(program))
            choice = event_choice(context, 'normal', context['state'], 1,
                vertical=[dict(state=1, plane=1, direction=-1)], support='floor')
            program = append_choice(program, choice)
            self.assertEqual(program['profile']['airtime'], t + 1)
        self.assertEqual(program['profile']['position'], [1] * 8)
        self.assertEqual(program['profile']['grounded'], [True] * 8)
        self.assertEqual(program['profile']['velocity'], [REST_CARRY] * 8)

    def test_unbounded_repeated_event_types_share_the_same_append_contract(self):
        program = empty_program(initial())
        for index in range(5):
            context = dict(state=boundary_state(program))
            y = context['state']['position']
            choice = named_choice(context, 'stepping', context['state'],
                dict(case='normal_step', input=0, plane=y + .5, box=[0, y, 0, 1, y + .5, 1], anchors={'hit': 1}), support=str(index))
            program = append_choice(program, choice)
        self.assertEqual(program['profile']['position'], [1, 1.5, 2, 2.5, 3, 3.5])
        self.assertEqual([u['type'] for u in program['movementUnits']], ['stepping'] * 5)
        self.assertEqual([e['anchors']['hit'] for e in program['profile']['caseEvents']], [1, 2, 3, 4, 5])
        self.assertEqual([c['state'] for c in program['profile']['contacts']], [1, 2, 3, 4, 5])

    def test_grind_can_end_in_air_then_choose_another_movement_type(self):
        program = empty_program(initial())
        context = dict(state=boundary_state(program))
        choice = named_choice(context, 'ceiling', context['state'],
            dict(case='grind', input=0, plane=1, clearance=.1, box=[1, 2.9, 0, 2, 4, 1], anchors={'jump': 1}), jumping=True)
        program = append_choice(program, choice)
        self.assertFalse(boundary_state(program)['onGround'])
        self.assertIsNone(program['terminalSupport'])
        context = dict(state=boundary_state(program))
        program = append_choice(program, event_choice(context, 'normal', context['state'], 1,
            vertical=[dict(state=1, plane=1, direction=-1)], support='floor'))
        context = dict(state=boundary_state(program))
        program = append_choice(program, event_choice(context, 'normal', context['state'], 1, jumping=True))
        self.assertAlmostEqual(program['profile']['position'][-1], 1 + f32(.42))
        self.assertEqual(program['profile']['jump'], [True, False, True])
        self.assertEqual([u['type'] for u in program['movementUnits']], ['ceiling', 'normal', 'normal'])

    def test_wrong_boundary_and_horizon_are_rejected_without_mutating_prefix(self):
        program = empty_program(initial())
        saved = copy.deepcopy(program)
        context = dict(state=initial())
        choice = event_choice(context, 'normal', initial(), 1, jumping=True)
        bad = copy.deepcopy(choice)
        bad['initial']['velocity'] = 1
        with self.assertRaises(ValueError):
            append_choice(program, bad)
        self.assertIsNone(append_choice(program, choice, max_inputs=0))
        self.assertEqual(program, saved)
