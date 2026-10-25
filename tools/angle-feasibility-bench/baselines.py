import time

import numpy as np


def ipopt(problem, initial, seconds=30, seed=0):
    import casadi as ca

    built = time.perf_counter()
    n = problem.ticks
    theta = ca.MX.sym("theta", n)
    residual = ca.mtimes(ca.DM(problem.a), ca.cos(theta)) + ca.mtimes(ca.DM(problem.b), ca.sin(theta)) - ca.DM(problem.rhs)
    options = {"print_time": False, "ipopt.print_level": 0, "ipopt.sb": "yes", "ipopt.max_iter": 1500,
               "ipopt.max_cpu_time": float(seconds), "ipopt.tol": 1e-9, "ipopt.constr_viol_tol": 1e-9,
               "ipopt.acceptable_tol": 1e-8, "ipopt.bound_relax_factor": 0.0}
    solver = ca.nlpsol("baseline", "ipopt", {"x": theta, "f": ca.MX(0), "g": residual}, options)
    build_seconds = time.perf_counter() - built
    started = time.perf_counter()
    initial = (np.asarray(initial) + np.pi) % (2 * np.pi) - np.pi
    try:
        result = solver(x0=initial, lbx=-np.pi, ubx=np.pi, lbg=-np.inf, ubg=-1e-7)
        candidate = np.asarray(result["x"]).ravel()
        stats = solver.stats()
        check = problem.validate(candidate)
        record = {"status": "feasible" if check["feasible"] else "unknown",
                  "native_status": stats["return_status"], "iterations": int(stats["iter_count"]),
                  "build_seconds": build_seconds, "seconds": time.perf_counter() - started, "validation": check}
        return candidate if check["feasible"] else None, record
    except RuntimeError as error:
        return None, {"status": "unknown", "native_status": str(error), "build_seconds": build_seconds,
                      "seconds": time.perf_counter() - started}


def scip(problem, initial, seconds=30, seed=0):
    from pyscipopt import Model, cos, quicksum, sin

    built = time.perf_counter()
    model = Model("continuous-angle-feasibility")
    model.hideOutput()
    model.setRealParam("limits/time", float(seconds))
    model.setIntParam("limits/solutions", 1)
    model.setIntParam("parallel/maxnthreads", 1)
    model.setIntParam("randomization/randomseedshift", int(seed))
    model.setRealParam("numerics/feastol", 1e-9)
    theta = [model.addVar(lb=-np.pi, ub=np.pi, name="theta_" + str(t)) for t in range(problem.ticks)]
    x = [cos(t) for t in theta]
    z = [sin(t) for t in theta]
    for j, wall in enumerate(problem.walls):
        terms = [float(problem.a[j, t]) * x[t] for t in np.flatnonzero(problem.a[j])]
        terms += [float(problem.b[j, t]) * z[t] for t in np.flatnonzero(problem.b[j])]
        model.addCons(quicksum(terms) <= float(problem.rhs[j] - 1e-7), name="wall_" + str(j))
    model.setObjective(0)
    solution = model.createSol()
    wrapped = (np.asarray(initial) + np.pi) % (2 * np.pi) - np.pi
    for variable, value in zip(theta, wrapped):
        model.setSolVal(solution, variable, float(value))
    model.addSol(solution, free=True)
    build_seconds = time.perf_counter() - built
    started = time.perf_counter()
    model.optimize()
    record = {"status": "unknown", "native_status": str(model.getStatus()), "nodes": model.getNNodes(),
              "build_seconds": build_seconds, "seconds": time.perf_counter() - started}
    if model.getNSols() == 0:
        return None, record
    solution = model.getBestSol()
    candidate = np.array([model.getSolVal(solution, variable) for variable in theta])
    check = problem.validate(candidate)
    record["validation"] = check
    if check["feasible"]:
        record["status"] = "feasible"
        return candidate, record
    return None, record
