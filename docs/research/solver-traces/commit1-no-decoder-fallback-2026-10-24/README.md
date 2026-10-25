# Tick77 retry with decoder fallback commented out

The norm-below1e-9 objective-direction substitution is commented out in both FacingPrefold.expand and JumpLinearModel.recoverAlongCostate. All costates now use ordinary atan2 angle recovery, including an exactly zero vector. This experiment adds no alternative zero-costate policy. The dual algorithm, cap10000, EPS2=1e-24, margin.0003, shared validation, retention and conditional seeding are unchanged.

[Previous investigation](../commit1-early-failure-2026-10-24/README.md), [metadata](metadata.json), [trace](solver-trace.txt), [production controls](production-controls.csv).

Same reconstructed state77, Z/min@91, all26 original constraints, continuous validation through91 and Minecraft validation through committed state80. One focused probe passed; no full suite or full-route rerun.

| Candidate / return | Before: continuous miss | After: continuous miss | Before: committed replay miss | After: committed replay miss |
|---|---:|---:|---:|---:|
|Initial unpatterned,120 iterations |.003152213371 |.003152213371 |0 |0 |
|Seeded patterned terminal,70 iterations |2.729171700929 |.007339308049 |.164974567099 |.001565974796 |
|Cold patterned terminal,161 iterations |2.729170349346 |.009256896255 |.164974567099 |.002706447350 |
|Cold production return,retained |0 |0 |0 |0 |

Seeded production still returns no accepted candidate. No intermediate candidate passes. It stops NO_DESCENT at70, with projected residual.006314843. All assumed zeroing threshold checks now hold at the terminal candidate. Two position rows fail:

| State | Constraint | Continuous position | Continuous miss | Minecraft miss |
|---|---|---:|---:|---:|
|79, committed |X >=732.6999999880792 |732.6984432202073 |.001556767872 |.001565974796 |
|89, future |X >=732.6999999880792 |732.6926606800297 |.007339308049 |.007429276945 |

The shared helper rejects the state79 replay violation and the whole-window continuous violations. [All terminal margins](true-pass1-constraints.csv), [threshold velocities](true-pass1-inertia.csv), [every iteration](true-pass1-iterations.csv).

Cold production still succeeds through intermediate retention. Before disabling the fallback, it retained iteration152 with final Z=-1091.25982934. Now it additionally accepts iteration158, improving final Z to-1091.25984430, about.00001496 blocks further in the minimizing direction. Both return whole-window continuous violation0 and committed replay violation0. Terminal161 remains invalid, so retention is still necessary. [Accepted path](accepted-cold-ticks.csv).

The dual iteration counts are unchanged because this substitution affects candidate angle decoding, not the dual update itself. The counter called `fallback` in the existing iteration observer still counts costates below1e-9; it does not mean a substitution occurred in this experiment. The max_decoder_angle_change column is0 throughout, confirming raw and decoded angles agree.

Conclusion: the substitution caused the enormous errors, but removing it does not resolve seeded convergence. A cold retry remains an independently demonstrated recovery for this window. No cold fallback was added to production in this change; seeding remains enabled by default. No statement about later route continuation follows from this local probe.

Reproduce in source-matched /tmp/pkc-endpoints-fixed, redirecting output asynchronously:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.commitOneEarlyProbe=true -Dpkc.commitOneEarlyOutput=docs/research/solver-traces/commit1-no-decoder-fallback-2026-10-24 :core:test -PslowTests --tests '*CommitOneEarlyFailureProbeTest' > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/commit1-no-decoder-fallback-2026-10-24/probe.log 2>&1
```

[Gradle output](probe.log), [JUnit result](focused-result.xml). Production control timings in this single warmed-JVM sample were16.84ms seeded and7.72ms cold; they are not a benchmark. Detailed manual audits are extra work outside those timed calls. Historical logs were preserved in their original directory.
