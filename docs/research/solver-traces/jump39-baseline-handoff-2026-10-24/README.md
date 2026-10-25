# Jump39 investigation handoff: preserved baselines

2026-10-24. Start here before investigating the failure. This document inventories existing evidence; no new solve or solver behavior change was made for this handoff. Current implementation commit **b84931e**, staged full-run artifact commit **8b9806b**. The records establish solver failure from two generated states, not proof of physical infeasibility.

## 1. Which baseline is which

| Reference |Policy |Result |Solve time |Artifact |
|---|---|---|---:|---|
|Current investigation baseline |Commit1, cap1000 across all windows; retry10000 only when stuck |Commits1–38, stops before39/state351 |93.031823s |[Staged report](../full-route-commit1-staged-caps-2026-10-24/README.md) |
|Previous performance/control baseline |Commit1, single10000 cap per dual run |Commits1–38, stops before39/state351 |317.605258s |[Single-cap report](../full-route-commit1-no-decoder-2026-10-24/README.md) |
|Older successful normal-margin reference |Normal margins,7-window ladder,30-jump truncated route |Completed30 jumps |About7.1s |[Original handoff](../linkcraft-real11-margin-investigation-handoff.md) |

The30-jump reference does not include39 and is not a successful full808-tick solution. The current baseline is staged, not the previous cap-removal policy or the old state55/77 focused probes. Keep those distinctions explicit.

The staged run is70.7% shorter,3.41x baseline/staged elapsed ratio. Neither reaches final original-route replay validation. The runs generate different prefixes, so comparing their39 failures is not an identical-entry control.

## 2. Original input and reproducibility

Original real/11.json:810 rows,808 modeled ticks,636 compiled original constraints,74 detected jump segments, final X/MAX@808. SHA256 `af5d2d1bc6e0ab466f1b6cf309f3a26e76e4a63765ac8b15ff02fb4ad55d28bf`.

- [Exact original input archive](real11-input.json.gz), [input metadata](input-metadata.json). Decompress to a temporary file and set PKC_TRACE_INPUT when reproducing a full-route probe if the external original changes.
- [Authored constraints and physics overrides near39](authored-constraints-340-493.json). These are original JSON entries, not compiled rows; the engine can expand/duplicate constraints.
- Source-matched core-only runner:/tmp/pkc-endpoints-fixed, detached b98f87f plus copied sources; it is not an independently authoritative source checkout. The tracked branch and source hashes in the staged metadata define the implementation.
- JDK21 at /Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home. Java8 source target.
- Focused full-route probe only; no full suite. Solver misses intentionally do not fail JUnit. Commands run asynchronously with output redirected.

Recovery commits:

| Commit |Purpose |
|---|---|
|e14dfe4 |Conditional inertia multiplier seeding |
|c392645 |Intermediate candidate retention |
|3d4557e |Advance provisional objective to unconstrained airborne state |
|a8bebf3 |Focused state77 seeded/cold failure investigation |
|c7f38d9 |Comment out small-costate decoder substitution |
|74f5b09 |Focused no-decoder evidence |
|d353459 |Full single-cap commit1 run through38 |
|6944c89 |Per-window timing audit |
|7c0c131 |First-valid iteration/quality audit |
|b84931e |Optional staged cap configuration and probe property |
|8b9806b |Staged full-route evidence |

Pre-existing user edit `core/src/test/resources/captures/hpk/d9/j127_Head_to_Head_Neo.json` remains modified and uncommitted. It is unrelated; do not stage, restore or overwrite it.

## 3. Current settings and actual solver path

Direct **LongRunSolver.solve**, fixed start, original authored inputs through engine compilation. This is not the entire Optimize graph. No CertifiedBnb, BoundPrunedRecovery, setup peel or homotopy stage is invoked by this probe's provisional window loop. There is no outer deadline.

