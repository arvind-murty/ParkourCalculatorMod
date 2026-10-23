# Linkcraft real/11 margin investigation handoff

Date: 2026-10-23  
Branch: `codex/first30-margin-trace-bookkeeping`  
Historical baseline: `f980e29` (`experiment(solver): record normal-margin 7-window trace`)

**Verified and corrected after model switch:** read [the verified comparison](linkcraft-real11-normal-vs-fixed-verified.md). It contains the identical-entry controls, held-pattern warm-start experiment, complete corrected window table and carried-yaw table. The earlier version of this handoff is preserved in commit `2a9b4a8`.

## Question under investigation

For the first 30 jumps of `~/Downloads/LinkcraftII_Competition_01/real/11.json`, compare LongRunSolver with its normal robust-margin ladder against a run fixed at margin `0.0003`. Both runs use `commit=1` and window ladder `7,5,3,2,1`. The practical question is why one run continues while the other fails, especially around zero-based start indices 11–13 (one-based jumps 12–14).

## Results established by the saved traces

- The normal-margin run solved all 30 jumps in about 7.1 seconds. Its report and objective-by-objective CSV are `linkcraft-real11-first30-normal7-commit1.md` and `linkcraft-real11-first30-normal7-commit1-objectives.csv` in this directory.
- The fixed-`0.0003` run committed through zero-based jump index 12, then failed at index 13 after the 7-, 5-, 3-, 2-, and 1-jump windows all missed. Its historical raw log is now preserved as [linkcraft-real11-first30-fixed0003-commit1.txt](linkcraft-real11-first30-fixed0003-commit1.txt).
- My previous chat table incorrectly said the normal run solved at index 9 with `Z/MIN` at margin 0.0. The CSV corrects this: window `[9,16)` solved with `Z/MAX`, using inertia pattern `x@9-15+z@7`, at margin `0.0012`. That successful pattern did not try `0.0003`, because it returned immediately at the certified `0.0012` rung. The `0.01739` violation at `0.0003` was in the earlier unpatterned pass. Source: the normal objective CSV row for window `[9,16)`, objective `primary Z/MAX@42`.
- At zero-based index 11, the normal run solved window `[11,16)` (five jumps) with `Z/MIN`; inertia pattern `x@4-10+z@2` certified at `0.0012`. The fixed run attempted `[11,16)` at `0.0003`; `Z/MAX`, `Z/MIN`, `X/MAX`, and `X/MIN` all missed. It then solved the smaller `[11,13)` two-jump window with `Z/MIN` at `0.0003`.
- These were **not the same five-jump optimization problem**. The normal run entered index 11 at tick 80 with `(x,z;vx,vz)=(732.813498656565,-1088.466968184592; 0.102282870380,0.270883459243)`. The fixed run entered with `(732.819704258099,-1088.639926044153; 0.108648592033,0.270482712973)`. Earlier commits had already changed the state. Therefore the original full-run comparison did not isolate a margin-only cause; the new controlled runs do (see the verified comparison).
- At index 12, the normal run solved `[12,19)` via `X/MIN` at `0.0003`; fixed margin tried that window and the smaller 5-, 3-, and 2-jump windows unsuccessfully, then solved the 1-jump `[12,13)` window. At index 13, fixed margin failed every ladder window. Normal margin solved `[13,18)` through fallback SLP after all four closed-form objectives missed.

## Corrected committed boundary states

The original table used each earlier window's uncommitted preview yaw at the boundary. The correct carried yaw is the previous input tick's facing (`wgf[commitTicks-1]`), which seeds the next window. See the [verified comparison](linkcraft-real11-normal-vs-fixed-verified.md) for the corrected table; [the exact-state CSV](margin-comparison-2026-10-23/margin-comparison-states.csv) retains full precision. These are jump-segment boundary states, not established as one tick before landing.

## Proposed experiment (now completed): isolate margin from entry state

