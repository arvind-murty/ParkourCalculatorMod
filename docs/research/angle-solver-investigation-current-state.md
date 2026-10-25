# Angle-solver investigation: current state and handoff

**Status: shelved at the user's request,2026-10-24.** No further solver experiments are scheduled. Read this before resuming; older notes describe several superseded policies.

Branch: `codex/first30-margin-trace-bookkeeping`.
Recorded production baseline: local `main` / recorded upstream/main **02844f45872604b21d0d139736d3ae0163454156**. No upstream fetch was performed for this handoff. Latest production implementation change: **2a218a0**; later commits contain focused probes, research and documentation.

## 1. Scope and input

We are investigating Minecraft1.8, per-axis inertia threshold checks, fixed-start receding-horizon angle optimization on Linkcraft real/11. We care about deriving a feasible, useful continuous/physical path through long routes, not byte-angle conversion for its own sake. Use **incoming momentum** for the quantity tested against.005; it is not displacement produced by that tick.

Input: `/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json`, archived exactly at [real11-input.json.gz](solver-traces/jump39-baseline-handoff-2026-10-24/real11-input.json.gz). SHA256 `af5d2d1bc6e0ab466f1b6cf309f3a26e76e4a63765ac8b15ff02fb4ad55d28bf`.

810 rows,808 modeled ticks,636 compiled original scalar constraints, final X/MAX@808. **Current exported jumpBoundaries returns76 segments**, correcting earlier prose saying74. Jump numbers are solver segments, not necessarily a human block-to-block count. Tick numbers are0-based pre-input states. State351 is before input351; jump39 uses inputs351..364 and ends at365.

The task has not produced a complete808-tick solution. The older normal-margin success was a30-jump truncated solve, not a complete-map ground truth.

## 2. What we discovered

### A. Larger windows can fail while smaller ones continue

The current recorded staged full-run baseline tries nominal windows10,7,5,3,2,1 and commits1. Near the stop, accepted windows retreat behind the next difficult segment. From start28 the10-jump window28–37 already fails (actual endpoint338), so not every large-window failure is caused by the future X356 constraint.

At start37 it accepts37–38 over319..352, commits through333. At38 it accepts38 alone over333..352, commits through351. These windows do NOT include the first39 constraint at356. Thus passing their full included constraints does not establish a feasible continuation through356.

At staged entry351: X702.302172074316,Z-1111.856715620752,VX0,VZ-.269432741874,raw yaw-135.000015259deg. X356 must be<=700.4250000119209, requiring~1.877172 negative-X displacement in5 inputs. SLP repeatedly reports its worst row at356, about.688 violation. We have NOT yet done the exact reachability/focused-state investigation needed to classify this as an impossible committed entry versus a solver failure.

Sources: [jump39 baseline handoff](solver-traces/jump39-baseline-handoff-2026-10-24/README.md), [compiled30–50 map and lead-up windows](solver-traces/jump30-50-route-map-2026-10-24/README.md), [all-jumps authored constraint table](linkcraft-real11-all-jumps-input-constraints.md).

### B. Most full-run time was unsuccessful larger-window work

The single10000-cap commit1 baseline took317.605s and stopped before39/state351. Only22.120s was spent on39; preceding failed windows consumed most of the time.

433 logged CF dual runs hit10000 without a retained valid candidate, costing248.404s. All26 logged CF runs that did retain a valid candidate cost only1.063s combined. A cap1000 truncates some formerly successful trajectories before first feasibility: examples start9 at3047 iterations and start11 at7194. This motivated a staged budget, not simply deleting the larger retry.

Logged CF iteration totals exclude SLP's separate dual seed. Its default cap is10000 even in the surrounding1000 CF stage, and its time is included in the SLP objective-call timing.

Sources: [time accounting](solver-traces/full-route-commit1-no-decoder-2026-10-24/timing-breakdown.md), [iteration/first-feasibility audit](solver-traces/full-route-commit1-no-decoder-2026-10-24/iteration-cap-audit.md).

### C. Intermediate retention and conditional inertia seeding both matter

A dual solve can visit a valid physical candidate, then end with invalid or worse angles. We now retain the best validated intermediate candidate and its OWN multipliers; it can be returned after the ordinary stop. This helped earlier j13/j14 focused problems and the cold retry of the old state77 window.

When a previously assumed fixed-model candidate is feasible, its multipliers can seed a new inferred inertia model. New opposing velocity-band row multipliers compensate for the changed position coefficients, approximately preserving costates and angles. A strict relative preservation guard previously rejected harmless~3e-16 reconstruction error at tiny costates; production records the error without that veto. The source still MUST satisfy its assumed model and assumed-zero velocity bands within1e-8. We do not seed large assumed-model misses.

