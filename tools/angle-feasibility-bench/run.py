import os

for variable in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "VECLIB_MAXIMUM_THREADS", "MKL_NUM_THREADS"):
    os.environ[variable] = "1"

import argparse
import json
import platform
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

from model import compile_problem, load_export


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXPORT = ROOT / "tools/angle-model-comparison/build/real11-problems.json"


def choose_start(problem, label):
    if label == "authored":
        return problem.authored.copy()
    if label == "waypoints":
        return problem.waypoint_angles()
    if label.startswith("random-"):
        return np.random.default_rng(int(label.split("-")[1])).uniform(-np.pi, np.pi, problem.ticks)
    if label == "constant":
        return np.zeros(problem.ticks)
    raise ValueError("Unknown start " + label)


def worker(args):
    import baselines
    import restrictions

    data = load_export(args.export, ROOT)[0]
    problem = compile_problem(data, args.ticks, "real11-" + str(args.ticks))
    initial = choose_start(problem, args.start)
    initial_check = problem.validate(initial)
    if args.method == "inner":
        candidate, result = restrictions.solve(problem, initial, args.seconds, args.max_nodes)
    elif args.method == "inner-no-outer":
        candidate, result = restrictions.solve(problem, initial, args.seconds, args.max_nodes, use_outer=False)
    else:
        candidate, result = getattr(baselines, args.method)(problem, initial, args.seconds, args.seed)
    result.update(case=problem.name, ticks=problem.ticks, rows=len(problem.walls), method=args.method,
                  start=args.start, seed=args.seed, time_budget=args.seconds, initial_validation=initial_check,
                  source=problem.source, sha256=problem.source_hash, fixed_position=problem.position.tolist(),
                  fixed_velocity=problem.velocity.tolist(), omitted_features=["inertia threshold", "facing constraints", "game arithmetic", "contact discovery"],
                  active_ticks=int(np.count_nonzero(np.any((problem.a != 0) | (problem.b != 0), axis=0))))
    if candidate is not None:
        positions, velocities, controls = problem.replay(candidate)
        result["witness"] = {"theta": candidate.tolist(), "yaw_degrees": np.rad2deg(candidate - problem.base).tolist(),
                             "positions": positions.tolist(), "velocities": velocities.tolist(), "controls": controls.tolist()}
    result["environment"] = {"python": platform.python_version(), "platform": platform.platform(),
                             "numpy": np.__version__, "threads": 1}
    Path(args.output).write_text(json.dumps(result, indent=2, allow_nan=False))
    print(json.dumps({k: result.get(k) for k in ("case", "method", "start", "status", "seconds", "nodes")}), flush=True)


def parent(args):
    directory = Path(args.output)
    directory.mkdir(parents=True, exist_ok=True)
    results = []
    for ticks in args.horizons:
        for start in args.starts:
            for method in args.methods:
                output = directory / (str(ticks) + "-" + start + "-" + method + ".json")
                command = [sys.executable, str(Path(__file__).resolve()), "--worker", "--export", str(args.export),
                           "--ticks", str(ticks), "--start", start, "--method", method, "--seconds", str(args.seconds),
                           "--max-nodes", str(args.max_nodes), "--output", str(output)]
                began = time.perf_counter()
                try:
                    completed = subprocess.run(command, capture_output=True, text=True, timeout=args.seconds + 45)
                    (directory / (output.stem + ".log")).write_text(completed.stdout + completed.stderr)
                    if completed.returncode != 0 or not output.exists():
                        result = {"case": "real11-" + str(ticks), "method": method, "start": start,
                                  "status": "error", "returncode": completed.returncode, "stderr": completed.stderr[-2000:]}
                    else:
                        result = json.loads(output.read_text())
                except subprocess.TimeoutExpired:
                    result = {"case": "real11-" + str(ticks), "method": method, "start": start, "status": "process_timeout"}
                result["process_seconds"] = time.perf_counter() - began
                results.append({k: v for k, v in result.items() if k not in ("witness", "log")})
                (directory / "summary.json").write_text(json.dumps(results, indent=2))
                print(json.dumps({k: result.get(k) for k in ("case", "method", "start", "status", "seconds", "nodes", "validation")}), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", action="store_true")
    parser.add_argument("--export", type=Path, default=DEFAULT_EXPORT)
    parser.add_argument("--output", required=True)
    parser.add_argument("--horizons", type=int, nargs="+", default=[60, 100, 200])
    parser.add_argument("--starts", nargs="+", default=["waypoints", "random-0", "random-1", "authored"])
    parser.add_argument("--methods", nargs="+", default=["inner", "ipopt", "scip"])
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--max-nodes", type=int, default=128)
    parser.add_argument("--ticks", type=int, default=100)
    parser.add_argument("--start", default="waypoints")
    parser.add_argument("--method", default="inner")
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    worker(args) if args.worker else parent(args)


if __name__ == "__main__":
    main()
