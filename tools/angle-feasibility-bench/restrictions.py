import heapq
import itertools
import time

import clarabel
import numpy as np
from scipy import sparse
from scipy.optimize import linprog


def trig_range(a, b, lower, upper):
    radius = np.hypot(a, b)
    phase = np.arctan2(b, a)
    left = a * np.cos(lower) + b * np.sin(lower)
    right = a * np.cos(upper) + b * np.sin(upper)
    lo, hi = np.minimum(left, right), np.maximum(left, right)
    has_max = np.ceil((lower - phase) / (2 * np.pi)) <= np.floor((upper - phase) / (2 * np.pi))
    has_min = np.ceil((lower - phase - np.pi) / (2 * np.pi)) <= np.floor((upper - phase - np.pi) / (2 * np.pi))
    return np.where(has_min, -radius, lo), np.where(has_max, radius, hi)


def row_ranges(problem, lower, upper):
    lo, hi = trig_range(problem.a, problem.b, lower, upper)
    pad = 1e-11 * (1 + np.sum(np.hypot(problem.a, problem.b), axis=1))
    return np.sum(lo, axis=1) - problem.rhs - pad, np.sum(hi, axis=1) - problem.rhs + pad


def inner_conic(problem, anchor, lower, upper, seconds, guard=1e-7):
    n = problem.ticks
    residual = problem.residuals(anchor)
    jac = problem.jacobian(anchor)
    unused, curvature = trig_range(-problem.a, -problem.b, lower, upper)
    curvature = np.maximum(curvature, 0) + 1e-13 * np.hypot(problem.a, problem.b)
    m = len(residual)
    rows = [sparse.hstack((sparse.csc_matrix(jac), sparse.csc_matrix(curvature / 2), np.ones((m, 1))), format="csc")]
    bounds = [-residual]
    eye = sparse.eye(n, format="csc")
    zeros = sparse.csc_matrix((n, n + 1))
    rows.extend((sparse.hstack((eye, zeros)), sparse.hstack((-eye, zeros))))
    bounds.extend((upper - anchor, anchor - lower))
    margin = sparse.csc_matrix(([1.0, -1.0], ([0, 1], [2 * n, 2 * n])), shape=(2, 2 * n + 1))
    rows.append(margin)
    bounds.append(np.array([0.01, -guard]))
    linear_count = m + 2 * n + 2
    cone_rows, cone_cols, cone_values = [], [], []
    for t in range(n):
        cone_rows.extend((3 * t, 3 * t + 1, 3 * t + 2))
        cone_cols.extend((n + t, t, n + t))
        cone_values.extend((-1.0, -2.0, -1.0))
    rows.append(sparse.csc_matrix((cone_values, (cone_rows, cone_cols)), shape=(3 * n, 2 * n + 1)))
    bounds.append(np.tile([1.0, 0.0, -1.0], n))
    matrix = sparse.vstack(rows, format="csc")
    vector = np.concatenate(bounds)
    objective = np.r_[np.zeros(n), np.full(n, 1e-7), -1.0]
    quadratic = sparse.diags(np.r_[np.full(n, 1e-3), np.zeros(n + 1)], format="csc")
    settings = clarabel.DefaultSettings()
    settings.verbose = False
    settings.max_threads = 1
    settings.max_iter = 120
    settings.time_limit = max(seconds, 0.001)
    settings.tol_feas = settings.tol_gap_abs = settings.tol_gap_rel = 1e-9
    cones = [clarabel.NonnegativeConeT(linear_count)] + [clarabel.SecondOrderConeT(3) for t in range(n)]
    solver = clarabel.DefaultSolver(quadratic, objective, matrix, vector, cones, settings)
    result = solver.solve()
    record = {"status": str(result.status), "iterations": result.iterations, "solve_seconds": result.solve_time}
    if str(result.status) not in ("Solved", "AlmostSolved"):
        return None, record
    delta = np.asarray(result.x[:n])
    bound = residual + jac @ delta + curvature @ (delta * delta) / 2
    record["upper_bound_max"] = float(np.max(bound, initial=-np.inf))
    candidate = anchor + delta
    if np.any(candidate < lower - 1e-8) or np.any(candidate > upper + 1e-8):
        record["rejected"] = "outside angle region"
        return None, record
    check = problem.validate(candidate)
    record["validation"] = check
    if np.max(bound, initial=-np.inf) > -guard / 2 or not check["feasible"]:
        record["rejected"] = "insufficient bound or replay failure"
        return None, record
    return candidate, record


