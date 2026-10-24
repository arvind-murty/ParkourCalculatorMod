# j14 compensation with preservation rejection commented out

2026-10-24, user-requested experiment. Only the test-only seeding probe changed. The block that resets new band prices when costate-preservation verification fails is commented out. Verification and per-tick errors remain diagnostic; nonfinite/negative multiplier seeds are still rejected by an explicit assertion. Decoder and production candidate retention unchanged. Main starting HEAD is the guard-ownership clarification following d451e88; isolated production sources are c392645 over b98f87f. User fixture modification untouched.

## Configuration and comparison

Same state55, Z/MAX@103,48 inputs, all70 positional rows (Z102 retained,103 uncapped), margin .0003, EPS2=1e-24, dual cap10000, candidate retention enabled. Compensation applied from the INITIAL no-zeroing solution to the FIRST inertia pattern, not delayed until the close cold pass2 candidate. No initial condition or margin change.

| Measurement | Guard enabled | Rejection commented out |
|---|---:|---:|
|Initial no-zeroing solve |10000 cap |10000 cap |
|Initial actual miss |.883040338417 |.883040338417 |
|Inertia retry starting yaw change |171.281598deg |.000011432051deg |
|Inertia retry starting actual miss |3.56419754051 |.883040287750 |
|Inertia retry iterations |10000 cap |2933 NO_DESCENT |
|Inertia retry final actual miss |1.46431155710 |.983792521127 |
|Inertia retry final assumed positional miss |.826669740517 |.721106358255 |
|Saved feasible intermediate |None |None |
|Accepted window |No |No |

With rejection disabled,30 nonzero multipliers seed the retry. The relative preservation check still reports false at tick78, but no longer discards the compensation. The decoded starting path is now effectively preserved. This resolves the unintended starting-angle jump, but does not make this original nonconverged trajectory solve. Retry has four inertia mismatches, one terminal decoder fallback, projected residual1.16049126635, committed replay miss .136310829328. Its actual miss is worse than the original .88304, so the existing no-improvement condition stops the pattern loop after two solves.

Do not conflate this with the [successful delayed seed experiment](../j14-near-repair-2026-10-24/README.md): that starts from the later close .00655692-miss candidate, whose seed already passed the guard, and saves a valid candidate at82. We did not rerun that unchanged-success transition here. Removing the guard alone is not enough for this initial .88304-miss path; the previous cold exploration reached a different better pattern before warm repair succeeded.

## Evidence and reproduction

[Every solve](passes.csv), [initial seed audit](j14-seed-1.0-pass1-initial.csv), [preservation errors](j14-seed-1.0-pass1-seed-errors.csv), [retention audit](j14-seed-1.0-pass1-retention.csv), [trace](solver-trace.txt), [JUnit result](focused-result.xml), [source snapshot](InertiaMultiplierSeedProbeTest.java). Candidate paths, per-constraint margins and inertia velocities persisted. Guard-enabled numbers are from [original retained j14 run](../j14-retained-2026-10-24/README.md).

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=j14-seed-no-guard :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/j14-seed-no-guard-2026-10-24/focused.log 2>&1
```

From the isolated core-only worktree. One focused test, zero failures/errors; no full suite/route/graph run. Timing in passes/outcomes includes retention auditing and is subject to JVM warmup; no benchmark claim. The guard rejection remains commented out in the current probe source as requested. Archived prior sources preserve the enabled version. `preservation_verified=false` remains an honest diagnostic, not a seed-use flag.


## Why this run never reaches the previous close candidate

The successful diagnostic used COLD initialization for passes0,1,2, then seeded only pass3. Its sequence was .8830403384 -> .3323525487 -> .0065569215 -> valid retained path. The new no-guard experiment instead seeds pass1 immediately from pass0: .8830403384 -> .9837925211 -> stop for no improvement. The initial input state and first inferred pattern are identical; initialization of that first patterned dual solve is the changed variable. Because it takes a different finite numerical trajectory, it produces different angles, hence different inferred threshold patterns or, here, stops before the next pattern can be attempted. The close candidate was never reached and then lost in the new run; it belongs to an alternative cold discovery trajectory that we replaced early. No claim that the convex dual has different local optima is implied by these nonconverged results.

This comparison means automatic warm starts should not be assumed to dominate cold discovery. A cold alternative when seeded repair fails/worsens would preserve the route to the close candidate demonstrated here. The current test-only hybrid manually delays seeding; there is no automatic production warm/cold selection policy yet. The agent should have made the earlier versus delayed application of seeding clearer before the rerun.
