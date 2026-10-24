# Fixed seven-ladder rerun with default dual cap1000

## Settings stated before launch

Full real/11 input:808 ticks,636 constraints, original X/MAX@808. Fixed-start LongRun, nominal windows7,5,3,2,1, commits3 then1 (restart), fixed CF margin0.0003. FORWARD_JUMP preflight policy prefers one jump forward on a directly capped intermediate+Z objective, with one-tick fallback if the preferred endpoint remains capped or unavailable. EPS2 remains1e-24. No outer deadline. Relative to the archived same-policy full run, only the default dual cap changed from100 to1000 (`c8d7616`); early stopping remains active. This also raises the default cap for SLP's dual seed.

Only `FullRouteFixedMarginTraceTest.traceSevenForwardJump` was run, backgrounded into a log. The probe completed; no solution was returned. As before, a passing diagnostic test records a miss rather than asserting route completion.

## Outcome

Commit3 stopped before jump14 at tick90 after1834.46ms of trace time. Commit1 restarted and also stopped before jump14 at tick90. Whole solve invocation4678.576ms. The saved cap100 same-policy invocation was1898.252ms; these individual instrumented runs are not a controlled performance benchmark.

Under commit1, jumps1–5 now all accept closed-form Z/MAX, matching the successful normal-margin reference's **solve types** there. Compared with cap100, jump2 changes Z/MIN→Z/MAX, jump4 centeredSLP→Z/MAX, jump6's successful smaller window changes Z/MAX→X/MAX, and jump10's successful smaller window changes SLP→Z/MIN. The first remaining difference from the successful normal reference is jump6: normal keeps7 jumps and accepts Z/MIN, while this run misses its adjusted nominal7-jump window and accepts X/MAX on5 jumps. Later lookahead still shrinks, and the route ultimately stops at the same place.

The normal reference is the saved successful250-tick/30-jump run with EPS2=1e-14 and normal margins. Comparisons with that run describe actual observed paths; they are not identical-entry or margin-only controls. The cap100 and cap1000 fixed runs use the same policy and route but can commit different states as earlier candidate selection changes.

## Solve types, commit1

Each window tries CF Z/MAX→Z/MIN→X/MAX→X/MIN→centeredSLP, stopping at success. A selected method implies all preceding methods failed and later ones were not called. `7×` means all four CF objectives plus SLP failed. Window sizes are nominal; actual ends are in the CSV.

| Starting jump | Normal-margin reference | Fixed cap100 | Fixed cap1000 |
|---:|---|---|---|
| 1 | 7: Z/MAX @0.05 | 7: Z/MAX | 7: Z/MAX |
| 2 | 7: Z/MAX @0.05 | 7: Z/MIN | 7: Z/MAX |
| 3 | 7: Z/MAX @0.02 | 7: Z/MAX | 7: Z/MAX |
| 4 | 7: Z/MAX @0.02 | 7: SLP | 7: Z/MAX |
| 5 | 7: Z/MAX @0.02 | 7: Z/MAX | 7: Z/MAX |
| 6 | 7: Z/MIN @0.0003 | 7×<br>5: Z/MAX | 7×<br>5: X/MAX |
| 7 | 7: Z/MIN @0.01 | 7: SLP | 7: SLP |
| 8 | 7: Z/MIN @0.0003 | 7×<br>5: Z/MIN | 7×<br>5: Z/MIN |
| 9 | 7: Z/MAX @0.0003 | 7×<br>5: Z/MIN | 7×<br>5: Z/MIN |
| 10 | 7: Z/MAX @0.0012 | 7×<br>5×<br>3: SLP | 7×<br>5×<br>3: Z/MIN |
| 11 | 7: X/MAX @0.0012 | 7×<br>5×<br>3: Z/MIN | 7×<br>5×<br>3: Z/MIN |
| 12 | 7×<br>5: Z/MIN @0.0012 | 7×<br>5×<br>3×<br>2: Z/MIN | 7×<br>5×<br>3×<br>2: Z/MIN |
| 13 | 7: X/MIN @0.0003 | 7×<br>5×<br>3×<br>2×<br>1: Z/MAX | 7×<br>5×<br>3×<br>2×<br>1: Z/MAX |
| 14 | 7×<br>5: SLP | 7×<br>5×<br>3×<br>2×<br>1× | 7×<br>5×<br>3×<br>2×<br>1× |


## Validation clarification during this run

`ClosedFormSolve` creates `WindowCandidateValidation` only when `cfg.replayTickLimit < window.numTicks`; `SlpSolve` has the same guard. In that partial-commit case, the helper requires:

1. Every original window constraint passes the continuous trajectory recomputed with actual Minecraft1.8 per-axis inertia threshold checks (strict absolute velocity below0.005 is zeroed).
2. Every original constraint in the committed prefix passes Minecraft replay.

The supplied fixed inertia pattern is for diagnostics only, not acceptance. The helper does not separately demand equality between the assumed and actual zeroing patterns, nor equality between continuous and replay trajectories. It checks the resulting actual trajectories against their original constraints.

**Exception:** when the entire window is committed, those call sites bypass the helper and use full Minecraft replay validation. Terminal recovery and the final whole-route Apply-style check also rely on full exact replay. Thus the pre-run shorthand “whole-window continuous plus prefix replay” applies to the partial-commit window path, not every solver invocation. No validator behavior was changed in this rerun.

Sources: `ClosedFormSolve.runLadder` at the `replayTickLimit < lin.n` guard and `validation.evaluate(yaws, lin)` call; `SlpSolve` at its analogous guard and phase1/final checks; `WindowCandidateValidation.evaluate` calls `JumpLinearModel.forwardContinuous` with the exact model's inertia threshold/per-axis flag, followed by full/prefix constraint checks.

## Evidence

- [Complete new trace](fixed-seven-forward-jump-cap1000-2026-10-23/solver-trace.txt), including every committed tick's position, velocity and yaw.
- [Every window attempt and elapsed time](fixed-seven-forward-jump-cap1000-2026-10-23/windows.csv), [every objective/SLP call and elapsed time](fixed-seven-forward-jump-cap1000-2026-10-23/objectives.csv). Includes both commit attempts and comparison sources, with trace line numbers.
- [Settings](fixed-seven-forward-jump-cap1000-2026-10-23/metadata.txt), [focused probe log](fixed-seven-forward-jump-cap1000-2026-10-23/focused-test.log).
- Prior cap100 same-policy trace: [seven forward jump](full-forward-shifts-2026-10-23/seven-forward-jump.txt). Successful reference: [normal shared validation](shared-inertia-validation-2026-10-23/normal7-commit1.txt).
