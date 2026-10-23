# Verified comparison: 7-jump ladder, normal vs fixed margin

Verified on 2026-10-23 from branch `codex/first30-margin-trace-bookkeeping`, starting at `2a9b4a8`. No production solver code was changed for these controls. Source capture SHA-256: `af5d2d1bc6e0ab466f1b6cf309f3a26e76e4a63765ac8b15ff02fb4ad55d28bf`.

## What was reproduced

Both runs use the first 30 jumps (250 ticks, 229 constraints) of `~/Downloads/LinkcraftII_Competition_01/real/11.json`, window ladder `7,5,3,2,1`, commit=1, and no time deadline. Normal robust margins are `0.05,0.02,0.01,0.005,0.0012,0.0003,0`; fixed robust margin is `0.0003`.

The new opt-in [comparison test](../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/First30MarginComparisonTest.java) calls the production window slicing and closed-form implementation using reflection to inject the margin list, then uses the same objective order, SLP fallback and commit rule as LongRunSolver. It reproduces all **36 normal window outcomes and all 31 fixed window outcomes** in the saved historical traces. Every recorded entry position, velocity and carried yaw matches the historical `LRS-WIN` records to their printed precision (maximum difference about 5.7e-13). The normal run completes all 30 jumps and passes the full chained exact replay. The fixed run stalls at zero-based start index 13 after committing indices 0–12. These reproduce the earlier results; they do not prove that the fixed entry state is mathematically infeasible.

Sources: [normal historical trace](linkcraft-real11-first30-normal7-commit1.txt), [preserved fixed historical trace](linkcraft-real11-first30-fixed0003-commit1.txt), [new exact entry states](margin-comparison-2026-10-23/margin-comparison-states.csv), [new objective attempts](margin-comparison-2026-10-23/margin-comparison-attempts.csv). Raw new traces are in [the comparison directory](margin-comparison-2026-10-23).

## Corrections to the handoff and chat