**Outcome:** from the normal tick-80 entry, the normal ladder succeeds and fixed 0.0003 misses. From the fixed tick-80 entry, both policies miss. With normal entry and the successful inertia pattern fixed, cold 0.0003 misses by 0.053617, while the normal warm ladder continued to 0.0003 is feasible. Details and source traces are in the verified comparison linked above. The following list records the original plan, which was carried out; do not treat it as remaining work.

1. Preserve the fixed-margin raw trace before any build cleanup. It is currently an ignored `core/build` artifact.
2. Reproduce the exact normal entry state at tick 80, including position, velocity, yaw, inputs, constraints, and all solver settings. Run the same `[11,16)` window twice from this identical state: once with the normal margin ladder and once with only `0.0003`.
3. Capture every objective, closed-form pattern, rung, replay violation, fallback SLP status, and candidate path. Confirm that changing margin is the only experimental variable.
4. If the two runs differ, inspect how the margin changes candidate generation/certification and whether a lower margin changes solver numerics or pattern discovery. Do not infer that from the current full-run comparison, because the start states differ.
5. Separately, run the reverse control from the fixed-margin tick-80 entry state under both margin settings. This tests whether the outcome is primarily entry-state-dependent.
6. Have the next investigator independently verify the trace-to-table extraction and the meaning of each reported tick before treating the table as “one tick before landing.” These rows are specifically the last logged committed states for each jump segment.

## Sources and bookkeeping

- Normal report and all-window attempt table: `docs/research/solver-traces/linkcraft-real11-first30-normal7-commit1.md`.
- Normal objective/rung data: `docs/research/solver-traces/linkcraft-real11-first30-normal7-commit1-objectives.csv`.
- Normal raw path/solver trace: `docs/research/solver-traces/linkcraft-real11-first30-normal7-commit1.txt`; key locations include tick-80 entry at line 647 and the `[11,16)` objective at lines 609 onward.
- Fixed raw trace: `docs/research/solver-traces/linkcraft-real11-first30-fixed0003-commit1.txt`; key locations include `[11,16)` attempt at lines 1410–1600, `[11,13)` success at lines 1627–1662, `[12,19)` at lines 1672–1879, and terminal index-13 ladder at lines 2490–2908.
- Reproducer: `core/src/test/java/de/legoshi/parkourcalc/anglesolver/First30NormalMarginTraceTest.java`; its default TAS path is the Linkcraft real/11 JSON and it supports `PKC_TRACE_INPUT`.
- Branch commit `5b632de` contains the temporary fixed-`0.0003` change; `79bb653` reverts it. Baseline commit `f980e29` has the normal margin ladder and normal trace; the comparison work leaves that production default intact. Use `git show 5b632de:<path>` to inspect the temporary override without changing branches.
- Current branch: `codex/first30-margin-trace-bookkeeping`. Earlier solver logs, tests, and experiment changes were separated into commits; see `git log --oneline`.


## Initial seven-jump constraint audit

The [initial-window audit](linkcraft-real11-initial-seven-constraint-audit.md) now lists every original constraint for all retained candidates and every inertia pass. Z/MIN and X/MAX have small replay-only misses; Z/MAX and X/MIN already violate the continuous constraints. The normal 0.05 candidate has minimum actual replay clearance about 0.03509 blocks. This experiment runs only ticks 0–68.


## Committed-prefix validation experiment

A subsequent [experiment](linkcraft-real11-prefix-replay-experiment.md) accepts nonterminal LongRun windows based on replay of the potentially committed prefix while keeping every lookahead constraint in the optimization. The initial Z/MIN near miss is accepted through tick 12. The normal-margin full route now stalls at index 12; the original route-completion assertion is retained and this failure is recorded explicitly.


## Prefix-regression follow-up and current test policy

Read the [same-entry prefix regression comparison](linkcraft-real11-prefix-regression-comparison.md) for the first divergent candidate, all window attempts through failure, committed-state table, timing records, and focused probe evidence. Full-window replay had also been filtering candidates that violate the continuous lookahead model; prefix-only replay accepts one such candidate at index 3. Keep full continuous-model feasibility distinct from replay scope in the next experiment. No change imposing full continuous feasibility has been implemented yet.

