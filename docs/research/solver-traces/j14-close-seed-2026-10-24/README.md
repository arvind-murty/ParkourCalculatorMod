# Close-miss-only seeding and why j14 needed two pattern revisions

2026-10-24. User requested seeding only close misses and asked why the successful sequence needed two pattern updates to reach one. New TEST-ONLY mode automatically seeds when previous max actual-window violation<=.01 blocks, assumed positional-model violation<=1e-8 and at least one inertia assumption mismatch exists. .01 is an explicit provisional experimental definition of close, not a physics constant or universal theorem. No pass number hardcoding; all earlier patterns stay cold because their outputs fail these conditions. Existing compensated seed rejection stays commented out, finite/nonnegative assertion stays, decoder unchanged. No new production edit.

## Exact sequence

| Solve | Assumed zeroings | Assumed positional miss | Actual continuous miss | Action afterward |
|---|---|---:|---:|---|
|0 |None |.879105563646 |.883040338417 |Infer first pattern, cold retry |
|1 |X62,X89..90,X100..102,Z81 |.236166363409 |.332352548739 |Infer revised pattern, cold retry |
|2 |X63,X88..90,X101..102 |0 |.006556921486 |Close inertia miss, compensated warm repair |
|3 |X63,X84..90,X101..102,Z82 |Selected candidate valid |0 |Return retained iteration82 |

Solve0 hits10000 with a LARGE violation even without inertia; this was not an accurate no-inertia optimum. Its replay chooses a first pattern based on those particular poor angles. Solve1 also reaches10000 without a feasible fixed-model solution. It does not satisfy several checks it assumed zero: fixed-model incoming X62=-.0214905161, Z81=+.0654284025, X100=-.0054756862. All exceed .005 in magnitude. These are failed constrained-optimizer outputs, not legitimate permission to zero those velocities.

Full actual continuous replay of solve1 instead zeros X63,X88..90,X101..102. Rebuilding from that replay removes X62/Z81/X100, adds X63/X88, and changes the optimization coefficients. The next cold solve (solve2) has valid assumed positional constraints and valid assumed-zero checks. Remaining mismatches are newly occurring Z82 and X84..87 that were not included. Their physical effect is small, causing X94 miss .006556921486. That is when preserving this path is appropriate.

Therefore this was not two small corrections to an already accurate no-inertia solution. It was two cold attempts on different fixed patterns before an internally feasible near-path emerged. The outer pattern loop uses deterministic threshold outcomes of even infeasible candidates as new hypotheses. In this sample changing the hypotheses also changes numerical convergence drastically. No proof that solve1's pattern was inherently infeasible or that exactly two iterations are necessary is established. The exact numerical failure inside the earlier10000-iteration solves remains distinct from this observed pattern sequence.

## Automatic policy result

The trace explicitly records eligibility:

- Before solve1: actual .88304, assumed .87911 -> no seed.
- Before solve2: actual .33235, assumed .23617 -> no seed.
- Before solve3: actual .00655692, assumed0, five inertia mismatches -> seed.

This reproduces the successful manually delayed experiment exactly in all non-timing pass fields. Compensated solve3 reaches its first valid candidate77, improves78 and82, and returns82 through retention. Terminal373 still NO_DESCENT/decoder-invalid; saved candidate has full continuous, committed exact, all assumed threshold checks and additionally full exact replay valid. Full continuous minclearance .0001301913, committed exact .0005075590, full exact .0002668437. No claim of global optimality.

## Reproduce

Same saved state55, Z/MAX103, all70 positional constraints, fixed margin .0003, EPS24, cap10000, four pattern passes at most. One focused opt-in test passes; no full suite, graph or route. Production warm/cold selection not yet integrated. User fixture untouched.

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=j14-close-seed :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/j14-close-seed-2026-10-24/focused.log 2>&1
```

Run from isolated core-only `/tmp/pkc-endpoints-fixed`, production b98f87f+c392645, with archived probe. [Every solve](passes.csv), [trace and eligibility decisions](solver-trace.txt), [selection](j14-seed-1.0-pass3-retention.csv), [JUnit result](focused-result.xml), [probe source](InertiaMultiplierSeedProbeTest.java). Exact check velocities from [original cold pass1 audit](../j14-retained-2026-10-24/j14-seed0.0-pass1-inertia.csv), replicated here under the close-only policy. Historical [matched delayed repair](../j14-near-repair-2026-10-24/README.md).
