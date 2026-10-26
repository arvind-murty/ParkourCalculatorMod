import gzip
import json
import math
import os
import pathlib
import unittest

from standalone.ipopt.model import prepare, validate
from standalone.ipopt.solver import solve
from standalone.ipopt.tests.test_model import problem


RUNTIME = os.environ.get('PKC_IPOPT_PYTHON')


@unittest.skipUnless(RUNTIME, 'Set PKC_IPOPT_PYTHON to run CasADi/IPOPT integration tests')
class IntegrationTests(unittest.TestCase):
    def test_known_optimum_with_relative_position_and_facing_rows(self):
        request = problem(4)
        request.update(velocity=[0, 0], magnitude=[0.1] * 4, threshold=0.005, yaw=[-90] * 4)
        request['walls'] = [{'mode': 'F', 't1': 0, 'cmp': 'EQ', 'rhs': -90},
                            {'mode': 'F', 't1': 1, 't2': 0, 'op': 'MINUS', 'cmp': 'EQ', 'rhs': 0},
                            {'mode': 'Z', 't1': 4, 't2': 0, 'op': 'MINUS', 'cmp': 'EQ', 'rhs': 0}]
        result = solve(request, seconds=5, python_executable=RUNTIME)
        self.assertTrue(result['feasible'], result)
        self.assertAlmostEqual(result['candidate']['objective'], 0.6125, places=6)

    def test_real11_200_and_400_with_all_rows_and_contacts(self):
        for ticks, rows, contacts in [(200, 197, 21), (400, 341, 34)]:
            with self.subTest(ticks=ticks):
                path = pathlib.Path(__file__).parents[1] / 'fixtures' / ('real11-' + str(ticks) + '.json.gz')
                with gzip.open(path, 'rt') as stream:
                    request = json.load(stream)
                result = solve(request, seconds=10, python_executable=RUNTIME)
                self.assertTrue(result['feasible'], result)
                self.assertEqual(result['problem']['constraints'], rows)
                self.assertEqual(result['problem']['contacts'], contacts)
                candidate = validate(prepare(request), result['candidate']['yaw'])
                self.assertTrue(candidate['feasible'])
                self.assertEqual(candidate['missedContacts'], [])
                self.assertEqual(candidate['patternMismatches'], [])
                self.assertLessEqual(abs(candidate['yaw'][187] - 125.80603), 0.0001001)
                self.assertTrue(all(math.isfinite(v) for p in candidate['positions'] for v in p))
                print('real11', ticks, 'seconds', round(result['elapsedSeconds'], 3),
                      'objective', candidate['objective'], 'violation', candidate['violation'])

    def test_inconsistent_rows_return_unknown(self):
        request = problem(1)
        request['walls'] = [{'mode': 'X', 't1': 1, 'cmp': 'GE', 'rhs': 2},
                            {'mode': 'X', 't1': 1, 'cmp': 'LE', 'rhs': -2}]
        result = solve(request, seconds=1, python_executable=RUNTIME)
        self.assertEqual(result['status'], 'unknown')
        self.assertIsNone(result['candidate'])

    def test_explicit_fixed_inertia_pattern(self):
        request = problem()
        request.update(velocity=[0.004, 0.2], threshold=0.005, inertiaPattern=[[0, 1], [0, 1]])
        result = solve(request, seconds=3, python_executable=RUNTIME)
        self.assertTrue(result['feasible'], result)
        self.assertEqual(result['candidate']['patternMismatches'], [])
        self.assertEqual(result['candidate']['positions'][2][0], 0)

    def test_real11_warm_start_recovers_objective_and_polishes_valid_seed(self):
        directory = pathlib.Path(__file__).parents[1] / 'fixtures'
        with gzip.open(directory / 'real11-400.json.gz', 'rt') as stream:
            original = json.load(stream)
        with gzip.open(directory / 'real11-400-warm-yaws.json.gz', 'rt') as stream:
            seeds = json.load(stream)
        for key, minimum, fallback in [('noInertia', 691.4017, False),
                                       ('noInertia', 691.4017, True),
                                       ('inertiaIncumbent', 691.3929, False)]:
            with self.subTest(seed=key, fallback=fallback):
                request = dict(original, yaw=seeds[key])
                if fallback:
                    request['fallbackYaw'] = original['yaw']
                result = solve(request, seconds=10, python_executable=RUNTIME)
                self.assertTrue(result['feasible'], result)
                self.assertGreaterEqual(result['candidate']['objective'], minimum)
                replay = validate(prepare(request), result['candidate']['yaw'])
                self.assertTrue(replay['feasible'])
                self.assertEqual(replay['positions'], result['candidate']['positions'])
                self.assertEqual(replay['velocity'], result['candidate']['velocity'])
                self.assertIn('incumbent_polish' if key == 'inertiaIncumbent' else 'zero_events',
                              [p['kind'] for p in result['phases']])
                print('real11 warm', key, 'fallback', fallback, 'seconds', round(result['elapsedSeconds'], 3),
                      'objective', replay['objective'], 'violation', replay['violation'])


if __name__ == '__main__':
    unittest.main()
