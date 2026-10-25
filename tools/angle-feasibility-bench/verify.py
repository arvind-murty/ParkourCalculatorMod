import argparse
import gzip
import json
from pathlib import Path

import numpy as np

from model import compile_problem, load_export


def verify(directory):
    root = Path(__file__).resolve().parents[2]
    source = load_export(directory / "problems.json.gz", root)[0]
    checked = 0
    minimum_slack = np.inf
    worst_disagreement = 0.
    for path in sorted(directory.rglob("*.json.gz")):
        record = json.loads(gzip.decompress(path.read_bytes()))
        if not isinstance(record, dict) or "method" not in record:
            continue
        successful = record.get("status") in ("feasible", "feasible_initial")
        if not successful:
            if "witness" in record:
                raise ValueError("Unexpected witness in " + str(path))
            continue
        if "witness" not in record or record.get("sha256") != source["sha256"]:
            raise ValueError("Missing witness or incorrect capture in " + str(path))
        problem = compile_problem(source, record["ticks"])
        witness = record["witness"]
        theta = np.array(witness["theta"])
        check = problem.validate(theta)
        if not check["feasible"] or check["magnitude_error"] > 1e-12 or check["matrix_replay_disagreement"] > 1e-9:
            raise ValueError("Replay rejected " + str(path) + ": " + str(check))
        for actual, field in zip(problem.replay(theta), ("positions", "velocities", "controls")):
            np.testing.assert_allclose(actual, np.array(witness[field]), atol=1e-12, rtol=0, err_msg=str(path))
        minimum_slack = min(minimum_slack, check["minimum_slack"])
        worst_disagreement = max(worst_disagreement, check["matrix_replay_disagreement"])
        checked += 1
    if not checked:
        raise ValueError("No witnesses found")
    return {"verified_witnesses": checked, "minimum_slack": minimum_slack, "maximum_matrix_replay_disagreement": worst_disagreement}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path, nargs="?", default=Path(__file__).resolve().parent / "benchmarks/real11-2026-10-25")
    args = parser.parse_args()
    print(json.dumps(verify(args.directory), indent=2))
