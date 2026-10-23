# Why normal margins regress with committed-prefix replay

Both runs use normal margins, commit=1 and the 7,5,3,2,1 window ladder. The difference is the exact acceptance horizon. Full-window validation solves all 30 jumps. Prefix validation stalls at zero-based index 12, tick 88. Feature commit: `ea25bc8`.

## Timings available

The traces record each window (`LRS ... ms`), objective call (`LRS-OBJ ... ms`), and closed-form margin rung (`CF ... ms`, including iteration count and residual). [Window timings](prefix-replay-2026-10-23/window-timings.csv) and [objective/rung timings](prefix-replay-2026-10-23/objective-timings.csv) preserve these records.

These are timings from different runs with different JVM warmup and system load. For example the identical initial candidate takes 53.935 ms in the historical run and 1.147 ms in the prefix run. These times cannot establish a speedup or slowdown. The prefix run also ends early. The new-policy full route took about 652 ms to fail; the historical run took about 7097 ms to complete, which is not an equivalent-work performance comparison.

## Every window attempt through the new failure

Indices are zero-based and windows are half-open. A successful entry names the objective, selected margin and total window time. “All miss” means Z/MAX, Z/MIN, X/MAX, X/MIN and centered SLP all returned no candidate passing the respective replay horizon. The objective CSV lists their individual times and every closed-form rung. Not-attempted cells occur because the preceding, larger window succeeded.

| Start index | Jump window | Full-window validation | Prefix validation |
|---:|---|---|---|
| 0 | [0,7) | Z/MAX@68 @ 0.05; **54.1 ms** | Z/MAX@68 @ 0.05; **1.2 ms** |
| 1 | [1,8) | Z/MAX@59 @ 0.05; **8.1 ms** | Z/MAX@59 @ 0.05; **1.2 ms** |
| 2 | [2,9) | Z/MAX@54 @ 0.02; **10.1 ms** | Z/MAX@54 @ 0.02; **1.6 ms** |
| 3 | [3,10) | Z/MAX@47 @ 0.02; **6.5 ms** | Z/MAX@47 @ 0.05; **0.8 ms** |
| 4 | [4,11) | Z/MAX@40 @ 0.02; **7.2 ms** | Z/MAX@40 @ 0.05; **1.1 ms** |
| 5 | [5,12) | Z/MIN@33 @ 0.0003; **119.0 ms** | Z/MAX@33 @ 0.005; **6.0 ms** |
| 6 | [6,13) | Z/MIN@26 @ 0.01; **12.8 ms** | Z/MAX@26 @ 0.0012; **6.1 ms** |
| 7 | [7,14) | Z/MIN@31 @ 0.0003; **23.4 ms** | Z/MAX@31 @ 0.02; **2.0 ms** |
| 8 | [8,15) | Z/MAX@42 @ 0.0003; **16.1 ms** | Z/MAX@42 @ 0.05; **1.7 ms** |
| 9 | [9,16) | Z/MAX@42 @ 0.0012; **18.6 ms** | Z/MAX@42 @ 0.05; **2.0 ms** |
| 10 | [10,17) | X/MAX@51 @ 0.0012; **129.7 ms** | Z/MAX@51 @ 0.05; **2.8 ms** |
| 11 | [11,18) | all miss; **340.1 ms** | all miss; **128.4 ms** |
| 11 | [11,16) | Z/MIN@37 @ 0.0012; **23.7 ms** | all miss; **62.2 ms** |
| 11 | [11,14) | not attempted | all miss; **1.6 ms** |
| 11 | [11,13) | not attempted | Z/MAX@10 @ 0.005; **0.7 ms** |
| 12 | [12,19) | X/MIN@44 @ 0.0003; **60.5 ms** | all miss; **293.4 ms** |
| 12 | [12,17) | not attempted | all miss; **131.7 ms** |
| 12 | [12,15) | not attempted | all miss; **3.4 ms** |
| 12 | [12,14) | not attempted | all miss; **0.5 ms** |
| 12 | [12,13) | not attempted | all miss; **0.6 ms** |

## First different solve, held at identical inputs

The first three committed states are identical. At index 3, both policies start from the same tick-30 state and solve the same `[3,10)` window, global ticks `[30,77)`. The potentially committed prefix ends at tick 40.

A focused probe reconstructed the first three commits at full double precision, then ran both acceptance policies on this identical window. It passed and reproduced both historical candidates. Source: [focused probe result](prefix-replay-2026-10-23/first-divergence-focused-test.log), [trace](prefix-replay-2026-10-23/solver-trace-prefix-first-divergence.txt), [all 40 constraints for both candidates](prefix-replay-2026-10-23/prefix-first-divergence-constraints.csv).

| Step | Full-window acceptance | Prefix acceptance |
|---|---|---|
| Solve margin 0.05 | 100 dual iterations; projected-gradient residual 0.1417; full replay violates later constraints | Same 100 iterations and same candidate; prefix through tick 40 passes |
| Decision | Reject margin 0.05 and continue | Accept margin 0.05 immediately |
| Next rung | Margin 0.02, warm start, 32 iterations; residual 1.252e-9; full replay passes | Not attempted |

The newly accepted 0.05 candidate already violates these **continuous** lookahead constraints. Its failure is substantially larger than angle-mapping error:

| Constraint (global tick) | Continuous violation | Exact replay violation |
|---|---:|---:|
| X@58 | 0.057755541 | 0.058021270 |
| Z@57 | 0.041336058 | 0.041675217 |
| X@66 | 0.052695806 | 0.047277920 |
| Z@68 | 0.046298247 | 0.046711687 |
| X@74hi | 0.051149327 | 0.040994277 |

All five constraints occur after the commit boundary at tick 40, so the new exact acceptance gate does not see them. The prefix constraints at tick 39 pass comfortably. The original exact full-window gate had also been rejecting unconverged approximate candidates; removing its suffix checks allows such a candidate to stop the margin ladder before the converged 0.02 rung. This identifies why the first different prefix is selected. The resulting exit then changes every later optimization problem.

At tick 40 the old/new committed X difference is +0.030075667 and Z difference is +0.045458832. This is an optimizer/candidate-selection change, not only a different conversion of an otherwise identical final yaw sequence.

## Committed state comparison

Jump numbers are one-based. These are boundary states after each committed jump, not pre-landing samples. Yaw is the last **committed** input's yaw, not the earlier window's uncommitted next-tick preview. Values are rounded here; [state CSV](prefix-replay-2026-10-23/committed-states-comparison.csv) retains the trace precision.

| After jump | Tick | Full position X,Z | Full velocity X,Z | Full yaw | Prefix position X,Z | Prefix velocity X,Z | Prefix yaw |
|---:|---:|---|---|---:|---|---|---:|
| 1 | 12 | 743.707005, -1103.714624 | -0.223148, 0.067035 | 115.534653 | 743.707005, -1103.714624 | -0.223148, 0.067035 | 115.534653 |
| 2 | 21 | 740.782891, -1102.628110 | -0.259248, 0.110825 | 101.456955 | 740.782891, -1102.628110 | -0.259248, 0.110825 | 101.456955 |
| 3 | 30 | 737.886542, -1101.003672 | -0.254481, 0.149129 | 102.224350 | 737.886542, -1101.003672 | -0.254481, 0.149129 | 102.224350 |
| 4 | 40 | 734.738602, -1099.039450 | -0.249842, 0.158262 | 101.704819 | 734.768677, -1098.993992 | -0.247171, 0.162290 | 100.741180 |
| 5 | 55 | 730.338594, -1096.094697 | -0.226905, 0.168495 | 86.486671 | 730.339285, -1096.088805 | -0.235888, 0.157269 | 99.026543 |
| 6 | 64 | 729.470230, -1093.541305 | 0.019594, 0.216686 | -10.911901 | 729.469843, -1093.596582 | 0.019539, 0.208092 | -10.674270 |
| 7 | 68 | 729.888283, -1092.709983 | 0.096759, 0.166518 | 4.878670 | 729.847846, -1092.755780 | 0.092945, 0.162407 | 10.570740 |
| 8 | 71 | 730.517873, -1091.763474 | 0.156344, 0.232296 | 8.010955 | 730.475649, -1091.821988 | 0.156123, 0.229350 | 7.595694 |
| 9 | 75 | 731.509106, -1090.430953 | 0.190637, 0.249848 | 1.478904 | 731.471170, -1090.502797 | 0.195138, 0.243419 | -11.030807 |
| 10 | 77 | 732.162233, -1089.651388 | 0.219667, 0.262326 | 5.367081 | 732.187809, -1089.793429 | 0.241583, 0.238337 | -2.407295 |
| 11 | 80 | 732.813499, -1088.466968 | 0.102283, 0.270883 | 99.895004 | 733.217232, -1088.939547 | 0.200009, 0.177860 | -213.616089 |
| 12 | 88 | 732.698000, -1089.740940 | -0.000111, -0.231609 | 224.731430 | 732.115126, -1088.959264 | -0.063987, -0.042879 | -403.605133 |
| 13 | 90 | 732.700850, -1090.631120 | -0.000805, -0.301385 | 223.052338 | — | — | — |

## Failure reached at tick 88

The new route enters tick 88 at X=732.115125866461 with VX=-0.063986548704. Its next positional constraint, at tick 89, requires X >= 732.699999988079: a required positive-X movement of 0.584874121618 in one tick despite entering with negative X velocity. The historical route enters tick 88 at X=732.697999599940, already almost at that wall, with VX=-0.000111150093. This directly identifies the problematic state committed by the new two-jump window at index 11. All attempted windows at index 12 then fail. Source: the final `LRS-CONS` and `LRS-WIN` records in [the new route trace](prefix-replay-2026-10-23/normal7-commit1-prefix-replay.txt).

This investigation has not yet isolated which later changed window is decisive beyond the first difference, or proved exact one-tick reachability limits for the tick-88 state. A next focused experiment could distinguish full continuous-model feasibility from exact replay feasibility: retain a full-window model-feasibility requirement while checking Minecraft replay only on committed ticks. That change is not implemented here.

## Test policy

Use focused tests/probes for these experiments. Background long-running commands and redirect output to files; inspect relevant failures without blocking investigation work. A green full suite is not the objective of these probes. The earlier full suite had already exited before the request to stop it could be acted on; it failed and is not being rerun. Its route-completion failure is the outcome investigated here. Other suite failures are not pursued as part of this focused experiment.
