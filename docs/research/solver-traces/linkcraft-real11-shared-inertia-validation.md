# Shared window acceptance using the existing continuous inertia loop

This supersedes the fixed-pattern continuous check in `f8929eb`. The acceptance rule is now independent of the solver's assumed pattern:

- Every original constraint must pass over the entire continuous window, with actual inertia threshold checks.
- Every original constraint belonging to the committed prefix must pass Minecraft replay. This means constraint feasibility, not equality between the continuous and Minecraft trajectories.

## Reuse of existing model calculations

`JumpLinearModel.zeroingPattern` already carries continuous X/Z velocity tick by tick. Its original loop is now the private `traceContinuous`, called both by `zeroingPattern` and by the new `forwardContinuous`. The latter records positions and carry velocities in `ForwardPath`. It applies threshold checks, adds the angle-dependent movement, advances position, then applies friction, using the same constants and operation order as the original zeroing loop. For Minecraft 1.8, each axis independently becomes zero only when its magnitude is strictly below 0.005. No game-angle mapping is used on this continuous path.

The fixed-pattern affine model can also derive positions, but those positions assume the supplied zeroing decisions. Using them alone cannot establish that the decisions obey the threshold checks. The shared recurrence instead evaluates the candidate with its actual threshold behavior. A closed-form rung has a fixed pattern (possibly unpatterned); inertia-aware SLP can update its local pattern between iterations. Neither determines the pattern used by acceptance.

`WindowCandidateValidation` compiles full and prefix constraints once. `evaluate` returns separate continuous/replay violations and their maximum. An overload reuses already computed game facings and replay, used by SLP. Closed-form rung acceptance, ordered closed form, and SLP phase-1/final acceptance call the helper when a shortened replay horizon is configured. It is public and can be reused after other candidate producers. Terminal and ordinary standalone solves retain their previous full-replay behavior.

The new forward method records horizontal movement only; Y stays at the initial value. All currently compiled constraints use X/Z or facing. It is not a replacement for the general Minecraft forward model.

## Focused evidence

All 10 selected tests passed: three helper tests, four prefix tests, two capture-specific comparison probes, and the 30-jump route probe. Commands ran in the background with output redirected to a file. No full suite was run.

The helper tests distinguish (1) fictitious survival of velocity below threshold, (2) fictitious zeroing of velocity above threshold, and (3) strict per-axis checks and recorded post-friction carry. The initial seven-jump Z/MIN prefix acceptance and the prevented fourth-window regression also still pass.

The 30-jump route now completes, unlike the prior fixed-pattern feasibility check. It returns 250 yaws and passes LongRun's final full Apply-style exact validation. Trace duration is 7730.24 ms (JUnit test 7.753 s); this single instrumented run is not a controlled performance comparison. We have not yet traced every changed window to attribute why route completion changed.

Sources: [focused log](shared-inertia-validation-2026-10-23/focused-tests.log), [complete route trace](shared-inertia-validation-2026-10-23/normal7-commit1.txt), [same-entry comparison](shared-inertia-validation-2026-10-23/first-divergence.txt), and saved JUnit XML alongside those files.


## Full-route fixed-margin 10-ladder probe

The [full-route experiment](linkcraft-real11-full-fixed0003-ten-ladder.md) runs the original 808-tick endpoint with standard windows 10,7,5,3,2,1, commit retries 3 then 1, fixed closed-form margin 0.0003, and shared actual-inertia validation. It returns no solution: both commit passes stall at tick 90 before jump 14. Total trace time 3829.48 ms; no outer deadline. The report preserves every window attempt and objective timing.