The user requests focused tests/probes only, backgrounded with output redirected to files. Do not run the full suite or pursue unrelated failures to make everything green. The earlier full suite has finished and is not being rerun.


## Subsequent full-continuous feasibility check

The [full continuous-window validation experiment](linkcraft-real11-prefix-continuous-validation.md) implements the requested combined policy and records focused results. It prevents the first bad margin-0.05 acceptance while preserving the initial Z/MIN prefix acceptance. The full 30-jump route still fails, now at index 13. Earlier descriptions of prefix-only acceptance in this document describe the prior experiment.


## Superseding shared validation

The [shared inertia-validation experiment](linkcraft-real11-shared-inertia-validation.md) now reuses the original continuous `zeroingPattern` recurrence to record positions/velocities and validate actual threshold behavior throughout the full window. It replaces acceptance under an assumed fixed pattern. All ten focused tests pass, including full completion of the 30-jump route. Earlier fixed-pattern results remain historical evidence.


## Full-route fixed-margin 10-ladder probe

The [full-route experiment](linkcraft-real11-full-fixed0003-ten-ladder.md) runs the original 808-tick endpoint with standard windows 10,7,5,3,2,1, commit retries 3 then 1, fixed closed-form margin 0.0003, and shared actual-inertia validation. It returns no solution: both commit passes stall at tick 90 before jump 14. Total trace time 3829.48 ms; no outer deadline. The report preserves every window attempt and objective timing.


## Fixed-margin 7-ladder control

The [full-route 7-ladder control](linkcraft-real11-full-fixed0003-seven-ladder.md) changes only the window ladder to 7,5,3,2,1. Both commit=3 and commit=1 still stall at tick 90 before jump 14. Total logged time 1512.21 ms; full objective/window tables and raw trace are preserved. This is distinct from the successful normal-margin 7-ladder run.


## Initial-window shared-helper validation audit

The [same-input 1–7 audit](linkcraft-real11-initial-seven-shared-validation-audit.md) compares fixed Z/MAX (two proposals), normal 0.05 Z/MAX, and the accepted fixed Z/MIN. It lists every continuous constraint margin, committed replay margin, assumed/actual inertia velocity and full per-tick paths. Fixed Z/MAX first misses the continuous Z68 upper wall by 0.01166335; its inertia retry obeys its explicit zeroing band but has larger positional violations. Margin 0.05 produces a feasible but unconverged witness for the smaller-margin problem. Shared diagnostics now apply to both closed form and SLP; ten focused tests pass.


## Larger dual iteration caps and recovery discrepancy

The [iteration-cap/convergence audit](linkcraft-real11-initial-seven-dual-iteration-convergence.md) tests fixed 0.0003 caps 100 through 20000. More iterations do not fix Z/MAX: at cap >=200 the first pass stops at 125 on no descent, and the retry at 83 on recovered-input stall. The returned smoothed dual derivative uses shorter-than-required movement vectors; restoring full magnitude in angle recovery adds 0.07244946 to final Z and causes the measured violation. Per-iteration curves, per-tick recovery vectors, constraint margins and timings are preserved. Seven focused tests pass; default cap stays 100. The older `pg` log at a cap is one iterate behind the returned state; the new `finalPg` reports the returned-state residual.


## EPS2 provenance and outside-square-root proposal

The [gh/blame investigation](linkcraft-real11-norm-smoothing-history.md) traces EPS2=1e-14 to the original closed-form commit inside PR #122. That PR has no description/comments; its original commit and patch document tiny norm smoothing with modest accuracy plus fallback, but no justification for the specific constant. Moving it outside the objective sqrt adds only a constant and does not smooth the kink. Changing only the gradient denominator would make derivatives inconsistent. A smaller EPS2 is a separate probe and interacts with conditioning, degenerate recovery and the 1e-9 zero-costate yaw fallback. No epsilon experiment was run.
