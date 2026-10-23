# Committed-prefix replay validation experiment

This experiment changes LongRun's nonterminal window acceptance: all window constraints still guide the optimization, but exact replay acceptance checks only the inputs that would be committed. Ordinary standalone closed-form and SLP solves keep their full replay horizon.

## Rules

- Compute the actual commit length before solving a window, including smaller fallback windows and the terminal remainder.
- Check positional constraints through the exit state, inclusive: committing input ticks `[0,k)` validates positions through tick `k`.
- Check facing constraints only on committed input ticks `[0,k)`. The next tick's proposed facing is uncommitted.
- A two-tick constraint is checked only when both referenced ticks belong to that replay prefix.
- Keep all window constraints in the closed-form wall model and SLP step construction. Future constraints are not deleted from the optimization.
- Scope both closed-form and centered SLP acceptance to the prefix. SLP continues using all rows to guide its steps, then may keep a prefix-feasible result even when full-window centering was unsuccessful.
- Include the number of committed ticks in the window cache key: commit=3 and commit=1 can accept different candidates from otherwise identical windows.
- A terminal window commits its whole remainder and uses full validation. The completed route still undergoes the original full exact replay after Apply-style yaw reconstruction.

Sources: `ClosedFormSolve.Config.replayTickLimit`, `SlpSolve.Config.replayTickLimit`, `JumpConstraintCompiler.compile(spec, inputTicks)`, and `LongRunSolver.runHorizon` / `solveWindow`.

## Focused regression

`First30MarginComparisonTest.initialSevenAcceptsExactCommittedPrefixDespiteLaterReplayMiss` uses the initial 68-tick window, Z/MIN, and margin 0.0003. Full replay rejects it. Prefix acceptance through the first jump's exit at tick 12 accepts it, while the full replay still misses by exactly the historical 0.000458454781 blocks. All 33 lookahead constraints remain in the solve. This regression passed.

`ReplayPrefixValidationTest` checks the inclusive exit-position boundary, exclusion of the next facing input, two-tick constraints crossing the prefix boundary, rejection of a violated exit position, and retention of an impossible future optimization constraint even when no replay constraints lie in the prefix. All four unit regressions passed, including a fixed-seed SLP check that prefix acceptance bypasses full-window phase-1 rejection. See [focused unit results](prefix-replay-2026-10-23/focused-unit-tests.log). The capture-specific Z/MIN regression also passed in the focused Gradle run.

Command:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.marginProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.ReplayPrefixValidationTest --tests de.legoshi.parkourcalc.anglesolver.First30MarginComparisonTest.initialSevenAcceptsExactCommittedPrefixDespiteLaterReplayMiss
```

## Route-completion regression

The new policy changes which candidate is accepted, and the prior normal-margin 7-ladder route now fails. In the full solver suite, `First30NormalMarginTraceTest.traceNormalMarginSevenWindowCommitOne` returns null: the route gets stuck at zero-based index 12, tick 88, after trying windows of 7, 5, 3, 2 and 1 jump. Its exact entry state there is `x=732.115125866461, z=-1088.959263717995, vx=-0.063986548704, vz=-0.042879314534, yaw=-403.605133057`.

The first policy difference occurs at zero-based index 3, global window ticks `[30,77)`, from the same entry state in both runs. The new replay horizon ends at the commit boundary, tick 40, so it accepts margin 0.05. The historical full-window check rejected that candidate for a later 0.05802-block violation and accepted margin 0.02. At tick 40, the new committed position is `(734.768677428967,-1098.993991550265)` versus historical `(734.738601761702,-1099.039450382282)`.

The historical full-window policy completed all 30 jumps. This is a known route-completion regression in the experiment, not a passing result, and the existing test assertion is retained. Prefix feasibility alone does not guarantee that the accepted state admits a continuation that the remaining window solves can find. Nor does it guarantee convergence of the full continuous lookahead model: a finite-iteration candidate can pass the prefix while violating later model constraints.

Source: [new route trace](prefix-replay-2026-10-23/normal7-commit1-prefix-replay.txt). The previously persisted historical traces are untouched. The earlier full suite had already exited before the request to stop it could be acted on. It failed and is not being rerun. Only focused tests and probes are in scope for this investigation; a green full suite is not the objective. The other reported suite failure, `YawDirectionCrossCheckTest.holdsTheSolvedPathsTurns[j001]`, has not been isolated or attributed and is not pursued here.

## Focused explanation of the regression

The [comparison report](linkcraft-real11-prefix-regression-comparison.md) preserves every window attempt through the new failure, committed positions/velocities/yaws, and per-window/objective/rung timing records. A focused same-entry probe passed (`tests=1 failures=0 timeMs=416`). At the first different window, both policies generate the same margin-0.05 candidate after 100 dual iterations. It already violates full continuous lookahead constraints by up to 0.05776 blocks; this is not merely a Minecraft angle-mapping miss. Prefix acceptance returns it immediately. Full-window acceptance rejects it and reaches a feasible margin-0.02 candidate after 32 additional warm-start iterations. The report links the complete constraint audit and raw trace.


## Subsequent full-continuous feasibility check

The [full continuous-window validation experiment](linkcraft-real11-prefix-continuous-validation.md) implements the requested combined policy and records focused results. It prevents the first bad margin-0.05 acceptance while preserving the initial Z/MIN prefix acceptance. The full 30-jump route still fails, now at index 13. Earlier descriptions of prefix-only acceptance in this document describe the prior experiment.
