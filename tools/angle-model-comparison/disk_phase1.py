import argparse
import json
from pathlib import Path

import casadi as ca
import numpy as np

from compare import forward, wall_residual


def support_bound(matrix, rhs, multipliers):
    weights = np.maximum(np.array(multipliers).ravel()[:len(rhs)], 0.0)
    if not np.all(np.isfinite(weights)) or np.sum(weights) <= 0:
        weights = np.ones(len(rhs))
    weights /= np.sum(weights)
    costates = (matrix.T @ weights).reshape((-1, 2))
    return float(-rhs @ weights - np.sum(np.linalg.norm(costates, axis=1))), weights


class BoundMonitor(ca.Callback):
    def __init__(self, matrix, rhs):
        ca.Callback.__init__(self)
        self.matrix, self.rhs = matrix, rhs
        self.nx = matrix.shape[1] + 1
        self.ng = len(rhs) + matrix.shape[1] // 2
        self.construct("disk_bound_monitor", {})

    def get_n_in(self):
        return ca.nlpsol_n_out()

    def get_n_out(self):
        return 1

    def get_sparsity_in(self, index):
        name = ca.nlpsol_out(index)
        if name in ("x", "lam_x"):
            return ca.Sparsity.dense(self.nx, 1)
        if name in ("g", "lam_g"):
            return ca.Sparsity.dense(self.ng, 1)
        return ca.Sparsity.dense(1, 1) if name == "f" else ca.Sparsity(0, 1)

    def eval(self, values):
        lower, _ = support_bound(self.matrix, self.rhs, values[ca.nlpsol_out().index("lam_g")])
        q = np.array(values[ca.nlpsol_out().index("x")]).ravel()[:-1].reshape((-1, 2))
        q /= np.maximum(np.linalg.norm(q, axis=1), 1.0)[:, None]
        upper = float(np.max(self.matrix @ q.ravel() - self.rhs))
        return [int(lower > 1e-7 or upper - lower <= 1e-8)]


def affine_walls(case):
    active = np.flatnonzero(np.array(case["magnitude"]) > 0)
    zero = np.zeros((case["ticks"], 2))
    origin, _ = forward(case, zero)
    response = np.zeros((case["ticks"] + 1, 2, 2 * len(active)))
    velocity = np.zeros((2, 2 * len(active)))
    columns = {tick: index for index, tick in enumerate(active)}
    for tick in range(case["ticks"]):
        displacement = velocity.copy()
        if tick in columns:
            index = columns[tick]
            displacement[:, 2 * index:2 * index + 2] += case["magnitude"][tick] * np.eye(2)
        response[tick + 1] = response[tick] + displacement
        velocity = case["friction"][tick] * displacement
    rows, rhs = [], []
    for wall in case["walls"]:
        constant = wall_residual(case, wall, origin)
        axis = 0 if wall["mode"] == "X" else 1
        row = response[wall["t1"], axis].copy()
        if "t2" in wall:
            row += (1 if wall["op"] == "PLUS" else -1) * response[wall["t2"], axis]
        signs = (1, -1) if wall["cmp"] == "EQ" else ((1,) if wall["cmp"] == "LE" else (-1,))
        for sign in signs:
            rows.append(row * sign)
            rhs.append(-constant * sign)
    return active, np.array(rows), np.array(rhs)


def diagnose(case):
    active, matrix, rhs = affine_walls(case)
    if not len(rhs):
        return {"case": case["name"], "status": "No walls"}
    q = ca.MX.sym("q", 2 * len(active))
    slack = ca.MX.sym("slack")
    rows = ca.mtimes(ca.DM(matrix), q) - ca.DM(rhs) - ca.repmat(slack, len(rhs), 1)
    disks = ca.vertcat(*[ca.dot(q[2 * t:2 * t + 2], q[2 * t:2 * t + 2]) - 1 for t in range(len(active))])
    monitor = BoundMonitor(matrix, rhs)
    solver = ca.nlpsol("disk_phase1", "ipopt", {"x": ca.vertcat(q, slack), "f": slack,
                                               "g": ca.vertcat(rows, disks)},
                      {"print_time": False, "ipopt.print_level": 0, "ipopt.sb": "yes",
                       "ipopt.max_iter": 10000, "ipopt.tol": 1e-11,
                       "ipopt.bound_relax_factor": 0, "error_on_fail": False,
                       "iteration_callback": monitor})
    answer = solver(x0=[0.0] * (2 * len(active)) + [float(max(-rhs) + 1)],
                    lbg=[-float("inf")] * (len(rhs) + len(active)), ubg=[0.0] * (len(rhs) + len(active)))
    bound, weights = support_bound(matrix, rhs, answer["lam_g"])
    projected = np.array(answer["x"]).ravel()[:-1].reshape((-1, 2))
    projected /= np.maximum(np.linalg.norm(projected, axis=1), 1.0)[:, None]
    upper = float(np.max(matrix @ projected.ravel() - rhs))
    return {"case": case["name"], "status": solver.stats()["return_status"],
            "rawSlackVariable": float(answer["x"][-1]), "diskPrimalUpperBound": upper,
            "supportDualLowerBound": bound, "diskGap": upper - bound,
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