At39 and at20–22 Z/max, none of the measured failed sources is seed-eligible. Seeding is configured on but never applied there. Do not explain those failures as a malfunctioning seeded retry.

Sources: [state77 seeded versus cold control](solver-traces/commit1-early-failure-2026-10-24/README.md), [implementation](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/InertiaRetrySeed.java), earlier [j14 assumed-model-feasible seed experiment](solver-traces/j14-model-feasible-seed-2026-10-24/README.md).

### D. The decoder's tiny-costate fallback caused drastic angle changes

Below costate norm1e-9, FacingPrefold/JumpLinearModel replaced the recovered direction with the raw objective direction. At the old state77 example, this changed angles by~173deg and inflated a~.00734 raw miss to2.729 blocks. We commented out that substitution in BOTH recovery sites. Removing it reduced the errors but did not by itself solve the seeded retry. Ordinary atan2 remains, including at exact zero.

Source: [before/after probe](solver-traces/commit1-no-decoder-fallback-2026-10-24/README.md), implementation **c7f38d9**.

### E. SLP already optimizes acceptance against Minecraft replay

SLP's local derivatives may be unpatterned or inertia-aware, but EVERY candidate is mapped to game facings, simulated with ExactJumpModel, and scored against the window constraints. Phase1 may accept an infeasible candidate if it improves replay violation; final return checks feasibility.

Our extra continuous veto rejected a genuinely replay-valid20–22 SLP candidate. At Z134, incoming momentum is-.00500667357 in replay (survives) versus-.00499406563 continuously (zeroes). The continuous path violates Z135 by.000643779 and Z160 by.047522722, while the full-window replay has~.001756 minimum clearance. Phase1 had passed, so its phase1-failure-only inertia-aware retry was never invoked after the final continuous rejection.

**We restored SLP's full-window replay-only acceptance to upstream baseline.** Closed-form's shared helper remains. Focused20–22 now passes via SLP;20–21 still passes via retained closed-form Z/min126. Do not treat this continuous disagreement as a game failure or a second continuous optimization attempt.

Sources: [before](solver-traces/jump20-window-comparison-2026-10-24/README.md), [restored validation and returned candidates](solver-traces/jump20-slp-replay-validation-2026-10-24/README.md), implementation **2a218a0**.

### F. Closed-form20–22 Z/max has a separate weak-costate/convergence failure

Same exact entry132, objective Z/MAX@178,46 inputs,38 rows, prefix23. Initial unpatterned solve already misses its own fixed model by.500630 at X154 after1000 iterations. Actual continuous/game misses are both~.4902, with identical threshold outcomes. This is not the SLP rounding-triggered threshold flip.

Caps1000/10000/100000 all fail; the last stops at20819 with NO_DESCENT. No valid intermediate is retained. Tick132's costate norm is1.588e-13. With EPS2=1e-24, the smoothed gradient uses only15.686% of that tick's prescribed acceleration:.020392 versus physical.130000. Decoding a full-magnitude angle worsens X154 residual from.110618 for derivative-implied controls to.500630. The derivative controls are ALSO infeasible; the projected residual is still.1495. This is not a feasible converged dual spoiled only by decoding.

The exact costate decomposition is cancellation: objective Z178 contributes+4.511200743; upper Z160 contributes-4.560697564; lower Z136 and early upper-Z walls balance the remainder. X contributions likewise cancel. First input132 has99.5% of its final-position coupling already present at160. This does not make132 physically irrelevant. Neighbor133 has costate norm.01032, so not every early tick is weak.

The weak tick's curvature weight~1.28e11 versus at most31.72 among others indicates conditioning trouble. The implementation uses Levenberg-damped Cholesky; the class-level truncated-eigen description is stale. Actual step/globalization diagnostics have not yet isolated a unique cause. Pinning the first1/2 SLP facings reduces initial continuous miss to.01024/.00160 but neither control solves.

Sources: [full Z/max controls](solver-traces/jump20-22-zmax-2026-10-24/README.md), [exact cancellation](solver-traces/jump20-22-zmax-2026-10-24/costate132-explanation.md).

## 3. What changed, and what is active now

These are cumulative investigation changes versus recorded production, not a claim that all are production-ready.

