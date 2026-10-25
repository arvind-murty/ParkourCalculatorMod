import argparse
import csv
import hashlib
import json
import math
import platform
import time
from pathlib import Path

import casadi as ca
import numpy as np


FORMULATIONS = ("state-angle", "state-vector", "position")


def forward(case, controls):
    n = case["ticks"]
    positions = np.zeros((n + 1, 2))
    velocities = np.zeros((n + 1, 2))
    velocities[0] = case["velocity"]
    for t in range(n):
        displacement = velocities[t] + controls[t]
        positions[t + 1] = positions[t] + displacement
        velocities[t + 1] = case["friction"][t] * displacement
    return positions, velocities


def objective_direction(case):
    obj = case["objective"]
    if "customYaw" in obj:
        angle = math.radians(obj["customYaw"])
        return np.array([-math.sin(angle), math.cos(angle)])
    return np.array([1.0, 0.0] if obj["axis"] == "X" else [0.0, 1.0])


def objective_value(case, positions):
    obj = case["objective"]
    value = positions[obj["tick"]] + case["position"]
    if obj.get("customYaw") is not None and obj.get("type") == "MOTION":
        value = positions[obj["tick"]] - positions[obj["tick"] - 1]
    return float(objective_direction(case) @ value)


def wall_residual(case, wall, positions):
    axis = 0 if wall["mode"] == "X" else 1
    value = positions[wall["t1"], axis] + case["position"][axis]
    if "t2" in wall:
        sign = 1 if wall["op"] == "PLUS" else -1
        value += sign * (positions[wall["t2"], axis] + case["position"][axis])
    return float(value - wall["rhs"])


def wall_violation(case, positions):
    worst = 0.0
    for wall in case["walls"]:
        residual = wall_residual(case, wall, positions)
        violation = abs(residual) if wall["cmp"] == "EQ" else residual * (1 if wall["cmp"] == "LE" else -1)
        worst = max(worst, violation)
    return worst


def initial_controls(case, seed):
    n = case["ticks"]
    if seed == 0:
        angle = np.array(case["base"]) + np.deg2rad(case["yaw"])
    elif seed == 1:
        direction = objective_direction(case) * (1 if case["objective"]["sense"] == "MAX" else -1)
        angle = np.full(n, math.atan2(direction[1], direction[0]))
    else:
        angle = np.random.default_rng(1000 + seed).uniform(-math.pi, math.pi, n)
    return np.array(case["magnitude"])[:, None] * np.column_stack((np.cos(angle), np.sin(angle)))


def verify_oracles(case):
    for oracle in case["oracles"]:
        angle = np.array(case["base"]) + np.deg2rad(oracle["yaw"])
        controls = np.array(case["magnitude"])[:, None] * np.column_stack((np.cos(angle), np.sin(angle)))
        positions, velocities = forward(case, controls)
        expected = np.array(oracle["positions"])
        error = float(np.max(np.abs(positions + case["position"] - expected)))
        if error > 2e-10:
            raise AssertionError((case["name"], "Java forward mismatch", error))
        recovered = np.diff(positions, axis=0).copy()
        recovered[0] -= case["velocity"]
        recovered[1:] -= np.array(case["friction"][:-1])[:, None] * np.diff(positions, axis=0)[:-1]
        if not np.allclose(recovered, controls, atol=2e-12, rtol=0):
            raise AssertionError((case["name"], "position reconstruction mismatch"))
        if not np.all(np.isfinite(velocities)):
            raise AssertionError("Nonfinite velocity")


