import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from compare import initial_controls, verify_oracles


def write_inputs(problems, output, seeds):
    data = problems.read_bytes()
    cases = json.loads(data)
    for case in cases:
        verify_oracles(case)
        case["problemsSha256"] = hashlib.sha256(data).hexdigest()
        case["nativeSeeds"] = []
        for seed in range(seeds):
            controls = initial_controls(case, seed)
            yaw = np.rad2deg(np.arctan2(controls[:, 1], controls[:, 0]) - np.array(case["base"]))
            case["nativeSeeds"].append(((yaw + 180.0) % 360.0 - 180.0).tolist())
    output.write_text(json.dumps(cases, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--problems", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seeds", type=int, default=3)
    arguments = parser.parse_args()
    write_inputs(arguments.problems, arguments.output, arguments.seeds)
