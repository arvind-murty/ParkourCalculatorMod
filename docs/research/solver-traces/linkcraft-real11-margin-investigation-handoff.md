# Linkcraft real/11 margin investigation handoff

**Current handoff (2026-10-24): [jump39 baselines and next investigation](jump39-baseline-handoff-2026-10-24/README.md).** The latest staged1000/10000 commit1 run commits38 jumps in93.032s and stops at state351. Both baseline entry states, all90 objective calls,175 closed-form inertia attempts and36 SLP starts are inventoried there, with source links and the exact input archive. Earlier30-jump normal-margin success below is historical context, not a solved route through39.

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


## Proposed objective/window preflight

The [whole-jump window versus motion design note](linkcraft-real11-window-objective-preflight-design.md) records the small-costate fallback, automatic matching-wall detection, and the concrete alternatives 1–6/end64, 1–7/end68, 1–8/end71. The user clarified whole-jump shifts as an alternative to terminal motion; a tick extension mixes position with one tick of movement rather than being a fundamentally separate strategy. Extending to 8 preserves existing constraints and removes the direct Z68 objective cap. This is a proposed next probe, not an implemented or tested policy. The note also identifies the existing scalar-MOTION evaluation inconsistency that a prototype must handle.


## Separate EPS2=1e-24 experiment

The [sharper-smoothing experiment](linkcraft-real11-eps24-experiment.md) changes EPS2 to 1e-24 independently of direct-bound detection. Above the 1e-9 recovery threshold, relative length shrink is bounded by about 0.00005%. Initial fixed Z/MAX still fails; its inertia retry now hits the arbitrary-direction fallback on all 68 controls and has a much larger violation. Fixed Z/MIN still passes, and the full fixed 7-ladder route still stalls at tick 90 before jump 14. Five focused tests pass; no full suite ran. The original fallback comment describes objective/wall cancellation, not a documented smoothing-error calibration.


## Latest inventory and seeded full-route result, 2026-10-24

Start with the [current inventory](full-route-seeded-inventory-2026-10-24/INVENTORY.md) and [full-route report](full-route-seeded-inventory-2026-10-24/README.md). Production now has validated intermediate retention (c392645) and assumed-model-feasible conditional inertia compensation (e14dfe4). The latter was previously test-only; it now reproduces direct j13/j14 successes. Decoder default remains unchanged. Full808-tick seven-ladder fixed .0003 run still misses: commit3 stuck before jump39/state351; commit1 before jump14/state90;47.09s; final route replay not reached. Every window, objective, dual rung and committed state/yaw is archived with source lines. Manual turnaround/window objective experiments are not automatically applied. The user j127 fixture modification is unrelated and was not staged.


## Latest full-route configuration: normal ten-jump ladder

User restored10,7,5,3,2,1, keeping fixed .0003 and current production seeding/retention. [Complete ten-ladder run](full-route-seeded-ten-ladder-2026-10-24/README.md): commit3 stuck before jump26/state205; commit1 before jump25/state196;161.386s; no final full-route replay reached. Only ladder changed from preceding seeded7 run. Source defaults already start10, so no production edit. All attempts and committed state/velocity/yaw data preserved; user j127 unchanged.


## Superseding objective policy: unconstrained airborne state

Current implementation3d4557e replaces endpoint-bound deletion. [Policy and checks](free-air-objective-2026-10-24/README.md), [current differences versus recorded production](free-air-objective-2026-10-24/comparison-to-prod.md). Scan forward from each nominal provisional end to first airborne tick with no original constraint referencing it (any axis/sense/mode, both row references). Extend lookahead and retain all rows; commit boundary unchanged. Terminal route objective stays authored. This is not an automatic turnaround-region detector. Sample88->91,99->104.20 focused checks pass; full route has not been rerun under this new rule. Older ten/seven full-run results used cap deletion and must not be attributed to this policy. User j127 unchanged.


## Latest full-route result with the free-air rule