def build_problem(case, formulation, seconds):
    n = case["ticks"]
    seed_controls = initial_controls(case, 0)
    seed_positions, seed_velocities = forward(case, seed_controls)
    variables, initial, slices, constraints, lower, upper = [], [], [], [], [], []
    positions = [ca.DM.zeros(2)]
    velocity = ca.DM(case["velocity"])
    controls = []
    constant_violation = 0.0

    def variable(name, value, kind, tick):
        value = np.atleast_1d(value)
        symbol = ca.SX.sym(name, len(value))
        offset = len(initial)
        variables.append(symbol)
        initial.extend(value.tolist())
        slices.append((offset, len(value), kind, tick))
        return symbol

    def equality(value):
        constraints.append(value)
        lower.extend([0.0] * value.numel())
        upper.extend([0.0] * value.numel())

    for t in range(n):
        m = case["magnitude"][t]
        if formulation == "position":
            next_position = variable("p" + str(t + 1), seed_positions[t + 1], "p", t + 1)
            previous_velocity = ca.DM(case["velocity"]) if t == 0 else case["friction"][t - 1] * (positions[t] - positions[t - 1])
            control = next_position - positions[t] - previous_velocity
            if m > 0:
                equality(ca.dot(control / m, control / m) - 1)
            else:
                equality(control)
        else:
            if m == 0:
                control = ca.DM.zeros(2)
            elif formulation == "state-angle":
                angle = variable("a" + str(t), math.atan2(seed_controls[t, 1], seed_controls[t, 0]), "a", t)
                control = m * ca.vertcat(ca.cos(angle), ca.sin(angle))
            else:
                direction = variable("q" + str(t), seed_controls[t] / m, "q", t)
                equality(ca.dot(direction, direction) - 1)
                control = m * direction
            next_position = variable("p" + str(t + 1), seed_positions[t + 1], "p", t + 1)
            next_velocity = variable("v" + str(t + 1), seed_velocities[t + 1], "v", t + 1)
            equality(next_position - positions[t] - velocity - control)
            equality(next_velocity - case["friction"][t] * (velocity + control))
            velocity = next_velocity
        positions.append(next_position)
        controls.append(control)

    position_matrix = ca.horzcat(*positions)
    control_matrix = ca.horzcat(*controls)
    for wall in case["walls"]:
        axis = 0 if wall["mode"] == "X" else 1
        expression = position_matrix[axis, wall["t1"]] + case["position"][axis]
        if "t2" in wall:
            sign = 1 if wall["op"] == "PLUS" else -1
            expression += sign * (position_matrix[axis, wall["t2"]] + case["position"][axis])
        expression = ca.simplify(expression - wall["rhs"])
        lo = 0.0 if wall["cmp"] in ("GE", "EQ") else -math.inf
        hi = 0.0 if wall["cmp"] in ("LE", "EQ") else math.inf
        if expression.is_constant():
            value = float(ca.DM(expression))
            constant_violation = max(constant_violation, lo - value, value - hi)
        else:
            constraints.append(expression)
            lower.append(lo)
            upper.append(hi)

    obj = case["objective"]
    objective_position = position_matrix[:, obj["tick"]]
    if obj.get("customYaw") is not None and obj.get("type") == "MOTION":
        objective_position -= position_matrix[:, obj["tick"] - 1]
    cost = ca.dot(ca.DM(objective_direction(case)), objective_position)
    if obj["sense"] == "MAX":
        cost = -cost
    x = ca.vertcat(*variables)
    g = ca.vertcat(*constraints)
    options = {
        "print_time": False,
        "ipopt.print_level": 0,
        "ipopt.sb": "yes",
        "ipopt.max_iter": 2000,
        "ipopt.max_wall_time": seconds,
        "ipopt.tol": 1e-9,
        "ipopt.constr_viol_tol": 1e-9,
        "ipopt.acceptable_tol": 1e-8,
        "ipopt.acceptable_constr_viol_tol": 1e-9,
        "ipopt.bound_relax_factor": 0.0,
    }
    solver = ca.nlpsol("comparison", "ipopt", {"x": x, "f": cost, "g": g}, options)
    decode = ca.Function("decode", [x], [position_matrix, control_matrix])
    evaluate = ca.Function("evaluate", [x], [g])
    jacobian_nnz = ca.jacobian(g, x).sparsity().nnz()
    multiplier = ca.SX.sym("lambda", g.numel())
    hessian_nnz = ca.hessian(cost + ca.dot(multiplier, g), x)[0].sparsity().nnz()
    return solver, decode, evaluate, slices, lower, upper, constant_violation, jacobian_nnz, hessian_nnz


def start_vector(case, seed, slices):
    controls = initial_controls(case, seed)
    positions, velocities = forward(case, controls)
    values = []
    for offset, size, kind, tick in slices:
        if offset != len(values):
            raise AssertionError("Variable layout mismatch")
        if kind == "p":
            value = positions[tick]
        elif kind == "v":
            value = velocities[tick]
        elif kind == "q":
            value = controls[tick] / case["magnitude"][tick]
        else:
            value = [math.atan2(controls[tick, 1], controls[tick, 0])]
        values.extend(np.atleast_1d(value).tolist())
        if len(values) != offset + size:
            raise AssertionError("Variable dimension mismatch")
    return values


