# Identical-entry comparison:20–22 misses,20–21 succeeds

2026-10-24. Focused replay of the current staged full run at tick132. No production changes. [Probe](../../../../core/src/test/java/de/legoshi/parkourcalc/core/anglesolver/solver/Jump20WindowComparisonProbeTest.java), [trace](solver-trace.txt), [Gradle output](probe.log), [JUnit result](focused-result.xml). One focused test executed twice (second execution corrects CSV quoting for pattern strings); solver outcomes identical. No full suite or full-route rerun.

## Identical entry

Reconstruct ExactJumpModel state132 from the staged run's committed game inputs0..131, using the exact archived real11 input. The shared entry matches the original trace to rounding and reproduces all observed solve outcomes, iteration counts and validation misses. [Entry export](entry.csv).

| Parameter |Value |
|---|---:|
|X |733.2993911151426 |
|Y |12.424009289414908 |
|Z |-1095.0468786224278 |
|VX |.00005327436713965728 |
|VY |.24813599859094576 |
|VZ |-.3022178746735658 |
|Raw seed yaw |258.950653076deg, float |

Physics inputs:132 slip.6, jump=false;133 slip.6, jump=true;134 AIR. Sprint true, forward.98, strafe0, speed amplifier0. Vertical state is reconstructed from the exact replay, not inferred from the annotated grounded ticks.

Both windows commit only jump20, inputs132..154, through state155. Fixed margin.0003, dual cap1000, EPS2=1e-24, decoder substitution disabled, conditional seeding and intermediate retention enabled. Actual-inertia continuous validation covers the entire window; Minecraft validation covers23 committed input ticks. No outer deadline.

## The two problems

| Window |Inputs |Nominal end |Actual objective state |Compiled rows |
|---|---|---:|---:|---:|
|20–22 |132..177 |176 |178 |38 |
|20–21 |132..168 |167 |169 |31 |

The longer window adds7 scalar rows:

| State |Added constraints |
|---:|---|
|175 |X in[733.6999999880792,735.3000000119208]; Z in[-1089.3000000119207,-1087.6999999880793] |
|176 |X <=734.6999999880791 |
|177 |Z >=-1088.699999988079; X <=734.6999999880791 |

The objective also moves from169 to178. Therefore it is not just appending constraints to an otherwise identical optimization objective. No claim of infeasibility follows from the longer window's failure.

##20–22: all closed forms miss

| Objective |Pass continuous misses, in order |Pass iterations/stops |Prefix replay miss of least-violating pass |
|---|---|---|---:|
|Z/MAX |.490218809 ->.603099247 |1000/cap,513/no descent |.490237087 |
|Z/MIN |.542210094 ->.226800589 ->.854229667 |1000/cap,644/no descent,874/no descent |.078764607 |
|X/MAX |1.258355919 ->.599289861 ->.516148620 ->.560142809 |1000/cap each |.235236958 |
|X/MIN |.806376330 ->1.059557687 |1000/cap each |.398782269 |

No valid intermediate candidate retained. No seed-eligible assumed-model source; inertia retries are cold despite seeding enabled. The first pass is unpatterned; later passes use inferred fixed zeroing patterns, printed in the full trace. Each objective stops on its normal pass cap or no-improvement rule. These misses are not all a small angle-mapping artifact; the closed forms also miss their assumed models substantially.

##20–22: SLP replay passes, continuous validation rejects

Centered SLP initializes from its own margin0 dual seed (default10000 internal cap), ordinary inertiaAware=false model.40 phase1 LP calls. The replay path ends with worst signed violation-.001755934, meaning all game constraints have at least approximately.001756 clearance. It therefore passes the ordinary phase1 gate. The final shared helper reports replay violation0 but continuous violation.047522722002895534, so SLP returns null.

Only one SLP attempt starts. No inertia-aware retry is invoked: that retry is inside the failed-phase1 branch, and this candidate passes phase1 then fails final continuous acceptance. This is a gap in the new continuous-validation recovery path, not evidence that an inertia-aware retry cannot solve this window. No such retry was added or tested here.

### Exact source of continuous/game disagreement

At the Z inertia threshold check at134:

| Path |Incoming Z momentum before the check |Outcome |
|---|---:|---|
|Continuous |-.004994065627665957 |Abs below.005, zero |
|Minecraft replay |-.005006673567427913 |Abs above.005, survives |

This is the only axis/tick where continuous and Minecraft zeroing outcomes differ for this candidate. Velocity difference is about.00001260794, but crossing the threshold removes a full~.005 carry from one path, accumulating position disagreement. The same candidate then has:

| State |Constraint |Continuous Z |Continuous miss |Minecraft Z |Minecraft clearance |
|---:|---|---:|---:|---:|---:|
|135, committed |Z <=-1095.425000011921 |-1095.424356233319 |.000643778602 |-1095.429388467010 |.004388455089 |
|160, provisional |Z <=-1093.6999999880793 |-1093.6524772660764 |.047522722003 |-1093.7018578821778 |.001857894099 |

