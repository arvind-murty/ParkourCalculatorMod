# Why commit1 stopped before jump11

Investigation2026-10-24, source HEAD687ff5d. Production solver code unchanged. One opt-in focused probe, no full suite or full-route rerun. [Probe source](../../../../core/src/test/java/de/legoshi/parkourcalc/core/anglesolver/solver/CommitOneEarlyFailureProbeTest.java), [metadata](metadata.json), [JUnit result](focused-result.xml), [Gradle log](probe.log).

## Entry state and attempted windows

The free-air full run stopped before jump11, state77. Reconstructing the already committed game facings with the exact forward model gives:

| State | X | Z | VX | VZ |
|---|---:|---:|---:|---:|
|77 |732.1843095478306 |-1089.6445311815335 |0.2268537249432544 |0.255584609936125 |

These match the original trace's rounded state. Using the reconstruction reproduces both original Z/min passes:120 and70 iterations, the .00315221337075 initial miss and2.72917170093 retry miss. The seed-facing parameter is3.081829071 degrees from the original LRS-WIN, not the yaw associated with the previous block's last displayed state. The committed-ticks row at77 displays9.097589493, which must not be substituted for that parameter.

The previous cap-removal run's state77 was X732.185793897916, Z-1089.657354601411, VX.226578364430, VZ.256025767590. It is a different entry state and different objective horizon, not an identical-entry control.

The original failed distinct horizons were77..161,77..137,77..121 and77..91, each trying Z/max, Z/min, X/max, X/min and SLP. Nominal windows of3,2 and1 jumps all expand to endpoint91, so the last two reuse the same failed result. [All original calls and timings](original-objectives.csv), [every original CF rung](original-cf-rungs.csv).

## Isolated Z/min at91

26 original constraints,14 input ticks, exact replay required through state80 (3 inputs), entire continuous window checked through91. Fixed margin.0003,10000 dual cap, EPS2=1e-24, intermediate retention enabled, decoder unchanged.

| Step | Iterations / stop | Fixed-model position miss | Actual continuous miss | Committed replay miss |
|---|---|---:|---:|---:|
|No assumed zeroing |120 / projected gradient |0 |.003152213371 |0 |
|Seeded inertia retry |70 / no descent |2.296156890272 |2.729171700929 |.164974567099 |
|Cold inertia retry, terminal |161 / no descent |2.296155538689 |2.729170349346 |.164974567099 |
|Cold retry, retained iteration152 |Returned by production retention |Not the acceptance criterion |0 |0 |

[Production controls](production-controls.csv) confirm seed enabled misses, seed disabled succeeds. Timing is a tiny focused warmed-JVM sample, not a performance benchmark. The cold control succeeds using the exact same entry, rows, margin, objective and validation; only multiplier seeding is toggled. [Raw trace](solver-trace.txt) shows retained candidates at151 and152 and production choosing152. The terminal cold iterate is bad too; intermediate retention is essential.

### 1. The initial miss is genuinely inertia, not angle discretization

The unpatterned solution satisfies all its fixed-model position rows and passes the committed game replay. Actual continuous simulation zeroes Z82 and X84..90. This produces these continuous violations:

| Constraint | Bound | Actual miss |
|---|---:|---:|
|X85 upper |732.6999999880791 |.001055329 |
|X86 upper |732.6999999880791 |.002047044 |
|X87 upper |732.6999999880791 |.002730979 |
|X88 upper |732.6999999880791 |.003152213 |
|Z89 lower |-1090.3000000119207 |.000524137 |

There are duplicate authored X87 rows. Initial pre-check velocities include Z82=.000295246 and X84=-.003270470, both genuinely below.005. [All row margins](true-pass0-constraints.csv), [all threshold checks](true-pass0-inertia.csv).

### 2. Seeding preserves the starting path, but does not guarantee convergence

The solver infers X84..90 plus Z82. The initial assumed model is valid, so seeding is eligible. Compensation reproduces costates within3.331e-16. At retry iteration0, recovered angles are unchanged and the continuous miss remains.003152213371. Seeding does not immediately destroy the path.

The seeded optimizer then finds no continuously valid iterate. At54 the smallest costate is6.225e-10; two ticks enter the decoder's norm-below1e-9 fallback. Raw angles at that iteration miss by.025724, but replacing the weak directions makes the decoded miss2.043325. At terminal70 five ticks are below the fallback threshold. Raw fixed-model and actual continuous misses are both.007339308; decoded misses become2.296157 and2.729172. The largest angular substitution is173.239 degrees.

The five affected controls are77..81, before Z82 zeroing. The drastic error is the decoder's objective-direction substitution when the two-component costate is tiny, not a Minecraft rounding error. However, bypassing this substitution alone would still leave the measured terminal raw path invalid by.007339; no claim of an automatic fix is justified.

At terminal, decoded Z82 velocity is approximately-.155127, so the assumed zeroing is also grossly invalid. The fixed model has explicit velocity-band rows; the returned decoded angles do not satisfy them. It is rejected by the shared validator. This is not accepted free reversal.

[Every seeded iteration](true-pass1-iterations.csv), [every tick's raw/decoded yaw and costates](true-pass1-ticks.csv), [terminal rows](true-pass1-constraints.csv), [terminal inertia](true-pass1-inertia.csv).

### 3. Cold retry visits valid intermediate paths

With seeding disabled, the same fixed inertia pattern visits continuously valid decoded paths at151..157. The shared validator and retention accept151 and152, and152 improves the objective. The remaining continuous-feasible samples need not improve the objective and are not necessarily retained. The solver continues to its normal no-descent stop161, where its decoded path is invalid again. Production returns retained152, whose whole-window continuous violation and committed-prefix game replay violation are both0.

[Every cold iteration](false-pass1-iterations.csv), [accepted continuous angles and states](accepted-cold-ticks.csv), [production selection trace](solver-trace.txt).

## Conclusion and next step

This early stop does not establish a dead committed entry state. A validated continuation through91 exists from that exact reconstructed state. The seeded numerical path never visits a valid candidate; the cold path does, and retention preserves it. Our current seeding logic replaces the cold retry rather than keeping it as a fallback.

A narrowly supported next experiment is: keep seeded inertia recovery first, but if it fails, retry that fixed model cold with intermediate retention. This preserves the previously demonstrated seeding benefits for j13/j14. Decoder replacement remains a separate issue to investigate. Neither a full route fix nor feasibility of the longer77..121/137/161 windows has been established by this probe.

## Reproduce

Copy the probe into the existing source-matched core-only runner and run, with output redirected asynchronously:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.commitOneEarlyProbe=true :core:test -PslowTests --tests '*CommitOneEarlyFailureProbeTest' > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/commit1-early-failure-2026-10-24/probe.log 2>&1
```

Runner:/tmp/pkc-endpoints-fixed. Full input is the external real/11.json recorded in the preceding full-run metadata. The probe reconstructs state77 from that full run's persisted committed game facings. Diagnostic files named true/false are seeded/cold controls, not pass/fail labels. Iteration observers may emit a second final sample for the terminal iteration. Manual pass tables record terminal angles; production-controls.csv and the trace record the actual retained return.