1. At start index 9, the normal run solved `[9,16)` with **Z/MAX**, pattern `x@9-15+z@7`, at margin **0.0012**. That successful pattern did not try 0.0003: the implementation returns at the first certified rung. The logged 0.0003 violation of 0.01739 was in the earlier **unpatterned** pass, not that successful pattern. Source: [normal objective CSV, line 14](linkcraft-real11-first30-normal7-commit1-objectives.csv#L14).
2. The runs diverge on the **first solve**, not only around index 12. From the identical initial state, normal margins certify the first `Z/MAX` closed-form candidate at 0.05. Fixed 0.0003 misses all four closed-form objectives, then succeeds via centered SLP. Their committed states already differ at tick 12. See the first rows of [the new attempt CSV](margin-comparison-2026-10-23/margin-comparison-attempts.csv) and the two `normal-entry-i0` raw control traces.
3. Several rows in the earlier chat window table were wrong. In particular the normal run solves the 7-jump windows at indices 7, 8, 9 and 10; it does not shrink them as the fixed run does. The complete corrected table is below.
4. The earlier state table's yaw column generally used `wgf[commitTicks]`, a **preview of an input tick outside the committed prefix**. LongRun carries `wgf[commitTicks-1]` into the next solve. The corrected table below uses that incoming committed yaw, verified against `LRS-WIN ... yaw=`. Source: [LongRunSolver commit code](../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/LongRunSolver.java#L291). Positions/velocities were valid apart from a rounding typo in the earlier jump-3 Z entry. These samples are at segment boundaries defined by ground/air annotations, not established as one tick before landing.

## Controlled five-jump comparison from identical states

The target `[11,16)` is zero-based jump indices 11–15, global ticks `[80,117)`: one-based jumps 12–16. Each row below holds position, velocity, incoming yaw, inputs and constraints identical between its two margin policies. The entry states were reconstructed by rerunning each prefix, preserving full double precision, not by parsing rounded coordinates from a trace.

| Tick-80 entry state taken from | Normal margin ladder | Fixed 0.0003 |
|---|---|---|
| Normal run | Z/MAX misses; Z/MIN succeeds at 0.0012 | All four closed-form objectives miss; fallback SLP misses |
| Fixed run | All four closed-form objectives miss; fallback SLP misses | All four closed-form objectives miss; fallback SLP misses |

The normal entry is `(x,z;vx,vz;yaw)=(732.8134986565648,-1088.4669681845915;0.10228287038020580,0.27088345924271480;99.895004272)`; fixed entry is `(732.8197042580988,-1088.6399260441533;0.10864859203271630,0.27048271297314497;88.294067383)`. Thus entry state matters, and margin search independently matters even after holding that state fixed. Sources: the four `i11-...-policy` traces in [the comparison directory](margin-comparison-2026-10-23) and [the attempt CSV](margin-comparison-2026-10-23/margin-comparison-attempts.csv).

## Why the same-state margin comparison differs

The margin ladder is also a numerical continuation method: [runLadder](../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/ClosedFormSolve.java#L576) passes each rung's dual multipliers as the next rung's warm start. A single 0.0003 rung starts from zero multipliers. [CostateDualSolver](../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/CostateDualSolver.java#L40) has a 100-iteration cap per call and additional early stopping rules. A failed candidate can still violate the approximate model's constraints; it is not evidence that the exact feasible set is empty.

The margin ladder also changes inertia pattern discovery. For `Z/MIN` at the normal tick-80 entry, the best unpatterned candidate from the full ladder leads to pattern `x@4-10+z@2`. Fixed 0.0003's unpatterned candidate instead leads to `z@2`; its following patterned candidate is worse and the pass loop stops. Local tick 2 is global tick 82; X ticks 4–10 are global ticks 84–90. Sources: [normal-entry normal-policy trace](margin-comparison-2026-10-23/solver-trace-margin-control-normal-entry-i11-normal-policy.txt#L60), [normal-entry fixed-policy trace](margin-comparison-2026-10-23/solver-trace-margin-control-normal-entry-i11-fixed-policy.txt#L48), and [solveWithPrefold](../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/ClosedFormSolve.java#L470).

To isolate warm starts from pattern discovery, an additional diagnostic held the **normal entry, Z/MIN objective, and the successful pattern `x@4-10+z@2` fixed**. It builds the same reduced dual as production and runs the following margin sequences. Unlike production, this diagnostic deliberately continues after a certified rung.

| Dual setup, identical entry and pattern | Replay result | Worst violated position constraint | Iterations at that margin |
|---|---|---|---:|
| Only 0.0003, cold start | misses by 0.053616983765 | Z@98hi | 100 |
| Only 0.0012, cold start | misses by 0.174854946575 | X@111 | 100 |
| Normal ladder through 0.0012 | feasible | none | 100 at 0.0012 |
| Continue that same ladder to 0.0003 | feasible | none | 5 at 0.0003 |
| Continue to 0 | feasible | none | 5 at 0 |

This establishes that **0.0003 itself does not force the observed miss**. The implementation's cold numerical solve and subsequent pattern discovery fail to find a candidate that the continued dual solve can find. It does not establish a general advantage for large margins, or distinguish the value of continuation from an equal iteration count spent repeatedly solving a single margin.

Source: [held-pattern diagnostic trace](margin-comparison-2026-10-23/solver-trace-margin-control-normal-entry-i11-rungs.txt#L10), produced by `diagnoseRungs` in the comparison test. The diagnostic records both tightened smooth-wall violations and exact replay violations. Even at the certified 0.0012 rung, the smooth tightened model still has a residual of 0.00114005 on Z@89lo; production accepts because the original exact replay constraints all pass. A selected margin is a requested inward buffer, not a verified minimum clearance guarantee when the dual solve is unfinished.

The cold 0.0003 candidate in this held-pattern control also shows the model/replay mismatch directly: it assumes X is zeroed at global tick 84, but the replay enters that tick with `vx=-0.006978663763`, outside the 0.005 threshold. The warm-ladder candidate has `vx=-0.003913402806` there, so the inertia threshold check actually zeros it. This control fixes the successful pattern explicitly; it is distinct from the fixed-policy discovery trace, which discovered only `z@2` for its Z/MIN retry.

For that certified candidate, the exact inertia threshold checks at global ticks 82 (Z) and 84–90 (X) all have absolute velocities below 0.005; the diagnostic logs their values. The exact replay remains valid when continued to 0.0003. Source: `RUNG-INERTIA` rows in the same trace.

## Downstream effect at the next commit

At index 12 (tick 88), the normal run solves the 7-jump window `[12,19)` via X/MIN at 0.0003. The fixed run misses 7,5,3,2 and solves only `[12,13)` via Z/MAX. By tick 90 the normal path has `z=-1090.6311197842494, vz=-0.301385002588`; fixed has `z=-1089.5637692596672, vz=+0.073811447870`. The fixed path then misses every window from index 13. This is the visible later divergence; the causes already affected the search and committed states much earlier.

As another control, the five-jump `[12,17)` window from **normal's tick-88 state** succeeds under fixed 0.0003 with Z/MAX; normal margins reach SLP, which succeeds. From **fixed's tick-88 state**, both policies miss. Sources: the `i12-...-policy` traces and [attempt CSV](margin-comparison-2026-10-23/margin-comparison-attempts.csv). The actual normal route did not attempt its five-jump fallback there because its preceding seven-jump attempt had already succeeded.

## Every attempted window through the fixed run's stopping point

Indices are zero-based. Objectives are shown in actual call order. ✓ means exact feasible; × means no feasible result returned. A miss can still have a best rejected candidate, recorded with its largest violation and worst constraint in the CSV. Every fixed closed-form call uses 0.0003. Normal closed-form rung details remain in the raw traces and historical objective CSV.

| Start index | Normal margin ladder: attempted windows | Fixed 0.0003: attempted windows |
|---:|---|---|
| 0 | **7 jumps**: Z/MAX ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ |
| 1 | **7 jumps**: Z/MAX ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ |
| 2 | **7 jumps**: Z/MAX ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN ✓ |
| 3 | **7 jumps**: Z/MAX ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ |
| 4 | **7 jumps**: Z/MAX ✓ | **7 jumps**: Z/MAX ✓ |
| 5 | **7 jumps**: Z/MAX × → Z/MIN ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ |
| 6 | **7 jumps**: Z/MAX × → Z/MIN ✓ | **7 jumps**: Z/MAX × → Z/MIN ✓ |
| 7 | **7 jumps**: Z/MAX × → Z/MIN ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN ✓ |
| 8 | **7 jumps**: Z/MAX ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN ✓ |
| 9 | **7 jumps**: Z/MAX ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**3 jumps**: Z/MAX × → Z/MIN ✓ |
| 10 | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**3 jumps**: Z/MAX × → Z/MIN ✓ |
| 11 | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**3 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**2 jumps**: Z/MAX × → Z/MIN ✓ |
| 12 | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**3 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**2 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**1 jump**: Z/MAX ✓ |
| 13 | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ | **7 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**5 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**3 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**2 jumps**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>**1 jump**: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP × |

## Corrected committed states

Each row is the state entering the next segment after one committed jump. Yaw is the last applied yaw from the committed prefix, so it matches the next window's seed yaw. Values are rounded to six decimals here; the CSV retains 17 significant digits for X/Z state and the exact float yaw to nine decimal places. Boundary state is start-of-tick; an input at that boundary has not yet been applied.

| Committed jump (1-based) | Boundary tick | Normal position X,Z | Normal velocity X,Z | Normal incoming yaw | Fixed position X,Z | Fixed velocity X,Z | Fixed incoming yaw |
|---:|---:|---|---|---:|---|---|---:|
| 1 | 12 | 743.707005, -1103.714624 | -0.223148, 0.067035 | 115.534653 | 743.704467, -1103.725681 | -0.223000, 0.067374 | 115.315399 |
| 2 | 21 | 740.782891, -1102.628110 | -0.259248, 0.110825 | 101.456955 | 740.785074, -1102.622263 | -0.261181, 0.107753 | 105.568260 |
| 3 | 30 | 737.886542, -1101.003672 | -0.254481, 0.149129 | 102.224350 | 737.879061, -1101.013137 | -0.255132, 0.148100 | 102.292244 |
| 4 | 40 | 734.738602, -1099.039450 | -0.249842, 0.158262 | 101.704819 | 734.751259, -1099.024040 | -0.247208, 0.161573 | 97.990646 |
| 5 | 55 | 730.338594, -1096.094697 | -0.226905, 0.168495 | 86.486671 | 730.330045, -1096.115837 | -0.227440, 0.167240 | 84.899483 |
| 6 | 64 | 729.470230, -1093.541305 | 0.019594, 0.216686 | -10.911901 | 729.422803, -1093.643065 | 0.018458, 0.206892 | -6.274253 |
| 7 | 68 | 729.888283, -1092.709983 | 0.096759, 0.166518 | 4.878670 | 729.796070, -1092.788378 | 0.087160, 0.172362 | 8.261790 |
| 8 | 71 | 730.517873, -1091.763474 | 0.156344, 0.232296 | 8.010955 | 730.421735, -1091.841418 | 0.156079, 0.231933 | 6.456628 |
| 9 | 75 | 731.509106, -1090.430953 | 0.190637, 0.249848 | 1.478904 | 731.437591, -1090.530906 | 0.196345, 0.244617 | -2.640330 |
| 10 | 77 | 732.162233, -1089.651388 | 0.219667, 0.262326 | 5.367081 | 732.139678, -1089.800145 | 0.236712, 0.245421 | -1.342495 |
| 11 | 80 | 732.813499, -1088.466968 | 0.102283, 0.270883 | 99.895004 | 732.819704, -1088.639926 | 0.108649, 0.270483 | 88.294067 |
| 12 | 88 | 732.698000, -1089.740940 | -0.000111, -0.231609 | 224.731430 | 732.699690, -1089.745818 | -0.000002, -0.226463 | 225.000000 |
| 13 | 90 | 732.700850, -1090.631120 | -0.000805, -0.301385 | 223.052338 | 732.700317, -1089.563769 | 0.000000, 0.073811 | 405.000000 |

## Reproduction and remaining questions

Run:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.marginProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.First30MarginComparisonTest
```

The persisted files can also be checked without running Java:

```sh
python3 docs/research/solver-traces/verify_saved_margin_runs.py
```

The [verification script](verify_saved_margin_runs.py) compares every saved window outcome and entry state against the historical logs and checks all controlled results. It passed. The [JUnit result](margin-comparison-2026-10-23/test-result.xml) records one passing opt-in comparison test.

`PKC_TRACE_INPUT` can point to the same capture at a different path. The test is opt-in because the capture is external. This command passed, including original-state checks, normal completion, fixed failure index, and full chained exact validation for the normal run. Raw output writes to `core/build/reports/solver-trace-margin-control-*.txt`; copies of this run are persisted here.

No solver behavior was changed. Potential next experiments, not implemented: (1) spend the same iteration count at fixed 0.0003, preserving multipliers between calls, to distinguish continuation from extra iterations; (2) use the successful pattern discovered by a broader search as a seed for the tighter-margin solve; (3) retain the feasible remaining suffix from an accepted window so a later window-search miss does not discard a known continuation. Failed controls above establish search failure, not mathematical infeasibility of their entry states.


## Initial seven-jump constraint audit

The [initial-window audit](linkcraft-real11-initial-seven-constraint-audit.md) now lists every original constraint for all retained candidates and every inertia pass. Z/MIN and X/MAX have small replay-only misses; Z/MAX and X/MIN already violate the continuous constraints. The normal 0.05 candidate has minimum actual replay clearance about 0.03509 blocks. This experiment runs only ticks 0–68.
