# Initial seven-window: shift the objective endpoint

## Scope and reproduction

Automatic endpoint selection is **not implemented**. Commit `4dd969c` implements detection and preflight logging only. This probe manually changes the initial window endpoint, using production `LongRunSolver.sliceScenario`, `sliceConstraints`, `solveWindow`, `ClosedFormSolve.optimizeRobust`, and shared `WindowCandidateValidation`. It tests whether an endpoint change helps before introducing a route-wide policy.

Input: `~/Downloads/LinkcraftII_Competition_01/real/11.json`, same original initial position/velocity/yaw for every case. Fixed margin 0.0003, dual iteration cap 100, current experimental EPS2=1e-24. Exact replay checks either the first 30 inputs (commit 3 jumps) or first 12 inputs (commit 1). Continuous validation checks every original constraint within the chosen window, with actual inertia threshold checks. The first 12 inputs contain no original constraint rows; their replay acceptance is consequently vacuous. No full-route experiment ran here.

Run the opt-in `InitialSevenWindowShiftProbeTest.compareJumpAndTickShifts` with `-Dpkc.marginProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.InitialSevenWindowShiftProbeTest` (JDK21 Gradle override as in previous investigations). The focused test passed. Each case first probes Z/MAX directly, then independently reruns the production window pipeline to record alternate-objective behavior. Detailed diagnostics are enabled, so elapsed times are observation overhead plus solve time, not benchmarks. The first case also pays JVM warmup.

## Results

| Change | End state | Original rows | Direct MAX Z cap | Z/MAX, commit 3 | Z/MAX, commit 1 | Production window outcome, commit 3 / commit 1 |
|---|---:|---:|---|---|---|---|
| Original 7 jumps | 68 | 33 | Yes | Fail | Fail | Z/MIN / Z/MIN |
| Minus one jump: 6 jumps | 64 | 26 | No | Fail replay | Pass | X/MAX / Z/MAX |
| Plus one jump: 8 jumps | 71 | 37 | No | Pass after inertia retry | Pass after inertia retry | Z/MAX / Z/MAX |
| Minus one tick | 67 | 32 | Yes | Pass first candidate | Pass first candidate | Z/MAX / Z/MAX |
| Plus one tick | 69 | 33 | No | Pass first candidate | Pass first candidate | Z/MAX / Z/MAX |

A direct cap is a risk indicator, not proof of failure: tick67 is directly capped yet succeeds. Shortening to tick67 drops the troublesome Z68 row. Shortening to tick64 drops seven rows in total, so those successful shorter solves do not establish feasibility through the original 7th jump. Extending to tick69 preserves all 33 original rows and adds one simulated input. Extending to tick71 preserves those rows and adds four rows from the next jump.

## Candidate-by-candidate acceptance

Signed margins below zero mean violation. The table shows the commit-3 run; continuous trajectories and acceptance outcomes for commit1 are identical except tick64 is accepted because its failed replay row is outside the committed prefix.

| End | Candidate / assumed pattern | Worst continuous row and signed margin | Worst committed replay row and signed margin | Accepted |
|---|---|---|---|---|
| 68 | 0, free | Z68 LE: -0.011663350627 | Z20 LE: +0.008058210870 | No |
| 68 | 1, zero X0/X63/Z0 | X58 LE: -16.800000011921 | X29 LE: -7.199999988079 | No |
| 64 | 0, free | Z63 LE: +0.000299999972 | Z20 LE: -0.000079167380 | No for commit3; yes for commit1 |
| 71 | 0, free | X67 GE: -0.119423799241 | Z20 LE: +0.008531678142 | No |
| 71 | 1, zero X0/X63/Z0 | X58 LE: +0.000299999371 | Z20 LE: +0.000106027667 | Yes |
| 67 | 0, free | X58 LE: +0.000299999025 | Z20 LE: +0.000085988396 | Yes |
| 69 | 0, free | Z68 LE: +0.000299999960 | Z20 LE: +0.000094328394 | Yes |

The end68 retry's severe failure is the already recorded EPS24/fallback interaction; the unchanged first candidate also fails, so sharpening epsilon did not fix the original endpoint. At end71 the replay-derived fixed inertia pattern succeeds on its retry. At end69 the initial unpatterned model already produces an accepted trajectory under actual continuous inertia threshold checks.

## Timing observations

From the production `solveWindow` rerun's LRS-OBJ trace (not the separate direct probe):

| End | Primary Z/MAX ms, commit3 | Primary Z/MAX ms, commit1 | Full window ms, commit3 | Full window ms, commit1 |
|---|---:|---:|---:|---:|
| 68 | 14.116 | 4.597 | 23.547 | 6.820 |
| 64 | 1.535 | 1.598 | 8.891 | 1.686 |
| 71 | 7.925 | 5.554 | 8.029 | 5.652 |
| 67 | 1.396 | 1.280 | 1.480 | 1.348 |
| 69 | 1.685 | 1.517 | 1.775 | 1.569 |

These single measurements with detailed CSV diagnostics do not establish a speed ranking.

## Conclusion and next step

Both forward shifts rescue the initial Z/MAX attempt at fixed margin. The one-tick extension preserves the original constraints with the least added lookahead; the one-jump extension includes the next landing box and still succeeds. Neither establishes that the full route will solve or that the committed trajectory is best for future jumps. The next policy experiment can select a forward endpoint when the detector finds a direct cap, preserve the commit span, and log every actual selected endpoint. Current production behavior still only logs the detector result.

## Evidence

- [Focused probe source](../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/InitialSevenWindowShiftProbeTest.java)
- [Summary CSV](initial-seven-window-shifts-2026-10-23/summary.csv)
- [All validation candidates](initial-seven-window-shifts-2026-10-23/candidates.csv)
- [Every constraint margin](initial-seven-window-shifts-2026-10-23/constraint-margins.csv)
- [Production trace, including objective and per-attempt times](initial-seven-window-shifts-2026-10-23/solver-trace.txt)
- [Settings](initial-seven-window-shifts-2026-10-23/metadata.txt), [focused test log](initial-seven-window-shifts-2026-10-23/focused-test.log)
