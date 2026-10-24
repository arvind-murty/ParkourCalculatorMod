# Retaining validated intermediate candidates: j13

2026-10-24. Implementation commit `c392645`. Starting main HEAD before edits was the decoder/path-preservation clarification commit; isolated execution worktree `/tmp/pkc-endpoints-fixed` remains pinned at b98f87f plus copied production sources and core-only settings. User's pre-existing j127 capture modification untouched. Historical [per-iteration control](../j13-inertia-detail-2026-10-24/README.md) and [90/103 probe](../post-turn-clear-cap-2026-10-24/README.md) remain intact.

## Implemented behavior

`CostateDualSolver.CandidateObserver` exposes the current costates and multipliers at loop-entry/final samples. `ClosedFormSolve.runLadder` attaches `DualCandidateRetention` by default, for every margin/pattern solve. No changes to Newton steps, stopping rules, inertia pattern discovery, or decoding. `Config.retainIntermediateCandidates=false` provides a focused control.

The collector decodes with the existing facing fold, simulates continuous motion with ACTUAL inertia checks, and screens all original window constraints. A finite feasible candidate that could improve the incumbent is checked through `WindowCandidateValidation`, which also validates Minecraft replay over the configured horizon. It ranks accepted candidates by continuous objective including the existing smoothing penalty, with the declared MIN/MAX sense. It copies the chosen multipliers and retains the candidate angles. Optimization continues. If the terminal result is invalid or scores worse, the caller returns the retained candidate with its own multipliers/costate metadata. A retained candidate also survives a null terminal dual result. The continuation/margin warm-start multipliers remain the terminal dual weights, preserving the existing optimization sequence.

For intermediate retention this uses full actual-inertia continuous checks even when the ordinary full-replay final path historically checked replay alone. Existing final-candidate acceptance has not been loosened. For receding-horizon windows the collector uses the existing full-continuous/committed-replay rule. It does not require .0003 clearance on every original row; .0003 is the surrogate optimization margin.

## Seeded j13 experiment

Same exact saved state55, all56 positional constraints, objective Z/MIN@90, fixed .0003, EPS2=1e-24, dual cap10000. The test uses the previously verified compensating inertia multiplier initializer. This initializer remains TEST-ONLY, not a new production change.

| Stage | Without retention | With retention |
|---|---|---|
| Initial no-zeroing solve |167 iterations; actual miss .0093914245623 |Same |
| Seeded inertia retry terminal |227 NO_DESCENT; decoded miss16.915739075 |Same terminal iterate |
| Returned candidate |Rejected terminal path |Accepted iteration114 |
| Full continuous violation |16.915739075 |0 |
| Committed Minecraft replay violation |3.893346499 |0 |
| Selected final Z90 |Invalid path |-1090.631257180407 |

The first incumbent is saved at5, then improved at18,50,51,52,78 and114. There are seven saved improvements. Every chosen path is validated before saving. Terminal decoding still replaces27 directions; the final candidate is no longer the only available result.

Selected iteration114: minimum continuous clearance .0000503982037; minimum committed replay clearance .000173631099. Its full-window replay has a future, uncommitted violation .000112079218, which is outside the required exact horizon and is NOT claimed valid. The full continuous path and all of its inertia assumptions pass. This distinction matters when retaining/using an exact suffix later.

`*-retention.csv` identifies selected and terminal iterations separately. The path/margins/inertia files for pass1 describe the selected candidate; `*-costates.csv`, `*-multipliers.csv` and solver stop/residual columns still describe the terminal dual for diagnostic comparison. `*-retained-multipliers.csv` contains the selected iteration's actual weights. The per-iteration files retain the full underlying trajectory.

## Production closed-form control

The production entry point is `ClosedFormSolve.optimizeRobust` with robust margin array containing ONLY .0003, replayTickLimit9 and dualMaxIterations10000. This still starts new inertia patterns COLD. It is intentionally not silently upgraded with the test's compensating initializer.

| Retention | Initial result | Cold patterned retry | Accepted | Measured total ms |
|---|---|---|---|---:|
| false | .0093914245623 actual miss |10000 cap;3.56048015608 continuous miss | false | 318.794 |
| true | .0093914245623 actual miss |10000 cap;3.56048015608 continuous miss | false | 182.621 |

No validated intermediate incumbent is discovered on either production cold trajectory, so retention alone cannot rescue that entry point. This is a distinct outcome from the seeded experiment. Porting the verified initializer remains separate follow-up work.

## Timing and checks

Only three focused classes ran: retention unit tests (2), shared validator tests (5), opt-in j13 probe (1). Eight tests, zero failures/errors. No full suite or route run. Unit coverage checks MIN/MAX improvement selection, keeping the better path after a worse candidate, copying saved multipliers, preferring a valid saved candidate to an invalid terminal one, and rejecting fictitious inertia feasibility.

Seeded retry collector audit time in the final run: 1.798ms over227 iterations. Per-rung audit timing is also logged for production cold solves. Total production times are single sequential measurements with JVM warmup effects; they are not evidence that retention makes the solver faster. The seeded detailed probe includes extra reflection/per-iteration audit/export overhead, so its total solve timing is not a clean performance comparison.

## Deferred decoder change

**Flagged for later review:** `FacingPrefold.expand` and `JumpLinearModel.recoverAlongCostate` replace costate directions below1e-9 with the objective direction. Disabling/changing that default is deferred. Neither source was modified in this implementation. Candidate retention deliberately uses the current decoder, then validates its output.

## Reproduce

From the isolated core-only worktree with archived modified production files and probe sources:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=j13-retained :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.DualCandidateRetentionTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.WindowCandidateValidationTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/j13-retained-2026-10-24/focused.log 2>&1
```

[Selection diagnostics](j13-seed-1.0-pass1-retention.csv), [production control](production-control.csv), [trace](solver-trace.txt), [log](focused.log), and archived source snapshots provide reproduction evidence. Exact original entry is [entry.txt](../manual-coupled-window-2026-10-24/entry.txt); downloaded source remains `/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json`.
