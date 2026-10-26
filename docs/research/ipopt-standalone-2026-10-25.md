# Standalone IPOPT production findings, 2026-10-25

## Scope and ownership

The user clarified that mod integration is unnecessary. The delivered implementation is standalone, in `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-standalone`, branch `feature/ipopt-standalone`, based on `9bc4d24479056edb010053ea32bee311b064b4a9`. It adds files under `standalone/ipopt/` and this independent research record. Core, loader, UI, save schema, existing solver implementation and the other agent's handoff are unchanged. The original shared checkout and its modified capture remain untouched.

The optional mod integration built before this clarification is preserved separately as commit `97f545a` on `feature/ipopt-local`. It is not part of the standalone branch. Its targeted Java 8 / JDK 21 checks and the observed pre-existing native-suite failures are recorded in that branch's `docs/research/ipopt-local-productionization-2026-10-25.md`. The expensive native suite was stopped after the user narrowed scope; it was not a passing full-suite run. The standalone change does not require modifying or repairing those native stages.

## Delivered interface

`standalone/ipopt/solve.py` reads a versioned compiled problem JSON, runs an isolated CasADi/IPOPT worker, and writes an atomic result JSON. `standalone.ipopt.solve` exposes the same capability as a Python API, with time limits, cancellation, warm starts and an incumbent callback. A caller can supply fixed or partial inertia patterns, or use bounded automatic pattern continuation. Feasibility-only mode stops at the first independently accepted candidate; optimization retains the best validated objective.

The parent and validator use only the Python standard library. The worker uses CasADi 3.8.1 and NumPy 2.5.4, tested with Python 3.12.2. A local interpreter can be selected through `--python` or `PKC_IPOPT_PYTHON`. No distribution packaging, downloads or environment mutations are performed automatically. Existing experiments and their environment were used read-only.

An optional `export.gradle` plus `java/ExportStandaloneIpopt.java` exports an ordinary captured TAS through the existing headless Java physics compiler. It does not launch Minecraft or edit the mod. It accepts a tick range and input/sprint policies; existing per-tick overrides remain effective. The exported model pins the initial state, restores absorbed start-position restrictions and an initial no-turn row, and uses typed facing equality corridors rather than sine-table-cell equivalence. Relative references outside the selected span and initial-state displacement constraints are rejected explicitly. Source position rows remain hard, including any legal goal wall. Combined horizontal inertia is not modeled; its export explicitly carries `threshold: 0` and marks per-axis inertia unsupported.

The headless 400-tick export reproduces the fixture's coefficients, all 341 rows, all 34 contacts, objective, initial state and physical-problem hash. The final normalized physical hash is `295b76ca7ee3f215fcd9714660119c0552ae951ac0e9f2f57b152da7ea97f0b3`. A serialization detail discovered during comparison was corrected: the exporter must serialize start yaw as the exact double representation of the stored float, rather than Gson's shorter Float rendering.

## Model and acceptance

The worker uses sparse explicit position, velocity and facing variables. It first solves unrestricted carry, then inferred zero events without survivor rows, then complete fixed inertia patterns with bounded refresh attempts. Directly freezing every zero/survival branch from the first carry solve sometimes failed when the original facing pin was restored. The zero-event intermediate solve successfully bridges that case. With an explicitly supplied `inertiaPattern`, it solves that requested pattern directly.

The parent reconstructs world-coordinate positions and stored velocities from the returned facings using scalar math, automatic thresholding and conditional wall clipping. The worker uses separate relative position variables, so its claimed state values and success flag are not used to establish feasibility. All compiled position, relative, cross-axis and wrapped-facing rows are checked at 1e-7 tolerance. Explicit inertia branches are checked separately. A declared wall must actually be reached from the allowed side: even a sub-tolerance shortfall cannot cause a velocity reset.

A valid result is `feasible`. Failure to find one is `unknown`, including IPOPT's local infeasibility status. Runtime and protocol failures are `error`. Cancellation discards the in-flight result; a timeout keeps a previously validated incumbent. CLI exit codes are 0, 2, 3 and 130 respectively. These statuses prevent a caller from treating a failed local search as a proof that an input pattern is impossible.

## Real11 results

