# Corrected post-turn endpoint probes, 2026-10-24

## Inventory and setup

Main HEAD before edits: `478d14f371e4502fa3eeda76693c5ca3db1de420`. Isolated execution worktree: `/tmp/pkc-endpoints-fixed`, HEAD `b98f87f275d3fd715a8cc3998c39d9158ee76d67`. Its only tracked change is core-only Gradle settings. No production source changed. The pre-existing user j127 capture modification was left untouched. See [inventory](inventory-before.txt).

The saved probe still used90/99 when execution was interrupted. This run explicitly selects90/103 and asserts no direct objective cap, retention of every sliced original row, and retention of the last capped Z state89/102.

Both start from the exact saved state55 in [entry.txt](../manual-coupled-window-2026-10-24/entry.txt). j13 uses Z/MIN at90 (35 inputs,56 positional rows); j14 uses Z/MAX at103 (48 inputs,70 positional rows).103 is98+5, inside jump15. Fixed margin .0003, EPS2=1e-24, dual iteration cap10000. Shared validation checks actual-inertia continuous motion over the entire window and exact replay over the proposed committed prefix through64. All original rows remain.

This is the test-side closed-form dual/pattern loop, up to four pattern solves, stopping on acceptance or no improvement. It does not execute SLP, B&B, the complete graph, or a full route. Seed variants are cold, full carried positional weights, quarter carried weights, and verified costate compensation. CSV seed_scale=-1 denotes the compensation algorithm, not negative multipliers.

## Results

Neither endpoint produced an accepted candidate. j13 best actual continuous miss is .00939142456229547 at X79 lower wall, despite zero assumed-model violation and zero committed-prefix replay violation. Verified compensation preserves its starting angles but its retry stalls. j14 first unpatterned solve already fails convergence/assumed constraints; moving the endpoint beyond its direct cap does not make this numerical problem converge. Cold pass2 gets closest (.006556921486094325 at X94 upper wall), but has five mismatched inertia assumptions. Its following retry worsens.

The j14 compensation guard declines preservation and resets new band weights to0, retaining positional weights. That result is identical to ordinary full carry. It must not be described as a successful angle-preserving trial. The archive records preservation_verified=false and a171.28 degree starting yaw change. No conclusion about impossibility of a better joint compensation fit is established.

| Endpoint | Seed | Accepted | Passes | Best continuous violation (blocks) | Dual solve time (ms) |
|---|---|---|---:|---:|---:|
| 13 | Cold | false | 2 | 0.00939142456 | 218.528 |
| 13 | Carry | false | 2 | 0.00939142456 | 9.534 |
| 13 | Quarter carry | false | 2 | 0.00939142456 | 149.304 |
| 13 | Verified compensation | false | 2 | 0.00939142456 | 3.262 |
| 14 | Cold | false | 4 | 0.00655692149 | 523.612 |
| 14 | Carry | false | 2 | 0.883040338 | 429.530 |
| 14 | Quarter carry | false | 3 | 0.7589292 | 419.065 |
| 14 | Verified compensation | false | 2 | 0.883040338 | 437.527 |

Time is accumulated kernel solve time per variant, not end-to-end validation/export time. JVM warmup affects comparisons; no repeated performance benchmark was run. Entire focused Gradle invocation finished in3s.

## Every attempted solve

Patterns use local input indices: add55 for global tick. Pass0 has no assumed zeroing. continuous means actual-inertia continuous path; assumed means fixed-pattern affine path. Violation0 is valid against original rows, not a claim of .0003 slack.

