# Restore SLP replay validation; keep closed-form shared validation

2026-10-24. Per user instruction, SLP's validation is restored to the recorded upstream baseline02844f45872604b21d0d139736d3ae0163454156. There is no local master branch; this is the previously recorded upstream/main baseline. Implementation commit in [metadata](metadata.json).

## Change

- SlpSolve compiles final checks with `JumpConstraintCompiler.compile(spec)`, including all window rows.
- Phase1 uses its original replay clearance gate, with its original ordinary→inertia-aware retry on phase1 failure.
- Removed the added prefix-plus-continuous phase1 bypass and final continuous veto.
- Removed SlpSolve.Config.replayTickLimit and LongRun's assignment to it.
- Kept diagnostic logging. The remaining SlpSolve.java diff from recorded upstream concerns diagnostics, not validation behavior.
- ClosedFormSolve and its DualCandidateRetention still use WindowCandidateValidation. Their whole-window continuous and committed-prefix replay acceptance are unchanged.
- Final full-route replay still checks every original constraint.

This is not a wholesale rollback of shared seed numerics. SLP's separate margin0 dual seed still uses the previously modified10000 cap/EPS2=1e-24, and the small-costate decoder substitution remains disabled. Its LP/trust-region rules, centered objective, ordinary/aware handling and LP budgets remain as before.

## Focused identical-entry results

Exact state132 reconstructed from the archived staged prefix, same as [the preceding diagnostic probe](../jump20-window-comparison-2026-10-24/README.md). Cap1000, fixed CF margin.0003, proposed commit through155. No full-route rerun.

| Window |Before |After |Full-window replay violation |Continuous violation, diagnostic only for SLP |
|---|---|---|---:|---:|
|20–22, endpoint178 |SLP rejected by continuous veto |SLP accepted |0 |.047522722002895534 |
|20–21, endpoint169 |CF Z/min retained126 accepted |Same CF success |0 |0 |

For20–22, the game path retains Z134 incoming momentum-.00500667357 while continuous simulation zeroes-.00499406563. It remains a continuous mismatch, but this no longer rejects a game-valid SLP return. The full-window game path has approximately.001756 minimum replay clearance. The continuous audit is now attached by the test AFTER the solver returns, solely to compare policies; production SlpSolve does not call the shared helper.

[Returned-candidate results](returns.csv), [trace](solver-trace.txt), [entry](entry.csv). Per-row margin/tick/inertia CSVs export the actual returned candidate as the last audit, rather than conflating an earlier terminal CF audit with the selected return. Earlier audit rows are still CF's own validation calls. Endpoints169 and178 both return accepted solutions.

## Evidence and scope

[Focused probe source](../../../../core/src/test/java/de/legoshi/parkourcalc/core/anglesolver/solver/Jump20WindowComparisonProbeTest.java) asserts both windows return and their FULL window replays have violation0. [JUnit result](focused-result.xml), [Gradle log](probe.log). One focused test executed, no full suite. Initial compile found a second legacy test still setting the removed SLP prefix config; [initial compile log](initial-compile.log) preserved. Both obsolete SLP-specific test expectations were updated for replay-only semantics so the test sources compile; those test classes were not run separately in this task.

Old full-route runs are preserved with their original validation policy. This local result does not predict later committed states or prove a complete route. No fresh baseline39 state has been generated under replay-only SLP acceptance.

## Reproduce

In source-matched /tmp/pkc-endpoints-fixed:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.jump20ComparisonProbe=true :core:test -PslowTests --tests '*Jump20WindowComparisonProbeTest' > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/jump20-slp-replay-validation-2026-10-24/probe.log 2>&1
```

Output redirected asynchronously. Default probe output now uses this new directory; the original rejected20–22 artifacts are unchanged. Set pkc.jump20Output for another output directory if required. Solver-trace temporary filename is reused but its saved copies remain in separate artifact directories. User j127 fixture untouched.