Both copied fixtures retain every facing row within their input domain. The 200-tick fixture has 197 compiled rows and 21 prescribed contacts; the 400-tick fixture has 341 rows and 34 contacts. Their fixed acceleration coefficients use the captured Force 45 / Derive policies. The archived source gzip SHA-256 is `ce8b765c0f8b1e1d0add83d2a690230e1df0898d4bc4c8919d7a72b358bd27be`.

Final separate CLI measurements, including worker startup, construction, solving, validation and process cleanup:

| Problem | Mode | Seconds | X objective | Maximum compiled-row violation |
| --- | --- | ---: | ---: | ---: |
| Real11 200 | Optimize, walls and automatic inertia | 0.553 | 737.5142588032 | 0 |
| Real11 400, freshly exported | Optimize, walls and automatic inertia | 3.334 | 691.4017522467 | 0 |
| Real11 400 | First feasible, walls and automatic inertia | 1.441 | 691.0763522496 | 0 |
| Real11 400 | Optimize, walls with inertia disabled | 1.212 | 691.4017529226 | 0 |

All four return independently validated feasible trajectories, with no missed contacts or explicit pattern mismatches. The feasibility-only result gives up approximately 0.3254 blocks of endpoint objective compared with the longer optimization. It is useful as a probe, but objective quality matters when choosing among candidate input patterns.

These are measurements on one captured route and local optimizations, not a broad reliability or global-optimum claim. The 200-tick solve can finish below a second because it converges and validates; the time limit is a maximum, not an artificial minimum. Invalid candidates are rejected independently regardless of solve speed.

The earlier full 808-tick mismatch remains accepted as a limitation of that authored/model combination. In particular, the exported rows near 651/652 imply a shortfall of about 3.61655e-5 blocks even under maximum continuous acceleration, and restoring all later facing rows adds a separate pin/zero-turn conflict. This standalone work does not loosen tolerances or silently omit those rows. Longer-route orchestration and splicing remain caller work.

## Verification and artifacts

All 22 standalone tests pass with the configured local worker. The suite includes a known optimum, an explicit fixed inertia pattern, inconsistent rows yielding unknown, both real11 cases, independent carry/zeroing replay, tangential preservation, wrong-side hits, a sub-tolerance missed wall, relative/angular/cross-axis rows, malformed/non-finite data, cancellation, deadline enforcement against an unread large request, retained incumbents, rejected fake worker success, and atomic CLI output including output errors.

The standard-library-only run passes with the four actual-worker tests skipped. Python bytecode compilation passes. The headless Gradle export task compiles Java 8-compatible source and succeeds without launching Minecraft or running solver suites. The freshly exported request was compared with the fixture and solved through the standalone CLI. A diff check confirms there are no tracked changes to core, forge-core or either loader tree.

Reproduction and the complete request/result contract are in `standalone/ipopt/README.md`. Saved result trajectories are gzip JSON files under `docs/research/solver-traces/ipopt-standalone-2026-10-25/`, alongside test/export logs and a source-hash/runtime provenance manifest. No Minecraft client was launched; an existing client remains running. Continuous trajectories still require separate game validation when used as actual TAS inputs.

## Ladder-model clarification

The standalone solver currently models prescribed wall contacts, not complete ladder physics. It requires the horizontal proposed move to reach the recorded plane, clips the normal position to that plane, resets only the stored normal velocity, and preserves tangential propagation. It does not apply a ladder's horizontal component cap, simulate vertical climbing, or discover ladder geometry. The continuous acceptance claims above refer to that explicit approximation.

The mod's actual game simulator delegates movement and collision to the Minecraft superclass. Thus the real ladder/vine checks, component caps, world collision clipping, normal velocity resets and collision-induced vertical climbing occur in the game simulation. The recorded medium is sampled for diagnostics.

The native solver has a separate collision-free `ExactJumpModel`. On ticks explicitly marked `SurfaceKind.LADDER`, it caps each horizontal component to `(double) 0.15F` before movement, limits downward Y speed, and prevents descent while sneaking. It also has a modern ladder-jump Y rule. It does not clip movement to wall geometry or implement wall-triggered climbing, because its move remains collision-free. Solver ladder flags come from per-tick medium overrides, not automatic world geometry detection. `JumpLinearModel`, used by the standalone exporter for acceleration/friction coefficients, does not implement the ladder cap. Consequently a successful standalone solve does not certify complete ladder behavior in Minecraft.

