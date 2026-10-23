# Why the forward-adjusted full routes fail at tick90

## Finding

From **every one of the eight committed tick90 states**, the jump14 landing wall Z98 <= -1093.7 is unreachable in the continuous physics model even if all X constraints and all other Z constraints are removed. The most favorable Z98 is short by2.52–3.85 blocks. This independently explains failure of all longer windows from those states too: they all include that same Z98 wall.

This conclusion is about the committed states. It is not a proof that the original route is infeasible, or that those states were unavoidable. Direct endpoint-cap detection fixes some local cancellation cases but does not infer the map's onward travel direction or preserve a feasible continuation when the window ladder shrinks.

## Concrete sequence: seven ladder, forward tick, commit1

At tick88 the chain has negative Z velocity. Every multi-jump window from88 fails, so LongRun falls back to one jump: inputs88–89, end state90. That small window checks the X/Z boxes at88 and89 but has no row at90 and contains none of jump14's constraints. Its nominal +Z endpoint objective is not directly capped, so the new adjustment policy leaves it alone. Closed form accepts Z/MAX, and LongRun commits it.

| State tick | X | Z | Carry vX | Carry vZ | Facing used for this tick |
|---:|---:|---:|---:|---:|---:|
| 88 | 732.699862766268 | -1089.745136655810 | +0.000628266260 | -0.227172423230 | 359.923492432 |
| 89 | 732.700302213270 | -1089.644909393062 | +0.000239938091 | +0.054724091817 | 405.000000000 |
| 90 | 732.700302213270 | -1089.564185302571 | 0 | +0.073458924464 | End of committed window |

Facings are raw accumulated game yaw;359.923 is approximately0 and405 is equivalent to45. The sprint-jump boost and acceleration at88 point mostly +Z. The next tick's diagonal-key facing also points the acceleration +Z. The small X carry is zeroed by the actual per-axis inertia threshold checks. Negative Z carry is reversed by acceleration, not by inertia zeroing.

The next jump needs Z98 <= -1093.7, requiring a Z change of -4.135814697429 in eight movement steps (inputs90 through97). Its best possible continuous Z change is only -1.609094600054 from this state, leaving2.526720097375 blocks unmet. State98 is before input98; the ninth input in the90–99 window cannot repair the state98 landing constraint.

## How the reachability bound is computed

Use `JumpLinearModel.mMag(t)` and `friction(t)` from the sliced authored schedule. For a lower Z bound, apply the real continuous per-axis inertia threshold check to the carried velocity, subtract the entire available acceleration magnitude along Z, advance Z by that velocity, and apply friction. Repeat to98. For an upper bound, add the entire magnitude. The threshold map is monotone and friction is positive, so choosing the most negative per-tick acceleration minimizes every later Z coordinate. This includes inertia; it does not assume an inertia pattern.

Each axis is considered independently and all walls are ignored. Simultaneously achieving both axis extrema is unnecessary: the Z-only best case already fails. For this route the start-state input90 has magnitude0.327400031194 and friction0.546; following airborne inputs have magnitude0.025999998674 and friction0.91.

As a numerical cross-check, construct facings whose combined input acceleration points directly -Z and replay them through Minecraft physics. For the representative case, Z98 is -1091.173279890353 versus the continuous extremum -1091.173279902625, a difference of about1.23e-8 blocks. This replay is a witness near the continuous extremum, not a certified enumeration of every discrete yaw. The impossibility claim above is for the continuous model required by current validation; its shortfall is orders of magnitude larger than this measured continuous/replay difference.

## All committed-state bounds

| Run | Commit | Z90 | vZ90 | Best possible continuous Z98 | Shortfall from Z98 wall |
|---|---:|---:|---:|---:|---:|
| seven-forward-jump | 3 | -1088.606103853 | +0.166621189 | -1089.848917051 | 3.851082949 |
| seven-forward-jump | 1 | -1089.564584901 | +0.073081723 | -1091.175162525 | 2.524837475 |
| seven-forward-tick | 3 | -1089.561044965 | +0.076405085 | -1091.158556293 | 2.541443707 |
| seven-forward-tick | 1 | -1089.564185303 | +0.073458924 | -1091.173279903 | 2.526720097 |
| ten-forward-jump | 3 | -1089.563444743 | +0.074216387 | -1091.169561264 | 2.530438736 |
| ten-forward-jump | 1 | -1089.564067571 | +0.073551814 | -1091.172796960 | 2.527203040 |
| ten-forward-tick | 3 | -1089.563444743 | +0.074216387 | -1091.169561264 | 2.530438736 |
| ten-forward-tick | 1 | -1089.564067571 | +0.073551814 | -1091.172796960 | 2.527203040 |

All eight shortest production windows return null. The original archived states are rounded to12 decimals in the trace; the focused probe reconstructs them from those logs. Such rounding is irrelevant to multi-block shortfalls but should not be described as a bit-for-bit re-run of the original committed state.

## Exactly where each solver stops

Added logging exposes the formerly opaque early return:

For the representative seven-forward-tick/commit1 shortest window90–99:

| Attempt | Outcome | Iteration recorded at failure |
|---|---|---:|
| Closed form Z/MAX | LAMBDA_LIMIT, no recovered angles | 2 |
| Closed form Z/MIN | LAMBDA_LIMIT, no recovered angles | 2 |
| Closed form X/MAX | LAMBDA_LIMIT, no recovered angles | 4 |
| Closed form X/MIN | LAMBDA_LIMIT, no recovered angles | 4 |
| Centered SLP | Dual seed rejected at LAMBDA_LIMIT; no LP/trust-region phase starts | 2 |

