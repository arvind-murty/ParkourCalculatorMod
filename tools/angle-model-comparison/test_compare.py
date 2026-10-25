import math
import unittest

import numpy as np

from compare import FORMULATIONS, build_problem, forward, initial_controls, start_vector, validate_candidate, wall_residual


def fixture(magnitude, velocity=(0.0, 0.0), walls=(), sense="MAX"):
    n = len(magnitude)
    return {
        "name": "analytic", "ticks": n, "position": [7.0, -3.0],
        "velocity": list(velocity), "magnitude": magnitude,
        "friction": [0.91] * n, "base": [0.0] * n, "yaw": [0.0] * n,
        "walls": list(walls),
        "objective": {"axis": "X", "sense": sense, "tick": n, "type": "POSITION"},
    }


def solve(case, formulation):
    solver, decode, _, slices, lower, upper, constant, _, _ = build_problem(case, formulation, 2.0)
    solution = solver(x0=start_vector(case, 0, slices), lbg=lower, ubg=upper)
    positions, controls = decode(solution["x"])
    validation = validate_candidate(case, np.array(positions).T, np.array(controls).T, 1e-7)
    if constant > 1e-7:
        validation["feasible"] = False
    return validation, np.array(controls).T


class ComparisonTest(unittest.TestCase):
    def test_unconstrained_optimum_with_carried_velocity(self):
        case = fixture([0.1, 0.1], velocity=(0.2, 0.0))
        expected = 7.0 + 1.91 * 0.2 + 2.91 * 0.1
        for formulation in FORMULATIONS:
            with self.subTest(formulation=formulation):
                result, _ = solve(case, formulation)
                self.assertTrue(result["feasible"])
                self.assertAlmostEqual(result["objective"], expected, places=7)

    def test_turn_greater_than_ninety_degrees_is_allowed(self):
        wall = {"mode": "X", "t1": 1, "cmp": "GE", "rhs": 7.08}
        case = fixture([0.1, 0.1], walls=[wall], sense="MIN")
        case["yaw"] = [math.degrees(math.acos(0.8)), 180.0]
        expected = 7.0 + 1.91 * 0.08 - 0.1
        for formulation in FORMULATIONS:
            with self.subTest(formulation=formulation):
                result, controls = solve(case, formulation)
                self.assertTrue(result["feasible"])
                self.assertAlmostEqual(result["objective"], expected, places=6)
                turn = math.degrees(math.acos(float(np.clip(np.dot(controls[0], controls[1]) /
                        np.prod(np.linalg.norm(controls, axis=1)), -1, 1))))
                self.assertGreater(turn, 140.0)

    def test_zero_input_ticks_remain_linear(self):
        case = fixture([0.0, 0.1, 0.0], velocity=(0.2, 0.0))
        expected = 7.0 + (1.0 + 0.91 + 0.91 ** 2) * 0.2 + 1.91 * 0.1
        for formulation in FORMULATIONS:
            with self.subTest(formulation=formulation):
                result, controls = solve(case, formulation)
                self.assertTrue(result["feasible"])
                self.assertAlmostEqual(result["objective"], expected, places=7)
                np.testing.assert_allclose(controls[[0, 2]], 0.0, atol=1e-9)

    def test_subphysical_acceleration_cannot_pass_validation(self):
        case = fixture([0.1], walls=[{"mode": "X", "t1": 1, "cmp": "LE", "rhs": 7.06}])
        controls = np.array([[0.05, 0.0]])
        positions, _ = forward(case, controls)
        result = validate_candidate(case, positions, controls, 1e-7)
        self.assertFalse(result["feasible"])
        self.assertAlmostEqual(result["rawWallViolation"], 0.0)
        self.assertAlmostEqual(result["wallViolation"], 0.04)

    def test_position_difference_and_sum_keep_coordinate_offsets(self):
        case = fixture([0.1, 0.1])
        positions, _ = forward(case, initial_controls(case, 0))
        wall = {"mode": "X", "t1": 2, "t2": 1, "op": "MINUS", "cmp": "EQ", "rhs": 0.191}
        self.assertAlmostEqual(wall_residual(case, wall, positions), 0.0)
        wall.update(op="PLUS", rhs=14.391)
        self.assertAlmostEqual(wall_residual(case, wall, positions), 0.0)

    def test_all_formulations_start_from_the_same_trajectory(self):
        case = fixture([0.1, 0.0, 0.026], velocity=(0.17, -0.09))
        for formulation in FORMULATIONS:
            _, decode, _, slices, _, _, _, _, _ = build_problem(case, formulation, 2.0)
            for seed in range(3):
                expected, _ = forward(case, initial_controls(case, seed))
                positions, _ = decode(start_vector(case, seed, slices))
                np.testing.assert_allclose(np.array(positions).T, expected, atol=1e-12)


if __name__ == "__main__":
    unittest.main()
