# Why jump6's seven-jump attempt still fails at cap10000

## Latest route attempts

The current per-objective extension run enters jump6 at state55, nominal end88. Each of Z/MAX,Z/MIN,X/MAX,X/MIN finds its matching cap at88 and independently extends to90. The constraints at88 include both lower and upper bounds on both axes, so the per-objective correction does not shorten these CF attempts. Centered SLP uses nominal55–88, as intended.

| Method | Actual endpoint | Continuous violations over successive proposals | Stopping |
|---|---:|---|---|
| CF Z/MAX | 90 | 0.399114076010 →1.51927103623 | Cap10000; inertia retry NO_DESCENT1764 |
| CF Z/MIN | 90 | 0.009376966022 →16.9160916096 | PROJECTED_GRADIENT319; inertia retry NO_DESCENT625 |
| CF X/MAX | 90 | 0.223450397358 →2.10753286694 | Cap10000 in both passes |
| CF X/MIN | 90 | 1.95437612402 | Cap10000, no new useful inertia pattern |
| Centered SLP | 88 | Both ordinary and inertia-aware phase1 fail |29 LP calls each, reported phase1 violation about4.744 |

After all of these fail, the nominal five-jump window is tried and CF Z/MAX succeeds. This note explains the closest seven-jump attempt, Z/MIN, which matches the successful normal reference's objective type.

## First Z/MIN proposal: the small inertia mismatch

This proposal is identical in the cap1000 and cap10000 runs: it converges after319 iterations. Its affine model has no zeroing pattern and satisfies original position rows, but actual continuous X velocity at63 is+0.002095075262 and is zeroed. The lost positive carry causes X79 lower-wall violation0.009376966022. Committed replay through64 passes, so full continuous lookahead validation rejects it.

Pattern discovery produces X zeroing at63 and84–89, plus Z zeroing at82. The solver re-runs the full affine dual with that pattern and its velocity-band rows. This retry terminates at625 on NO_DESCENT, irrespective of the configured1000 or10000 maximum.

## Why27 costates nearly disappear in that retry

Z is assumed zeroed at82. Therefore input ticks55–81 can change the position at82 but cannot change the Z velocity carried onward from82. Their position contribution persists equally to Z89 and Z90. The focused probe asserts **exact equality of the two affine Z coefficients** for each of those27 ticks.

The objective is minimize Z90, while the newly included constraint is **Z89 >=-1090.300000011921**. Its returned dual multiplier is **1.0000000000059024**. Because the coefficients match, this lower-wall multiplier nearly cancels the objective contribution for all27 earlier input vectors. Their complete costate magnitudes are around1e-11, below the existing1e-9 angle-recovery fallback threshold.

The Z89 lower wall is one of the four rows added by extending end88 to90. The normal reference's jump6 window ends88 and does not contain this row. This identifies a concrete structural difference from the successful reference; it does not by itself prove that solving the nominal window from the current fixed entry would succeed.

An input can have a negligible dual costate while its physical direction still matters to feasibility. Here the fallback treats those directions as free and substitutes the raw objective direction, -Z, for all input ticks55–81.

At tick55, for example:

| Representation of the same returned dual iterate | Added X velocity | Added Z velocity | Facing |
|---|---:|---:|---:|
| Normalize the actual costate direction | -0.077786468086 | +0.318025228258 |13.744252704 degrees |
| Production tiny-costate fallback | Approximately0 | -0.327400031194 |180 degrees |

The fallback flips the large Z acceleration even though the original recovered direction points mostly+Z. A costate's tiny magnitude does not make either physical choice automatically feasible.

## Direct before/after recovery experiment

One focused probe reconstructs the same unpatterned dual, asserts it stops at319, derives the same pattern, reconstructs the retry dual including velocity-band rows, and asserts its625-iteration NO_DESCENT result and production violation. Then it evaluates three representations of **that same returned iterate**, without changing margin, entry state, window, objective, multipliers or inertia pattern.

| Representation | Original positional violation in the assumed affine pattern | Actual continuous violation with inertia checks | Committed Minecraft replay violation |
|---|---:|---:|---:|
| Dual derivative's variable-length control vectors |0.025078113599 | Not angle-realizable | Not angle-realizable |
| Full-length controls along the actual costate, bypassing direction fallback |0.026619956014 |0.050769887840 |0 |
| Production full-length recovery with objective-direction fallback |16.347987768163 |16.916091609578 |3.893650457131 |

