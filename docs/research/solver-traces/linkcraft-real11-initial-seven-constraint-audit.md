# Initial seven-jump window: constraint audit

Scope: the original start state, ticks `[0,68)`, seven jumps, 33 positional constraints. Each closed-form objective uses only robust margin `0.0003`. A separate Z/MAX control uses `0.05`. No full route rerun was needed for this audit.

The diagnostic reruns every actual inertia-pattern pass and checks that its retained candidate's angles are **identical** to the production closed-form result. It evaluates each of the 33 original constraints in two ways: the continuous position model with that pass's fixed inertia pattern, and the exact replay after game-facing conversion. The [CSV](margin-comparison-2026-10-23/initial-seven-constraints.csv) includes all 264 evaluations (seven fixed-margin candidate passes plus the normal-margin control), including each bound, predicted position, replay position, clearance, violation and drift. The [trace](margin-comparison-2026-10-23/solver-trace-initial-seven-constraint-audit.txt) records iterations and residuals for every pass. The duplicated rung lines following each audit are the independent production-call comparison, not additional search passes.

## What fails

Z/MIN and X/MAX solve the continuous positional constraints to numerical precision, then replay violates a few walls by about 0.000085–0.000458 blocks. Z/MAX and X/MIN already have substantial continuous constraint violations before replay. Their misses cannot be explained as only a small replay perturbation.

For the normal Z/MAX candidate at margin 0.05, all replay constraints pass; its minimum actual clearance is 0.035093824190 blocks at X@67lo. This is an interior candidate. The dual still reaches its 100-iteration cap, and the requested 0.05 buffer is not fully realized; success here means the original constraints pass exact replay.

## Each violated constraint on the retained candidates

Positive continuous clearance means the continuous candidate is inside the original wall. Negative means it already violates it. Replay violation is positive when outside. Bounds below are rounded for display; the CSV retains the actual stored bounds.

| Objective | Constraint | Required bound (rounded) | Continuous clearance | Replay violation |
|---|---|---|---:|---:|
| Z/MAX | Z@68 | Z ≤ -1092.7000 | -0.011663351 | 0.012402020 |
| Z/MIN | X@58 | X ≤ 729.7000 | +0.000299999 | 0.000084909 |
| Z/MIN | Z@63hi | Z ≤ -1093.7000 | +0.000300000 | 0.000458455 |
| X/MAX | X@66 | X ≤ 729.7000 | +0.000300001 | 0.000191611 |
| X/MIN | Z@29lo | Z ≥ -1101.3000 | -0.450231081 | 0.239998017 |
| X/MIN | Z@39lo | Z ≥ -1100.3000 | -0.409476333 | 0.126288312 |
| X/MIN | Z@54lo | Z ≥ -1096.3000 | -1.157077600 | 0.853238211 |
| X/MIN | X@58 | X ≤ 729.7000 | -0.199010732 | 0.199392334 |
| X/MIN | Z@64 | Z ≥ -1093.7000 | -1.054791331 | 0.748379125 |
| X/MIN | X@67lo | X ≥ 729.7000 | -1.891012694 | 1.890560977 |
| X/MIN | Z@67lo | Z ≥ -1093.3000 | -1.081566406 | 0.774867430 |

All original constraints not listed for a given objective pass in that objective's exact replay.

For example, Z/MIN's X@58 wall is `X <= 729.6999999880791`. The continuous candidate gives `729.6996999889905`, about 0.0003 inside. Replay gives `729.7000848973513`: a +0.000384908361 displacement relative to the continuous model, leaving a violation of 0.000084909272.

Z/MIN's Z@63hi prediction is `-1093.7002999885747`; replay gives `-1093.6995415332988`, a +0.000758455276 drift, producing a 0.000458454781 violation. X/MAX's X@66 prediction is `729.6996999869609`; replay gives `729.7001915991870`, a +0.000491612226 drift, producing a 0.000191611108 violation. Thus a requested 0.0003 buffer is smaller than the accumulated continuous-to-exact displacement on these walls.