- Commit ladder **1 only** for this run. LongRun's default commit ladder remains3,1.
- Nominal window ladder **10,7,5,3,2,1**, largest first.
- At each committed entry, exhaust all nominal windows at CF dual cap1000. On no continuation, retry all windows at cap10000 from the same committed entry. On success commit and reset cap to1000 for the next jump.
- Higher-cap retries restart optimizer work, not resume it. Cache keys include cap, actual horizon, commit prefix, start state, margin and objective policy. Duplicate actual windows can reuse a cached miss within a stage.
- Fixed closed-form margin **.0003**, not the normal margin ladder. EPS2=1e-24.
- Decoder norm-below1e-9 objective-direction substitution commented out in FacingPrefold.expand and JumpLinearModel.recoverAlongCostate. Ordinary atan2 decoding remains, including at exactly zero costate.
- Conditional inertia multiplier seeding ON; intermediate valid-candidate retention ON. No cold retry fallback has been added after a failed seeded pass.
- Provisional objective tick advances from the nominal jump end until it is airborne and referenced by no original constraint, of any axis/mode/sense, including either reference of paired constraints. All constraints inside that actual extended horizon are retained. Commit boundary remains the nominal first jump's end.
- Position state[t] is before input[t]. Original tick numbers here are0-based solver states. Jump numbers are1-based human labels. A pattern-relative tick t is original state351+t in these39 windows.

For each provisional window, try in order:

1. ClosedFormSolve.optimizeRobust, Z/MAX at the actual endpoint.
2. Same closed-form solve, Z/MIN at that endpoint.
3. Same, X/MAX.
4. Same, X/MIN.
5. SlpSolve.optimizeCentered, feasibility/clearance restoration, not objective maximization. It is initialized using the Z/MAX objective's dual direction, but the centered solve does not run objective-hugging phase2.

Return on the first accepted call; otherwise shrink the window. At39 all calls miss for every window and both budget stages.

**Validation:** Shared WindowCandidateValidation requires actual-inertia continuous feasibility for every included window constraint, and Minecraft replay feasibility through committed input prefix14, ending at state365. Any final full-route acceptance would retain every original constraint. That final check was not reached.

## 4. Entry states: do not mix them

| Parameter at original state351 |Current staged baseline |Single10000 control |
|---|---:|---:|
|X |702.302172074316 |702.301967261052 |
|Z |-1111.856715620752 |-1111.839635559697 |
|VX |0 |-.000002268767 |
|VZ |-.269432741874 |-.269424831020 |
|Seed yaw, degrees |-135.000015259 |224.999984741 |

Values above come from the LRS-WIN seed parameters, rounded by logging. The two seed yaw representations differ by360 degrees. Do not replace raw seedYaw with a wrapped one or with the yaw shown at a boundary in committed-path.csv: it can change float accumulation. The displayed boundary yaw can describe a different input tick from seedYaw, which is the last committed input's facing.

### Exact reproduction of a focused39 window

Use the same approach already proven by CommitOneEarlyFailureProbeTest at77:

1. Rebuild the full JumpSpec from the archived TAS using SaveIO, Fixtures.buildBoxes, AngleSolverEngine.debugBuildSpec and ExactJumpModel.forMcVersion.
2. Read the relevant baseline's committed-ticks.csv; select commit1 input ticks0..350. Where boundary rows duplicate a tick, the later committed block's input yaw is the authoritative one. Parse the stored game yaw through float, as the simulator used floats.
3. Replay that full committed prefix using ExactJumpModel; take the state at351 for position and velocity, including Y/VY. The displayed X/Z/VX/VZ should match the table within rounding. Do not construct a supposedly exact state from12-decimal X/Z log values alone. Do not infer Y/VY or grounded state from this table.
4. SeedYaw is the game facing used at350; verify against the LRS-WIN parameter. Preserve its raw float representation.
5. Reflect/call LongRunSolver.sliceScenario(fullScenario,351,end,pos,vel,yaw) and sliceConstraints(fullSpec,351,end,yaw). Objective is window-local end-351; prefix validation limit14. Start with the22-input window ending373 if the goal is to isolate the nearest failure.
6. Check the focused run reproduces the archived per-pass iterations, violations and patterns before interpreting diagnostic differences.

The archived rows are sufficient to reconstruct the current original input scenario but exact prefix reconstruction still needs to be executed and verified at351. It was not done as part of this documentation task. The existing state77 probe hardcodes77 and must not be reused unchanged.

## 5. Every39 window and proposed commitment

All entries start at351, propose committing through365 and fail without committing any inputs.

| Nominal jumps |Nominal endpoint |Actual objective endpoint |Input count |Original compiled rows |1000 time ms |10000 time ms |
|---|---:|---:|---:|---:|---:|---:|
|39–48 |490 |493 |142 |135 |475.5 |2257.8 |
|39–45 |449 |450 |99 |105 |1631.3 |8075.8 |
|39–43 |438 |441 |90 |89 |1170.9 |6015.5 |
|39–41 |406 |412 |61 |74 |445.2 |2314.6 |
|39–40 |381 |388 |37 |63 |149.8 |885.7 |
|39 only |365 |373 |22 |35 |38.0 |188.5 |