Relevant sources checked: `ExactJumpModel.java` around 330-356, `AngleSolverEngine.buildPhys` / `effMedium`, Forge `SimulatorEntity.travel` and `move`, Fabric `SimulatorEntity.travel`, and the repository's mirrored ladder reference in `docs/reference/mcpk/04-blocks-and-friction.md`.

## Proposed ladder-cap formulation

For a prescribed ladder tick and one horizontal axis, compute raw displacement `r = zeroedIncomingVelocity + control`. Apply `d = clamp(r, -c, c)` before position advancement, wall clipping and friction, using `c = (double) 0.15F` (approximately 0.15000000596046448) to match the native solver's cap. With no wall hit, `pNext = p + d` and `vNext = friction * d`; a prescribed wall hit subsequently clips position and sets normal stored velocity to zero. Reaching the wall must be tested using the capped displacement.

A cap pattern has three branches per ladder tick/axis: negative saturation (`r <= -c`, `d = -c`), free (`-c <= r <= c`, `d = r`), and positive saturation (`r >= c`, `d = c`). Once fixed, these add affine rows around the existing smooth angle controls. A saturated component fixes its displacement and next stored velocity, cutting incoming-velocity dependence without requiring a new absolute position frame. Simply restricting the raw component to the cap interval would incorrectly prohibit legitimate saturation, for example raw .20 clipping to .15.

Recommended first implementation: fix the ladder-tick schedule, infer saturation branches from an initial trajectory, solve with branch membership rows, and independently replay the actual clamp for acceptance. If a pattern fails, or optimization reaches a switching boundary, try adjacent branches within the time budget. A failed fixed pattern remains unknown for the broader problem. Unlike inertia zeroing, adjacent cap branches agree on their boundary, so strict disjointness guards are unnecessary for their physical consistency. A valid fixed branch can still miss a better objective under another branch.

A direct min/max clamp is another useful benchmark, but introduces derivative kinks. IPOPT's documented formulation expects twice continuously differentiable objective and constraint functions: https://www.coin-or.org/Ipopt/ . It should not be assumed that direct clamps will have the same robustness as the branch-fixed smooth NLP. The ladder occupancy schedule is a separate input: recorded wall hits alone do not determine whether a player is on a ladder. No cap implementation or solver behavior changed during this design discussion.

## Discovering ladder schedules and reusing baseline solutions

The current standalone request contains prescribed horizontal contact planes, not ladder activation geometry or a vertical trajectory. The native angle model's ladder flag is also a per-tick medium override, rather than an automatic geometry query. The actual Minecraft simulator has the world geometry and delegates activation to Minecraft. Consequently the existing standalone fixture cannot yet discover ladder occupancy by itself. A wall hit is insufficient evidence that a tick is on a ladder.

With version-correct ladder activation regions and vertical state supplied, a replay can classify ladder ticks. Use the activation predicate, not merely intersection with the thin solid collision box. The mirrored 1.8 reference distinguishes the player's coordinates being inside the ladder voxel from the ladder's physical collision box. Initially a captured vertical trace could be an explicit approximation; changes in horizontal collisions and climbing can alter that trace, so a complete physical check needs vertical replay too.

A practical extension is to solve a baseline, replay with automatic inertia and ladder clamping, freeze the inferred ladder schedule and cap/zeroing branches, then solve the resulting smooth NLP. The next path must be replayed again with automatic detection. Check both that declared ladder ticks remain on ladders and that nominally ordinary ticks did not enter a ladder region. If occupancy changes, refresh the schedule and try nearby assignments within the budget. Adding membership constraints can reduce schedule drift; enforcing nonmembership requires selecting an outside face or handling the alternative regions. The same general refinement approach as inertia applies, but ladder activation depends on position including Y, whereas inertia branches depend on velocity. A failed fixed schedule is unknown for the broader problem.

Previous solutions can already supply facings through `--initial-solution`; the worker reconstructs position and velocity guesses under the current phase's physics. This is a primal seed, not reuse of IPOPT dual multipliers. A prior no-inertia/no-cap candidate must be validated under the new physics before it qualifies as an incumbent. It can remain a useful initial guess even if invalid. Likewise a failed baseline is not a proof that the model with caps and inertia is infeasible: these physical modifications are not automatically nested relaxations.

### Measured no-inertia warm start

