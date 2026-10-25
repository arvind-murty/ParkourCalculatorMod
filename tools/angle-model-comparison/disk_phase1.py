import argparse
import json
from pathlib import Path

import casadi as ca
import numpy as np

from compare import forward, wall_residual


def affine_walls(case):
    active = np.flatnonzero(np.array(case["magnitude"]) > 0)
    zero = np.zeros((case["ticks"], 2))
    origin, _ = forward(case, zero)
    rows, rhs = [], []
    for wall in case["walls"]:
        constant = wall_residual(case, wall, origin)
        row = []
        for tick in active:
            for axis in range(2):
                controls = zero.copy()
                controls[tick, axis] = case["magnitude"][tick]
                positions, _ = forward(case, controls)
                row.append(wall_residual(case, wall, positions) - constant)
        signs = (1, -1) if wall["cmp"] == "EQ" else ((1,) if wall["cmp"] == "LE" else (-1,))
        for sign in signs:
            rows.append(np.array(row) * sign)
            rhs.append(-constant * sign)
    return active, np.array(rows), np.array(rhs)


def diagnose(case):
    active, matrix, rhs = affine_walls(case)
    if not len(rhs):
        return {"case": case["name"], "status": "No walls"}
    q = ca.MX.sym("q", 2 * len(active))
    slack = ca.MX.sym("slack")
    rows = ca.mtimes(ca.DM(matrix), q) - ca.DM(rhs) - slack
    disks = ca.vertcat(*[ca.dot(q[2 * t:2 * t + 2], q[2 * t:2 * t + 2]) - 1 for t in range(len(active))])
    solver = ca.nlpsol("disk_phase1", "ipopt", {"x": ca.vertcat(q, slack), "f": slack,
                                               "g": ca.vertcat(rows, disks)},
                      {"print_time": False, "ipopt.print_level": 0, "ipopt.sb": "yes",
                       "ipopt.max_iter": 10000, "ipopt.tol": 1e-11,
                       "ipopt.bound_relax_factor": 0, "error_on_fail": False})
    answer = solver(x0=[0.0] * (2 * len(active)) + [float(max(-rhs) + 1)],
                    lbg=[-float("inf")] * (len(rhs) + len(active)), ubg=[0.0] * (len(rhs) + len(active)))
    weights = np.maximum(np.array(answer["lam_g"]).ravel()[:len(rhs)], 0.0)
    if not np.all(np.isfinite(weights)) or np.sum(weights) <= 0:
        weights = np.ones(len(rhs))
    weights /= np.sum(weights)
    costates = (matrix.T @ weights).reshape((-1, 2))
    bound = float(-rhs @ weights - np.sum(np.linalg.norm(costates, axis=1)))
    return {"case": case["name"], "status": solver.stats()["return_status"],
            "diskWorstSlack": float(answer["x"][-1]), "supportDualLowerBound": bound,
            "nonnegativeWeights": weights.tolist(), "weightSum": float(np.sum(weights)),
            "interpretation": "A positive support bound excludes circle feasibility; a nonpositive one is inconclusive. Floating-point evaluation, not an interval certificate."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--problems", type=Path, required=True)
    parser.add_argument("--cases")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    selected = set(arguments.cases.split(",")) if arguments.cases else None
    results = [diagnose(case) for case in json.loads(arguments.problems.read_text())
               if selected is None or case["name"] in selected]
    arguments.output.write_text(json.dumps(results, indent=2) + "\n")
    for result in results:
        print(result)
