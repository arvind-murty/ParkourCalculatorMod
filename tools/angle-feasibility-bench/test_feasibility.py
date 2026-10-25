import unittest
from pathlib import Path

import numpy as np

from model import compile_problem, load_export
from restrictions import inner_conic, outer_conic, row_ranges, solve, trig_range


ROOT = Path(__file__).resolve().parents[2]
EXPORT = Path(__file__).resolve().parent / "benchmarks/real11-2026-10-25/problems.json.gz"


def example(walls, ticks=1, magnitude=None, position=None, velocity=None):
    return compile_problem({"name": "analytic", "ticks": ticks, "position": position or [0, 0],
                            "velocity": velocity or [0, 0], "magnitude": magnitude or [1] * ticks,
                            "friction": [.7] * ticks, "walls": walls})


def wall(axis, cmp, rhs, t1=1, t2=None, op="PLUS"):
    result = dict(mode=axis, cmp=cmp, rhs=rhs, t1=t1, op=op, name=axis + cmp + str(rhs))
    if t2 is not None:
        result["t2"] = t2
    return result


class FeasibilityTests(unittest.TestCase):
    def test_java_recurrence_oracles(self):
        data = load_export(EXPORT, ROOT)[0]
        for ticks in (60, 100, 200, 808):
            problem = compile_problem(data, ticks)
            for oracle in data["oracles"]:
                theta = problem.base + np.deg2rad(oracle["yaw"][:ticks])
                positions = problem.replay(theta)[0]
                np.testing.assert_allclose(positions, np.array(oracle["positions"][:ticks + 1]), atol=2e-10, rtol=0)
                self.assertLess(problem.validate(theta)["matrix_replay_disagreement"], 2e-10)

    def test_sum_difference_and_initial_velocity(self):
        problem = example([wall("X", "LE", 21, 2, 1), wall("Z", "GE", -.2, 2, 1, "MINUS")],
                          ticks=3, position=[10, -20], velocity=[.4, -.7])
        for theta in (np.array([.2, .5, -.9]), np.array([2, -1, 1])):
            self.assertLess(problem.validate(theta)["matrix_replay_disagreement"], 1e-12)

    def test_joint_circle_coupling_is_not_dropped(self):
        problem = example([wall(axis, cmp, value) for axis in ("X", "Z") for cmp, value in (("GE", -.1), ("LE", .1))])
        candidate, record = solve(problem, np.array([.7]), seconds=1, max_nodes=8)
        self.assertIsNone(candidate)
        self.assertEqual("unknown", record["status"])

    def test_inner_construction_from_infeasible_anchor(self):
        problem = example([wall("X", "LE", .8), wall("X", "GE", .2), wall("Z", "GE", .4)])
        anchor = np.array([.5])
        self.assertFalse(problem.validate(anchor)["feasible"])
        candidate, record = inner_conic(problem, anchor, np.array([.1]), np.array([1.4]), seconds=2)
        self.assertIsNotNone(candidate, record)
        self.assertTrue(problem.validate(candidate)["feasible"])
        self.assertLess(record["upper_bound_max"], 0)

    def test_exact_row_ranges_enclose_joint_samples_across_wrap(self):
        problem = example([wall("X", "GE", .2, 3), wall("Z", "LE", .7, 2)], ticks=3)
        rng = np.random.default_rng(12)
        for i in range(10):
            lower = rng.uniform(-8, 6, 3)
            upper = lower + rng.uniform(.01, 2 * np.pi, 3)
            lo, hi = row_ranges(problem, lower, upper)
            for j in range(50):
                residual = problem.residuals(rng.uniform(lower, upper))
                self.assertTrue(np.all(residual >= lo - 1e-12))
                self.assertTrue(np.all(residual <= hi + 1e-12))

    def test_quadratic_upper_bounds_enclose_nonlinear_rows(self):
        problem = example([wall("X", "GE", .2, 3), wall("Z", "LE", .7, 2)], ticks=3)
        rng = np.random.default_rng(24)
        for i in range(10):
            anchor = rng.uniform(-5, 5, 3)
            lower, upper = anchor - .7, anchor + .9
            unused, curvature = trig_range(-problem.a, -problem.b, lower, upper)
            curvature = np.maximum(curvature, 0)
            for j in range(50):
                theta = rng.uniform(lower, upper)
                delta = theta - anchor
                bound = problem.residuals(anchor) + problem.jacobian(anchor) @ delta + curvature @ (delta * delta) / 2
                self.assertTrue(np.all(problem.residuals(theta) <= bound + 1e-12))

    def test_disk_outer_bound_preserves_feasible_arcs_across_wrap(self):
        problem = example([wall("X", "GE", -.8), wall("Z", "LE", .1)])
        witness = np.array([4.])
        self.assertTrue(problem.validate(witness)["feasible"])
        angle, record = outer_conic(problem, witness - .1, witness + .1, witness, 2)
        self.assertIsNotNone(angle, record)
        self.assertLessEqual(record["maximum_control_norm"], 1 + 1e-7)
        self.assertTrue(problem.validate(angle)["feasible"])

    def test_zero_input_ticks_and_magnitude(self):
        problem = example([wall("X", "GE", -.5, 3)], ticks=3, magnitude=[1, 0, .2])
        theta = np.array([.2, -2.0, .7])
        changed = theta.copy()
        changed[1] = 3.0
        np.testing.assert_array_equal(problem.replay(theta)[0], problem.replay(changed)[0])
        self.assertLess(problem.validate(theta)["magnitude_error"], 1e-14)

    def test_waypoint_seed_does_not_use_authored_angles(self):
        data = load_export(EXPORT)[0]
        first = compile_problem(data, 100).waypoint_angles()
        changed = dict(data, yaw=[0] * data["ticks"])
        np.testing.assert_array_equal(first, compile_problem(changed, 100).waypoint_angles())

    def test_prefix_retains_original_walls_without_goal_removal(self):
        data = load_export(EXPORT)[0]
        for ticks, count in ((100, 83), (200, 195)):
            problem = compile_problem(data, ticks)
            self.assertEqual(count, len(problem.walls))
            self.assertEqual(data["position"], problem.position.tolist())
            self.assertTrue(all(max(w["t1"], w.get("t2") or 0) <= ticks for w in problem.walls))


if __name__ == "__main__":
    unittest.main()