Both runs below used the saved `real11-400-no-inertia-result.json.gz` facings, restored the fixture's .005 inertia threshold, retained all constraints and prescribed contacts, and allowed 10 seconds. They used the same existing Python 3.12.2 / CasADi 3.8.1 runtime read-only. No ladder cap is implemented, so this measures no-inertia reuse only.

| Seed and mode | Total seconds | First valid seconds | Final X objective | Maximum compiled-row violation |
| --- | ---: | ---: | ---: | ---: |
| No-inertia seed, first feasible | 1.145 | 1.039 | 691.0763522907 | 0 |
| No-inertia seed, optimize | 10.110 | 1.075 | 691.2422668156 | 0 |
| Earlier default seed, first feasible | 1.441 | not separately tabulated | 691.0763522496 | 0 |
| Earlier default seed, optimize | 3.334 | not separately tabulated | 691.4017522467 | 0 |

Warm feasibility was about .30 seconds faster in these individual runs. Warm optimization reached the time limit with an objective about .15949 blocks worse than the earlier default-seed solve. The zero-event phase hit its phase time limit and two subsequent complete fixed-pattern phases reported local infeasibility. The parent retained a valid incumbent. Two invalid candidates were rejected; the final rejected one missed the wall at state 356 by .04003531862701948 under automatic inertia replay despite its near-optimal nominal objective.

Thus warm starts are supported and useful candidates, but this example does not support making them the only search route. Retaining a default or alternative seed alongside the warm seed is a reasonable future policy. These are single measurements, not a statistical speed claim. Both saved accepted candidates were independently replayed again after completion; positions and stored velocities exactly matched their saved arrays and every compiled row passed. The two complete results are archived beside the earlier results as `real11-400-warm-inertia-result.json.gz` and `real11-400-warm-feasibility-result.json.gz`.

No production solver, mod, ladder geometry export or cap implementation changed in this turn. Only this research record and the two measured result artifacts were added.

## Why the no-inertia seed degraded optimization

The user correctly challenged the .159485-block warm-start objective gap. This is a material optimization shortfall, not rounding or the validation tolerance. Default mode really does optimize the requested endpoint; only `--feasibility-only` stops on the first independently accepted path. The result status `feasible` certifies acceptance, not objective optimality or successful completion of all optimization phases.

Inspection found a weakness in the initial continuation implementation. `worker.phase` returns IPOPT's final iterate regardless of whether the phase succeeded. `worker.main` assigns that return to its working yaw and infers the next pattern from it, including after time limits or local infeasibility. The parent preserves the best validated output, but the worker does not return to that incumbent for optimization. Thus it can keep searching from a badly violated final iterate while leaving an earlier valid path unpolished. This happens at `worker.py` lines 193 and 212-224 in the current implementation. No production fix was made in this diagnostic turn.

To distinguish pattern selection from angle optimization, a separate diagnostic worker wrapped the existing phase function, without modifying its equations, options, parent acceptance, or production files. It recorded all phase seeds, final iterates, fixed patterns and streamed candidates. Five runs used the same 400-tick problem, .005 inertia threshold, prescribed contacts, full constraints and 10-second maximum. The two fixed-pattern runs initialized from previous inertial results use the actual threshold outcomes from each saved candidate's stored velocities. The fixed-pattern run initialized from the no-inertia path uses the successful cold result's pattern, so it tests optimization conditional on a known good pattern rather than automatic pattern discovery.

| Diagnostic | Total seconds | Accepted X objective | Completion |
| --- | ---: | ---: | --- |
| Original/default seed, automatic patterns | 3.438 | 691.4017522467 | All three phases succeeded |
| Saved no-inertia seed, automatic patterns | 7.757 | 691.2422668156 | Zero-event phase timed out; three complete-pattern attempts locally infeasible |
| Saved no-inertia seed, known successful cold pattern | 1.092 | 691.4017521829 | Fixed-pattern optimization succeeded |
| Poor warm incumbent, its own actual inertia pattern | 0.900 | 691.3930032028 | Fixed-pattern optimization succeeded |
| Good cold incumbent, its own actual inertia pattern | 1.146 | 691.4017522467 | Fixed-pattern optimization succeeded |

All five returned candidates were independently replayed with actual inertia and contact tests again after completion. Positions and stored velocities exactly matched the saved result arrays, and all compiled constraints passed. The successful fixed-pattern solves also enforce branch consistency. The diagnostic wrapper adds logging/replay overhead, so these are diagnostic timings rather than an overhead-free benchmark.

