# Compiled route map for jump39 investigation

2026-10-24. Exact archived real11 input from the [jump39 handoff](../jump39-baseline-handoff-2026-10-24/README.md), SHA256 af5d2d1bc6e0ab466f1b6cf309f3a26e76e4a63765ac8b15ff02fb4ad55d28bf. Export generated using the current AngleSolverEngine.debugBuildSpec and LongRunSolver.jumpBoundaries. This task compiles the input and exports data, it does not optimize or run the full route again.

- [Constraints and tick ranges for jumps30–50](constraints-table.md).
- [Accepted/failed windows leading into39](leading-windows.md).
- [Exact636 compiled constraints, unrounded, including duplicates](compiled-constraints.csv).
- [All jump boundaries](jump-boundaries.csv).
- [Compiled per-tick inputs and physics annotations](physics-inputs.csv).
- [Table generator](build_tables.py).
- [Focused opt-in export probe](../../../../core/src/test/java/de/legoshi/parkourcalc/core/anglesolver/solver/RouteConstraintMapProbeTest.java).
- [Final Gradle log](export.log), [final result](export-result.xml).

## Indexing

Jump numbers are the solver's1-based human labels, not necessarily a human count of physical block-to-block jumps. The detector splits on annotated airborne-to-ground transitions and merges pieces shorter than2 ticks. It can produce a2-tick segment, as jump45 here. Input ticks for jump39 are351..364, with exit state365. The failure's X356 constraint is five input steps after entry351.

A position row at t constrains the state before input t. Table ownership of boundary constraints uses states(start,end], so a shared exit/start constraint is listed with the preceding jump. At the same time, jump j's input range is[start,end). This avoids duplicating a boundary row across both table entries. The tables do not omit constraints on grounded ticks. Adjacent ticks with identical displayed constraints are grouped. Duplicate rows are collapsed for display only; CSV preserves them.

Values in readable tables are rounded to3 decimals. Original walls include float-sized offsets, for example X356 <=700.4250000119209. Exact values are in the compiled CSV. X/Z intervals use the strongest compiled lower/upper rows at each tick. No paired or facing constraints occur in the displayed30–50 span, although the full compiled CSV contains facing rows elsewhere. Slipperiness NaN in physics-inputs means an authored AIR annotation; it is not a measured replay collision state.

## Correction to older bookkeeping

The export detects **76 segments**, not74 as stated in previous handoff prose. It verifies808 ticks and636 compiled constraints. The initial export test asserted the historical74 count and failed after writing the CSVs; that evidence is preserved in initial-export.log and initial-export-result.xml. The assertion was corrected to the actual76 and the focused export rerun successfully. This corrects documentation, not the solver's boundary algorithm. Jumps30–50 in the CSV match the original full-run LRS-WIN labels/ticks; the stop remains before39/state351.

## Observations for the next probe

From start30 onward, accepted lookaheads increasingly retreat behind39. At start37 the accepted37–38 window is319..352, committing to333. At start38 the accepted38-only window is333..352, committing to351. Neither includes the next authored/compiled constraint at356. At38, every larger attempted window that includes39 misses.

Entry351 in the staged run has X702.302172074316 and VX0. X356 requires <=700.4250000119209. This is a1.877172062395 change toward negative X in five input steps. SLP's archived worst row at356 is still about.688 away. This is a concrete reachability/entry-state lead; the export alone does not prove feasibility or infeasibility. Jump37's narrow X band at332 and carried lower wall through337 explain which preceding restrictions need to remain in any control extending the38 window.

## Reproduce export

Source-matched core-only runner /tmp/pkc-endpoints-fixed:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.routeConstraintMapProbe=true :core:test -PslowTests --tests '*RouteConstraintMapProbeTest' > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/jump30-50-route-map-2026-10-24/export.log 2>&1
```

The probe reads the archived input, not the mutable external Downloads copy. Rebuild Markdown with `python3 docs/research/solver-traces/jump30-50-route-map-2026-10-24/build_tables.py`. Two focused export invocations, no solver solve or full suite. Production sources unchanged. User j127 fixture unchanged.