[Current full rerun](full-route-free-air-ten-ladder-2026-10-24/README.md): normal ten ladder, fixed .0003, production conditional seeds/retention, all constraints retained, no deadline. No full route. Commit3 stuck before jump25/state196; commit1 before jump11/state77;74.190s; final full-route replay not reached. First nominal window extends77->91 and switches accepted objective to Z/MIN, changing early states. At state77 the3/2/1 rungs all share endpoint91/commit exit80 and cache the same miss. Every attempted solve, extension and committed state/yaw is preserved; no failure diagnosis yet. User j127 unchanged.


## 2026-10-24: commit1 early failure isolated at state77

[Detailed investigation and all per-iteration evidence](commit1-early-failure-2026-10-24/README.md). Production solver unchanged; one focused opt-in probe only. Reconstructing the committed game inputs reproduces the full run's initial120-iteration Z/min miss and70-iteration seeded retry at77..91. Initial assumed-model violation0, actual continuous miss.003152213371 from Z82 and X84..90 threshold checks. Seeding preserves costates to3.331e-16, but its subsequent path never becomes feasible. Five early costates enter the decoder fallback; terminal raw continuous miss.007339 becomes2.729172 after objective-direction substitution. The seeded retry stops NO_DESCENT, not the10000 cap.

Identical-entry cold retry, with intermediate retention still enabled, succeeds: production retains iteration152, whole continuous window violation0 and committed-prefix Minecraft replay violation0. Its terminal161 iterate is invalid, so retention is necessary. This demonstrates a valid continuation through91 from the current state77; it does not prove the longer horizons or full route feasible. Next supported experiment: retain seeded retry first, then a cold fixed-pattern retry on failure. Do not globally remove seeding, since earlier j13/j14 cases benefited. No such production fallback has been implemented yet.


## 2026-10-24: decoder fallback commented out and focused retry

[Full before/after evidence](commit1-no-decoder-fallback-2026-10-24/README.md). Per user request, objective-direction substitution below costate norm1e-9 is commented out in FacingPrefold.expand and JumpLinearModel.recoverAlongCostate. Ordinary atan2 recovery remains even at exactly zero. Same state77 Z/min@91 probe: seeded terminal miss drops2.729172 to.007339308, but production still rejects it. It obeys all assumed zeroing thresholds; remaining continuous violations are X79 lower by.001556768 and X89 lower by.007339308. Prefix replay misses.001565975 at79. NO_DESCENT still occurs at70; cap unchanged. Cold retry still succeeds via retention, now saving iteration158 instead of152, improving minimized final Z by about.00001496. Its terminal161 is still invalid. No cold production fallback added; no full route rerun or full suite. All probe data preserved separately.


## 2026-10-24: full commit1 rerun without decoder fallback

[Full report, all windows, objective types, timings, committed states and archived raw trace](full-route-commit1-no-decoder-2026-10-24/README.md). Same normal10,7,5,3,2,1 ladder and fixed.0003 margin, cap10000, EPS2=1e-24, conditional seeding and intermediate retention enabled. Decoder substitutions remain commented out; no cold fallback added. Commit1 only, starting from original route. Direct LongRun, no outer deadline.

Result: committed jumps1–38, stopped before jump39/state351 after317.605s. All6 horizons failed from351: end493/450/441/412/388/373, each trying four coordinate objectives and SLP. Proposed commit exit365 was not reached.97 attempted windows,391 objective/SLP calls,38 accepted commits:26 closed form and12 SLP. No final original-route replay reached. Previous fallback-enabled commit1 stopped before11/state77, but routes now diverge earlier and this is not an identical-entry control. Probe property pkc.fullRouteCommit=1 selects only1; its omitted default remains3,1. No full suite, user fixture untouched.


## 2026-10-24: full commit1 timing audit

[Every attempted window timing by starting jump, plus full accounting](full-route-commit1-no-decoder-2026-10-24/timing-breakdown.md). Existing trace only, no rerun.317.605s total: failed windows283.079s, successful windows including earlier failed objectives34.501s. Closed-form pattern/margin runs291.581s; SLP24.490s.438 of795 dual runs hit10000 iterations and consume249.300s, about78.5% of total.4,987,427 dual iterations total. Accepted objective/SLP calls alone sum3.574s;353 failed calls sum313.995s. The per-run cap multiplies across objectives, inertia patterns and failed window rungs. Retention auditing30.438s is included in closed-form time. No margin ladder sweep: fixed.0003. Instrumented wall time includes tracing and validation; do not treat it as an uninstrumented benchmark.