def validate_candidate(case, raw_positions, raw_controls, tolerance):
    if not np.all(np.isfinite(raw_positions)) or not np.all(np.isfinite(raw_controls)):
        return {"feasible": False, "reason": "nonfinite candidate"}
    magnitude = np.array(case["magnitude"])
    lengths = np.linalg.norm(raw_controls, axis=1)
    normalized = np.zeros_like(raw_controls)
    active = magnitude > 0
    if np.any(lengths[active] < 1e-14):
        return {"feasible": False, "reason": "zero direction at a positive-magnitude tick"}
    normalized[active] = raw_controls[active] * (magnitude[active] / lengths[active])[:, None]
    positions, _ = forward(case, normalized)
    raw_forward, _ = forward(case, raw_controls)
    magnitude_error = float(np.max(np.abs(lengths - magnitude)))
    dynamics_error = float(np.max(np.abs(raw_positions - raw_forward)))
    replay_error = float(np.max(np.abs(raw_positions - positions)))
    violation = wall_violation(case, positions)
    raw_violation = wall_violation(case, raw_positions)
    return {
        "feasible": max(magnitude_error, dynamics_error, violation, raw_violation, replay_error) <= tolerance,
        "wallViolation": violation,
        "rawWallViolation": raw_violation,
        "magnitudeError": magnitude_error,
        "dynamicsError": dynamics_error,
        "replayDifference": replay_error,
        "objective": objective_value(case, positions),
        "rawObjective": objective_value(case, raw_positions),
        "yaw": ((np.rad2deg(np.arctan2(normalized[:, 1], normalized[:, 0]) - np.array(case["base"])) + 180.0) % 360.0 - 180.0).tolist(),
        "positions": (positions + case["position"]).tolist(),
    }


def compare(args):
    cases = json.loads(args.problems.read_text())
    if args.cases:
        selected = set(args.cases.split(","))
        cases = [case for case in cases if case["name"] in selected]
        if selected != {case["name"] for case in cases}:
            raise ValueError("Unknown case selection")
    report = {
        "scope": "continuous fixed-start model; unrestricted turns; no inertia threshold, facing constraints or game replay",
        "python": platform.python_version(),
        "casadi": ca.__version__,
        "numpy": np.__version__,
        "machine": platform.machine(),
        "processor": platform.processor(),
        "secondsPerSolve": args.seconds,
        "seeds": args.seeds,
        "feasibilityTolerance": args.tolerance,
        "problemsSha256": hashlib.sha256(args.problems.read_bytes()).hexdigest(),
        "scriptSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "results": [],
    }
    for case in cases:
        verify_oracles(case)
        for formulation in FORMULATIONS:
            begin = time.perf_counter()
            solver, decode, evaluate, slices, lower, upper, constant_violation, jac_nnz, hess_nnz = build_problem(case, formulation, args.seconds)
            build_seconds = time.perf_counter() - begin
            for seed in range(args.seeds):
                start = start_vector(case, seed, slices)
                initial_positions, initial_u = decode(start)
                initial_positions = np.array(initial_positions).T
                initial_u = np.array(initial_u).T
                expected_positions, _ = forward(case, initial_controls(case, seed))
                if not np.allclose(initial_positions, expected_positions, atol=2e-10, rtol=0):
                    raise AssertionError("Initial trajectory mismatch")
                initial_g = np.array(evaluate(start)).reshape(-1)
                initial_violation = float(max(0.0, np.max(np.array(lower) - initial_g), np.max(initial_g - np.array(upper))))
                begin = time.perf_counter()
                solution = solver(x0=start, lbg=lower, ubg=upper)
                elapsed = time.perf_counter() - begin
                stats = solver.stats()
                raw_positions, raw_controls = decode(solution["x"])
                validation = validate_candidate(case, np.array(raw_positions).T, np.array(raw_controls).T, args.tolerance)
                validation["feasible"] = validation["feasible"] and constant_violation <= args.tolerance
                result = {
                    "case": case["name"], "formulation": formulation, "seed": seed,
                    "ticks": case["ticks"], "walls": len(case["walls"]),
                    "variables": len(start), "constraints": len(lower),
                    "jacobianNnz": jac_nnz, "hessianNnz": hess_nnz,
                    "buildSeconds": build_seconds, "solveSeconds": elapsed,
                    "iterations": stats["iter_count"], "status": stats["return_status"],
                    "initialViolation": initial_violation,
                    "initialWallViolation": wall_violation(case, initial_positions),
                    "constantViolation": constant_violation,
                    **validation,
                }
                report["results"].append(result)
                print(f'{case["name"]:22} {formulation:13} seed={seed} feasible={result["feasible"]} '
                      f'objective={result.get("objective", float("nan")):.9f} '
                      f'wall={result.get("wallViolation", float("nan")):.2g} '
                      f'time={elapsed:.3f}s status={result["status"]}', flush=True)
                args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    rows = [{key: value for key, value in row.items() if key not in ("yaw", "positions")} for row in report["results"]]
    keys = list(dict.fromkeys(key for row in rows for key in row))
    with args.output.with_suffix(".csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--problems", type=Path, default=Path(__file__).parent / "build/problems.json")
    parser.add_argument("--output", type=Path, default=Path(__file__).parent / "build/results.json")
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--seeds", type=int, default=3)
    parser.add_argument("--tolerance", type=float, default=1e-7)
    parser.add_argument("--cases")
    arguments = parser.parse_args()
    if arguments.seconds <= 0 or arguments.seeds <= 0 or arguments.tolerance <= 0:
        parser.error("Seconds, seeds and tolerance must be positive")
    compare(arguments)
