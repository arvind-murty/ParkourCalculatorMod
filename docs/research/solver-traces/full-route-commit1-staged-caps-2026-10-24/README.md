# Full commit1 run:1000 iterations first,10000 only when stuck

Implementation b84931e,2026-10-24. [Metadata and source hashes](metadata.json). Original810-row real/11.json,808 modeled ticks,636 constraints,74 jump segments. User j127 edit untouched. One focused full-route LongRun probe, no full suite or full Optimize graph.

## Exact scheduling

At each committed entry state:

1. Try nominal windows10,7,5,3,2,1 with a1000 dual-iteration cap. Each window still tries Z/max, Z/min, X/max, X/min and centered SLP in its usual order until one succeeds.
2. If every window fails, retry the same window ladder from the same committed entry with cap10000.
3. On success commit one jump and start the next entry at1000 again. On exhaustion of both stages return a miss.

The cap is per closed-form dual run, including its inertia-pattern retries. SLP's own stopping rules are unchanged. The10000 stage restarts window solving; it does not resume a1000-iteration optimizer state. Cache keys include the cap, so a1000 miss cannot suppress its10000 retry. Actual-inertia multiplier seeding within a solve remains enabled. Intermediate retention remains enabled.

The configurable ladder is optional. LongRunConfig retains single10000 as its default. The full-route probe property `pkc.fullRouteDualCaps=1000,10000` opts into this experiment. Changing fixed margin preserves the configured caps and vice versa. The probe default remains single10000.

Fixed margin.0003, EPS2=1e-24, decoder fallback disabled, normal free-air objective placement and every original constraint retained. Continuous window validation and committed-prefix game validation unchanged. No outer deadline, no cold fallback added.

## Outcome

**No complete route. Same progress as the baseline, much less time:**

| Measurement |Single10000 baseline |Staged1000,10000 |
|---|---:|---:|
|Instrumented solve |317.605s |93.032s |
|Committed jumps |38 |38 |
|Stop |Before39/state351 |Before39/state351 |
|Attempted nominal windows |97 |106 |
|Objective/SLP calls |391 |435 |
|Logged CF dual runs |795 |848 |
|Logged CF dual iterations |4,987,427 |1,050,626 |
|Accepted CF windows |26 |27 |
|Accepted SLP windows |12 |11 |

Elapsed reduction70.7%, baseline/staged ratio3.41. Dual iteration reduction78.9%. These are instrumented whole-run policy comparisons: routes and the amount/type of solver work differ. No guarantee about solution quality or an uninstrumented speedup follows.

Every accepted commit1..38 came from the1000 stage. Jump39 was the first and only entry that exhausted that stage and used10000. The accepted objective/method/window can change much earlier, so this is not a continuation from the baseline's exact state351.

All1000 window calls total73.270s, including the final failed ladder. The10000 fallback at39 adds19.738s. Before attempting39, the staged run reaches the state351 boundary in69.383s; the baseline first attempts39 after295.485s.

At39, the new entry is X702.302172074316, Z-1111.856715620752, VX0, VZ-.269432741874, seed yaw-135.000015259deg. Both budgets miss all windows and all coordinate/SLP calls. This does not establish physical infeasibility. Final full-route replay validation is never reached.

## All attempts at39

| Nominal jumps |Actual endpoint |1000 stage ms |10000 stage ms |
|---|---:|---:|---:|
|39–48 |493 |475.5 |2257.8 |
|39–45 |450 |1631.3 |8075.8 |
|39–43 |441 |1170.9 |6015.5 |
|39–41 |412 |445.2 |2314.6 |
|39–40 |388 |149.8 |885.7 |
|39 only |373 |38.0 |188.5 |

Both stages propose committing through365, but all these attempts commit nothing. There are106 nominal windows:100 at1000 and6 at10000. Duplicate actual horizons can reuse cache results within the same cap. Accepted methods:16 Z/min,10 Z/max,1 X/max closed forms,11 SLP.

## Data

- [Every window, budget and timing](windows.md), [CSV](windows.csv).
- [Per-start timing comparison](timing-comparison.md), [CSV](timing-comparison.csv).
- [Accepted method/window comparison](comparison.md). Entries use `cap/window-size`, with × for misses.
- [All435 objective/SLP calls](objectives.csv), [all848 dual rungs](cf-rungs.csv).
- [Committed positions, velocities and yaw](committed-ticks.csv), [stop](stops.csv).
- [Complete trace, gzip](solver-trace.txt.gz), [parser](parse_trace.py).
- [Gradle log](full-route.log), [JUnit result](focused-result.xml).

The parser reads compressed or uncompressed raw trace. CSV line numbers refer to decompressed lines. A window CSV dual_cap identifies the stage, including for SLP calls; it does not mean SLP itself has that iteration cap. Coordinate-objective calls can include multiple dual runs and inertia-pattern passes. Paths can differ even when accepted method names match. This probe only records total returned-route validation if LongRun returns a full route, which did not happen here.

## Reproduce

In /tmp/pkc-endpoints-fixed with source hashes recorded in metadata, run asynchronously with output redirected:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true -Dpkc.fullRouteCommit=1 -Dpkc.fullRouteDualCaps=1000,10000 -Dpkc.fullRouteLabel=-commit1-staged-caps-2026-10-24 :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginTenLadder > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/full-route-commit1-staged-caps-2026-10-24/full-route.log 2>&1
```

Gradle1m35s, solver93.031823s. One focused test, zero assertion failures; solver miss is an allowed experiment outcome. No solver settings changed during execution. Baseline source and run artifacts are preserved in the sibling full-route-commit1-no-decoder directory.

### Iteration-count scope clarification

The recorded dual-run and iteration totals count the logged closed-form rungs. They exclude the separate dual solve used to initialize SLP. That seed uses the CostateDualSolver default cap10000 at margin0, even during the optional1000 stage; its time is inside the SLP objective-call timing. See the [jump39 handoff](../jump39-baseline-handoff-2026-10-24/README.md).