These timings are staged baseline only. [Both baselines' entry lines](window-entries.csv), [all90 objective calls from the two baselines](objective-attempts.csv). The single-cap control has six windows and30 calls; staged has12 windows and60 calls. Objective times include their multiple inertia attempts. SLP call time includes construction of its separate dual seed.

## 6. Closed-form inertia behavior and what actually happened

Per objective, the first model has no assumed zeroing. Decode angles, evaluate actual inertia and shared validation. If the candidate fails, infer separate X/Z threshold checks from deterministic continuous simulation and rebuild a fixed-pattern model with velocity-band rows for the assumed zero checks. Up to4 pattern passes; stop earlier on acceptance, no improvement in max violation, an ineffective pattern, unchanged pattern, or empty solve. Fixed margin.0003 is applied to the compiled wall RHS, including generated bands. Normal dual stopping rules remain projected residual, recovered-input stall, divergence stall, no descent, multiplier cap and iteration cap.

Seeding is conditional on the preceding candidate's assumed fixed-model position/band violation being <=1e-8, identity prefold and supported axis rows. Source multipliers are mapped and new opposing velocity-band multipliers compensate the coefficient change. Valid intermediate candidates can be retained; stopping the dual is separate from saving an incumbent.

### Observed at39

| Evidence |Single10000 |Staged1000+10000 |
|---|---:|---:|
|Logged CF pattern/rung attempts |64 |111 |
|Hit iteration cap |48 |91 |
|Divergence stall |14 |15 |
|Recovered-input stall |2 |4 |
|No descent |0 |1 |
|Valid intermediate candidates retained |0 |0 |
|Eligible seeding sources |0 |0 |
|Seed applications |0 |0 |

Seeding is configured on but **never actually applied at39**: none of these source candidates satisfies its assumed-model eligibility check. Thus this failure has not exercised the near-feasible seeded recovery that helped earlier j13/j14. Zeroing patterns still change and are retried cold.

- [Every pattern attempt, readable table](cf-inertia-attempts.md).
- [Every pattern attempt with relative/absolute ticks, replay/continuous misses, source assumed-model violation, seed eligibility, retention and stop](cf-inertia-attempts.csv).
- [Single-cap condensed events](single10000-events.txt), [staged condensed events](staged-events.txt). These preserve original raw-trace line numbers and omit only detailed SLP-GATE rows. Full raw traces remain in each source run's gzip archive.

Best logged terminal CF continuous miss is.708925174724 in the single-cap control (3-jump window, X/MIN, unpatterned). Staged best is.696717910869 (cap10000,2-jump window, X/MAX, pass1, assumed X351 zero and Z384..387 zero). Its prefix replay miss is.696728374390 and its assumed-model source violation is.696717910869, so seeding is ineligible. These are not good near-feasible candidates and do not establish the best point ever visited: we logged accepted incumbents, not every invalid candidate.

## 7. SLP behavior, objective and separate seed budget

All18 fallback calls across the two baseline traces run both ordinary SLP and the inertia-aware retry:36 SLP starts. [Every SLP attempt](slp-attempts.csv).

- Centered feasibility solve, target clearance.02, bestEffort=false.
- Ordinary starts from its own margin0 dual seed, not the final preceding closed-form candidate. Its internal CostateDualSolver uses the default10000 cap even when the surrounding CF stage is1000.
- Therefore previously reported logged dual-run/iteration totals count CF rungs only; they exclude this separate SLP seed work. Seed time is included in the LRS-OBJ SLP duration, but occurs before the SLP start timestamp. This explains why a fallback can take hundreds of ms while its recorded LP phases take only a few ms.
- Ordinary model retry: inertiaAware=false. On failed phase1, inertiaAware=true retry starts from that phase1 path, seed=given. Each invocation has phase1 budget40 LPs, total60, trust radius starts30deg, max45deg, min1e-7deg, LP inner limit2000. Some phases terminate before40 LPs.
- All attempts stop in phase1; no successful restoration or phase2 objective polish.
- Many stop around reported violation.688. The22-input ordinary and aware attempts run40 phase1 LPs each at both stage budgets. The last aware LP rows identify **X@356** as the worst row, about.687966 violation. This is a useful next lead, not yet a full shared-validator row audit.

SLP's reported phase1 max violation is not automatically identical to the full continuous/prefix replay diagnostic values. Detailed per-row shared diagnostics and proposal velocities were not attached for these full-run attempts. The SLP-GATE lines in full raw traces do contain local/replay zeroing changes and trust-region proposals if needed.

## 8. The preceding commits and nearby constraints

The current accepted predecessor windows were:

| Starting jump |Window |Inputs |Commit through |Objective/method |Budget |
|---|---|---|---:|---|---:|
|37 |37–38 |319..352 |333 |Z/MIN closed form |1000 |
|38 |38 only |333..352 |351 |Z/MIN closed form |1000 |

Both actual objective endpoints are352. **Neither includes the authored constraint at356.** They can be valid for their entire chosen windows without establishing continuation to356. This is relevant when separating a poor committed entry from a solver failure.

Nearest authored entries from the saved JSON:

| State |Constraints / override |
|---:|---|
|350 |X in[701.6999999880792,703.3000000119208]; Z in[-1112.3000000119207,-1110.6999999880793] |
|351 |Slipperiness override DEFAULT, no authored constraint |
|356 |X <=700.4250000119209; Z in[-1114,-1113] |
|357 |X in[700.4250000119209,701.3000000119208] |
|359 |Same X interval; Z in[-1114.3000000119207,-1112.6999999880793] |
|364 |X in[699.6999999880792,700.4250000119208]; same Z interval as359 |
|365 |Slipperiness override DEFAULT, no authored constraint |

The chronological constraint list is assembled from unsorted JSON tick records; use numerical ticks, not file order. Exact compiled rows and per-tick accelerations/ground states still need to be exported from JumpSpec for the focused probe.

## 9. Next investigation, without assuming the conclusion

1. Reconstruct exact staged state351 from its committed game inputs; isolate351..373, prefix14, all35 compiled rows. Verify archived behavior.
2. Export compiled constraints, per-tick key/physics inputs, Y/ground/slipperiness, and original jump boundaries around333..388. Check specifically the five-input reach from351 to X356, including the actual jump boost and acceleration directions.
3. Attach the shared validation diagnostics: all original row margins, entire continuous window, committed-prefix Minecraft path, assumed fixed model and inertia-band velocities. Record yaw and state per tick.
4. Separate no-inertia assumed-model miss from actual inertia mismatch and small-costate decoding. The decoder substitution is already off. None of the current39 CF sources is assumed-model-valid, so do not attribute this automatically to inertia seeding.
5. Compare the preceding accepted333..352 window with a window that includes356 or later. That requires an identical-entry control at333; do not compare different generated states as if only the horizon changed.
6. If a reachability bound proves the entry cannot meet356, investigate window/commit planning rather than increasing iterations. Otherwise inspect numerical convergence and SLP proposal/rejection logs. Both outcomes remain open.

## 10. Commands and source references

Full staged reproduction, using the source-matched runner:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true -Dpkc.fullRouteCommit=1 -Dpkc.fullRouteDualCaps=1000,10000 -Dpkc.fullRouteLabel=-commit1-staged-caps-2026-10-24 :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginTenLadder > /path/to/new-run/full-route.log 2>&1
```

Use a new trace label and output directory to preserve these logs. The test's solver trace is under runner core/build/reports. A failed solver continuation is an allowed JUnit outcome. No need to rerun the full route merely to inspect its logged39 attempts.

Rebuild this inventory from archived traces:

```sh
python3 docs/research/solver-traces/jump39-baseline-handoff-2026-10-24/extract_baselines.py
```

Code sources at implementation b84931e:

- [LongRunSolver](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/LongRunSolver.java): optional cap config, runHorizon scheduling/cache/commits, freeAirObjectiveTick, solveWindow, sliceScenario/sliceConstraints.
- [ClosedFormSolve](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/ClosedFormSolve.java): solveWithPrefold pattern loop, runLadder, patternEffective.
- [InertiaRetrySeed](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/InertiaRetrySeed.java): eligibility and coefficient compensation.
- [DualCandidateRetention](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/DualCandidateRetention.java): observe and preferredTo.
- [WindowCandidateValidation](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/WindowCandidateValidation.java): actual continuous/full versus Minecraft/prefix acceptance and optional audit hook.
- [SlpSolve](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/SlpSolve.java): optimizeCentered, separate dual seed, ordinary/aware phase1 retry.
- [Full-route probe](../../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/FullRouteFixedMarginTraceTest.java), [existing state77 focused probe](../../../../core/src/test/java/de/legoshi/parkourcalc/core/anglesolver/solver/CommitOneEarlyFailureProbeTest.java).

All table claims derive from the archived solver traces or the linked source, not recalled descriptions of upstream behavior.