## 2026-10-24: iteration cap versus intermediate return

[Saved-trace audit](full-route-commit1-no-decoder-2026-10-24/iteration-cap-audit.md), no rerun. Jump39 used22.120s, not most of317.605s.433 of438 hit-cap runs never retained a valid candidate and cost248.404s. All26 CF runs that ever retained valid candidates cost1.063s total; early return only after feasibility would save little direct work on this trace. Existing retention already returns the best valid intermediate when preferred to the terminal candidate, but does not stop immediately. Snapshot truncation at100/500/1000/2000/5000/10000 retains a valid path in18/23/23/24/25/26 of the26 observed successful CF trajectories. First feasibility at starts9/11/36 occurs at3047/7194/1745. These are not predictions for a lower-cap pipeline, because pattern and warm-start changes alter trajectories. Quality can improve after first feasibility: start14 improves.028919 blocks between50 and2055, start36 improves.137547 blocks between1745 and1807. No claim of physical infeasibility for39 or new cap chosen.


## 2026-10-24: staged1000 then10000 budget experiment

[Full report and reproducible artifacts](full-route-commit1-staged-caps-2026-10-24/README.md). Implementation b84931e adds optional LongRunConfig.withDualIterationLadder; default single10000 remains. At each committed entry, exhaust windows10,7,5,3,2,1 with cap1000, then retry them with10000 only if all fail. Same entry on retry, cap-specific cache, optimizer restarts rather than resumes. Per-window objective order, SLP, fixed margin.0003, EPS2=1e-24, disabled decoder substitution, conditional seeding and intermediate retention unchanged.

Focused full commit1 run:93.032s versus317.605s,70.7% lower elapsed, same38 committed jumps then stop before39/state351. All38 accepted windows use1000. Only39 triggers10000; that adds19.738s after all1000 windows fail. No final original-route replay reached.106 windows,435 objective/SLP calls,848 dual runs,1,050,626 iterations versus4,987,427 baseline. Accepted27 CF and11 SLP. Generated paths differ; equal progress is not equal path quality or an identical-entry control. All attempts, budget stages, timings, committed states and compressed raw trace persisted. No full suite, user fixture untouched.


## 2026-10-24: preserved jump39 baseline handoff

[Read this before new probes](jump39-baseline-handoff-2026-10-24/README.md). Existing archived traces extracted without rerunning. Optional staged cap code b84931e and results8b9806b already committed. Per-window scheduling, objective order, fixed margin, acceptance, conditional seed eligibility, all absolute/relative zeroing patterns and stopping reasons are recorded for both distinct states351. At39 neither run ever has a seed-eligible CF source or retained valid candidate. SLP ordinary+aware attempts all fail phase1, with X356 showing about.688 violation. The predecessor accepted38-only window ends352 and does not include356. Exact focus-state reconstruction and reachability checks remain next work, not conclusions.

Scope correction: reported dual iteration/run totals are logged CF rungs; SLP constructs its own margin0 dual seed with default10000 cap, even in a1000 CF stage. Its time is included in the SLP objective call, before the SLP-start timer. Source and trace citations are in the new handoff.


## 2026-10-24: compiled constraints and windows for jumps30–50

[Authoritative exported tables](jump30-50-route-map-2026-10-24/README.md). Rebuilt exact archived input through current engine; exported all636 compiled constraints, jump boundaries and per-tick input physics, no optimization rerun. Current jumpBoundaries returns76 segments, correcting earlier prose count74;30–50 ranges match the actual full-run labels. Initial export assertion caught the stale count and the corrected focused export passes; both records preserved. At starts37/38 accepted horizons319..352 and333..352 omit the first39 wall at356. Entry351 X702.302172 requires X356<=700.425, a negative-X displacement1.877172 in five inputs. No reachability conclusion yet. Boundary-position rows are displayed with the jump ending at that state, while input ranges are half-open. Exact float walls/duplicates preserved in CSV.


## 2026-10-24: why20–22 misses while20–21 succeeds

