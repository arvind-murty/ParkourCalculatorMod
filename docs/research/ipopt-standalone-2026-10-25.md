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
