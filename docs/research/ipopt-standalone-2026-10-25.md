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
