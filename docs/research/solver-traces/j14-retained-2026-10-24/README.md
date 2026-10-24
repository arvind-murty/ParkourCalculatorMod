# j14 with intermediate candidate retention

2026-10-24. Production retention implementation c392645, test-only compensated initializer preserved. Only test probe configuration changed for this run; no new production source edit. Isolated worktree `/tmp/pkc-endpoints-fixed` at b98f87f plus the production changes already archived in [j13 retention](../j13-retained-2026-10-24/README.md). Main HEAD before this run is the j13/j14 summary commit. Existing user j127 capture modification untouched.

## Configuration

Exact same saved state55, Z/MAX@103,48 inputs, all70 original positional rows retained, including repeated Z98..102 bands. Probe asserts no direct cap at103 and retention of the last Z102 upper wall. Margin .0003, EPS2=1e-24, dual cap10000, up to four inertia-pattern passes with no-improvement stop. Shared acceptance is full actual-inertia continuous validity plus Minecraft replay through state64 (nine committed inputs). Decoder unchanged. Candidate retention enabled for all four seed variants.

## Outcome

**No accepted candidate in any variant.** No intermediate incumbent was saved. Every non-timing field of every attempted dual solve exactly matches the previous [run without retention](../post-turn-clear-cap-2026-10-24/README.md). j14 is not the same candidate-loss failure as seeded j13: these particular trajectories never visit a validated feasible path.

| Seed strategy | Accepted | Pattern solves | Best continuous miss | Accumulated dual ms |
|---|---|---:|---:|---:|
| Cold | false | 4 | 0.00655692148609 | 827.450 |
| Full multiplier carry | false | 2 | 0.883040338417 | 500.082 |
| Quarter multiplier carry | false | 3 | 0.758929199649 | 451.214 |
| Verified compensation attempt | false | 2 | 0.883040338417 | 523.173 |

The compensation guard again declines the first pattern transition and retains positional weights with new band prices0. It is not a successfully angle-preserving seed for this case. That retry ends with actual miss1.4643115571, worse than initial .8830403384.

Closest path is cold pass2:883 iterations, RECOVERED_INPUT_STALL, projected residual .0001621098171. Assumed-model positional violation0, actual continuous violation .00655692148609 at the X94 upper wall, committed-prefix replay violation0, five inertia mismatches, NO decoder fallback ticks. This particular near-miss is not caused by the tiny-costate decoder replacement. Its following cold pass worsens to1.1286632815. The initial no-zeroing solve already hits10000 iterations with assumed-model miss .8791055636 and actual miss .8830403384.

Quarter-carry pass2 returns null at LAMBDA_LIMIT667, recorded as a missing candidate rather than a probe failure. There are no CF-INCUMBENT entries in the complete trace, including this null-returning solve.

## Every attempted solve

| Seed | Pass | Iterations | Stop | Continuous miss | Prefix replay miss | Assumed miss | Inertia mismatches | Solve ms |
|---|---:|---:|---|---:|---:|---:|---:|---:|
| Cold | 0 | 10000 | ITERATION_LIMIT | 0.8830403384 | 0.06310665826 | 0.8791055636 | 7 | 311.744 |
| Cold | 1 | 10000 | ITERATION_LIMIT | 0.3323525487 | 0 | 0.2361663634 | 4 | 218.280 |
| Cold | 2 | 883 | RECOVERED_INPUT_STALL | 0.006556921486 | 0 | 0 | 5 | 14.600 |
| Cold | 3 | 10000 | ITERATION_LIMIT | 1.128663281 | 0.1576608904 | 0.9468998587 | 5 | 282.825 |
| Full multiplier carry | 0 | 10000 | ITERATION_LIMIT | 0.8830403384 | 0.06310665826 | 0.8791055636 | 7 | 199.119 |
| Full multiplier carry | 1 | 10000 | ITERATION_LIMIT | 1.464311557 | 0.1379670012 | 0.8266697405 | 3 | 300.963 |
| Quarter multiplier carry | 0 | 10000 | ITERATION_LIMIT | 0.8830403384 | 0.06310665826 | 0.8791055636 | 7 | 192.549 |
| Quarter multiplier carry | 1 | 10000 | ITERATION_LIMIT | 0.7589291996 | 0.1481518295 | 0.5476155298 | 3 | 248.223 |
| Quarter multiplier carry | 2 | 667 | LAMBDA_LIMIT | nan | nan | nan | -1 | 10.442 |
| Verified compensation attempt | 0 | 10000 | ITERATION_LIMIT | 0.8830403384 | 0.06310665826 | 0.8791055636 | 7 | 229.510 |
| Verified compensation attempt | 1 | 10000 | ITERATION_LIMIT | 1.464311557 | 0.1379670012 | 0.8266697405 | 3 | 293.663 |

Per-pass retention CSVs identify terminal stop, zero saved improvements and audit time. Costate, yaw/path, per-constraint margin, inertia velocity, multiplier and sampled iteration CSVs are preserved for all recovered candidates.

## Production cold-start control

`ClosedFormSolve.optimizeRobust`, fixed robust margin .0003, replay prefix9, max10000. Production still starts a changed inertia pattern cold, not with the test-only compensating initializer.

| Retention | Accepted | Measured total ms |
|---|---|---:|
| false | false | 545.680 |
| true | false | 596.649 |

Both controls miss, with no incumbent in the enabled run. These sequential timings include JVM warmup and are not a controlled performance benchmark. Trace includes per-rung retention audit time. The seeded experiment's accumulated dual times also include its retention/iteration observer costs.

## Reproduction and checks

One focused opt-in test, zero failures/errors; Gradle completed in4s. No full suite, SLP/B&B graph, or full route run.

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=j14-retained :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/j14-retained-2026-10-24/focused.log 2>&1
```

Run from the isolated core-only worktree with the archived modified sources/probe. [Outcomes](outcomes.csv), [every solve](passes.csv), [production control](production-control.csv), [trace](solver-trace.txt), [focused JUnit result](focused-result.xml), [source snapshot](InertiaMultiplierSeedProbeTest.java). Historical input/entry hashes are in the90/103 archive.
