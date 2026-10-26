import random
import unittest

from standalone.autonomy.screen import contradiction
from standalone.ipopt import prepare, validate


class ScreenTests(unittest.TestCase):
    def test_unreachable_wall_is_rejected(self):
        p = {'ticks': 1, 'position': [0, 0], 'velocity': [0, 0], 'magnitude': [.1],
             'friction': [.91], 'walls': [{'mode': 'X', 't1': 1, 'cmp': 'GE', 'rhs': .2}]}
        self.assertEqual(contradiction(p)['state'], 1)

    def test_per_axis_zeroing_and_turns_do_not_reject_a_known_witness(self):
        rng = random.Random(163)
        for _ in range(150):
            n = rng.randint(2, 25)
            p = prepare({'version': 1, 'ticks': n, 'position': [rng.uniform(-100, 100) for _ in range(2)],
                         'velocity': [rng.uniform(-.2, .2) for _ in range(2)],
                         'magnitude': [rng.uniform(.001, .33) for _ in range(n)],
                         'friction': [rng.uniform(.5, .95) for _ in range(n)],
                         'base': [rng.uniform(-3, 3) for _ in range(n)],
                         'yaw': [rng.uniform(-180, 180) for _ in range(n)], 'threshold': .005,
                         'walls': [], 'objective': {'axis': 'X', 'sense': 'MAX', 'tick': n}})
            witness = validate(p, p['yaw'])
            for t, point in enumerate(witness['positions']):
                for axis, coordinate in zip(('X', 'Z'), point):
                    p['walls'].extend([{'mode': axis, 't1': t, 'cmp': 'GE', 'rhs': coordinate - 1e-5},
                                       {'mode': axis, 't1': t, 'cmp': 'LE', 'rhs': coordinate + 1e-5}])
            self.assertIsNone(contradiction(p))


if __name__ == '__main__':
    unittest.main()
