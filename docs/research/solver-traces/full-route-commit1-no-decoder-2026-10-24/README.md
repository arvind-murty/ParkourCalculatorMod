# Full route, commit1, decoder fallback disabled

2026-10-24. Production HEAD74f5b09; the opt-in probe gained a commit-count property for this run. No production solver change during this rerun. [Metadata and source hashes](metadata.json) verify that production sources match /tmp/pkc-endpoints-fixed. User j127 fixture modification untouched.

## Configuration

Original real/11.json,810 rows,808 modeled ticks,636 original constraints,74 jump segments, final X/MAX@808. Normal nominal window ladder10,7,5,3,2,1, **commit ladder1 only**. Fixed CF margin.0003, dual cap10000, EPS2=1e-24, conditional inertia multiplier seeding enabled, intermediate retention enabled. Objective-direction decoder fallback commented out in both recovery sites. No cold production fallback has been added. No outer deadline.

Provisional objective advances to the first unconstrained airborne state; every original row inside the extended horizon remains in solve/validation. Continuous validation covers the entire window, game replay validation covers its committed prefix. Final route replay would retain every original constraint. This is a direct LongRun solve over the full input, not a full Optimize graph invocation. SLP fallback remains enabled. No full test suite.

## Result

**No complete route returned.** Committed jumps1–38 and stopped before **jump39/state351**, after **317605.258ms (5m17.605s)** of instrumented solving. Final full-route replay validation was not reached. The focused test passed because solver misses are recorded outcomes, not assertions of infeasibility.

| Commit1 run | Stop | Time scope |
|---|---|---|
|Previous free-air run, fallback enabled |Before jump11/state77 |Previous74.190s total included commit3 and commit1 |
|Current, fallback disabled |Before jump39/state351 |317.605s, commit1 only |

Runtime cannot be treated as a speed comparison because the new run does much more continuation work and omits the initial commit3 sweep. Entry states can differ from the start: removing decoding substitutions changes retained candidates even when accepted method names remain the same. By jump5 the accepted objective changes from Z/max to Z/min. Passing jump11 here is not an identical-entry replay of the separate tick77 focused probe.

There were97 attempted windows,38 accepted commits and391 objective/SLP calls. Accepted windows:26 closed form (13 Z/max,11 Z/min,2 X/max) and12 SLP. No final-window success or full-route replay check.

## Last attempted windows, all from351

Every rung tried Z/max, Z/min, X/max, X/min, then SLP; all missed. Proposed commitment was through state365, but none of these attempts committed inputs.

| Nominal jumps | Inputs end at state | Nominal end | Extension | Time ms | Outcome |
|---|---:|---:|---:|---:|---|
|39–48 |493 |490 |3 |6017.3 |Miss |
|39–45 |450 |449 |1 |5956.6 |Miss |
|39–43 |441 |438 |3 |6898.5 |Miss |
|39–41 |412 |406 |6 |2122.8 |Miss |
|39–40 |388 |381 |7 |901.5 |Miss |
|39 only |373 |365 |8 |223.2 |Miss |

Entry: X702.301967261052, Z-1111.839635559697, VX-.000002268767, VZ-.269424831020, seed yaw224.999984741deg. This is the actual LRS-WIN parameter; displayed committed-path yaw at a boundary can refer to a different input row. Missing all rungs does not establish physical infeasibility. No detailed diagnostic isolation of jump39 has been performed in this run.

## Data and recovery

- [Every window and timing](windows.md), [machine-readable windows](windows.csv).
- [Accepted objective/window comparison with previous commit1](comparison.md).
- [Every objective and fallback](objectives.csv), [every closed-form rung](cf-rungs.csv).
- [Every committed position, velocity and yaw](committed-ticks.csv).
- [Stop](stops.csv).
- [Complete raw trace, gzip](solver-trace.txt.gz), [parser](parse_trace.py).
- [Gradle output](full-route.log), [focused JUnit result](focused-result.xml).

The parser reads either solver-trace.txt or its gzip archive. CSV line numbers refer to the decompressed raw trace. Decompress with `gzip -dc solver-trace.txt.gz` for inspection. The comparison shows nominal jump counts; actual tick horizons include forward extensions. No output yaw file for a complete route exists because none was returned.

## Reproduce

From /tmp/pkc-endpoints-fixed with source hashes from metadata:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true -Dpkc.fullRouteCommit=1 -Dpkc.fullRouteLabel=-commit1-no-decoder-2026-10-24 :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginTenLadder > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/full-route-commit1-no-decoder-2026-10-24/full-route.log 2>&1
```

Executed asynchronously with output redirected throughout. One focused test, zero assertion failures; Gradle5m20s. `pkc.fullRouteCommit` defaults to3, preserving the probe's previous3,1 behavior when omitted. Commit1 avoids the commit3 sweep and starts from the original route entry.

## Timing follow-up

[Detailed accounting and per-start table](timing-breakdown.md), [CSV](timing-by-start.csv). Generated from this saved trace without rerunning.