| Area |Current code |Default versus probe |
|---|---|---|
|Dual smoothing |EPS2=1e-24 instead of1e-14 |Changed shared default |
|Dual iteration cap |10000 instead of100; caller-configurable CF cap |Changed shared default; SLP seed also uses10000 |
|Tiny-costate decoder fallback |Commented out in FacingPrefold and JumpLinearModel |Changed shared behavior |
|CF intermediate retention |Best valid intermediate plus its matching multipliers |Enabled by default |
|CF inertia multiplier seeding |Only assumed-model-feasible source, coefficient-change compensation |Enabled by default |
|CF LongRun validation |Shared helper: continuous all included rows, game replay committed prefix |LongRun supplies prefix; no acceptance weakening of original full-route replay |
|SLP validation |Restored upstream full-window replay-only phase1/final rules |Active since2a218a0; no shared continuous veto |
|Provisional objective location |Advance nominal end to first unconstrained airborne state |Active; preserves all included original constraints |
|Iteration-budget ladder |Optional1000 across all windows, then10000 only if stuck; cache includes cap |Opt-in; default single10000 preserved |
|Commit ladder |Run uses1 only |Probe opt-in; default remains3,1 |
|Margin |Run uses fixed.0003 |Probe opt-in; normal robust margin ladder remains default |
|Diagnostics |Per-objective/window time, inertia masks, stopping reasons, shared per-row audits and focused probes |Logging/probe additions |

Earlier endpoint-cap deletion and direction-sensitive extension experiments are superseded. Current code does not remove those constraints. No automatic turnaround/coupled-region detector, per-window B&B integration, general seeded-failure cold fallback, or new nonsmooth dual solver has been implemented. ObjectiveBounds remains as diagnostic/helper code; its presence is not evidence that constraints are still deleted.

SLP's validation matches baseline, but its entire numerical behavior is NOT identical to production: shared dual cap/epsilon and decoder changes remain. Its LP/trust-region and ordinary/inertia-aware algorithms were not rewritten.

Sources: implementation commits **e14dfe4,c392645,3d4557e,c7f38d9,b84931e,2a218a0**, current source hashes and policy in the linked run metadata. To inspect cumulative changes: `git diff 02844f4 -- core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver`.

## 4. What we accomplished, with measured limits

| Milestone |Measured outcome |Limit |
|---|---|---|
|Free-air endpoint, decoder enabled |Commit1 stopped before11/state77 |Historical, superseded decoder policy |
|Decoder disabled, single10000 |Committed38, stopped before39/state351,317.605s |SLP still had continuous veto |
|Staged1000 then10000 |Committed38, same stop,93.032s |Different generated path, SLP still had continuous veto |
|Timing reduction |70.7% lower elapsed,3.41x baseline/staged ratio |Policy comparison, not identical-entry microbenchmark |
|Focused SLP validation restoration |20–22 now accepts, full-window game violation0;20–21 still accepts |No full-route rerun afterward |
|Z/max diagnosis |Measured cancellation, control-radius mismatch, failed high-cap/pin controls |No CF repair implemented or proven |

**The93-second run is the last full-route baseline, NOT a run of the latest SLP validation policy.** Do not say the latest working tree still fails39 without rerunning or using an identical-prefix focused probe. Its earlier commits can now change.

Full staged data:106 windows,435 objective/SLP calls,848 logged CF rungs,1,050,626 logged CF iterations; every accepted1..38 continuation used1000, only39 invoked10000. Accepted27 CF windows and11 SLP. Final full-route validation was never reached. Prior single-cap recorded4,987,427 CF iterations. SLP's internal seed iterations are outside those counters.

Sources: [single-cap baseline](solver-traces/full-route-commit1-no-decoder-2026-10-24/README.md), [staged baseline](solver-traces/full-route-commit1-staged-caps-2026-10-24/README.md), [per-start timing](solver-traces/full-route-commit1-staged-caps-2026-10-24/timing-comparison.md), [starts20–30 solve table](solver-traces/full-route-commit1-staged-caps-2026-10-24/windows-and-solves-20-30.md).

## 5. Shelved costate ideas: research, not changes

See [options and primary sources](costate-cancellation-recovery-options.md), committed **5d8dde8**.

- Exact unsmoothed norm gives full-magnitude response away from zero. A consistent experiment must change value, derivative, Hessian/step handling and stopping rules together. Editing onlygrad is inconsistent with the existing smoothed line-search objective/Hessian.
- At zero, physical directions are not uniquely determined; use nonsmooth handling and/or coupled primal recovery. More precision alone cannot resolve exact cancellation.
- Smaller smoothing/continuation and better scaling/damping are numerical leads, not proven fixes.
- Forcing norm(g)>=delta changes the dual problem and can exclude its optimum. Objective bias/secondary reference directions change the objective and need explicit accounting.
- Recover weak controls and neighbors using original constraints and full magnitudes; existing SlpSolve.optimizeLocked and relaxation/replay methods are reuse leads. Its locked call is best-effort and requires explicit validation.
- Circle and disk share the same support function, so the dual alone does not guarantee a full-radius feasible recovery. The class comment asserting automatic zero gap is too strong in general. Our actual20–22 iterate is unconverged and does not prove a duality gap.

No exact-norm prototype, epsilon continuation, new damping strategy, or restricted weak-control recovery has been implemented or tested. Keep this line of work shelved unless the user resumes it.

## 6. Next steps when resumed