The unfixed dual already has a residual: its worst tightened position row is X89lo at0.025378113600, consistent with final projected residual0.025378113600. Thus even the relaxed derivative controls are not a feasible primal solution. Normalizing without the direction fallback slightly worsens the fixed-model violation, and actual inertia worsens it further. It still fails acceptance. But switching the27 tiny-costate directions to the objective direction changes the error scale from0.05077 to16.91609.

The zeroing requirements themselves also fail:

| Representation | Assumed X63 velocity | Assumed Z82 velocity |
|---|---:|---:|
| Derivative controls | -0.005186250553 | +0.004853366780 |
| Full length, no direction fallback | -0.005195689504 | +0.004899335757 |
| Production direction fallback | -0.065983460097 | -0.203522537854 |

Actual zeroing needs magnitude strictly below0.005. The margin0.0003 tightens these band rows to0.0047. In the first two representations X63 is slightly outside even the actual threshold; both Z82 values obey the actual threshold but miss the requested inward margin. In the production representation both assumed checks are grossly outside the threshold.

## Relationship to the normal successful solve

Normal's jump6 window55–88 finds its successful Z/MIN candidate using margin continuation and a different pattern, X63/X81. Its final0.0003 rung passes after71 iterations. The fixed run has a different entry, extra Z89/X89 rows and a different inferred pattern. The new probe isolates recovery on the failed fixed iterate; it does not isolate which change from the reference is sufficient to obtain a feasible solution.

The specific failure chain is now measured: small actual-inertia mismatch in a converged unpatterned proposal → discovered fixed pattern → dual stops without feasible primal controls in a cancellation-sensitive region → objective-direction recovery destroys27 earlier input directions → validation rejects → remaining objectives/centered SLP also miss → shrink window. A larger iteration cap does not affect the625-iteration NO_DESCENT stop. Merely bypassing the direction fallback also does not pass, as the0.05077 continuous violation demonstrates.

## Sources and reproduction

- [Latest seven-window excerpt](jump-six-zmin-recovery-2026-10-23/latest-seven-window-excerpt.txt), with original source line numbers from the current10000-cap full trace.
- [Same-iterate summary](jump-six-zmin-recovery-2026-10-23/summary.csv), [every per-tick costate, direction, control and Z89/Z90 coefficient](jump-six-zmin-recovery-2026-10-23/controls.csv), [all wall residuals and multipliers](jump-six-zmin-recovery-2026-10-23/wall-residuals.csv), [inertia velocities](jump-six-zmin-recovery-2026-10-23/inertia-velocities.csv).
- [Probe source](../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/JumpSixZminRecoveryProbeTest.java), [metadata](jump-six-zmin-recovery-2026-10-23/metadata.txt), [focused test log](jump-six-zmin-recovery-2026-10-23/focused-test.log). Run via `-Dpkc.marginProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.JumpSixZminRecoveryProbeTest`, with the existing JDK21 override. Only this focused probe ran; no production code was changed.
- [Earlier initial-candidate/inertia audit](linkcraft-real11-jump-six-solve-type-divergence.md) provides every original constraint margin and actual threshold check for the unchanged Z/MIN proposals.
- `JumpLinearModel.coefAxis`, `zeroingPattern`, `velocityWalls`, `recoverAlongCostate` and `recoverYawDeg`; `CostateDualSolver.solve/grad`; `ClosedFormSolve.runLadder` plus the inertia-pass improvement stopping rule.

The reconstructed seed uses the exact archived X/Z state from the earlier audit; Y is set0 because authored contact state and all X/Z dynamics here are independent of Y. The probe reproduces the production failure to1e-9 and asserts the coefficient-cancellation identity and27 fallback controls. Numerical timings from this fully instrumented probe are not used as performance benchmarks.


## Original position bounds at states88–90

Using the same absolute state-tick numbering as the traces:

| State tick | X lower | X upper | Z lower | Z upper |
|---:|---:|---:|---:|---:|
|88|731.6999999880792|732.6999999880791|-1090.3000000119207|-1088.6999999880793|
|89|732.6999999880792|734.3000000119208|-1090.3000000119207|-1088.6999999880793|
|90|None|None|None|None|

These are original bounds before any requested inward margin. There are no facing or paired rows at these states in the compiled jump6 window. The same Z interval is constrained at both88 and89; the X interval moves to the right, meeting near732.7. Extending end88→90 includes the four state89 rows; state90 has no authored position bound and becomes the objective endpoint. A state88 constraint is checked before input88, and similarly for89/90. Source: `jump-six-fixed-cap1000-2026-10-23/constraint-margins.csv`, case `end90-ZMIN`, candidate0; the latest trace uses the same original row list.
