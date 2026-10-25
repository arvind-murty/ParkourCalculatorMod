# Investigation inventory and recovery map

2026-10-24. Branch `codex/first30-margin-trace-bookkeeping`. Scope is every investigation change since the parent of e67a2ac, the first SLP-inertia trace commit, through production seeding e14dfe4, plus this run's trace-label addition. Exact starting reference is in [metadata](inventory-metadata.json); [commits.tsv](commits.tsv) lists96 commits and [changed-files.tsv](changed-files.tsv) lists1259 committed paths, mostly archived CSVs and docs. These counts describe the snapshot before committing this report. [Production patch](production-source.patch.gz) and [stat](production-diff.stat) capture all ten modified/added production files. Historical experiments remain separately committed.

## Working tree and environment

At turn start HEAD5370274, the only uncommitted change was the user's `core/src/test/resources/captures/hpk/d9/j127_Head_to_Head_Neo.json`. It was never staged or modified. Another capture, j147, is a historical committed edit in e67a2ac; it is included in the raw inventory and was not changed this turn.

Execution used isolated `/tmp/pkc-endpoints-fixed` at b98f87f plus copied production sources. All ten changed production sources were compared byte-for-byte with the main workspace and MATCHED before the full run. Core-only settings are archived as [settings.core-only.gradle](settings.core-only.gradle); the normal loader build settings were not edited in the main branch. Source snapshots and hashes are archived here. No processes from earlier interrupted probes were reused.

## Active production changes

| Area | Active behavior | Principal commits/files |
|---|---|---|
|Trace instrumentation |SLP proposal/velocity/inertia/trust-region diagnostics; LongRun window/objective/committed state and yaw logs; dual stops and iteration diagnostics |e67a2ac,9751d69,eb8aea8; SlpSolve,LongRunSolver,CostateDualSolver |
|Prefix replay validation |Compile exact constraints only through committed inputs, including exit position but excluding next uncommitted facing |ea25bc8; JumpConstraintCompiler |
|Actual continuous validity |Shared window helper requires full continuous path with real threshold checks plus committed-prefix Minecraft replay; detailed constraint/velocity audit; applies to CF and SLP window acceptance |f8929eb,aac6527,4e3db72; WindowCandidateValidation,JumpLinearModel,ClosedFormSolve,SlpSolve |
|LongRun configurable margins |Per-run fixed-margin override supported; normal robust ladder remains the default when no override is supplied |LongRunConfig.withFixedMargin; fixed .0003 used by this full probe |
|Objective cap handling |Detect a directly conflicting objective wall; temporarily remove matching ENDPOINT bounds separately for each objective/window attempt; rebuild from authored constraints when the window/objective changes |4dd969c,ae541ac; ObjectiveBounds,LongRunSolver |
|Final route validation |Retain ALL authored constraints in final original-route replay; no accumulated dropped-wall exemptions |ae541ac; LongRunSolver.validateRoute |
|Dual numerics |EPS2=1e-24; default cap10000; projected-gradient, input-stall and no-descent stops remain active; diagnostics expose final residual |b96d5a3,c8d7616,94d7a80; CostateDualSolver |
|Intermediate candidates |Save validated improving continuous-objective candidates while continuing the dual solve; prefer saved candidate over invalid/worse terminal decode |c392645; DualCandidateRetention,ClosedFormSolve,CostateDualSolver |
|Conditional inertia seeding |NOW ported from probe: seed changed-pattern retry only if previous assumed model is feasible within1e-8, including its zeroing velocity bands. No actual-miss cutoff. Map positional prices and project costate changes into opposing band prices; no relative preservation-error veto |e14dfe4; InertiaRetrySeed,ClosedFormSolve |

The new production seed path deliberately covers MC per-axis inertia, identity facing folds and one-axis wall rows. Changed row counts/bases, nonidentity folds, incompatible paired rows or nonfinite multipliers decline to the existing cold path. In scope there is no hardcoded jump/tick, .01 actual-miss threshold, or strict relative reconstruction-error rejection. Reconstruction error remains logged. Source eligibility is based on the returned best failed candidate's fixed-model path, not a smoothed variable-length control or a nominally converged flag. `Config.seedInertiaRetries=false` provides the focused control; default is true.

`Config.retainIntermediateCandidates` defaults true. Candidate scoring uses actual continuous objective plus existing smoothing penalty, among validated candidates. It does not promise the .0003 inward target on every original row, globally optimal early lines, or a fully exact uncommitted suffix. The final full-route check remains authoritative.

## Experimental/test changes and research, not active route policy

| Work | Status and recovery references |
|---|---|
|Margin policy comparisons |First30 normal versus fixed traces, identical-entry controls, multiplier ladder studies; historical successful normal reference is FIRST30, not proof of a successful808-tick route. f980e29 and subsequent verified handoff |
|Window extension |Whole-jump/tick forward extensions and direction-specific checks were tried; superseded by fixed window plus endpoint-bound removal. 824f962,0edbfbf,ae541ac |
|Turnaround/coupled endpoint selection |Manual test planner and route-only detection proposals; not wired into this full run. f7918a1 and manual-coupled-window archive |
|Direct j13/j14 after-turn objectives |Standalone ZMIN90/ZMAX103 experiments retain all rows; these endpoint choices are not forced into LongRun's fixed7 window policy |
|Seeding research |Initial cold/carry/scaled/compensated comparisons, strict preservation guard and its removal, delayed close-path repair, assumed-feasibility-only rule; 0aaf590,bd1fd03,d451e88,1aa14cc,adeae32. Latest rule is now ported in e14dfe4 |
|Control-stall removal |Tested separately in0f72262; current production still stops on recovered-input stagnation |
|Decoder default |Flagged for later: norm(g)<1e-9 replaces direction with objective direction in FacingPrefold/JumpLinearModel. NOT disabled or changed |
|Long-route B&B |0c09e1d adds a regression probe of existing over256-tick graph guards. No new CertBnb/BnB integration into LongRun |
|Probe coverage |Opt-in full route, first30, initial7, jump2/6, same-angle model, seeding, iteration and constraint audits; exact paths in changed-files.tsv |
|Docs |Scratch pipeline/flowchart, convergence plan and failure research, full constraints/jump mapping, PR/blame provenance, handoff, all archived trace tables |

## Changes made during this turn

1. Inventory and source consistency check; user j127 kept untouched.
2. Port conditional compensation to production in e14dfe4.
3. Add opt-in `InertiaSeedIntegrationProbeTest` using the exact saved state55; verify production j13 and j14 off/on controls.
4. Add optional full-route trace label suffix to avoid overwriting historical runs.
5. Run full808-tick LongRun with seven-window ladder, fixed .0003, commit3 then1, no outer deadline.
6. Preserve source/settings snapshots, metadata, cumulative patch, complete commit/file lists, all window/objective/dual-rung/committed-tick tables and comparison with the previous fixed run.

## Recovering work

Use the individual commits above for selective recovery; the raw commit list gives the full dependency order. c392645 supplies retention; e14dfe4 builds on it with conditional seed capture/projection and production integration controls. The final report/probe commit supplies unique trace labels and evidence, not another solver algorithm change. Create a separate checkout at the desired commit rather than resetting this working tree or overwriting the user fixture. For the isolated core-only reproduction, apply the archived settings file there and use JDK21. [README](README.md) records exact commands and results.