Polishing the poor warm incumbent alone recovered .1507363872 blocks, about 94.5% of the .1594854311 gap. Its remaining difference from the cold result is .0087490439 blocks. That remaining difference has not been proved to be intrinsic to the pattern: fixed-pattern IPOPT is still a local search. Starting from the no-inertia facings with the known successful pattern closed the full gap to about 6.4e-8 blocks relative to the earlier cold result. This demonstrates that the starting angles themselves do not prevent reaching that objective; selecting and repairing the inertia regimes and preserving a useful optimization seed are central issues here.

The initial carry-only solves have almost identical endpoint objectives (cold 691.4017529226171, warm 691.4017530096013), yet 77 ticks differ by more than one degree and the per-tick facing difference has RMS 10.74594 degrees. Their inferred zero masks differ at five axis/tick pairs: Z@201, Z@238, Z@246, X@255 and X@257 (all zero-based pre-input ticks). A nearly identical carry objective therefore does not imply an equivalent intermediate path or inertia schedule. This observation is consistent with a long route's endpoint providing weak guidance for earlier constrained sections, but is not a mathematical uniqueness or conditioning proof.

The warm zero-event phase streams valid candidates up to 691.2422668156 by about 1.60 seconds into the diagnostic worker, then finishes at an invalid iterate whose actual replay has violation 9.07226 blocks. The next three pattern guesses are inferred from that and subsequent invalid iterates. The cold zero-event phase instead reaches a validated 691.4017522467 path and the final full-pattern phase succeeds. This directly explains why the warm run's returned feasible path was not a converged optimized result.

The next implementation priority should be to keep the best independently validated incumbent available to the worker, polish its actual inertia pattern, and avoid relying only on failed final iterates when refreshing patterns. Validate and retain any supplied seed that already satisfies the requested physics. Preserve an alternative/default initialization when the seed's inferred schedule stalls. Objective improvement and phase completion should be reported separately from feasibility. These changes belong to the continuation/search policy, without weakening constraints or tolerances.

The evidence supports fast useful optimization of this fixture conditional on a suitable inertia pattern. It does not yet establish reliable near-optimal optimization across captures, nor a global optimum. IPOPT's official documentation describes a method targeting a local solution: https://www.coin-or.org/Ipopt/ . A broader objective-quality benchmark remains needed.

Reproduction: run `python3 docs/research/solver-traces/ipopt-standalone-2026-10-25/seed-optimization-diagnostic.py --python /path/to/the/casadi/python`. The runner uses only the standard library and starts the existing worker runtime without altering its environment installation. Both diagnostic scripts, the five result gzip files, five compressed phase/candidate traces, summary JSON and numerical comparison JSON are archived in that directory. Python syntax checks passed. No production solver or mod files changed.

## Implemented incumbent-preserving continuation fix

The user authorized fixing the regression. Production changes remain entirely within the isolated standalone worktree. The mod, Java solver, loader trees, source captures and the other agent's handoff remain unchanged.

`continuation.py` now owns the worker's validated incumbent and continuation policy. Every nominally feasible NLP iterate is eligible for actual automatic-inertia replay, rather than only nominal objective improvements. Only actual feasible improvements are published. The parent continues independently validating every worker candidate. A phase retains its best nominally feasible iterate for continuation; if it never finds one, it returns its input seed. It no longer passes a failed final iterate to subsequent pattern inference.

Once an actual valid path exists, the continuation loop derives the full inertia pattern from that path's scalar replayed stored velocities and optimizes from its facings. Failed attempts cannot displace that incumbent. A successful polish is recorded for the specific incumbent, avoiding repeating it when an alternative attempt does not improve the best path. Explicit inertia patterns and threshold-zero requests still use their requested single-phase solve. Constraint equations, branch guards, contact reach rules and validation tolerances are unchanged.

The parent validates the initial facings before launching a child, calls the incumbent callback for an accepted seed, and preserves it through timeout, a weaker candidate or worker/runtime failure. Feasibility-only requests can return an already valid seed without launching IPOPT. Cancellation still discards the result. Worker failure with an accepted seed returns feasible with the failure details retained, consistently with the previous behavior for accepted worker candidates.

