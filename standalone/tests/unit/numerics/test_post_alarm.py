import math
import unittest
from types import SimpleNamespace
from unittest import mock

from parkour_solver.numerics import disk, post_alarm, solver
from parkour_solver.validation.numerical import validate
from tests.support.post_alarm import alarmed

# The study's answers (research/ipopt-seeds 1a5dcb53, configuration "O2 + no iterative refinement"); this source's
# disk points differ by its contact margin, so answers agree to solver precision.
STUDY = dict(push=-385.9620712857143, probe=-385.3082456476679, race=754.6023070789445)
# IPOPT's answers to the requests the pipeline now answers without it: the study's production-seed and polish calls,
# and on shortTick a start with tick 69 reversed (research/post-alarm/README.md#seed-bench).
IPOPT = dict(prodSeed=-385.96199530132577, polish=496.51754119429324, shortTick=754.6119201657268)


def no_ipopt(request, **options):
    raise AssertionError('IPOPT was called')


class PipelineTests(unittest.TestCase):
    def setUp(self):
        patcher = mock.patch.object(solver, 'solve', side_effect=no_ipopt)
        patcher.start()
        self.addCleanup(patcher.stop)

    def confirm(self, problem, gate=None, seconds=20.):
        dispatch = disk.RelaxedDispatch(gate=gate or SimpleNamespace(cache={}), post_alarm='pipeline')
        incumbents = []
        result = dispatch.confirmed(problem, seconds=seconds, stop_on_feasible=False, on_incumbent=incumbents.append)
        return result, incumbents

    def assert_answer(self, problem, result, method, termination, reference, delta):
        confirmation = result['confirmation']
        self.assertEqual((result['status'], result['termination'], confirmation['method'], result['backend']),
                         ('feasible', termination, method, 'clarabel'))
        self.assertAlmostEqual(result['candidate']['objective'], reference, delta=delta)
        self.assertEqual(confirmation['ipoptObjective'], result['candidate']['objective'])
        self.assertEqual(confirmation['ipoptCalls'], 0)
        self.assertNotIn('model', result['candidate'])
        self.assertTrue(validate(problem, result['candidate']['yaw'])['feasible'])

    def test_a_push_answer_is_confirmed_once_and_cached(self):
        problem = alarmed('push')
        gate = SimpleNamespace(cache={})
        result, incumbents = self.confirm(problem, gate)
        again, _ = self.confirm(problem, gate)
        self.assert_answer(problem, result, 'push', 'post_alarm_push', STUDY['push'], 1e-6)
        self.assertGreater(result['confirmation']['clarabelSolves'], 0)
        self.assertEqual(incumbents, [result['candidate']])
        self.assertEqual(again['candidate'], result['candidate'])
        self.assertEqual({k: again['confirmation'][k] for k in ('cached', 'ipoptCalls', 'clarabelSolves')},
                         dict(cached=True, ipoptCalls=0, clarabelSolves=0))

    def test_an_answer_far_below_the_bound_is_improved_by_objective_cut_probes(self):
        problem = alarmed('probe')
        result, _ = self.confirm(problem)
        self.assert_answer(problem, result, 'probe', 'post_alarm_probe', STUDY['probe'], 1e-6)
        self.assertGreaterEqual(result['confirmation']['probes'], 1)

    def test_a_sector_certificate_rejects_and_is_cached(self):
        gate = SimpleNamespace(cache={})
        result, incumbents = self.confirm(alarmed('quadrant'), gate)
        self.assertEqual((result['status'], result['termination'], result['candidate'], incumbents),
                         ('unknown', 'quadrant_infeasible', None, []))
        self.assertEqual(result['confirmation']['quadrant'], 'infeasible')
        self.assertEqual(len(gate.cache), 1)

    def test_a_reversal_cut_reaches_the_other_basin(self):
        problem = alarmed('race')
        result, _ = self.confirm(problem)
        # The study finished its best 2 candidates; this race stops at the first screened answer, within G of it.
        self.assert_answer(problem, result, 'race', 'post_alarm_race', STUDY['race'], post_alarm.G)
        self.assertEqual(result['confirmation']['reversalTick'], 81)

    def test_the_race_reverses_the_most_short_tick_of_any_kind(self):
        problem = alarmed('shortTick')
        self.assertIsNone(problem['vertical']['jumpVelocity'][69])
        result, _ = self.confirm(problem)
        self.assert_answer(problem, result, 'race', 'post_alarm_race', IPOPT['shortTick'], post_alarm.G)
        self.assertEqual(result['confirmation']['reversalTick'], 69)

    def test_a_decaying_push_answers_what_ipopt_answered(self):
        for kind in ('prodSeed', 'polish'):
            with self.subTest(kind=kind):
                problem = alarmed(kind)
                result, _ = self.confirm(problem)
                self.assert_answer(problem, result, 'decay', 'post_alarm_decay', IPOPT[kind], post_alarm.G)

    def test_a_minimized_objective_never_beats_its_bound(self):
        problem = alarmed('probe')
        problem['objective']['sense'] = 'MIN'
        result, _ = self.confirm(problem)
        self.assertEqual(result['status'], 'feasible')
        self.assertTrue(0 <= result['confirmation']['gap'] <= post_alarm.G)
        self.assertGreaterEqual(result['candidate']['objective'], result['disk']['objective'])

    def test_the_deadline_leaves_the_request_undecided_and_uncached(self):
        gate = SimpleNamespace(cache={})
        result, _ = self.confirm(alarmed('quadrant'), gate, seconds=1e-3)
        self.assertEqual((result['status'], result['termination']), ('unknown', 'time_limit'))
        self.assertEqual(gate.cache, {})

    def test_the_deadline_keeps_the_answer_found_before_it(self):
        problem = alarmed('probe')
        with mock.patch.object(post_alarm._Pipeline, 'probes', side_effect=post_alarm._Stop('time_limit')):
            result, _ = self.confirm(problem)
        self.assertEqual((result['status'], result['termination'], result['confirmation']['method']),
                         ('feasible', 'post_alarm_push', 'push'))
        self.assertLess(result['candidate']['objective'], STUDY['probe'])

    def test_post_alarm_is_ipopt_or_pipeline(self):
        with self.assertRaises(ValueError):
            disk.RelaxedDispatch(gate=SimpleNamespace(cache={}), post_alarm='race')


class SectorTests(unittest.TestCase):
    def test_sector_children_split_into_quadrants_then_halves(self):
        quadrants = post_alarm.sector_children({}, 3, [1., 0.])
        self.assertEqual(len(quadrants), 4)
        self.assertEqual(quadrants[0][3], (-math.pi/4, math.pi/4))
        halves = post_alarm.sector_children(quadrants[0], 3, [1., 0.])
        self.assertEqual([h[3] for h in halves], [(-math.pi/4, 0.), (0., math.pi/4)])


if __name__ == '__main__':
    unittest.main()