`CostateDualSolver.solve` returns null when a multiplier exceeds1e9 or becomes NaN; `lastStopReason` is LAMBDA_LIMIT. Existing ClosedForm logs call that “dual unbounded,” which is an operational numerical guard rather than a standalone formal infeasibility certificate. Here the independent scalar reachability calculation supplies the actual explanation.

There are no proposed angles to fail replay or continuous validation in these shortest-window attempts. The shared observer records zero calls, and the CF/SLP early-return trace confirms no candidate is produced. Longer windows have other optimization traces, but their original committed entry states already fail the contained Z98 requirement.

## Counterfactual: minimize Z on the preceding two ticks

Re-solve88–90 from the same archived88 state, retain all original constraints and margin0.0003, switch only Z/MAX to Z/MIN, replay the accepted result, then run the production90–99 window again.

For seven-forward-tick/commit1, Z/MIN is accepted and changes the exit to:

- Z90=-1090.628505241466 instead of -1089.564185302571 (1.064319938895 blocks toward the next landing).
- vZ90=-0.299204761509 instead of +0.073458924464.
- The Z-only minimum at98 becomes -1093.702782953869, which clears the isolated Z98 wall by0.002782953869.
- The complete next jump still returns null for all four objectives and the centered-SLP fallback. Its dual seed reaches LAMBDA_LIMIT after15 iterations. The Z-only bound ignores X and other constraints; clearing that bound alone does not establish a feasible jump. No joint feasibility certificate was derived for this counterfactual.

Thus blindly maximizing +Z on the short fallback demonstrably destroys a large amount of useful approach motion. Switching that one objective is not yet a demonstrated route fix; some earlier committed state or a stronger continuation solve may also be needed. In two ten-ladder commit3 cases even the preceding Z/MIN attempt is not accepted. Full counterfactual results:

| Run | Commit | Jump13 objective | Accepted jump13 | Z90 | vZ90 | Next jump accepted |
|---|---:|---|---|---:|---:|---|
| seven-forward-jump | 3 | MAX | true | -1088.6061038527544 | 0.16662118940838727 | false |
| seven-forward-jump | 3 | MIN | true | -1089.5943106233576 | -0.18158092300157652 | false |
| seven-forward-jump | 1 | MAX | true | -1089.5645849010198 | 0.07308172300196705 | false |
| seven-forward-jump | 1 | MIN | true | -1090.6289035498323 | -0.2995815483589374 | false |
| seven-forward-tick | 3 | MAX | true | -1089.5610449645478 | 0.07640508497646006 | false |
| seven-forward-tick | 3 | MIN | true | -1090.6253651338152 | -0.296258675034718 | false |
| seven-forward-tick | 1 | MAX | true | -1089.5641853025702 | 0.07345892446391751 | false |
| seven-forward-tick | 1 | MIN | true | -1090.6285052414657 | -0.29920476150934033 | false |
| ten-forward-jump | 3 | MAX | true | -1089.5634447430584 | 0.07421638734020772 | false |
| ten-forward-jump | 3 | MIN | false |  |  | false |
| ten-forward-jump | 1 | MAX | true | -1089.56406757055 | 0.07355181442270305 | false |
| ten-forward-jump | 1 | MIN | true | -1090.628387371222 | -0.29911182712780265 | false |
| ten-forward-tick | 3 | MAX | true | -1089.5634447430584 | 0.07421638734020772 | false |
| ten-forward-tick | 3 | MIN | false |  |  | false |
| ten-forward-tick | 1 | MAX | true | -1089.56406757055 | 0.07355181442270305 | false |
| ten-forward-tick | 1 | MIN | true | -1090.628387371222 | -0.29911182712780265 | false |

## Next investigation

The next useful boundary is88, before the one-jump fallback reverses direction. Inspect why windows containing both jump13 and jump14 failed from that entry, including the narrow tradeoff between the X wall at94 and Z landing at98. Keep those two jumps in one feasibility problem rather than interpreting success of the isolated two-tick window as evidence of a viable continuation. No policy change or full-route re-run was made in this inspection.

## Sources and reproduction

- [Focused probe](../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/Tick90ContinuationProbeTest.java): opt in with `-Dpkc.marginProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.Tick90ContinuationProbeTest` using the previous JDK21 Gradle override. Focused probe passes; no full suite ran.
- [Original full-route traces](linkcraft-real11-full-forward-window-adjustment.md), especially seven-forward-tick trace LRS-WIN at88–90 and LRS-COMMIT-PATH at88/89/90.
- [Exact focused solver trace](tick90-continuation-2026-10-23/solver-trace.txt): early-return reasons for every objective and SLP seed, plus every counterfactual.
- [Every original state/outcome](tick90-continuation-2026-10-23/outcomes.csv), [per-tick axis extrema](tick90-continuation-2026-10-23/axis-bounds.csv), [every constraint's optimistic interval/margin](tick90-continuation-2026-10-23/constraint-bounds.csv).
- [Pure -Z continuous/replay tick paths](tick90-continuation-2026-10-23/fastest-minus-z-paths.csv), [all counterfactual outcomes](tick90-continuation-2026-10-23/counterfactuals.csv), [preceding two-tick counterfactual paths](tick90-continuation-2026-10-23/previous-window-paths.csv).
- [Settings/provenance](tick90-continuation-2026-10-23/metadata.txt), [focused test log](tick90-continuation-2026-10-23/focused-test.log).
- Implementation: `LongRunSolver.runHorizon/solveWindow` (window shrinking, +Z surrogate, prefix commit); `JumpLinearModel.precompute/forwardContinuous` (acceleration, friction and inertia checks); `CostateDualSolver.solve` (LAMBDA_LIMIT guard); `ClosedFormSolve.runLadder` and `SlpSolve.solve` (dual failure before candidate recovery/LP steps). The only production edits for this inspection add failure-reason logging.