`--initial-solution` now preserves the input request's original facings as optional `fallbackYaw`, unless the caller already supplied a fallback. The automatic-inertia worker gives a distinct initial attempt 40% of its remaining budget and explores the alternative with the remaining time, sharing the validated incumbent. An API caller can also supply `fallbackYaw`; its length and finite values are checked, and it is excluded from the physical problem hash. The fallback is useful extra coverage, but the regression is also fixed without it. Phase diagnostics now name the phase kind and initial/fallback attempt, so feasibility and optimization progress can be inspected separately. Success remains a local result, without a global-optimality certificate.

### Results after the fix

All runs use the same local runtime, original 400-tick physical problem, .005 inertia, every compiled row and prescribed contact. The no-inertia seed is the same saved path used in the failed warm-start experiment. The poor valid inertial seed is the old 691.2422668156 result. Results below include parent validation and process cleanup.

| Case | Seconds | X objective | Maximum compiled-row violation |
| --- | ---: | ---: | ---: |
| Default 200-tick optimization | 0.819 | 737.5142588032 | 0 |
| Default 400-tick optimization | 3.373 | 691.4017522467 | 0 |
| No-inertia seed, 400 ticks, without fallback | 3.762 | 691.4017522748 | 0 |
| No-inertia seed, 400 ticks, with fallback | 5.745 | 691.4017522748 | 0 |
| Old poor valid seed, polish its actual pattern | 0.890 | 691.3930032028 | 0 |
| No-inertia seed, first-feasible mode with fallback | 2.833 | 691.2190320644 | 0 |

The no-fallback measurement is a separate direct API run. The other five were measured sequentially in a single run, without the test suite running alongside them. An earlier CLI call exercising `--initial-solution` also returned the same 691.4017522748 objective in 5.928 seconds. The restored warm result differs from the previous good cold result by about +2.8e-8 blocks, closing the .159485-block regression. The old poor valid seed by itself still optimizes to 691.3930032028 under its own pattern, about .008749 below the good cold path; that remaining pattern/local-solution difference is not hidden by the fix or claimed to be a global gap.

The no-fallback warm path used a successful carry phase (85 iterations), zero-event phase (272) and complete-pattern phase (382). Its full pattern has 58 zeroed and 742 surviving axis/tick entries. The fallback explores the original carry/zero-event route and keeps the stronger warm incumbent; the repeat polish is skipped. The default cold route continues to reach its prior objective. First-feasible mode stops earlier and yields a materially weaker endpoint than optimization, as expected. These are measurements on this fixture, not a broad optimization-reliability benchmark.

### Verification and archived evidence

All 30 standalone test methods pass with the actual worker runtime; the final run takes 16.390 seconds. This includes three real11 warm subcases: no-inertia seed without fallback (3.585 s), no-inertia seed with fallback (5.759 s), and direct polishing of the old valid inertia seed (.878 s). Both no-inertia subcases assert objective >= 691.4017 and the valid-seed polish asserts >= 691.3929. They independently replay the returned facings and compare all saved position/velocity arrays. The ordinary 200/400 cases, known optimum, contradictory rows and explicit pattern tests also pass.

Deterministic continuation tests cover a phase that emits a valid path and then fails at an invalid iterate, actual-pattern polishing, fallback exploration even after a successful initial polish, minimization, and explicit-pattern preservation. Process tests cover valid-seed retention through timeout and runtime error, refusal to replace a seed with a weaker result, feasibility without worker startup, invalid-seed rejection, cancellation and the existing protocol/CLI cases. The standard-library run passes its 25 tests with five actual-worker methods skipped. Python compilation and `git diff --check` pass.

The required Java fast gate was also attempted with JDK 21: 791 tests completed, three failed and 33 were skipped. The failures are `CrossJvmDeterminismTest.searchPathIsBitIdenticalAcrossJvms` (closed-form golden hash 7622806883939910482 vs expected 7100074979304643135), `InertiaFoldingTest.loopmmInertiaTermDropsOutOfFoldedModel` (clamp-free difference 2.6337802410125732e-5 vs expected > .01), and `CostateSmoothingScaleTest.bothRecoveryPathsUseObjectiveDirectionOnlyForVanishingCostates` (expected 0, got 180). All Java code, tests and resources in this checkout exactly match the pre-fix HEAD, verified with `git diff HEAD` over core and both loader trees. These failures are in unchanged native code; the Java gate is not reported as passing. No native solver fixes or unrelated slow-suite work were added.