| Endpoint | Seed | Pass | Iterations | Stop | Continuous miss | Prefix replay miss | Assumed-model miss | Inertia mismatches | Solve ms |
|---|---|---:|---:|---|---:|---:|---:|---:|---:|
| 13 | Cold | 0 | 167 | NO_DESCENT | 0.00939142456 | 0 | 0 | 8 | 17.681 |
| 13 | Cold | 1 | 10000 | ITERATION_LIMIT | 3.56048016 | 2.53954974 | 3.47921483 | 5 | 200.847 |
| 13 | Carry | 0 | 167 | NO_DESCENT | 0.00939142456 | 0 | 0 | 8 | 2.327 |
| 13 | Carry | 1 | 368 | NO_DESCENT | 3.07827937 | 2.41517392 | 3.07827937 | 3 | 7.207 |
| 13 | Quarter carry | 0 | 167 | NO_DESCENT | 0.00939142456 | 0 | 0 | 8 | 2.924 |
| 13 | Quarter carry | 1 | 10000 | ITERATION_LIMIT | 3.02388976 | 2.37834224 | 3.0098854 | 4 | 146.380 |
| 13 | Verified compensation | 0 | 167 | NO_DESCENT | 0.00939142456 | 0 | 0 | 8 | 1.784 |
| 13 | Verified compensation | 1 | 227 | NO_DESCENT | 16.9157391 | 3.8933465 | 16.3476353 | 22 | 1.478 |
| 14 | Cold | 0 | 10000 | ITERATION_LIMIT | 0.883040338 | 0.0631066583 | 0.879105564 | 7 | 166.230 |
| 14 | Cold | 1 | 10000 | ITERATION_LIMIT | 0.332352549 | 0 | 0.236166363 | 4 | 97.797 |
| 14 | Cold | 2 | 883 | RECOVERED_INPUT_STALL | 0.00655692149 | 0 | 0 | 5 | 12.174 |
| 14 | Cold | 3 | 10000 | ITERATION_LIMIT | 1.12866328 | 0.15766089 | 0.946899859 | 5 | 247.411 |
| 14 | Carry | 0 | 10000 | ITERATION_LIMIT | 0.883040338 | 0.0631066583 | 0.879105564 | 7 | 167.136 |
| 14 | Carry | 1 | 10000 | ITERATION_LIMIT | 1.46431156 | 0.137967001 | 0.826669741 | 3 | 262.395 |
| 14 | Quarter carry | 0 | 10000 | ITERATION_LIMIT | 0.883040338 | 0.0631066583 | 0.879105564 | 7 | 207.008 |
| 14 | Quarter carry | 1 | 10000 | ITERATION_LIMIT | 0.7589292 | 0.14815183 | 0.54761553 | 3 | 203.813 |
| 14 | Quarter carry | 2 | 667 | LAMBDA_LIMIT | nan | nan | nan | -1 | 8.244 |
| 14 | Verified compensation | 0 | 10000 | ITERATION_LIMIT | 0.883040338 | 0.0631066583 | 0.879105564 | 7 | 164.995 |
| 14 | Verified compensation | 1 | 10000 | ITERATION_LIMIT | 1.46431156 | 0.137967001 | 0.826669741 | 3 | 272.532 |

Quarter-carry j14 pass2 returns null at LAMBDA_LIMIT667; no recovered candidate exists for that row. The first execution aborted because the probe incorrectly asserted every dual result was nonnull. We corrected the probe to record null outcomes and continue; production code was unchanged. The first execution log/pass table/assertion are preserved separately. The second execution completed one focused test with no failures/errors. Solver misses are experimental outcomes.

## Reproduction

Run from the isolated core-only worktree with the archived probe source and the pre-existing WindowCandidateValidation/diagnostics production code at its pinned HEAD:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=post-turn-clear-cap :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/post-turn-clear-cap-2026-10-24/focused.log 2>&1
```

Only the focused class was selected. It has opt-in assumptions and reads the original downloaded TAS. Archived per-pass CSVs contain yaws, paths, per-row margins, costates, multipliers, initial preservation diagnostics, and iteration history. [passes.csv](passes.csv), [outcomes.csv](outcomes.csv), [trace](solver-trace.txt), [JUnit result](focused-result.xml), [source snapshot](InertiaMultiplierSeedProbeTest.java).

## SHA256 inputs

- `/Users/arvindmurty/CS/ParkourCalculatorMod/core/src/test/java/de/legoshi/parkourcalc/core/anglesolver/solver/InertiaMultiplierSeedProbeTest.java`: `ff0908d08043463301d8b3a459b0874f0025e2a96d0a52b479008b405e9bac68`
- `/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json`: `af5d2d1bc6e0ab466f1b6cf309f3a26e76e4a63765ac8b15ff02fb4ad55d28bf`
- `/Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/manual-coupled-window-2026-10-24/entry.txt`: `2e0ddf097e0ce02a4308d62056e7e08db0d0886c5ad3436b5b7abf50d64fbd89`
