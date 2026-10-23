# Separate norm-smoothing change: EPS2 1e-14 to 1e-24

This experiment follows the independent direct-cap detection commit `4dd969c`. It changes the global `CostateDualSolver.EPS2` to 1e-24 (norm smoothing length 1e-12), keeping it inside the objective/gradient/Hessian square roots. Iteration defaults remain 100. Fixed route margin remains 0.0003, and the shared actual-inertia/full-window plus committed-prefix replay validation remains in use. Direct-cap detection reports risk only; no automatic window selection has been added.

## Fallback intent from upstream source/history

The original creation commit [8263d65c](https://github.com/Leg0shii/ParkourCalculatorMod/commit/8263d65c20464d73ed574ad5d38c1a9d7a3f12ed) already contains both EPS2=1e-14 and the original `ClosedFormSolve.recover` fallback below squared costate norm 1e-18. Its explicit comment describes a vanishing costate when objective and walls cancel and the direction is undetermined, defaulting to the objective axis. Thus there is source evidence for an indeterminate-direction heuristic, **not an epsilon-calibrated smoothing-error guarantee**. The original comment does not say the fallback catches all materially perturbed costates. It clearly did not catch our previous 9.8e-8 case because that exceeds the 1e-9 fallback norm.

The guard was copied into `FacingPrefold` by `d80d3e24` and into `JumpLinearModel.recoverAlongCostate` in the #427 rework; [PR #427](https://github.com/Leg0shii/ParkourCalculatorMod/pull/427) does not give a smoothing calibration rationale. `gh` CLI was used to inspect the upstream descriptions/commit metadata. The original excerpt is saved with the evidence.

## Conditional error bound

For costate norm r, the derivative-implied length ratio is `r/sqrt(r*r+EPS2)`. For every r >= 1e-9, the worst shrink is at the 1e-9 boundary:

| EPS2 | Smoothing length | Maximum relative shrink above fallback threshold |
|---:|---:|---:|
| 1e-14 | 1e-7 | 99.00005% |
| 1e-22 | 1e-11 | 0.004999625% |
| **1e-24** | **1e-12** | **0.00004999996%** |

The new constant therefore exceeds the requested 0.1% or 1% precision for this specific length distortion whenever recovery actually uses the costate direction. Below 1e-9, the fallback substitutes the objective direction and this bound does not constrain the resulting path error or feasibility. Nor does it bound accumulated positional error from all sources.

## Focused results

Five selected JUnit tests passed: two scale/fallback unit tests, the first-window audit, the iteration-cap sweep and the full fixed-margin 7-ladder route probe. No full suite ran. Test success means the probes executed and recorded their outcomes, not that every solve succeeded.

At the default cap of 100:

| Initial 1–7 candidate | Old EPS2=1e-14 continuous violation | New EPS2=1e-24 continuous violation | Outcome with new EPS2 |
|---|---:|---:|---|
| Fixed Z/MAX, initial pass | 0.011663350626 | 0.011663350627 | Reject |
| Fixed Z/MAX, inertia retry | 0.071773792746 | **16.800000011921** | Reject |
| Fixed Z/MIN | 0 | 0 | Accept |
| Normal margin 0.05 Z/MAX control | 0 | 0 | Accept |

The inertia retry's returned costates are below the direction-fallback scale for **all 68 active controls**. Its recovered angles therefore all accelerate along +Z (adjusted for keys). The committed-three-jump replay misses by 7.199999988079 blocks. This is not the intended smoothing improvement turning into a feasible solution; it exposes the fallback's arbitrary-direction failure in this degenerate case. The retained best Z/MAX miss is still the first pass, so the first window continues to use Z/MIN.

At larger iteration caps, the initial fixed Z/MAX pass now stops at iteration 110 on recovered-input stagnation, with no below-fallback controls, and continuous violation 0.007548653091. This is better than the old larger-cap best miss 0.071773792746, but still infeasible. The retry reaches the cap or eventually stops at 1651 and still has 68 below-fallback controls. Detailed timings/residuals and all constraint margins are preserved.

The full 808-tick fixed-margin 7-ladder run still fails before jump 14 at tick 90, for both commit retries. Total trace time 1327.23 ms; this is a single instrumented run, not a controlled performance comparison. The experiment does not establish corpus-wide improvement and no broad regression suite was requested or run.

The historical outcome assertions in the investigative first-window/cap probes now apply explicitly only to EPS2=1e-14. Other epsilon profiles are measured without assuming the old outcome. They record their EPS2 in metadata and still verify that direct dual reconstruction matches the production candidate. All previously archived 1e-14 traces remain untouched.

## Evidence and reproduction

- [Focused test log](eps24-2026-10-23/focused-tests.log)
- [Original fallback comment and code](eps24-2026-10-23/original-fallback-comment-and-code.txt)
- [First-window results](eps24-2026-10-23/initial-seven-shared-validation/summary.csv), [every constraint margin](eps24-2026-10-23/initial-seven-shared-validation/constraint-margins.csv), [inertia checks](eps24-2026-10-23/initial-seven-shared-validation/inertia-checks.csv), [per-tick paths](eps24-2026-10-23/initial-seven-shared-validation/paths.csv)
- [Iteration-cap outcomes](eps24-2026-10-23/initial-seven-dual-iteration-sweep/outcomes.csv), [candidate residuals and fallback counts](eps24-2026-10-23/initial-seven-dual-iteration-sweep/candidates.csv), [all iterations](eps24-2026-10-23/initial-seven-dual-iteration-sweep/iterations.csv)
- [Full route trace](eps24-2026-10-23/full-fixed-seven-trace.txt)

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.marginProbe=true -Dpkc.fullRouteProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.CostateSmoothingScaleTest --tests de.legoshi.parkourcalc.anglesolver.InitialSevenSharedValidationAuditTest --tests de.legoshi.parkourcalc.anglesolver.InitialSevenDualIterationProbeTest --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginSevenLadder
```

Run in the background with output redirected to a file. Current experimental EPS2 is 1e-24. This is a separate commit so it can be reverted independently of direct-bound detection. Next work should consider a principled small-costate recovery or the constraint-aware window choice, rather than treating the direction fallback as a certificate.
