"""Verify the persisted comparison data against the two historical traces. No solver execution."""
import csv
import re
from pathlib import Path

BASE = Path(__file__).resolve().parent
DATA = BASE / "margin-comparison-2026-10-23"

with (DATA / "margin-comparison-attempts.csv").open() as handle:
    attempts = list(csv.DictReader(handle))
with (DATA / "margin-comparison-states.csv").open() as handle:
    states = list(csv.DictReader(handle))

for label, name in [
    ("normal-full", "linkcraft-real11-first30-normal7-commit1.txt"),
    ("fixed-full", "linkcraft-real11-first30-fixed0003-commit1.txt"),
]:
    trace = (BASE / name).read_text()
    seeds = {}
    for match in re.finditer(
        r"LRS-WIN commit=1 jumps=\[(\d+),(\d+)\) ticks=\[(\d+),(\d+)\) "
        r"seed=\(([^;]+);([^\)]+)\) yaw=([^ ]+)", trace
    ):
        seeds[int(match[1])] = [
            *map(float, match[5].split(",")),
            *map(float, match[6].split(",")), float(match[7]),
        ]
    differences = []
    for row in states:
        if row["run"] != label:
            continue
        actual = [float(row[k]) for k in ("x", "z", "vx", "vz", "incoming_committed_yaw")]
        differences.extend(abs(a - b) for a, b in zip(actual, seeds[int(row["start_index"])]))
    assert differences and max(differences) < 1e-11

    original = {
        tuple(map(int, match.group(1, 2))): match[3]
        for match in re.finditer(
            r"LRS\s+commit=1 window i=(\d+) we=(\d+).*? -> (solved|miss)", trace
        )
    }
    reproduced = {}
    for row in attempts:
        if row["run"] == label:
            reproduced[int(row["start_index"]), int(row["end_index"])] = row["result"]
    assert original == reproduced, label
    print(f"{label}: all {len(original)} window outcomes match; max state difference {max(differences):.3g}")

for index in (0, 11, 12):
    for entry in ("normal", "fixed"):
        for policy in ("normal", "fixed"):
            label = f"{entry}-entry-i{index}-{policy}-policy"
            rows = [row for row in attempts if row["run"] == label]
            expected = index == 0 or (entry == "normal" and (index == 12 or policy == "normal"))
            assert rows and (rows[-1]["result"] == "solved") == expected, label
print("All 12 identical-entry controls match the reported result matrix.")

diagnostic = (DATA / "solver-trace-margin-control-normal-entry-i11-rungs.txt").read_text()
checks = [
    ("cold-3.0E-4", "0.00030000", 0.0536169837646),
    ("cold-0.0012", "0.0012000", 0.174854946575),
    ("warm-ladder", "0.0012000", 0.0),
    ("warm-ladder", "0.00030000", 0.0),
    ("warm-ladder", "0.0000", 0.0),
]
for sequence, margin, expected in checks:
    match = re.search(
        rf"RUNG-DIAG pattern=x@4-10\+z@2 sequence={re.escape(sequence)} "
        rf"margin={re.escape(margin)} .*?exactViolation=([^ ]+)", diagnostic
    )
    assert match and abs(float(match[1]) - expected) < 1e-11, (sequence, margin)
print("All five held-pattern cold/warm results match the reported replay violations.")