Saved evidence is `continuation-fix-results.json.gz` (five sequential runs), `continuation-fix-no-fallback-result.json.gz`, `continuation-fix-tests.log` and `continuation-fix-core-tests.log` beside the prior traces. All six accepted trajectories were independently replayed and their positions/velocities matched their saved arrays. The small production test fixture `standalone/ipopt/fixtures/real11-400-warm-yaws.json.gz` copies only the two seed facing arrays from the prior archived results. The earlier diagnostic scripts describe the phase interface at commit `84a2f17`; reproduce those historical measurements at that commit. Current reproduction uses the standalone README CLI and test commands.

## Solver versus orchestration boundary

The user asked whether the numerical solver only knows horizontal state/constraints/objective, while an orchestrator handles retries, inertia, wall hits, ladders and Y. This is close as an architectural direction, but event selection and event enforcement must be distinguished.

The horizontal NLP optimizes per-tick facings and X/Z positions/velocities. Its supplied data include initial state, fixed per-tick acceleration magnitude/direction offset and friction, position and facing constraints (including relative rows), the objective, a chosen inertia pattern and prescribed contact planes/ticks. The solver itself enforces the chosen zeroing/carry equations and threshold branch inequalities. It also enforces wall reach from the allowed side, the pinned contact coordinate and the normal-velocity reset. These physical effects cannot simply be patched onto an accepted path after optimization. Future fixed ladder-cap branches would likewise have to be encoded in the NLP dynamics.

The existing continuation orchestrator chooses or refreshes inertia patterns, supplies warm/fallback starts, retries phases, manages time and preserves/polishes valid incumbents. It does not currently discover new wall schedules or ladder occupancy from geometry. The exporter supplies recorded wall contacts; the standalone request has no ladder geometry or Y state. The independent scalar replay applies automatic inertia and conditional prescribed wall hits and verifies all requested constraints. It does not yet validate ladder caps or vertical climbing.

A future outer world/vertical layer can replay a trajectory, discover grounded/airborne states, wall contacts and ladder occupancy, update their schedules and repeat the horizontal optimization. It must account for coupling: Y determines contact and ladder eligibility, while horizontal wall contact can trigger climbing and affect later Y/ground states. Captured per-tick acceleration/friction currently bake in assumptions about those states; a changed schedule may require recompiling coefficients. Thus Y can remain outside the horizontal numerical variables, but cannot be assumed independent of the horizontal route. The actual mod's Minecraft simulator already has full world/vertical physics; that is distinct from implementing it in this standalone pipeline.

Sources rechecked for this clarification: `continuation.py`, `worker.phase`, `model.validate`, the standalone README and `ExportStandaloneIpopt.java`. No solver or mod behavior changed and no additional numerical tests were run for this explanation.

## Ladder implementation research and challenged entry claim

Read the mod's world-query ports, simulator delegation, live medium/friction sampling, `Application.applyPathSurfaceState`, native ladder cap code, saved debug schema and exporter. Inspected the cached mapped Minecraft 1.8.9/Forge movement bytecode read-only, without launching Minecraft. Found that the cap query occurs before collision movement, while the climb query occurs afterward. The initial commentary overgeneralized the possible same-tick entry/climb case from that ordering without checking a trajectory; after the user's challenge, a geometry probe using Minecraft's own AABB collision-offset routine demonstrated the conditional high-speed case. Ordinary slower entries in that probe did not collide on entry. No claim is made that Real11 exhibits that case or that a full game replay was performed.

The concrete support plan, exact source provenance, geometry probe results, scope distinctions and implementation/verification order are in [the ladder implementation plan](ipopt-ladder-implementation-plan-2026-10-25.md). The mod already preserves useful before-move ladder samples in live `TickState` and can save them as surface overrides; plain saved debug rows omit that sample. Proper standalone support still needs explicit ladder data, cap dynamics, world/vertical replay and schedule consistency checks. No production solver, mod code or model behavior changed in this research turn.

## Ladder entry timing clarification

The user clarified the two example entry cases: both have uncapped horizontal movement on the first tick and both cap X and Z on the second tick, assuming the reported post-entry ladder occupancy. The slower entry uses normal air vertical movement; the entry that also hits a wall carries approximately +.1176 Y velocity after the climb assignment and gravity/drag. A large tangential X component is capped on the second tick too. Recorded this explicitly in the ladder plan. No production code or model behavior changed; no additional tests were needed for this clarification.