The rejection is legitimate under our requirement that BOTH whole-window continuous and committed replay pass. It is not a replay failure. Even if we restricted the continuous check to the committed prefix, Z135 would still fail. The violated rows already exist in the shorter window; it is the newly optimized candidate, not simply a new constraint at175..177, that fails them.

[All shared-helper evaluations and row margins](178-margins.csv), [last candidate angles and positions/velocities](178-last-candidate-ticks.csv), [all inertia checks](178-last-inertia.csv).

##20–21: Z/min finds a valid intermediate

Z/max first misses: unpatterned1000 iterations continuous.235346259, patterned1000 continuous.331762495, so it stops without improvement. Z/min then finds an accepted candidate at iteration126 in its initial unpatterned pass, before adding inertia-band rows. The actual continuous simulation and game replay satisfy all included constraints. No continuous/game zero-pattern disagreement in the saved final validation.

The dual continues to252 and stops RECOVERED_INPUT_STALL. Production selects retained126, score/final Z=-1092.55232396, instead of the terminal candidate. Final continuous violation0 and committed replay violation0. SLP is not attempted for this smaller window.

[All shared evaluations](169-margins.csv), [angles and states of the final terminal validation](169-last-candidate-ticks.csv), [last inertia diagnostics](169-last-inertia.csv). IMPORTANT: the last candidate audit is the terminal candidate checked BEFORE retention selection, not necessarily the retained126 return. Production CF-CHECK reports the selected retained candidate's final0/0 acceptance. The last terminal audit here also passes validation, but can have a worse objective; do not identify its path as retained126 without exporting the actual returned angles. The trace records retention selection.

## What this establishes

Same entry, different horizon/objective. The larger problem's closed forms fail to find a valid path. SLP does find a valid game path but it is rejected because its continuous threshold outcome differs at134. The current final-validation branch never tries inertia-aware recovery afterward. The smaller problem finds a candidate passing both checks through ordinary Z/min with retained intermediates. Neither establishes whether20–22 is mathematically impossible or whether larger iteration budgets would change its result.

An appropriate next control, if requested, is to feed this SLP closest path into its existing inertia-aware retry after continuous-only final rejection, or explicitly repair the Z134 threshold consistency. Do not weaken acceptance to call this candidate valid. No algorithm changes or additional controls were performed in this probe.

## Reproduce

Source-matched /tmp/pkc-endpoints-fixed with production b84931e and current diagnostics:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.jump20ComparisonProbe=true :core:test -PslowTests --tests '*Jump20WindowComparisonProbeTest' > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/jump20-window-comparison-2026-10-24/probe.log 2>&1
```

Execution was asynchronous, redirected to file. Instrumentation changes timing; the original window wall times remain145.1ms and16.4ms in the staged full-run trace. No full suite. User j127 fixture untouched.

## Terminology: incoming momentum versus this tick’s movement

The reported ±.005 quantities are incoming Z momentum at tick134, before inertia zeroing and before this tick’s acceleration. The code calls this `velZ[134]`; it is not Z displacement produced by input134. For the rejected SLP candidate, Minecraft Z displacement over134→135 is+.01997835672614201, and the carried Z momentum at135 is+.018180305144646698 after drag. The continuous equivalents are+.02498516670175377 and+.022736501698647934. Its Minecraft X displacement over134→135 is+.1739656109353973. Thus this recorded candidate does not move.549 blocks along Z on134. Source:178-last-candidate-ticks.csv. Use incoming momentum for the threshold-check quantities in explanations.


### SLP local model versus validation,20–22

This attempt is ordinary SLP (`inertiaAware=false`), so its local LP derivatives use an unpatterned model. However, every current/proposed candidate is scored with ExactJumpModel, including the actual Minecraft inertia checks. The final game path keeps incoming Z134 momentum-.00500667357. Evaluating the same continuous angle candidate with double-precision trig yields incoming Z134 momentum-.00499406563, so that continuous simulation zeroes it and misses the rows. This is a second validation of the same candidate, not a new continuous optimization or an inertia-aware retry. The local solve, game candidate checking and final shared continuous validation must be distinguished.


## SLP acceptance policy: replay already drives the solve

Verified directly in SlpSolve: every current iterate and LP proposal is converted with toGameFacings(wrapAll(...)), run through ExactJumpModel.forward, and scored against all window LP constraint rows. Phase1 can accept an infeasible proposal if it improves replay violation; ordinary feasibility-returning mode validates before returning a solution. Thus the local continuous derivative approximation is not the acceptance oracle.

The extra whole-window continuous veto was added by this investigation and is not necessary to establish Minecraft replay feasibility for SLP. Recommended policy if changed: restore full-window compiled replay validation (`compile(spec)`), remove the added phase1 prefix-plus-continuous bypass, and remove the final continuous veto. Merely deleting the continuous veto while leaving the prefix-only final compiler would not fully restore production SLP validation. Keep full-route original replay validation. Closed-form intermediate continuous acceptance is a separate policy; no need to change it together.

For the isolated20–22 SLP candidate, full-window replay already has worst signed violation-.001755934 and therefore would pass replay-only acceptance. This shows that candidate is game-valid under the current mapping; it does not establish that later receding-horizon windows or final whole-route mapping succeed. No production code or acceptance policy was changed in this discussion.