### First: generate a current full-route baseline

Rerun the full input under **restored SLP replay acceptance**, keeping staged1000,10000, commit1, fixed.0003, all other current settings. Use a NEW label/output directory. Persist every attempted window/objective, selected method, CF pattern/stop/seed outcome, actual objective endpoint, committed state and yaw, and per-call timings. Final original-route replay remains mandatory if a route is returned.

This will determine whether39 is still the first stop. It is the highest-priority new measurement because the latest SLP change can accept earlier windows that previously failed.

### Then: investigate the actual stopping entry

If it still stops at39, reconstruct the exact new state351 from saved committed game inputs0..350. Avoid rebuilding a supposedly exact seed from rounded12-decimal trace values. Preserve raw float yaw and exact Y/VY. Start with351..373,35 rows, prefix14; export all compiled constraints and per-tick input/physics masks.

Test reachability of X356 from that state, including the actual jump boost, annotated ground/air, and acceleration directions. Compare a preceding window that includes356 against one ending352 from an IDENTICAL entry at333. Separate a bad committed state from local numerical failure. Do not infer physical impossibility from repeated solver misses or compare different generated prefixes as controls.

If the first stop moves, isolate THAT entry before repeating old39 controls.

### Separately, only if costate work resumes

Use the existing132..178 Z/max fixture. First instrument actual Newton/gradient step decisions, damping/free-set/Hessian/line-search residuals around weak132. Then compare consistent exact-norm/nonsmooth handling, smoothing continuation/scaling, and constrained primal recovery separately. Keep all original rows and physical validation, and report feasible quality/time as well as dual residual. Existing SLP replay solution is a game-feasible witness, not proof that a particular unpatterned continuous fixed model is feasible or globally optimal.

No full suite is required for exploratory probes unless requested. The standing user instruction is focused tests/probes, asynchronously redirected logs; misses may remain experimental outcomes.

## 7. Recovery and reproduction

Important artifacts:

- [Jump39 complete baseline handoff](solver-traces/jump39-baseline-handoff-2026-10-24/README.md): both old states,90 objective calls,175 CF attempts,36 SLP starts, masks and stop reasons.
- [All authored constraints, every jump](linkcraft-real11-all-jumps-input-constraints.md): standalone user-requested table.
- [Compiled636 constraints and physics masks](solver-traces/jump30-50-route-map-2026-10-24/README.md): full unrounded CSVs plus30–50 readable map.
- [Focused20–22 restored-SLP proof](solver-traces/jump20-slp-replay-validation-2026-10-24/README.md).
- [Focused20–22 Z/max numerical controls](solver-traces/jump20-22-zmax-2026-10-24/README.md).
- [Historical comprehensive inventory/recovery snapshot](solver-traces/full-route-seeded-inventory-2026-10-24/README.md): older inventory, not current acceptance policy.
- [Older cumulative investigation handoff](solver-traces/linkcraft-real11-margin-investigation-handoff.md): chronological history, superseded in places; use this current-state note first.

The isolated runner is `/tmp/pkc-endpoints-fixed`, using a core-only setup. Its detached commit does not identify the copied production sources. Before reuse, compare/copy current tracked source files and probe classes; source hashes are recorded in run metadata. JDK21 is `/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home`. The canonical implementation is this branch, not the temporary runner.

Full-route focused command, from that source-matched runner:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true -Dpkc.fullRouteCommit=1 -Dpkc.fullRouteDualCaps=1000,10000 -Dpkc.fullRouteLabel=-NEW-LABEL :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginTenLadder > /path/to/NEW-RUN/full-route.log 2>&1
```

Launch asynchronously and keep output redirected. SolverTrace writes to runner core/build/reports with the supplied label. Set PKC_TRACE_INPUT to an uncompressed copy of the archived exact input if the Downloads original is unavailable/changed. The probe's JUnit success alone does NOT mean the route solved; inspect RESULT and final replay validation.

Focused numerical probe property: `pkc.jump20ZMaxProbe=true`, class `Jump20ZMaxProbeTest`. Focused restored-validation comparison: `pkc.jump20ComparisonProbe=true`, class `Jump20WindowComparisonProbeTest`; use `pkc.jump20Output` for a new artifact directory. Full details and their assumptions are in each report.

### Git / workspace safety

Implementation and evidence are committed separately throughout; no revert is needed to resume. Compare current source to recorded prod, or checkout historical code in an isolated worktree to reproduce old policies. Do not overwrite historical artifacts with reruns.

**Uncommitted user change:** `core/src/test/resources/captures/hpk/d9/j127_Head_to_Head_Neo.json`. It predates this work, is unrelated and remains untouched. Do not stage, reset or restore it.

This handoff task runs no tests or solver probes and changes no implementation. Work is shelved after committing this record.
