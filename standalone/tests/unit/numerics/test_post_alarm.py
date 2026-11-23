import gzip
import math
import json
import os
import pathlib
import unittest
from types import SimpleNamespace
from unittest import mock

from parkour_solver.numerics import disk, post_alarm, prepare, solver
from parkour_solver.validation.numerical import validate, validate_controls

# One alarmed request per pipeline outcome, from the post-alarm study's populations (research/post-alarm/README.md).
FIXTURE = pathlib.Path(__file__).resolve().parents[2]/'fixtures'/'post_alarm'/'alarmed-requests.json.gz'
RUNTIME = os.environ.get('PKC_IPOPT_PYTHON')
# The study's answers (research/ipopt-seeds 1a5dcb53, configuration "O2 + no iterative refinement"); this source's
# disk points differ by its contact margin, so answers agree to solver precision.
STUDY = dict(push=-385.9620712857143, probe=-385.3082456476679, race=754.6023070789445,
             prodSeed=-385.96199530132577, polish=496.51754119429324)


def alarmed(kind):
    with gzip.open(FIXTURE, 'rt') as stream:
        row = json.load(stream)['requests'][kind]
    return prepare(dict(row['problem'], objectiveMode='configured'))


def no_ipopt(request, **options):
    raise AssertionError('IPOPT was called')


class Confirming:
    def confirm(self, problem, gate=None, seconds=20.):
        dispatch = disk.RelaxedDispatch(gate=gate or SimpleNamespace(cache={}), post_alarm='pipeline')
        incumbents = []
        result = dispatch.confirmed(problem, seconds=seconds, stop_on_feasible=False, python_executable=RUNTIME,
                                    on_incumbent=incumbents.append)
        return result, incumbents

    def assert_answer(self, problem, result, kind, termination):
        confirmation = result['confirmation']
        self.assertEqual((result['status'], result['termination'], confirmation['method']),
                         ('feasible', termination, kind))
        self.assertAlmostEqual(result['candidate']['objective'], STUDY[kind], delta=1e-6)
        self.assertEqual(confirmation['ipoptObjective'], result['candidate']['objective'])
        check = validate_controls if result['candidate'].get('model') == 'relaxed_disk' else validate
        argument = result['candidate']['controls' if check is validate_controls else 'yaw']
        self.assertTrue(check(problem, argument)['feasible'])


class PipelineTests(Confirming, unittest.TestCase):
    def test_a_push_answer_is_confirmed_once_and_cached(self):
        problem = alarmed('push')
        gate = SimpleNamespace(cache={})
        with mock.patch.object(solver, 'solve', side_effect=no_ipopt):
            result, incumbents = self.confirm(problem, gate)
            again, _ = self.confirm(problem, gate)
        self.assert_answer(problem, result, 'push', 'post_alarm_push')
        self.assertEqual((result['backend'], result['confirmation']['ipoptCalls']), ('clarabel', 0))
        self.assertGreater(result['confirmation']['clarabelSolves'], 0)
        self.assertEqual(incumbents, [result['candidate']])
        self.assertEqual(again['candidate'], result['candidate'])
        self.assertEqual({k: again['confirmation'][k] for k in ('cached', 'ipoptCalls', 'clarabelSolves')},
                         dict(cached=True, ipoptCalls=0, clarabelSolves=0))

    def test_an_answer_far_below_the_bound_is_improved_by_objective_cut_probes(self):
        problem = alarmed('probe')
        with mock.patch.object(solver, 'solve', side_effect=no_ipopt):
            result, _ = self.confirm(problem)
        self.assert_answer(problem, result, 'probe', 'post_alarm_probe')
        self.assertGreaterEqual(result['confirmation']['probes'], 1)

    def test_a_sector_certificate_rejects_without_ipopt_and_is_cached(self):
        gate = SimpleNamespace(cache={})
        with mock.patch.object(solver, 'solve', side_effect=no_ipopt):
            result, incumbents = self.confirm(alarmed('quadrant'), gate)
        self.assertEqual((result['status'], result['termination'], result['candidate'], incumbents),
                         ('unknown', 'quadrant_infeasible', None, []))
        self.assertEqual((result['confirmation']['quadrant'], result['confirmation']['ipoptCalls']), ('infeasible', 0))
        self.assertEqual(len(gate.cache), 1)

    def test_a_reversal_cut_reaches_the_other_basin(self):
        problem = alarmed('race')
        with mock.patch.object(solver, 'solve', side_effect=no_ipopt):
            result, _ = self.confirm(problem)
        self.assert_answer(problem, result, 'race', 'post_alarm_race')
        self.assertEqual(result['confirmation']['reversalTick'], 81)

    def test_a_minimized_objective_never_beats_its_bound(self):
        problem = alarmed('probe')
        problem['objective']['sense'] = 'MIN'
        with mock.patch.object(solver, 'solve', side_effect=no_ipopt):
            result, _ = self.confirm(problem)
        self.assertEqual(result['status'], 'feasible')
        self.assertTrue(0 <= result['confirmation']['gap'] <= post_alarm.G)
        self.assertGreaterEqual(result['candidate']['objective'], result['disk']['objective'])

    def test_the_deadline_leaves_the_request_undecided_and_uncached(self):
        gate = SimpleNamespace(cache={})
        with mock.patch.object(solver, 'solve', side_effect=no_ipopt):
            result, _ = self.confirm(alarmed('quadrant'), gate, seconds=1e-3)
        self.assertEqual((result['status'], result['termination']), ('unknown', 'time_limit'))
        self.assertEqual(gate.cache, {})

    def test_the_deadline_keeps_the_answer_found_before_it(self):
        problem = alarmed('probe')
        with mock.patch.object(solver, 'solve', side_effect=no_ipopt), \
             mock.patch.object(post_alarm._Pipeline, 'probes', side_effect=post_alarm._Stop('time_limit')):
            result, _ = self.confirm(problem)
        self.assertEqual((result['status'], result['termination'], result['confirmation']['method']),
                         ('feasible', 'post_alarm_push', 'push'))
        self.assertLess(result['candidate']['objective'], STUDY['probe'])

    def test_post_alarm_is_ipopt_or_pipeline(self):
        with self.assertRaises(ValueError):
            disk.RelaxedDispatch(gate=SimpleNamespace(cache={}), post_alarm='race')


