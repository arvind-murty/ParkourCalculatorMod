# Clarabel iterative refinement off in the disk solve

`numerics/disk.py` `_solve` sets `iterative_refinement_enable = False` (2026-11-23). Two Real11 A/Bs, the first on main
`a7ddc3be` (2026-11-22/23), the second on main `580656d9` after its status mapping (every Clarabel status but solved or a
limit is an infeasible verdict). Clarabel 0.11.1, CasADi 3.8.1, NumPy 2.5.4, SciPy 1.18.1, Python bridge, single-threaded
BLAS, shared machine (load average 2.0 to 6.4). Arm A = main; arm B = main with refinement off through an uncommitted
environment switch in `_solve` (each run recorded it in `source.patch`). Transitions were compared with the migration parity
harness (`tools/profiling/parity.py` at `364f5699`). Motivation: on 371 alarmed requests of the post-alarm study each disk solve
took 2.20 instead of 3.84 ms with refinement off, all still `Solved`, objectives within 2.1e-8.

## 580656d9 ([comparison log](compare-580656d.log))

Fixed work (`research/prefix-reuse-keys/2026-11-21/extended-fixedwork100.json`, 100 solver calls), A/B alternated 3 times,
`--profile-mode none`:

| arm | search s | mean | Clarabel / IPOPT calls | confirmations | screen retried | candidate feasible / infeasible / retried | transitions |
|---|---|---|---|---|---|---|---|
| A refinement on | 40.06, 38.91, 39.11 | 39.36 | 71 / 5 | 5 | 1 | 32 / 24 / 1 | 19 |
| B refinement off | 37.70, 37.29, 37.52 | 37.50 | 70 / 5 | 5 | 0 | 31 / 25 / 0 | 19 |

Search time -4.7%; every pair's transitions are identical, exactly. A bookkeeping pair logged every request (95 each): on the
89 fingerprints both made every verdict agrees; two infeasible requests change only the status name (`AlmostPrimalInfeasible`
and `NumericalError` with refinement, `PrimalInfeasible` without). Each arm made 6 repair requests the other did not: where the
relaxed optimum is not unique the returned point can move (controls up to 0.01 apart at equal objective), so a repair loop
can meet a different obstacle first. The search's decisions are the same.

Full extended case (`cases/real11/extended-anchors.json`, 300 s), one run per arm, `--profile-mode bookkeeping`:

| arm | termination | solver calls | Clarabel requests: count, mean ms, total s | statuses | confirmations / IPOPT | transitions |
|---|---|---|---|---|---|---|
| A | deadline | 713 | 672, 17.01, 11.43 | Solved 435, AlmostSolved 1, PrimalInfeasible 233, AlmostPrimalInfeasible 1, NumericalError 2 | 41 / 41 | 19 |
| B | deadline | 732 | 691, 11.46, 7.92 | Solved 453, AlmostSolved 1, PrimalInfeasible 237 | 41 / 41 | 19 |

On the 654 requests both made no verdict differs; transitions identical. Short case (`cases/real11/physical-anchors.json`):
both T64, objective -1093.238302878354, `exhausted`; one intermediate incumbent differs by 7e-9.

## a7ddc3be

Fixed work as above: search 45.66 s (A) against 43.25 s (B) mean of 3, -5.3%, every pair identical. With refinement on, 5
screen solves needed a retry and 3 ended undecided, and 2 candidate requests ended undecided and fell back to IPOPT; without it
none did. Full extended case, 300 s: B made 421 solver calls against 382 (+10.2%) with identical transitions.
