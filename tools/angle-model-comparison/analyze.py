import argparse
import hashlib
import json
from collections import defaultdict
from pathlib import Path

import numpy as np

from compare import forward, validate_candidate, verify_oracles


def native_validation(case, row, tolerance=1e-7, roundoff=2e-10):
    if not row["returned"]:
        return {"feasible": False}
    angle = np.array(case["base"]) + np.deg2rad(row["yaw"])
    controls = np.array(case["magnitude"])[:, None] * np.column_stack((np.cos(angle), np.sin(angle)))
    positions = np.array(row["positions"]) - case["position"]
    replay, _ = forward(case, controls)
    error = float(np.max(np.abs(positions - replay)))
    if error > 2e-10:
        raise AssertionError((case["name"], row["method"], "Java replay mismatch", error))
    result = validate_candidate(case, positions, controls, tolerance + roundoff)
    if abs(max(0.0, row["wallViolation"]) - result["wallViolation"]) > roundoff:
        raise AssertionError((case["name"], "Java wall residual mismatch"))
    result["originalFeasible"] = row["feasible"]
    if abs(row["objective"] - result["objective"]) > 2e-10:
        raise AssertionError((case["name"], "Java objective mismatch"))
    return result


def analyze(problems, native, reports):
    data = problems.read_bytes()
    cases = {case["name"]: case for case in json.loads(data)}
    for case in cases.values():
        verify_oracles(case)
    methods = defaultdict(list)
    native_report = json.loads(native.read_text())
    if native_report["problemsSha256"] != hashlib.sha256(data).hexdigest():
        raise AssertionError("Native baseline used different problems")
    for row in native_report["results"]:
        result = native_validation(cases[row["case"]], row)
        method = row["method"]
        if method == "main-slp":
            method += "/dual-seed" if row["seed"] == -1 else "/common-seeds"
        methods[method].append({**row, **result})
    for dual in methods["main-dual"]:
        slp = next((row for row in methods["main-slp/dual-seed"] if row["case"] == dual["case"]), None)
        chosen = dual if dual["feasible"] or slp is None else slp
        methods["main-dual-then-slp"].append({**chosen, "solveSeconds": dual["solveSeconds"]
                                               + (0.0 if dual["feasible"] or slp is None else slp["solveSeconds"])})
    for path in reports:
        report = json.loads(path.read_text())
        if report["problemsSha256"] != hashlib.sha256(data).hexdigest():
            raise AssertionError("NLP benchmark used different problems")
        for row in report["results"]:
            scored = dict(row, originalFeasible=row["feasible"])
            residuals = ("wallViolation", "rawWallViolation", "magnitudeError", "dynamicsError", "replayDifference")
            scored["feasible"] = max([row.get(key, float("inf")) for key in residuals]
                                     + [row["constantViolation"]]) <= 1e-7 + 2e-10
            methods[report["solver"] + "/" + row["formulation"]].append(scored)
    best = {}
    for rows in methods.values():
        for row in rows:
            if row["feasible"]:
                name = row["case"]
                sign = 1 if cases[name]["objective"]["sense"] == "MAX" else -1
                best[name] = max(best.get(name, -float("inf")), sign * row["objective"])
    summary = {}
    for method, rows in methods.items():
        grouped = defaultdict(list)
        for row in rows:
            grouped[row["case"]].append(row)
        times = [row["solveSeconds"] for row in rows]
        failures = [name for name, group in grouped.items() if not any(row["feasible"] for row in group)]
        gaps = {}
        for name, group in grouped.items():
            sign = 1 if cases[name]["objective"]["sense"] == "MAX" else -1
            feasible = [sign * row["objective"] for row in group if row["feasible"]]
            if feasible:
                gaps[name] = best[name] - max(feasible)
        summary[method] = {
            "runs": len(rows), "feasibleRuns": sum(row["feasible"] for row in rows),
            "cases": len(grouped), "feasibleCases": len(grouped) - len(failures), "failedCases": failures,
            "witnessedFeasibleCases": sum(name in best for name in grouped),
            "unsolvedWitnessedCases": [name for name in failures if name in best],
            "medianSeconds": float(np.median(times)), "p95Seconds": float(np.percentile(times, 95)),
            "maxSeconds": max(times), "totalSeconds": sum(times),
            "maxBestObservedObjectiveGap": max(gaps.values(), default=None), "objectiveGaps": gaps,
            "firstFeasibleSeconds": {
                name: min((sum(row["solveSeconds"] for row in group[:index])
                           + (row.get("firstFeasibleSeconds") or row["solveSeconds"]))
                          for index, row in enumerate(group) if row["feasible"])
                for name, group in grouped.items() if any(row["feasible"] for row in group)
            },
            "bestOfAllSeedsSeconds": {name: sum(row["solveSeconds"] for row in group)
                                      for name, group in grouped.items()},
        }
    return {"problemsSha256": hashlib.sha256(data).hexdigest(), "searchTolerance": 1e-7,
            "scoringRoundoffAllowance": 2e-10, "methods": summary,
            "bestObservedNormalizedObjective": best}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--problems", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--reports", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    result = analyze(arguments.problems, arguments.native, arguments.reports)
    arguments.output.write_text(json.dumps(result, indent=2) + "\n")
    for method, row in result["methods"].items():
        print(f'{method:24} feasible={row["feasibleRuns"]}/{row["runs"]} '
              f'cases={row["feasibleCases"]}/{row["cases"]} '
              f'median={row["medianSeconds"]:.4f}s max={row["maxSeconds"]:.3f}s '
              f'failures={row["failedCases"]}')