@unittest.skipUnless(RUNTIME, 'Set PKC_IPOPT_PYTHON to run the post-alarm IPOPT calls')
class PipelineIpoptTests(Confirming, unittest.TestCase):
    def tearDown(self):
        solver._close_workers()

    def test_the_production_seed_answers_after_the_race(self):
        problem = alarmed('prodSeed')
        result, _ = self.confirm(problem)
        self.assert_answer(problem, result, 'prodSeed', 'post_alarm_prod_seed')
        self.assertEqual((result['backend'], result['confirmation']['ipoptCalls']), ('ipopt', 1))

    def test_the_polish_starts_ipopt_from_the_push_iterate(self):
        problem = alarmed('polish')
        calls = []
        original = solver.solve
        def recorded(request, **options):
            calls.append((request, options))
            return original(request, **options)
        with mock.patch.object(solver, 'solve', side_effect=recorded):
            result, _ = self.confirm(problem)
        self.assert_answer(problem, result, 'polish', 'post_alarm_polish')
        (seed, seed_options), (polish, polish_options) = calls
        self.assertNotIn('fallbackYaw', seed)
        self.assertEqual((polish['ipoptMuInit'], len(polish['stateSeed']['positions'])),
                         (post_alarm.POLISH_MU, problem['ticks']+1))
        self.assertEqual((seed_options['seconds'], polish_options['keep_alive']), (post_alarm.IPOPT_CAP, True))
        self.assertTrue(result['workerLaunched'])


class StatesTests(unittest.TestCase):
    def test_states_follow_the_exact_dynamics_and_contacts(self):
        problem = dict(ticks=2, position=[5., 0.], velocity=[.1, 0.], friction=[.5, .5],
                       contacts=[dict(state=2, axis=0, plane=5.25, direction=1)])
        seeded = post_alarm.states(problem, [[.1, .2], [.1, 0.]])
        self.assertEqual(seeded['positions'][1], [.2, .2])
        self.assertEqual(seeded['velocities'][1], [.1, .1])
        self.assertEqual((seeded['positions'][2], seeded['velocities'][2]), ([.25, .30000000000000004], [0., .05]))

    def test_sector_children_split_into_quadrants_then_halves(self):
        quadrants = post_alarm.sector_children({}, 3, [1., 0.])
        self.assertEqual(len(quadrants), 4)
        self.assertEqual(quadrants[0][3], (-math.pi/4, math.pi/4))
        halves = post_alarm.sector_children(quadrants[0], 3, [1., 0.])
        self.assertEqual([h[3] for h in halves], [(-math.pi/4, 0.), (0., math.pi/4)])


if __name__ == '__main__':
    unittest.main()
