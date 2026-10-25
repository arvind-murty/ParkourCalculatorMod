import argparse
import json
import math
from collections import defaultdict
from pathlib import Path


def facing_pin_conflicts(case):
    absolute = defaultdict(list)
    relative = defaultdict(list)
    for row in case.get("facingWalls", []):
        if "t2" not in row:
            absolute[row["t1"]].append(row)
        elif row["op"] == "MINUS":
            relative[row["t1"], row["t2"]].append(row)

    def band(rows):
        lo = max((row["rhs"] for row in rows if row["cmp"] in ("GE", "EQ")), default=-math.inf)
        hi = min((row["rhs"] for row in rows if row["cmp"] in ("LE", "EQ")), default=math.inf)
        return (lo, hi) if 0 <= hi - lo < 1.0 else None

    conflicts = []
    for (tick, previous), rows in relative.items():
        current = band(absolute[tick])
        earlier = band(absolute[previous])
        allowed = band(rows)
        if current is None or earlier is None or allowed is None:
            continue
        low = current[0] - earlier[1]
        high = current[1] - earlier[0]
        shift = 360.0 * round(((allowed[0] + allowed[1]) - (low + high)) / 720.0)
        low, high = low + shift, high + shift
        gap = max(low - allowed[1], allowed[0] - high)
        if gap > 0:
            conflicts.append({"tick": tick, "previousTick": previous,
                              "possibleTurnDegrees": [low, high], "requiredTurnDegrees": list(allowed),
                              "separationDegrees": gap,
                              "rowNames": [row["name"] for row in absolute[tick] + absolute[previous] + rows]})
    return conflicts


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--problems", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    results = [{"case": case["name"], "conflicts": facing_pin_conflicts(case),
                "scope": "Narrow absolute facing bands and narrow relative-turn bands only; no conflicts found does not establish feasibility."}
               for case in json.loads(arguments.problems.read_text())]
    arguments.output.write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))
