# Objective-specific forward window adjustment

## Requested correction

Previously `runHorizon` chose one window endpoint using the intermediate Z/MAX objective, then `solveWindow` ran every alternate objective and centered SLP on that same extended scenario. Thus an extension motivated by a Z/MAX upper wall was inherited even by an objective that had no matching cap of its own.

The decision now happens inside the closed-form objective loop. Each objective starts from the **nominal** scenario, checks its own axis/sense against the nominal endpoint's original constraints, and independently chooses an endpoint.

| Objective | Matching direct wall that can trigger extension | Opposite wall alone |
|---|---|---|
| X/MAX | X upper bound | Does not trigger |
| X/MIN | X lower bound | Does not trigger |
| Z/MAX | Z upper bound | Does not trigger |
| Z/MIN | Z lower bound | Does not trigger |

An equality pin matches either sense. Only same-axis, same-objective-tick absolute positional rows are recognized, through the existing `ObjectiveBounds.directCap` helper. Relative rows, custom directions and motion objectives remain outside this detector's scope. A detected cap is a structural risk indicator, not proof that the wall will be active.

## Actual execution

For an enabled adjustment policy on a fixed-start nonterminal window:

1. Keep nominal start/end, entry position/velocity/yaw and committed prefix.
2. Try CF Z/MAX, Z/MIN, X/MAX and X/MIN in the existing order, stopping on the first feasible result.
3. For each objective independently, inspect its matching nominal-end wall. If no match, solve the nominal window. If matched, try the configured forward size first (+1 jump or +1 tick), then the other size if the first endpoint is capped for this same objective or unavailable. If neither forward candidate removes the match, retain the nominal endpoint.
4. Rebuild scenario, constraints and endpoint objective for that attempt only. An alternate never inherits the preceding attempt's endpoint.
5. Centered SLP, if reached, solves the **nominal** feasibility problem. Its surrogate objective is not being maximized in this fallback, so an objective cap does not justify extending that feasibility solve.
6. When a candidate succeeds, recover the accepted scenario from its yaw-array length before exact replay/commit. This also applies to a window-cache hit. The committed interval is the nominal interval throughout.

The cache key now includes the policy and next-jump endpoint, since the nominal start/end alone no longer identify the set of attempted scenarios. `LRS-WIN` describes the nominal window; each `LRS-ADJUST` records its objective and selected endpoint; `LRS-SELECT` records the accepted endpoint. Objective timing/result lines contain the selected objective tick. All selected scenario constraints pass the same existing acceptance path as before.

Final-goal windows and free-start windows keep their existing paths. The adjustment remains opt-in (`WindowAdjustment.NONE` by default); the current fixed-run probes explicitly enable FORWARD_JUMP or FORWARD_TICK. Dual cap1000, EPS2=1e-24, fixed-margin option and validator semantics were not changed by this edit.

## Consequence for the current jump6 investigation

The saved nominal endpoint88 has both Z88 upper and Z88 lower bounds (and X bounds). Therefore Z/MAX can still qualify for extension because of its upper wall, while Z/MIN can independently qualify because of its lower wall. The correction prevents inheritance; it does not imply Z/MIN must always retain end88. No new route outcome is claimed from this implementation.

## Evidence and status

Implementation: `LongRunSolver.runHorizon`, `WindowContext`, the objective-taking `adjustedEnd` overload, `hasDirectCap`, `objectiveWindow`, and the per-objective `solveWindow` loop. The original private +Z preflight signature and standalone `solveWindow` signatures remain as adapters for existing diagnostics.

Ran only `:core:compileJava` in the background with the JDK21 Gradle override; compilation succeeded. [Compile log](objective-specific-window-adjustment-2026-10-23/compile.log). No tests were added or run, and no route probes or entry/margin controls were run for this change. Behavioral effects on the saved reference comparison remain to be measured.
