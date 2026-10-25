# Closed-form Z/max on jumps20–22: why it misses

2026-10-24. Same reconstructed state132 and exact input as the [20–22 versus20–21 comparison](../jump20-window-comparison-2026-10-24/README.md). This probe isolates closed-form Z/MAX@178; no SLP fallback, alternative objective or full-route solve. Production code unchanged. [Metadata](metadata.json), [probe source](../../../../core/src/test/java/de/legoshi/parkourcalc/core/anglesolver/solver/Jump20ZMaxProbeTest.java), [trace](solver-trace.txt), [JUnit](focused-result.xml), [Gradle log](probe.log).

## Problem and entry

Entry132: X733.2993911151426,Z-1095.0468786224278,VX.00005327436713965728,VZ-.3022178746735658,raw yaw258.950653076deg. Actual horizon132..178,46 inputs,38 original compiled scalar rows. Commit through155,23 inputs. Margin.0003,EPS2=1e-24, decoder substitution disabled, conditional seeding and intermediate retention enabled.

The objective tick178 is free of authored constraints. This failure is not caused by directly maximizing Z against an upper cap at178. There are earlier Z walls, including upper Z160, and tight X corridors at154/156..160. Exact rows are in the preceding compiled-constraint export and this probe's margin CSVs.

## Production cap controls

| Cap |Unpatterned pass |Continuous miss |Assumed fixed-model miss |Next patterned pass |Continuous miss |Return |
|---:|---|---:|---:|---|---:|---|
|1000 |1000,iteration limit |.490218808733 |.500630247359 |513,no descent |.603099246533 |Miss |
|10000 |10000,iteration limit |.467372026535 |.476205988439 |513,no descent |.603099246533 |Miss |
|100000 |20819,no descent |.592445704951 |.604462332217 |537,no descent |.956317042341 |Miss |

[Outcomes/timing](outcomes.csv), [per-iteration scalar diagnostics](1000-iterations.csv), [10000](10000-iterations.csv), [100000](100000-iterations.csv). Timing is an instrumented tiny focused probe, not a whole-run performance comparison. Higher caps do not find an accepted intermediate candidate.100000 stops early, it does not execute100000 iterations. None of the failed assumed-model sources is eligible for inertia multiplier seeding.

At1000 the dominant violation is X154<=734.6999999880791, but continuous X154=735.1902187968117, missing.490218808733. Similar misses occur at156..160. Prefix game replay misses.490237087138. All continuous/game inertia outcomes match for this unpatterned terminal candidate; the error is already large before any slight threshold disagreement could explain it.

## What happens inside the initial dual solve

A separate manual unpatterned dual audit builds the exact same34 nonconstant position walls and identity prefold. It reproduces the production unpatterned iteration counts and terminal decoded violations. It exposes gx/gz and ux/uz through test-only reflection. It does not modify the algorithm.

At1000, the costate for input132 has norm **1.588269351191274e-13**. Every other tick's costate is far larger. The derivative computes controls using `m*g/sqrt(g²+1e-24)`. With this tiny costate, the recovered derivative-implied control radius is only **.15686077293** times the prescribed magnitude:

| Tick132 quantity |Value |
|---|---:|
|Prescribed acceleration magnitude |.1300000293007851 |
|Internal derivative-implied magnitude |.020392 roughly |
|Physical angle-recovered magnitude |.1300000293007851 |
|Recovered yaw |-9.769896989948045deg |

The old objective-direction fallback is disabled, so the yaw is the raw costate direction. The magnitude discrepancy is still present: angle recovery always uses the full prescribed acceleration, while the smoothed dual gradient uses a shorter vector near zero.

### Per-row consequence, before inertia

Positive signed violation means outside the authored wall. These are original margin0 wall residuals; the optimizer additionally tightens by.0003.

| Row |Internal derivative controls |Decoded full-magnitude controls |
|---|---:|---:|
|Z136 lower |.149208591 |.015454469 |
|Z160 upper |.122424317 |.406164237 |
|X154 upper |.110618447 |.500630247 |
|X156 upper |.101317370 |.499060021 |

[All rows](1000-terminal-wall-audit.csv). This distinction matters: the internal controls themselves are STILL infeasible, so this is not a feasible relaxed optimum spoiled solely by decoding. The projected residual is.149508590579, nowhere near tolerance1e-8. We have not reached a dual optimum and cannot infer a duality gap from this state.

