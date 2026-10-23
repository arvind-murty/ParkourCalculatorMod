# Full continuous-window feasibility with committed-prefix Minecraft replay

Acceptance now requires both conditions for a nonterminal LongRun window:

1. All original window constraints pass on the continuous candidate, using the fixed inertia pattern of its model, before mapping angles to Minecraft facings.
2. Original constraints in the potentially committed prefix pass Minecraft replay, including position at the exit boundary.

The check uses the supplied feasibility tolerance (zero in these probes), against original walls rather than the inward margin. A candidate does not need to achieve the requested centering margin to pass. Terminal windows retain full Minecraft replay validation.

## Why the old experiment accepted an infeasible continuous candidate

`CostateDualSolver` can return a recovered candidate after its finite iteration limit without primal feasibility. The old acceptance code checked only Minecraft replay. Prefix scoping removed the suffix checks that had also rejected continuous-model violations. Constraints appearing in the optimization do not alone guarantee its finite-iteration output satisfies them.

## Implementation

- `JumpLinearModel.maxConstraintViolation` evaluates full continuous X/Z positions using the same affine coefficients and inertia pattern as optimization; `JumpConstraintCompiler` evaluates original position, paired-position, facing and cross-axis constraints on that path.
- `ClosedFormSolve.runLadder` combines prefix replay violation and full continuous violation before accepting a rung or selecting its best miss. `CF-CHECK` logs each separately. Ordered solves retain the inner fixed-pattern feasibility decision and check all continuous facing rows, including those stripped for the tie cascade.
- `SlpSolve` checks full continuous feasibility before bypassing phase-1 clearance rejection for a valid prefix, and again before returning a feasible final candidate. Inertia-aware SLP derives the final pattern from its continuous candidate; ordinary SLP uses its unpatterned model. `SLP-CHECK` logs both violations.
- These extra checks apply when `replayTickLimit` is shorter than the window. Standalone solves retain their prior acceptance behavior.

## Focused results

At global window ticks `[30,77)`, committing through tick 40, margin 0.05 has prefix replay violation zero but continuous violation `0.0577555413404`. It is now rejected after 100 dual iterations. The warm-start margin-0.02 rung passes both checks after 32 iterations, reproducing the original committed position `(734.738601761702, -1099.039450382282)`.

The initial seven-jump Z/MIN, margin-0.0003 probe still accepts its valid prefix despite a later Minecraft replay miss. Four prefix unit tests pass, including the updated fixed-seed SLP test rejecting a continuous lookahead violation. These tests distinguish the desired policy from restoring full-window Minecraft replay validation.

The first focused invocation ran seven tests: six passed; the 30-jump completion probe failed. The route now stalls at zero-based index 13, tick 90, rather than the prior prefix-only failure at index 12, tick 88. Full route success has not been established. See [route trace](prefix-continuous-2026-10-23/normal7-commit1.txt) and [focused invocation log](prefix-continuous-2026-10-23/focused-probes-with-route.log). No full suite was run.

The earlier [prefix-only comparison](linkcraft-real11-prefix-regression-comparison.md) remains historical evidence for the previous policy; its outcome is not the current acceptance rule.

A second focused invocation, with explicit assertions for the original tick-40 position and continuous feasibility, passed all six validation tests. See [log](prefix-continuous-2026-10-23/focused-validation-checks.log) and [same-entry trace](prefix-continuous-2026-10-23/first-divergence.txt).

## Inertia-pattern validity audit

The new full-window continuous check is **not a complete inertia-pattern consistency check**. `maxConstraintViolation` checks only the original `JumpSpec` constraints against positions under the supplied pattern. It does not evaluate internally generated velocity-band rows or require the supplied pattern to match the threshold checks of the candidate's actual continuous trajectory.

- Closed form: `runLadder` adds `lin.velocityWalls(vBound)` for ticks marked zeroed. These rows participate in the dual solve but are not explicitly revalidated by the new acceptance helper. Finite-iteration dual output may violate them. No keep-alive rows are added here for ticks assumed not zeroed; an unpatterned first pass has no such inertia rows at all. `velocityWalls` also uses non-strict band boundaries, whereas the actual zeroing comparison is strict.
- Ordinary SLP: the full continuous check uses the unpatterned model; it does not enforce the inertia threshold checks across the full window.
- Inertia-aware SLP: the new final continuous check calls `zeroingPattern` on the candidate itself, then evaluates positions using that derived pattern. This pattern is consistent with that candidate's deterministic continuous threshold simulation by construction (up to floating-point differences between affine evaluation and stepwise recurrence). This does not add velocity-band constraints to the LP or promise that the proposal obeyed the previous iterate's fixed pattern.
- Minecraft replay checks the actual inertia threshold behavior in the committed prefix. It does not validate suffix pattern consistency under the new prefix scope.

Sources: `ClosedFormSolve.runLadder`, `JumpLinearModel.velocityWalls`, `JumpLinearModel.zeroingPattern`, `JumpLinearModel.maxConstraintViolation`, and `SlpSolve.continuousViolation`. No behavior change or tests were run for this audit. A stronger acceptance rule would check the entire candidate using continuous simulation with actual per-axis inertia threshold checks, or explicitly validate both zeroed and surviving ticks of its fixed model.


## Superseding shared validation

The [shared inertia-validation experiment](linkcraft-real11-shared-inertia-validation.md) now reuses the original continuous `zeroingPattern` recurrence to record positions/velocities and validate actual threshold behavior throughout the full window. It replaces acceptance under an assumed fixed pattern. All ten focused tests pass, including full completion of the 30-jump route. Earlier fixed-pattern results remain historical evidence.
