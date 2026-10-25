# Full route with unconstrained airborne objectives

2026-10-24. Implementation3d4557e, source/notes HEADf4ac831 at execution start. [Metadata](metadata.json) records exact HEAD, input hash and settings. Production files match the isolated runner; no code change was made during this rerun. Historical runs are preserved. User j127 fixture edit untouched.

## Configuration

Original real/11 input:810 rows,808 modeled ticks,636 original constraints,74 detected jump segments, final X/MAX@808. Normal nominal ladder10,7,5,3,2,1 and commit3 then1. Fixed CF margin .0003, EPS2=1e-24, dual cap10000, conditional assumed-model-feasible seeding and intermediate retention enabled, decoder unchanged, no outer deadline. Direct LongRun invocation, not the full Optimize graph; SLP fallback remains enabled.

For provisional windows, extend forward to the first AIRBORNE tick referenced by NO original constraint (any axis/sense/mode; both references of paired rows). All original rows inside the extended horizon remain in solve and validation. Nominal jump boundaries still determine the committed prefix. Final route endpoint/objective remains authored and final replay retains every original constraint. No turnaround-region detector or manual banned section is applied.

## Outcome

**No complete route returned.**

| Commit policy | Previous ten-ladder, cap removal | Current ten-ladder, free-air endpoint |
|---|---|---|
|3 |Stopped before jump26/state205 |Stopped before jump25/state196 |
|1 retry |Stopped before jump25/state196 |Stopped before jump11/state77 |
|Complete route |No |No |
|Solve ms |161386.312 |74189.982 |

Neither pass reaches final original-route replay validation. These are window/continuation failures, not full-route replay rejections. Different horizons and retained constraints produce different early solutions and seam states, so later windows are not identical-entry controls. Lower runtime covers different search work and is not evidence of a faster solver algorithm.

The first nominal1–10 window already changes from ending77 with cap removal to ending91 with all original rows. The new accepted method is Z/MIN, whereas the preceding cap-removal run accepts Z/MAX. The subsequent states therefore diverge from the beginning. Standalone j13/j14 success from the earlier saved state55 does not establish success from these different states/horizons.

Current run has45 nominal window attempts,157 objective/fallback calls,34 conditional seed applications and61 saved incumbent improvements. Commit3 makes25 attempts with8 successes; commit1 makes20 with10 successes. Actual extensions are at most14 ticks among attempted windows. Trace contains zero LRS-DROP entries.

## Window collapse at the commit1 stop

At state77, before jump11, all candidates must commit only through80. The nominal ladder produces:

| Nominal window size | Nominal end | Selected free-air endpoint | Outcome |
|---:|---:|---:|---|
|10 |155 |161 |Miss |
|7 |128 |137 |Miss |
|5 |113 |121 |Miss |
|3 |90 |91 |Miss |
|2 |88 |91 |Same cached miss |
|1 |80 |91 |Same cached miss |

The final three rungs have identical input horizon77..91, original constraint set, seed and commit exit80. Reusing the cached result is expected; there are not three independent shorter solves. The first solve for that common horizon spends162.2ms. This is an observed implication of the new placement rule, not a diagnosis of physical infeasibility. We have not yet isolated whether the failure is numerical, constraint-related or a poor committed entry state.

## Full data

- [Every attempted window, nominal end and extension](windows.md).
- [Accepted solve-type comparison against the previous ten-ladder run](comparison.md).
- [Every objective/fallback](objectives.csv).
- [Every closed-form margin rung](cf-rungs.csv).
- [Every committed state, velocity and yaw](committed-ticks.csv).
- [Stops](stops.csv).
- [Raw trace](solver-trace.txt), [Gradle log](full-route.log), [JUnit result](focused-result.xml).

The jump ranges in window tables are NOMINAL; actual tick horizons may include additional jump inputs. `block_start_jump` in committed-ticks is the trace's committed block start, not the individual jump ownership of every tick. All CSV line fields refer to raw trace lines. Blank timing on cached windows means no new timed solve. [Parser](parse_trace.py) reconstructs the tables from this raw trace and the preceding ten-ladder CSV.

## Reproduce

From isolated core-only `/tmp/pkc-endpoints-fixed` with the same production files/settings archived by the preceding inventory and [free-air implementation](../free-air-objective-2026-10-24/README.md):

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true -Dpkc.fullRouteLabel=-free-air-2026-10-24 :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginTenLadder > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/full-route-free-air-ten-ladder-2026-10-24/full-route.log 2>&1
```

One focused full-route probe, zero test failures/errors; the solver miss is a recorded experiment. Gradle1m15s, instrumented solve74.190s. No full suite or additional engine-graph solve. No fixture, source algorithm, margin or stopping rule was changed to force a success.