[Identical-entry focused probe and all validation margins](jump20-window-comparison-2026-10-24/README.md). Exact reconstructed entry132: X733.2993911151426,Z-1095.0468786224278,VX.00005327436714,VZ-.3022178746736,raw yaw258.950653076deg. Both commit23 inputs through155, margin.0003, cap1000.20–22 horizon178 has38 rows;20–21 horizon169 has31 and a different endpoint objective. All20–22 CF objectives miss, best continuous.226800589 from Z/min; no eligible assumed-model seeds. Ordinary centered SLP passes replay phase1 (worst signed slack-.001755934) but final actual continuous check fails.047522722. The only actual/game zeroing disagreement is Z134: continuous-.00499406563 zeroes, replay-.00500667357 survives. Continuous Z135 upper fails.000643779 inside committed prefix; Z160 upper fails.047522722 provisionally. Both game rows pass. Phase1 passed, so the existing phase1-failure-only inertia-aware retry is never invoked after this final continuous-only rejection. No claim of20–22 infeasibility.

20–21 Z/min retains valid unpatterned iteration126, continues to252 recovered-input stall, selects retained126, reports continuous0/replay0; SLP not reached. Candidate-specific threshold mismatch is a concrete cause of the longer SLP rejection; the added future rows are not themselves the violated rows. No production change or new recovery control in this probe.


## 2026-10-24: SLP replay-only validation restored

[Implementation and focused before/after evidence](jump20-slp-replay-validation-2026-10-24/README.md). Per user instruction, SLP phase1/final validation now matches recorded upstream02844f4: full-window Minecraft replay, no prefix bypass or continuous veto. Removed SLP replayTickLimit and LongRun assignment; kept shared helper on closed form/retention. Diagnostic logging and previously changed shared seed numerics remain. Exact state132 focused comparison now accepts20–22 via SLP (full-window replay0, continuous audit.047522722) and20–21 via retained CF Z/min126 (both0). No full route or full suite. Existing full-route93s baseline is historical under the prior SLP continuous veto; new acceptance can change earlier commits and later states. Both obsolete test references to removed SLP config were adapted to compile.


## 2026-10-24: isolated20–22 closed-form Z/max

[Focused cap/iteration/inertia audit](jump20-22-zmax-2026-10-24/README.md). Same exact state132, endpoint178, margin.0003, prefix23, no production change. Caps1000/10000/100000 all miss; unpatterned stops1000(cap),10000(cap),20819(no descent), continuous misses.490219/.467372/.592446. Unpatterned1000 already violates its own fixed model by.500630 at X154 upper, with actual/game zeroing outcomes matching. First costate at132 is1.588e-13; internal smoothed-gradient control radius is.15686 of physical magnitude. Internal X154 violation.110618 becomes.500630 after full-magnitude angle decoding, while projected residual.149509 remains unconverged. Curvature weight of first tick~1.284e11 vs max31.723 among other ticks, a conditioning signal, not a proven Hessian condition number or duality gap. Raising cap fails; pinning first1/2 SLP facings diagnostically reduces initial continuous misses to.010243/.001602 but neither returns feasible. Cold inertia retry at1000 assumes X132/149..177 and Z145; actual proposed Z145 momentum-.07319 violates its zeroing band. No seed source eligible and no retained valid path. Full diagnostic files and controls persisted; no algorithm fix yet.


## 2026-10-24: why the tick132 costate cancels

[Exact signed objective/wall decomposition](jump20-22-zmax-2026-10-24/costate132-explanation.md). Offline reconstruction matches saved gX132=1.297364993e-13,gZ132=9.162115511e-14 bit-for-bit, no solve rerun. Objective Z178 contributes+4.511200743; upper Z160 contributes-4.560697564 (lambda1.016307627), lower Z136 contributes+.148065279; upper Z133/134/135 subtract the remainder. For X, lower X136 contributes+.054150091, opposed by upper X134 -.045055562, X176 -.006197155 and X154/following walls. First input has99.5% of its final-position coupling already present at Z160. Tiny costate is cancellation of priced sensitivities, not physical insignificance. Tick133 costate norm.01032059 is not tiny because it sees different coefficients and cannot affect state133. This does not certify a dual optimum/gap; residual remains.1495.
