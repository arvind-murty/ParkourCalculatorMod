# j14: repair the close cold candidate instead of restarting

2026-10-24. Follows [j14 retention run](../j14-retained-2026-10-24/README.md). Main HEAD before edits0c71cc4; production implementation remains c392645. Only the test probe changed. Same fixed entry55, Z/MAX@103, all70 positional rows, .0003 margin, EPS24,10000 dual cap. New experiments retain the same first three cold solves, then compare cold, positional carry and verified costate-preserving seeding on pass3. A separate control reproduces the earlier compensation attempt from pass1. Intermediate retention is enabled throughout.

## The five mismatches in the close candidate

Cold pass2 stops after883 iterations with0 assumed positional violation, .00655692148609 actual continuous miss at X94, zero committed replay miss and no decoder fallback. Its pattern includes X63, X88..90, X101..102. Actual checks additionally zero Z82 and X84..87:

| New zeroing check | Incoming actual continuous velocity |
|---|---:|
|Z82 |-.000593438540349904 |
|X84 |-.003932008029633576 |
|X85 |+.001528010460939578 |
|X86 |+.001050978928816254 |
|X87 |+.000645690504715981 |

All are physically inside abs(v)<.005 and inside the inward .0047 band at the unchanged previous angles. Existing assumed zeroings remain valid too. Thus a modest path repair is plausible; the physical check is not demanding a large velocity change just to enter a threshold band.

At the SAME angles, changing from the old assumed pattern to the actual inferred pattern shifts X94 from732.574664536859 to732.581556909566. Original wall X94<=732.574999988079 becomes violated .006556921486. The old assumed clearance was .000335451220. Max whole-window assumed-position change is .006892372707X and .002007799018Z. New fixed-model and actual continuous paths agree within1.03e-12X and1.69e-11Z. Full same-angle positions including endpoint103 are archived for each arm. The physical zeroing explains milliblock changes, not the large failed retry.

## Why the old next retry fails badly

The next fixed model correctly adds all five missing zeroings, but the baseline starts a new dual solve with lambda0. It abandons the close path: initial decoded yaw can differ180 degrees and starts with continuous miss15.3353369517. It then reaches10000 iterations, assumedmiss .9468998587, actualmiss1.1286632815. These are different optimized angles, not the effect of evaluating the close candidate with the new checks. No valid intermediate path is retained on that cold trajectory.

## Matched repair experiments

First three cold solves reproduce exactly across the first three arms: actualmiss .8830403384, .3323525487, .006556921486. Then only pass3 initialization differs:

| Pass3 initialization | Starting physical miss | Starting max yaw change | Terminal iterations/stop | Selected intermediate | Accepted |
|---|---:|---:|---|---|---|
|Cold |15.3353369517 |180deg |10000 cap |None |No |
|Position multipliers from close pass2 |1.56911134412 |121.542478deg |1076 NO_DESCENT |96, Z103=-1093.6528320031634 |Yes |
|Compensated from close pass2 |.00655692148609 |8.65e-13deg |373 NO_DESCENT |82, Z103=-1093.6526875545678 |Yes |

The compensated seed starts at projected residual .006929812970, preserves all old costates within the unchanged guard, and first saves a valid candidate at iteration77. It improves at78 and82. Position carry first saves95 then96. Both terminal iterates are still rejected with physical miss3.61652403689 and27 costate fallbacks. **Both seeding and intermediate retention matter in this matched repair experiment.** This does not assert convergence/global optimality; a validated feasible intermediate path is sufficient for the tested window acceptance.

| Selected path | Full continuous min clearance | Committed exact min clearance | Full exact min clearance |
|---|---:|---:|---:|
|Compensated,82 |.000130191309381 |.000507558976665 |.000266843737563 |
|Position carry,96 |.000311266629865 |.000298625983987 |.000298625983987 |

Both selected paths pass actual-inertia continuous motion, the shared committed exact check, every assumed inertia threshold check, and additionally FULL window exact replay. The margin .0003 is an optimization target, not the required minimum replay clearance. Callback retention chooses the best observed primary score, not the highest clearance.

## Why the earlier compensated-from-start control was declined

That different experiment seeds pass1 from the original nonconverged initial solution, rather than pass3 from the close candidate. The projection reproduces costates to roundoff, but one nearly canceled vector at tick78 has norm1.5014005156e-9. Its reconstruction error3.1312366910e-16 exceeds the strict relative guard's allowed1.5014005156e-17. It is the sole failing tick. Largest absolute reconstruction error over the whole path is1.8994828874e-15; largest raw yaw difference is .000011432051 degrees at78. The guard then discards ALL new band prices, retains only position weights, and the actual starting yaw difference becomes171.28 degrees as already archived.

This is numerical rejection of an almost identical proposed costate seed, not evidence that the velocity-row compensation cannot represent the change. The guard was NOT relaxed, the fallback was NOT disabled, and we have NOT demonstrated that relaxing it alone would make this original bad-start trajectory solve. Initial path miss is already .88304, so it is a separate problem from repairing the later .00656 path. A more robust seed acceptance policy could consider a cancellation-aware absolute error bound and decoded-path validation, but remains future work.

## Complete outcomes and timing

Seed_scale labels are experiment identifiers:0=cold;1=carry only after cold pass2;-1=compensate only after cold pass2;-2=original compensation from pass1. No negative dual weights are used.

| Experiment | Accepted | Solves | Best violation | Accumulated solve ms |
|---|---|---:|---:|---:|
| Cold throughout | false | 4 | 0.006556921486 | 961.317 |
| Cold passes0..2, position carry at3 | true | 4 | 0 | 361.456 |
| Cold passes0..2, compensated seed at3 | true | 4 | 0 | 341.148 |
| Original compensated-from-pass1 control | false | 2 | 0.8830403384 | 530.573 |

Successful final repair solve times were13.932ms for position carry and4.790ms for compensation; collector audit times3.904ms/1.258ms. These are single sequential JVM measurements, warmup-sensitive, not a benchmark. [passes.csv](passes.csv) contains every attempted solve, including exact residuals and timings. Earlier cold solves dominate total time.

## Scope and reproduction

Only the focused opt-in test ran: one test, zero failures/errors, Gradle3s. No full suite, graph, or full route. This is a successful controlled TEST-ONLY hybrid, cold discovery followed by warm repair; the production path still starts each new pattern cold and has not been upgraded with this delayed seed policy. Existing user j127 fixture untouched. Decoder unchanged; no guard relaxation.

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=j14-near-repair :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/j14-near-repair-2026-10-24/focused.log 2>&1
```

Run from the isolated core-only worktree with the archived modified sources. [Trace](solver-trace.txt), [JUnit result](focused-result.xml), [source](InertiaMultiplierSeedProbeTest.java), per-pass initial/terminal/retained diagnostics, same-angle model comparisons, and per-tick seed reconstruction error CSVs are archived. Pass3 path/margins/inertia files for successful arms describe selected intermediate candidates; terminal costate/multiplier columns and stopping stats still describe the terminal dual. Retained multipliers are exported separately.