At10000, the weak tick132 costate is1.721538723e-13, radius ratio.169658153, decoded fixed miss.476206. At20819 it is1.415720124e-12, ratio.816786210, decoded fixed miss.604462. Extra iterations continue near the same poorly recovered first control, without converging to a feasible path.

The same near-zero costate produces a large curvature scale: tick132's `m/sqrt(g²+eps²)` weight is about1.284e11 versus a maximum31.723 among other ticks at1000. This is a conditioning indicator, not a measured Hessian condition number. The implemented Newton step is Levenberg-damped Cholesky with damping scaled by max Hessian diagonal; its class-level truncated-eigen description is stale. We did not instrument individual Newton/gradient step decisions or isolate damping as the unique cause. The evidence supports investigating this first-control degeneracy and primal recovery rather than simply raising the cap.

[Internal unpatterned samples](1000-unpatterned-inner.csv), [costates](1000-terminal-costates.csv), [multipliers](1000-terminal-multipliers.csv), [curvature-scale inputs](1000-curvature-scale.csv). Equivalent10000/100000 files are preserved. Snapshots sample early iterations and every100 thereafter, plus terminal; they are not a record of every invalid intermediate angle sequence.

## Inertia retry at cap1000

The first failed path predicts X zeroing at132 and149..177, Z zeroing at145. Production then solves a new fixed-pattern model with the corresponding explicit velocity-band walls. It starts cold because the preceding assumed model already violates positions by.500630, so the conditional seeding guard correctly declines it.

It stops NO_DESCENT at513. Actual continuous miss worsens to.603099 at X156. The assumed fixed-model positional miss is.486953. Its assumed Z145 zero is inconsistent: incoming momentum is-.0731878043, far outside.005. X149's assumed zero likewise has incoming+.0244020366. These are failures to satisfy generated bands/positions, not evidence that the model permits such a path as a valid result. The shared validator rejects it. The outer pattern loop stops on no improvement; no further pattern is attempted.

The cap10000 run derives the same mask and reproduces this same cold513-iteration retry. The100000 run derives a slightly different X mask,149→150 as its first noninitial zero, then fails at537. [All zeroing/momentum diagnostics](1000-inertia.csv), [row margins](1000-margins.csv), [angles/positions](1000-states.csv).

## Diagnostic controls: hold the weak early controls

As an isolation control, pin the first1 or2 input facings to those of the already saved replay-valid SLP candidate for this SAME window/entry. This adds facing equalities and reduces degrees of freedom; it is not the same optimization problem and not a production fallback. Cap10000, all original rows retained.

| Diagnostic restriction |Initial continuous miss |Initial prefix replay miss |Initial fixed-model positional miss |Inertia retry continuous miss |
|---|---:|---:|---:|---:|
|Pin132 |.010242525146 |.005885691416 |.039968060899 |.095476415324 |
|Pin132 and133 |.001601803320 |.004073753076 |.004024839513 |.104818350718 |

Neither control returns a feasible result. But fixing the first input reduces the initial actual miss from~.47 to~.010; fixing two gets to~.0016 continuous. This reinforces that the early controls matter strongly. It does not prove that pinning is a robust repair; the subsequent inertia recovery still fails. [Pin1 margins](pinned1-margins.csv), [pin2](pinned2-margins.csv), [trace](solver-trace.txt).

## What this establishes

The expectation of a Minecraft-feasible route is supported by the existing accepted SLP replay candidate. Closed-form Z/max fails to find one. Its initial failure is an unconverged dual with a near-zero first-tick costate, significant derivative-versus-angle control-radius mismatch and ordinary position misses. It is not primarily the same continuous/game threshold flip that rejected SLP previously. Increasing the cap to100000 does not repair it. Inertia retries start from an already invalid model and worsen the result; they do not receive multiplier seeding here.

No production repair was made. Next targeted measurements, if needed, are the actual Newton damping/free-set step history near zero first costate, and a primal recovery or continuation strategy that preserves a good early path when the dual direction becomes weak. Do not label this a proven duality gap or infeasible map.

## Reproduce

Source-matched /tmp/pkc-endpoints-fixed, current production including restored SLP validation (SLP not invoked by this probe):

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.jump20ZMaxProbe=true :core:test -PslowTests --tests '*Jump20ZMaxProbeTest' > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/jump20-22-zmax-2026-10-24/probe.log 2>&1
```

One focused test, zero failures, Gradle about2s. Iterative probe development repeated it to add controls and diagnostics; final artifacts reflect the final version. No full suite, full-route rerun or user j127 fixture change. Solver misses are recorded outcomes, not assertion failures.
