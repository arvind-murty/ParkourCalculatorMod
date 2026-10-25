import math
import gzip
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

from compare import CandidateMonitor, FORMULATIONS, build_problem, execute_solver, forward, initial_controls, start_vector, validate_candidate, wall_residual
from analyze import native_validation
from disk_phase1 import affine_walls, diagnose
from audit_facing import facing_pin_conflicts
from copy_without_facing import copy_without_facing
from certify_disk_bound import certified_lower_bound


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
    def test_saved_real11_first350_witness_is_physically_feasible(self):
        root = Path(__file__).parent / "benchmarks/real11-first350-2026-10-25"
        data = gzip.decompress((root / "problems.json.gz").read_bytes())
        case = json.loads(data)[0]
        saved = json.loads((root / "witness.json").read_text())
        self.assertEqual(hashlib.sha256(data).hexdigest(), saved["problemsSha256"])
        self.assertEqual(case["ticks"], 350)
        self.assertEqual(len(case["walls"]), 273)
        self.assertEqual(case["facingWalls"], [])
        self.assertEqual(case["objective"]["tick"], 350)
        self.assertTrue(all(max(row["t1"], row.get("t2", 0)) <= 350 for row in case["walls"]))
        states = json.loads((root / "state-witnesses.json").read_text())
        self.assertEqual(states["problemsSha256"], saved["problemsSha256"])
        for witness in [saved["witness"]] + states["witnesses"]:
            with self.subTest(formulation=witness["formulation"]):
                angle = np.array(case["base"]) + np.deg2rad(witness["yaw"])
                controls = np.array(case["magnitude"])[:, None] * np.column_stack((np.cos(angle), np.sin(angle)))
                positions = np.array(witness["positions"]) - case["position"]
                result = validate_candidate(case, positions, controls, saved["tolerance"])
                self.assertTrue(result["feasible"])
                self.assertAlmostEqual(result["objective"], 703.3000000119208, places=8)

    def test_saved_real11_copy_and_interval_infeasibility_bound(self):
        root = Path(__file__).parent / "benchmarks/real11-full-2026-10-25"
        data = gzip.decompress((root / "problems.json.gz").read_bytes())
        case = json.loads(data)[0]
        saved = json.loads((root / "interval-bound.json").read_text())
        diagnostic = json.loads((root / "disk-relaxation.json").read_text())[0]
        self.assertEqual(hashlib.sha256(data).hexdigest(), saved["problemsSha256"])
        self.assertEqual(case["ticks"], 808)
        self.assertEqual(len(case["walls"]), 628)
        self.assertEqual(case["facingWalls"], [])
        certificate = certified_lower_bound(case, diagnostic["nonnegativeWeights"])
        self.assertTrue(certificate["excludesZeroViolation"])
        self.assertGreater(float(certificate["supportDualLowerBound"]), 0.0068)
        self.assertEqual(certificate["supportDualLowerBound"], saved["supportDualLowerBound"])
        capture = json.loads((Path(__file__).parent / "fixtures/linkcraft-real11-no-facing.json").read_text())
        self.assertFalse(any(constraint.get("field") in ("F", "DF")
                             for tick in capture["angleSolver"]["ticks"]
                             for constraint in tick.get("constraints", [])))

    def test_capture_copy_removes_only_facing_constraints(self):
        document = {"rows": [{"yaw": 45.0}], "start": {"yaw": 12.0}, "angleSolver": {
            "ticks": [{"tick": 7, "override": {"slipperiness": "AIR"}, "constraints": [
                {"field": "F", "op": "EQ", "value": 45.0},
                {"field": "X", "op": "LE", "value": 8.0},
                {"field": "DF", "op": "EQ", "value": 0.0, "disabled": True}]}]}}
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "original.json", Path(directory) / "copy.json"
            original = json.dumps(document).encode()
            source.write_bytes(original)
            manifest = copy_without_facing(source, output)
            copied = json.loads(output.read_text())
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(copied["rows"], document["rows"])
            self.assertEqual(copied["start"], document["start"])
            tick = copied["angleSolver"]["ticks"][0]
            self.assertEqual(tick["override"], {"slipperiness": "AIR"})
            self.assertEqual(tick["constraints"], [{"field": "X", "op": "LE", "value": 8.0}])
            self.assertEqual(len(manifest["removedFacingConstraints"]), 2)
            self.assertEqual(manifest["sourceSha256"], hashlib.sha256(original).hexdigest())

    def test_capture_copy_rejects_source_or_existing_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "original.json", Path(directory) / "copy.json"
            source.write_text("{}")
            with self.assertRaises(ValueError):
                copy_without_facing(source, source)
            output.write_text("preserve this")
            with self.assertRaises(FileExistsError):
                copy_without_facing(source, output)
            self.assertEqual(source.read_text(), "{}")
            self.assertEqual(output.read_text(), "preserve this")

    def test_interval_certificate_on_analytic_disk_bounds(self):
        case = fixture([0.1], walls=[
            {"mode": "X", "t1": 1, "cmp": "GE", "rhs": 7.08},
            {"mode": "Z", "t1": 1, "cmp": "GE", "rhs": -2.92}])
        expected = 0.08 - 0.1 / math.sqrt(2.0)
        bound = float(certified_lower_bound(case, [0.5, 0.5])["supportDualLowerBound"])
        self.assertGreater(bound, expected - 1e-14)
        self.assertLessEqual(bound, expected + 1e-14)
        case["walls"] = case["walls"][:1]
        bound = certified_lower_bound(case, [1.0])
        self.assertFalse(bound["excludesZeroViolation"])
        self.assertAlmostEqual(float(bound["supportDualLowerBound"]), -0.02, places=12)

    def test_interval_adjoint_agrees_with_independent_affine_matrix(self):
        case = fixture([0.1, 0.0, 0.026, 0.05], velocity=(0.17, -0.09), walls=[
            {"mode": "X", "t1": 4, "t2": 1, "op": "PLUS", "cmp": "LE", "rhs": 14.5},
            {"mode": "Z", "t1": 3, "t2": 1, "op": "MINUS", "cmp": "EQ", "rhs": 0.1}])
        case["friction"] = [0.91, 0.546, 0.8, 0.7]
        _, matrix, rhs = affine_walls(case)
        from disk_phase1 import support_bound
        expected, _ = support_bound(matrix, rhs, [0.2, 0.3, 0.5])
        certified = certified_lower_bound(case, [0.2, 0.3, 0.5])
        self.assertAlmostEqual(float(certified["supportDualLowerBound"]), expected, places=12)

    def test_facing_pin_conflict_audit_handles_wrapped_turns(self):
        def problem(first, second, turn):
            rows = []
            for tick, yaw in enumerate((first, second)):
                for cmp, rhs in (("GE", yaw - 0.001), ("LE", yaw + 0.001)):
                    rows.append({"mode": "F", "t1": tick, "op": "PLUS", "cmp": cmp,
                                 "rhs": rhs, "name": "pin" + str(tick) + cmp})
            for cmp, rhs in (("GE", turn - 0.0001), ("LE", turn + 0.0001)):
                rows.append({"mode": "F", "t1": 1, "t2": 0, "op": "MINUS", "cmp": cmp,
                             "rhs": rhs, "name": "turn" + cmp})
            return {"facingWalls": rows}

        conflict = facing_pin_conflicts(problem(42.54849, 45.01, 0.0))
        self.assertEqual(len(conflict), 1)
        self.assertGreater(conflict[0]["separationDegrees"], 2.45)
        self.assertEqual(facing_pin_conflicts(problem(45.01, 45.01, 0.0)), [])
        self.assertEqual(facing_pin_conflicts(problem(179.999, -179.999, 0.002)), [])

    def test_saved_benchmark_witnesses_meet_the_frozen_physical_model(self):
        root = Path(__file__).parent / "benchmarks/sqp-2026-10-25"
        data = gzip.decompress((root / "problems.json.gz").read_bytes())
        saved = json.loads((root / "witnesses.json").read_text())
        self.assertEqual(hashlib.sha256(data).hexdigest(), saved["problemsSha256"])
        cases = {case["name"]: case for case in json.loads(data)}
        self.assertEqual(len(cases), 15)
        self.assertEqual(len(saved["witnesses"]), 14)
        for witness in saved["witnesses"]:
            case = cases[witness["case"]]
            angle = np.array(case["base"]) + np.deg2rad(witness["yaw"])
            controls = np.array(case["magnitude"])[:, None] * np.column_stack((np.cos(angle), np.sin(angle)))
            positions = np.array(witness["positions"]) - case["position"]
            result = validate_candidate(case, positions, controls, saved["tolerance"])
            self.assertTrue(result["feasible"], witness["case"])
            self.assertAlmostEqual(result["objective"], witness["objective"], places=8)

    def test_sqp_constraint_jacobians_match_finite_differences(self):
        case = fixture([0.1, 0.026, 0.0], velocity=(0.17, -0.09), walls=[
            {"mode": "X", "t1": 3, "cmp": "LE", "rhs": 7.8},
            {"mode": "Z", "t1": 2, "t2": 1, "op": "PLUS", "cmp": "GE", "rhs": -6.0}])
        case["yaw"] = [30.0, 60.0, 0.0]
        for formulation in FORMULATIONS:
            solver, _, evaluate, slices, _, _, _, _, _ = build_problem(case, formulation, backend="sqp")
            start = np.array(start_vector(case, 0, slices))
            jacobian = np.array(solver.get_function("nlp_jac_fg")(start, [])[3])
            finite = np.zeros_like(jacobian)
            for column in range(len(start)):
                shift = np.zeros_like(start)
                shift[column] = 1e-6
                finite[:, column] = (np.array(evaluate(start + shift)).ravel()
                                     - np.array(evaluate(start - shift)).ravel()) / 2e-6
            np.testing.assert_allclose(jacobian, finite, atol=2e-8, rtol=2e-8)

    def test_affine_disk_rows_match_absolute_sum_and_difference_walls(self):
        walls = [{"mode": "X", "t1": 3, "t2": 1, "op": "PLUS", "cmp": "LE", "rhs": 14.5},
                 {"mode": "Z", "t1": 2, "t2": 1, "op": "MINUS", "cmp": "GE", "rhs": 0.1}]
        case = fixture([0.1, 0.0, 0.026], velocity=(0.17, -0.09), walls=walls)
        active, matrix, rhs = affine_walls(case)
        controls = initial_controls(case, 2)
        q = controls[active] / np.array(case["magnitude"])[active, None]
        positions, _ = forward(case, controls)
        expected = [wall_residual(case, wall, positions) * (1 if wall["cmp"] == "LE" else -1)
                    for wall in walls]
        np.testing.assert_allclose(matrix @ q.ravel() - rhs, expected, atol=2e-14)

    def test_backend_exception_keeps_the_last_candidate_and_error_status(self):
        case = fixture([0.1])
        monitor = CandidateMonitor(case, 1e-7)
        build_problem(case, "condensed-angle", backend="sqp", monitor=monitor)
        monitor.reset([0.0], 0.0)

        class FailingBackend:
            def __call__(self, **kwargs):
                raise RuntimeError("QP elastic penalty overflow")

            def stats(self):
                return {"iter_count": 1, "return_status": "Unfinished"}

        answer, stats, _, error = execute_solver(FailingBackend(), [0.0], [], [], monitor)
        self.assertEqual(stats["return_status"], "Backend_Exception")
        self.assertIn("penalty overflow", error)
        self.assertTrue(monitor.best["feasible"])
        np.testing.assert_allclose(answer["x"], [[0.0]])

    def test_disk_support_bound_detects_coupled_axis_infeasibility(self):
        case = fixture([0.1], walls=[
            {"mode": "X", "t1": 1, "cmp": "GE", "rhs": 7.08},
            {"mode": "Z", "t1": 1, "cmp": "GE", "rhs": -2.92}])
        result = diagnose(case)
        expected = 0.08 - 0.1 / math.sqrt(2.0)
        self.assertAlmostEqual(result["supportDualLowerBound"], expected, places=9)
        self.assertGreaterEqual(result["diskPrimalUpperBound"], expected - 1e-12)
        self.assertTrue(all(weight >= 0 for weight in result["nonnegativeWeights"]))
        self.assertAlmostEqual(result["weightSum"], 1.0)

    def test_disk_support_bound_does_not_exclude_an_analytic_feasible_case(self):
        case = fixture([0.1], walls=[{"mode": "X", "t1": 1, "cmp": "GE", "rhs": 7.08}])
        result = diagnose(case)
        self.assertAlmostEqual(result["supportDualLowerBound"], -0.02, places=9)

    def test_disk_phase_one_closes_the_bound_on_a_multitick_problem(self):
        case = fixture([0.1, 0.1], walls=[
            {"mode": "X", "t1": 2, "cmp": "GE", "rhs": 7.13},
            {"mode": "Z", "t1": 2, "cmp": "GE", "rhs": -2.85}])
        expected = (0.28 - math.sqrt(2 * 0.291 ** 2 - 0.02 ** 2)) / 2
        result = diagnose(case)
        self.assertLess(result["diskGap"], 2e-8)
        self.assertAlmostEqual(result["supportDualLowerBound"], expected, places=7)

    def test_native_output_validation_checks_the_replayed_path_and_objective(self):
        case = fixture([0.1, 0.1])
        controls = initial_controls(case, 0)
        positions, _ = forward(case, controls)
        result = validate_candidate(case, positions, controls, 1e-7)
        row = {**result, "returned": True, "method": "native-test"}
        self.assertTrue(native_validation(case, row)["feasible"])
        row["positions"][1][0] += 0.001
        with self.assertRaisesRegex(AssertionError, "Java replay mismatch"):
            native_validation(case, row)
        row["positions"][1][0] -= 0.001
        row["objective"] += 0.001
        with self.assertRaisesRegex(AssertionError, "Java objective mismatch"):
            native_validation(case, row)

    def test_sqp_backends_reach_analytic_optimum_from_a_rotated_seed(self):
        case = fixture([0.1, 0.1], velocity=(0.2, 0.0), walls=[
            {"mode": "X", "t1": 1, "cmp": "GE", "rhs": 7.28}], sense="MIN")
        case["yaw"] = [30.0, 150.0]
        expected = 7.0 + 1.91 * 0.28 - 0.1
        for backend in ("sqp", "slsqp"):
            for formulation in FORMULATIONS:
                with self.subTest(backend=backend, formulation=formulation):
                    solver, decode, _, slices, lower, upper, _, _, _ = build_problem(case, formulation, backend=backend)
                    answer = solver(x0=start_vector(case, 0, slices), lbg=lower, ubg=upper)
                    positions, controls = decode(answer["x"])
                    result = validate_candidate(case, np.array(positions).T, np.array(controls).T, 1e-7)
                    self.assertTrue(result["feasible"])
                    self.assertAlmostEqual(result["objective"], expected, places=6)

    def test_monitor_retains_a_feasible_incumbent_after_an_infeasible_iterate(self):
        case = fixture([0.1], walls=[{"mode": "X", "t1": 1, "cmp": "GE", "rhs": 7.08}])
        monitor = CandidateMonitor(case, 1e-7)
        build_problem(case, "condensed-angle", backend="sqp", monitor=monitor)
        monitor.reset([0.0], 0.0)
        monitor.record([math.pi])
        self.assertTrue(monitor.best["feasible"])
        self.assertAlmostEqual(monitor.best["objective"], 7.1)
        self.assertFalse(monitor.history[-1]["feasible"])
        self.assertIsNotNone(monitor.first_feasible)

    def test_all_zero_controls_and_a_constant_contradiction(self):
        case = fixture([0.0, 0.0], walls=[{"mode": "X", "t1": 2, "cmp": "LE", "rhs": 6.9}])
        solver, decode, _, slices, lower, upper, constant, _, _ = build_problem(case, "condensed-angle", backend="sqp")
        answer = solver(x0=start_vector(case, 0, slices), lbg=lower, ubg=upper)
        positions, controls = decode(answer["x"])
        result = validate_candidate(case, np.array(positions).T, np.array(controls).T, 1e-7)
        self.assertAlmostEqual(constant, 0.1)
        self.assertFalse(result["feasible"])
        self.assertEqual(solver.stats()["iter_count"], 0)

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