The retained Z/MIN replay has threshold checks zeroing X only at tick 0 and tick 63, and Z only at tick 0. Its failed X@58 constraint occurs before the tick-63 X zeroing; its failed Z@63hi constraint is on Z, whose carry has not zeroed again. X/MAX has only the initial tick-0 zeroing on both axes. Consequently these particular small misses are continuous-to-exact movement drift, rather than a newly triggered inertia threshold check on the constrained axis. Source: [threshold check locations and entry velocities extracted from the saved replay](margin-comparison-2026-10-23/initial-seven-threshold-checks.csv) and the underlying [recorded candidate paths](margin-comparison-2026-10-23/solver-trace-margin-control-normal-entry-i0-fixed-policy.txt).

## Every candidate pass

| Objective | Margin | Pass | Retained as best | Continuous maximum violation | Replay maximum violation |
|---|---:|---:|---|---:|---:|
| Z/MAX | 0.0003 | 0 | true | 0.011663351 | 0.012402020 |
| Z/MAX | 0.0003 | 1 | false | 0.071773793 | 0.072609314 |
| Z/MIN | 0.0003 | 0 | true | 0.000000000 | 0.000458455 |
| Z/MIN | 0.0003 | 1 | false | 0.000000000 | 0.000589780 |
| X/MAX | 0.0003 | 0 | true | 0.000000000 | 0.000191611 |
| X/MIN | 0.0003 | 0 | false | 4.998886253 | 5.018831182 |
| X/MIN | 0.0003 | 1 | true | 1.891012694 | 1.890560977 |
| Z/MAX | 0.05 | 0 | true | 0.000000000 | 0.000000000 |

- Z/MAX's patterned retry is worse than its unpatterned candidate, so it retains pass 0.
- Z/MIN's patterned retry still satisfies the continuous positional constraints, but exact replay gets worse: X@58 misses by 0.000154771379 and Z@63hi by 0.000589779670. It retains pass 0.
- X/MAX derives no effective inertia pattern beyond the initial zero velocity, so there is no patterned retry.
- X/MIN's patterned retry reduces a 5.018831-block replay miss to 1.890561 blocks, still infeasible. It retains pass 1.

Z/MAX pass 0 and both X/MIN passes hit the 100-iteration cap with large residuals. Z/MIN's two passes and X/MAX converge with projected-gradient residuals below 1e-8. The detailed trace supplies the actual residual and iteration count for each pass.

## Every original constraint: exact violation on each retained candidate

Zero means satisfied. All quantities are blocks; the CSV additionally includes continuous and replay values and the original inequalities.

| Constraint | Z/MAX | Z/MIN | X/MAX | X/MIN |
|---|---:|---:|---:|---:|
| X@20lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@20hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@20lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@20hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@29lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@29hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@29lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.239998017 |
| Z@29hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@39lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@39hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@39lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.126288312 |
| Z@39hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@54lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@54hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@54lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.853238211 |
| Z@54hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@58 | 0.000000000 | 0.000084909 | 0.000000000 | 0.199392334 |
| Z@57 | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@56 | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@63lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@63hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@63lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@63hi | 0.000000000 | 0.000458455 | 0.000000000 | 0.000000000 |
| Z@64 | 0.000000000 | 0.000000000 | 0.000000000 | 0.748379125 |
| X@64 | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@55 | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@67lo | 0.000000000 | 0.000000000 | 0.000000000 | 1.890560977 |
| X@67hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| Z@67lo | 0.000000000 | 0.000000000 | 0.000000000 | 0.774867430 |
| Z@67hi | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@65 | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 |
| X@66 | 0.000000000 | 0.000000000 | 0.000191611 | 0.000000000 |
| Z@68 | 0.012402020 | 0.000000000 | 0.000000000 | 0.000000000 |

## Reproduce only this window

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.marginProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.First30MarginComparisonTest.initialSevenConstraintAudit
```

This passed. The [JUnit result](margin-comparison-2026-10-23/initial-seven-test-result.xml) records one passing initial-window test. Source is [First30MarginComparisonTest](../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/First30MarginComparisonTest.java), methods `initialSevenConstraintAudit` and `auditConstraints`. It invokes production `ClosedFormSolve.runLadder`, reproduces its inertia-pattern update/stopping rules, and confirms the retained yaw array equals production `optimizeReturning` exactly. The position audit uses the production `JumpLinearModel.constPos` and `coefAxis` coefficients, and `ExactJumpModel.forward` for replay. Production solver behavior is unchanged.
