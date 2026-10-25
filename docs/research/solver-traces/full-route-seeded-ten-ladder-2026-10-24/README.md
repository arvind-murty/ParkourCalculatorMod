# Full route with the normal ten-jump window ladder

2026-10-24. User requested returning to the normal ladder starting at10. Production `LongRunSolver.WINDOW` already equals10 and its default ladder is10,7,5,3,2,1. The previous run explicitly selected the seven-ladder probe. This run selects `traceFullRouteFixedMarginTenLadder`; no production source or input JSON was changed. Current experiment configuration is now the normal ten-ladder. Historical seven-ladder traces remain intact.

## Settings held constant

Original real/11 capture:810 rows,808 modeled ticks,636 original constraints,74 jump segments, final X/MAX@808. Commit ladder3 then1; fixed CF margin .0003; EPS2=1e-24; dual cap10000; conditional inertia seeding and intermediate retention enabled; decoder unchanged. Fixed windows temporarily remove matching endpoint caps for each objective's window solve/commit validation only. Final route replay still retains every original constraint. No outer deadline, no manual turnaround-window edits. SLP fallback enabled. This directly invokes LongRun, not the entire Optimize graph.

Execution used `/tmp/pkc-endpoints-fixed` with the same tested production sources as [the previous inventory run](../full-route-seeded-inventory-2026-10-24/INVENTORY.md). Source files were unchanged between these runs. User j127 fixture modification remains untouched and unstaged. [Metadata](metadata.json) preserves HEAD/configuration/input SHA256.

## Result

**No full route returned.**

| Commit policy | Seeded seven ladder | Seeded ten ladder |
|---|---|---|
|3 |Stopped before jump39, state351 |Stopped before jump26, state205 |
|1 retry |Stopped before jump14, state90 |Stopped before jump25, state196 |
|Complete route |No |No |
|Solve ms |47091.646 |161386.312 |

Each stuck seam exhausts its window ladder. Neither ten-ladder pass reaches final original-route replay validation, so the outcome is a window/continuation miss, not final route rejection. Windows and accepted candidates change earlier committed states, so these are policy comparisons, not identical-entry controls. Single instrumented runtimes cover different amounts of route/search and are not a controlled performance benchmark.

Ten ladder:84 window attempts,340 objective/fallback calls,26 conditional seed applications,57 incumbent improvements. Commit3 makes29 window attempts with9 successes; commit1 makes55 with24 successes. Normal ten-window attempts add work even when they later fall back to smaller windows. The numerical/geometric reason for the resulting stuck seams is not isolated by this run.

## Complete tables

- [Every attempted window](windows.md).
- [Side-by-side solve-type comparison with seven ladder](comparison.md).
- [Every objective and fallback attempt](objectives.csv).
- [Every closed-form rung and iteration count](cf-rungs.csv).
- [Every committed state, velocity and yaw](committed-ticks.csv).
- [Stopping seams](stops.csv).
- [Raw trace](solver-trace.txt), [Gradle log](full-route.log), [JUnit result](focused-result.xml).

CSV line fields refer to the raw trace. `block_start_jump` is the committed block's first jump as logged, not a claim that every tick belongs to that one jump. Tick states are before input. [parse_trace.py](parse_trace.py) rebuilds the tables and comparison from the archived trace and previous seven-ladder CSV.

## Reproduction

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true -Dpkc.fullRouteLabel=-seeded-2026-10-24 :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginTenLadder > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/full-route-seeded-ten-ladder-2026-10-24/full-route.log 2>&1
```

One focused opt-in test ran and completed successfully, recording the solver miss; no full suite or extra control runs. Gradle2m42s; instrumented solve161.386s. Same source/settings snapshots and recovery references as the preceding production-seeding inventory; no algorithm implementation commit is needed for selecting the existing normal ladder.