def outer_lp(problem, lower, upper, preference, seconds):
    n = problem.ticks
    px = np.cos(preference)
    pz = np.sin(preference)
    matrices = [sparse.csc_matrix(np.hstack((problem.a, problem.b)))]
    vectors = [problem.rhs]
    directions = np.arange(16) * (2 * np.pi / 16)
    for phi in directions:
        matrices.append(sparse.hstack((np.cos(phi) * sparse.eye(n), np.sin(phi) * sparse.eye(n)), format="csc"))
        vectors.append(np.ones(n))
    center = (lower + upper) / 2
    half = (upper - lower) / 2
    matrices.append(sparse.hstack((-sparse.diags(np.cos(center)), -sparse.diags(np.sin(center))), format="csc"))
    vectors.append(-np.cos(np.minimum(half, np.pi)))
    xlo, xhi = trig_range(np.ones(n), np.zeros(n), lower, upper)
    zlo, zhi = trig_range(np.zeros(n), np.ones(n), lower, upper)
    bounds = list(zip(np.r_[xlo, zlo] - 1e-12, np.r_[xhi, zhi] + 1e-12))
    result = linprog(-np.r_[px, pz], A_ub=sparse.vstack(matrices, format="csc"), b_ub=np.concatenate(vectors),
                     bounds=bounds, method="highs", options={"time_limit": max(seconds, .001),
                     "primal_feasibility_tolerance": 1e-9, "dual_feasibility_tolerance": 1e-9})
    record = {"status": int(result.status), "message": result.message}
    if result.x is None:
        return None, record
    x, z = result.x[:n], result.x[n:]
    angle = np.arctan2(z, x)
    angle += 2 * np.pi * np.round((center - angle) / (2 * np.pi))
    angle = np.clip(angle, lower, upper)
    angle = np.where(np.hypot(x, z) < 1e-5, np.clip(preference, lower, upper), angle)
    record["minimum_control_norm"] = float(np.min(np.hypot(x, z)))
    record["guidance_violation"] = float(np.max(problem.residuals(angle), initial=-np.inf))
    return angle, record


def solve(problem, initial, seconds=30, max_nodes=128, use_outer=True):
    started = time.perf_counter()
    deadline = started + seconds
    initial = np.asarray(initial).copy()
    queue = []
    serial = itertools.count()
    heapq.heappush(queue, (0, next(serial), initial - np.pi, initial + np.pi, initial))
    logs = []
    best = problem.validate(initial)
    if best["feasible"]:
        return initial, {"status": "feasible_initial", "seconds": time.perf_counter() - started, "nodes": 0, "log": logs, "validation": best}
    processed = 0
    while queue and processed < max_nodes and time.perf_counter() < deadline:
        priority, node_id, lower, upper, preference = heapq.heappop(queue)
        processed += 1
        node = {"node": node_id, "priority": float(priority), "initial_validation": problem.validate(preference), "inner": []}
        low, high = row_ranges(problem, lower, upper)
        if np.any(low > 0):
            node["pruned"] = "row range"
            logs.append(node)
            continue
        guidance = preference
        if use_outer:
            guidance, outer = outer_lp(problem, lower, upper, preference, min(2, deadline - time.perf_counter()))
            node["outer"] = outer
            if guidance is None and outer["status"] == 2:
                node["pruned"] = "numerical outer LP"
                logs.append(node)
                continue
            if guidance is None:
                guidance = preference
        anchors = [guidance]
        if processed == 1 and np.max(np.abs(guidance - preference)) > 1e-8:
            anchors.append(preference)
        for anchor in anchors:
            for radius in (.35, 1.0):
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    break
                local_lower = np.maximum(lower, anchor - radius)
                local_upper = np.minimum(upper, anchor + radius)
                candidate, record = inner_conic(problem, anchor, local_lower, local_upper, min(remaining, 5))
                record["radius"] = radius
                node["inner"].append(record)
                if candidate is not None:
                    logs.append(node)
                    return candidate, {"status": "feasible", "seconds": time.perf_counter() - started,
                                       "nodes": processed, "log": logs, "validation": problem.validate(candidate)}
        logs.append(node)
        residual = problem.residuals(guidance)
        score = np.maximum(residual, 0) + 1e-4
        importance = (score @ np.hypot(problem.a, problem.b)) * (upper - lower) ** 2
        split = int(np.argmax(importance))
        if upper[split] - lower[split] < 1e-7:
            continue
        middle = (lower[split] + upper[split]) / 2
        for left in (True, False):
            child_lower, child_upper = lower.copy(), upper.copy()
            if left:
                child_upper[split] = middle
            else:
                child_lower[split] = middle
            child_preference = np.clip(guidance, child_lower, child_upper)
            violation = float(np.max(problem.residuals(child_preference), initial=-np.inf))
            heapq.heappush(queue, (max(violation, 0), next(serial), child_lower, child_upper, child_preference))
    return None, {"status": "unknown", "seconds": time.perf_counter() - started, "nodes": processed,
                  "pending_regions": len(queue), "log": logs}
