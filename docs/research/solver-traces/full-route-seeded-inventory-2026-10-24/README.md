# Full808-tick solve with retained candidates and conditional inertia seeds

2026-10-24. [Complete change inventory](INVENTORY.md), [raw commits](commits.tsv), [changed files](changed-files.tsv), [production patch](production-source.patch.gz).

## Active run

Input is the ORIGINAL downloaded `/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json`:810 input rows,808 modeled ticks,636 internal constraints,74 detected jump segments, original final X/MAX@808. Window ladder7,5,3,2,1; commit3 then1; fixed CF margin .0003; EPS2=1e-24; dual cap10000; no outer deadline. The LongRun solver is invoked directly, not the complete Optimize graph. SLP fallback remains enabled. Fixed windows temporarily remove directly capping endpoint rows per objective; final full-route replay retains all authored rows. Manual turnaround exclusion and +1/+5 standalone objective choices are not applied.

Seeding and candidate retention are NOW active production changes. Before this run seeding was test-only; e14dfe4 ports the tested assumed-feasibility rule for supported positional identity-fold cases. Ten changed production source files matched the main workspace byte-for-byte in the isolated runner. User j127 fixture untouched.

## Result

**No complete route returned.**

| Commit policy | Previous fixed run | Current seeded/retained run |
|---|---|---|
|3 |Stuck before jump14, state90 |Stuck before jump39, state351 |
|1 retry |Stuck before jump25, state196 |Stuck before jump14, state90 |
|Full route |Not returned |Not returned |
|Solve time |52691.357ms |47091.646ms |

Previous reference is [fixed seven-window local-bound removal](../linkcraft-real11-fixed-seven-window-local-bounds.md), code ae541ac through b98f87f before retention/seeding. These are single instrumented runs covering DIFFERENT prefixes; timing is not a speedup claim. The new commit3 pass progresses much farther, but the independently restarted commit1 route performs worse. It reuses permitted cached windows, as existing logic does; committed states depend on accepted candidates. This is not an identical-entry comparison after their paths diverge.

Current commit3 makes29 window attempts with13 successes, committing through state351; commit1 makes31 attempts with13 successes, committing through state90. Total60 window attempts and221 objective/fallback calls. All window sizes fail at the stated seams. Neither pass reaches final original-route replay validation, so the miss is a continuation/window-solving failure, NOT a final full-route replay rejection. The trace logs19 conditional seed applications and57 incumbent improvements across both passes. This result does not diagnose the numerical or geometric reason for the new stuck seams.

[Every attempted window](windows.md), [comparison of accepted solve types](comparison.md), [every objective](objectives.csv), [every CF rung](cf-rungs.csv), [every committed state including yaw/velocity](committed-ticks.csv), [stops](stops.csv). `block_start_jump` in committed-ticks is the trace's committed BLOCK start, not a claim every tick belongs to that one physical jump. State tick indexing remains pre-input. The CSV records trace source line numbers for checking.

## Focused production endpoint checks before full run

Same exact saved state55 and all original rows, fixed .0003. Retention enabled in both off/on seed controls:

| Window objective | Seeding off | Seeding on | Full exact replay audit |
|---|---|---|---|
|Z/MIN@90 |Miss |Accepted saved114; continuous/prefix violations0 |Uncommitted suffix miss .000112079218 |
|Z/MAX@103 |Miss |Accepted saved82; continuous/prefix violations0 |Pass |

The off/on controls demonstrate the port reproduces the successful direct experiments through the actual production `ClosedFormSolve.optimizeRobust` entry point. Both current shared validations pass for selected paths; full exact replay of j13's uncommitted suffix is an additional audit, not required acceptance. These are the saved entry55 experiments, not the new route's necessarily different states55/90.

Eight focused tests passed (integration probe1, retention units2, shared validation5), followed by ONE full-route probe, which completed successfully as a test while recording an experimental solver miss. No full test suite ran. Endpoint XML output was replaced by the later Gradle task; [focused endpoint log](focused-endpoints.log), [explicit outcomes](production-endpoints.csv), [endpoint trace](production-endpoints-trace.txt) preserve its evidence. Full-route JUnit XML is archived.

## Reproduce

Isolated core-only `/tmp/pkc-endpoints-fixed`, JDK21; source snapshots and [settings](settings.core-only.gradle) archived. [Metadata and input hash](inventory-metadata.json), [source hashes](source-hashes.csv).

Focused endpoints:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaIntegrationProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaSeedIntegrationProbeTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.DualCandidateRetentionTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.WindowCandidateValidationTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/full-route-seeded-inventory-2026-10-24/focused-endpoints.log 2>&1
```

Full route:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true -Dpkc.fullRouteLabel=-seeded-2026-10-24 :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginSevenLadder > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/full-route-seeded-inventory-2026-10-24/full-route.log 2>&1
```

[Full raw trace](solver-trace.txt) contains all bound omissions, model/replay checks, inertia patterns, seed eligibility/errors, incumbent updates and timings. Historical traces were not overwritten.
