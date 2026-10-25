# Handoff: build a new angle optimizer using the existing framework

2026-10-24. The next task is to design and build a solver from scratch, reusing this repository's movement, block, input and constraint infrastructure. The previous optimizer investigation is shelved. This brief is a reading map; follow the linked records for measurements, derivations and provenance.

## Scope and reading order

We care about Minecraft **1.8**, fixed-start yaw optimization with authored keys/contact schedule, hard windows around 100 ticks, and eventually maps with more than 100 jumps. Long routes need useful intermediate objectives and safe commits as well as feasibility. Free-start optimization can wait. The user knows general optimization and parkour; explain implementation choices precisely.

1. Read [CONTEXT.md](../../CONTEXT.md), [AGENTS.md](../../AGENTS.md) and [coding guide](../CODING_GUIDE.md). The [Minecraft Parkour Wiki mirror](../reference/mcpk/) records physics constants and update order.
2. Read [the current investigation state](angle-solver-investigation-current-state.md): discoveries, active changes versus production, exact fixtures, results and unresolved questions. This is the authoritative checkpoint, **562425f**. Earlier notes contain superseded policies.
3. Read [fixed-pattern primal/dual theory](solver-traces/angle-dual-feasibility-theory.md), then [existing failures and recoveries](angle-solver-existing-failures-and-recoveries.md) and [convergence and fallback provenance](solver-traces/closedform-convergence-and-fallbacks.md). These distinguish demonstrated failures from conjectures and preserve commit/PR evidence.
4. Use the framework and algorithm maps below while reading code. Propose the new solver's formulation, acceptance rules and comparison cases before implementing it.

## Reusable framework

Paths in this table are relative to `core/src/main/java/de/legoshi/parkourcalc/core/`; algorithm files are under `anglesolver/solver/` unless stated otherwise.

| Concern | Entry points and what to preserve |
| --- | --- |
| TAS → problem | `anglesolver/AngleSolverEngine.java`: `buildJob`, `buildPhys`, constraint mapping and `debugBuildSpec`. Reuse this conversion rather than reconstructing a different problem from logs. `anglesolver/AngleSolverState.java` holds UI settings. |
| Problem representation | `JumpSpec`, `JumpPhysicsInputs`, `JumpConstraint`, `JumpConstraintCompiler`, `Objective`, `ForwardPath`. Inputs include exact entry position/momentum/yaw, per-tick keys, sprint, ground/slipperiness and effects; read slicing and carried state carefully. |
| Continuous movement | `JumpLinearModel`: affine coefficients for a fixed per-axis zeroing pattern, continuous forward simulation and pattern discovery. `Constants` defines movement parameters. |
| Minecraft replay | `ExactJumpModel`, `McSineTable`, and facing conversion in `JumpPhysicsInputs`. Preserve the distinction between continuous angles and game float/sine-table execution. |
| Authored/block constraints | `anglesolver/Constraint`, `TickConstraints`, `ConstraintDeriver` and engine mapping. `ConstraintDeriver` converts selected block faces/AABBs into player-center walls with the correct player size. Read [block-solver findings](block-solver.md) before trusting block-only support generation: a bounding box around a union can admit paths through gaps. |
| Save files and fixtures | `save/SaveIO`, `save/SaveFile`, `ui/InputData`; test harness `core/src/test/java/de/legoshi/parkourcalc/anglesolver/harness/Fixtures.java`, including `buildBoxes`. Authored tick constraints and automatic block-only route planning are different inputs. |
| Existing acceptance diagnostics | `WindowCandidateValidation`: per-row continuous/replay margins and inertia consistency. Current closed-form window acceptance uses whole-window actual-inertia continuous validation plus committed-prefix game replay. SLP was restored to full-window game replay acceptance. Choose and document acceptance for the new method explicitly. |

**Indexing and mechanics:** `pos[t]` is the state before input `t`; input `t` produces `pos[t+1]`. The 1.8 inertia threshold check separately zeroes each incoming X/Z momentum whose absolute value is below `0.005`, before acceleration. Incoming momentum is not that tick's displacement. Fixed-pattern coefficients alone do not prove that a proposed path obeys that pattern; evaluate velocities at the checks. Use the phrase “inertia threshold check.”

## Existing optimization methods: code and explanations

| Method | Implementation and background |
| --- | --- |
| “Closed form” / dual chain | `ClosedFormSolve`, `CostateDualSolver`, engine `dualChain`: iteratively optimize wall multipliers, recover yaw from per-tick costates, infer inertia outcomes, and retry fixed patterns with threshold bands. It is not a one-shot formula. Read [theory](solver-traces/angle-dual-feasibility-theory.md), [Newton audit](dual-newton-iteration-audit.md), [Hessian notes](dual-newton-hessian-handoff.md). Actual numerical step is damped Cholesky Newton plus projected-gradient fallback; the truncated-eigen class description is stale. |
| Facing structure | `FacingPrefold`, `YawTies`, `YawOrder`, `YawOrderDp`: eliminate pins/equality chains and handle supported one-way yaw order. These change the independent-control formulation; do not assume all facing constraints are supported by every method. |
| SLP | `SlpSolve`, `TrustRegionLp`: sequential linear programs in yaw within a trust region; replay-scored feasibility and objective phases, with inertia-aware local models/retries. It does not impose closed-form threshold-band rows in its LP. Read [pipeline notes](scratch-angle-pipeline-input-to-smooth.md) and [fallback provenance](solver-traces/closedform-convergence-and-fallbacks.md). |
| Relaxation recovery | `RelaxationRecovery`: disk-relaxed primal solve using augmented Lagrangian/FISTA, followed by angle recovery/seeds and SLP. A separate recovery method from the wall-multiplier Newton solve. |
| Homotopy / fold recovery | `RecoveryLadder`, `FoldReplayDriver`, `DiskSocpKernel`: continuation/recovery with folded controls, disk/chord geometry and replay/pattern updates. See [historical solver design](angle-solver.md) and the pipeline notes for eligibility and invocation. |
| Certified B&B | `CertifiedBnb`, `SineTableGeometry`, `GateFoldFinder`: branch on inertia outcomes, supported yaw-order overlap and yaw intervals; bound with convex relaxations/kernel or supported dual fallback; prune against incumbent/gap and enumerate small leaves. Consult the pipeline flowchart for bounds, splitting and stopping rules. |
| Older B&B | `BoundPrunedRecovery`: inertia hypotheses plus intermediate-position interval branching, per-pattern dual bounds and local recovery. Its branching and bounds differ from certified B&B. |
| Scheduling / long routes | `anglesolver/graph/BuiltinGraphs.java`, graph nodes and runner; `LongRunSolver` handles window selection, objective alternates and commits. Its windows do not automatically invoke both B&B solvers. See [graph design](solver-node-graph-design.md), [window failure patterns](angle-solver-window-failure-patterns.md), and the current-state record for experimental policies. |

[Historical solver design](angle-solver.md) and [pipeline notes with flowcharts](scratch-angle-pipeline-input-to-smooth.md) provide the broad landscape. They mix historical and current behavior: resolve discrepancies using current code and the checkpoint. PR descriptions/blame are already archived through the provenance links above; avoid repeating that research without a new question.

## Mathematical background and established limits

For a fixed inertia pattern and independent free controls, horizontal positions are affine in acceleration vectors `u[t]`. Keys/contact/effects prescribe each magnitude `m[t]`; yaw chooses direction. Position walls are linear, but `||u[t]|| = m[t]` is a circle equality, so this primal is nonconvex. Inertia adds discrete regimes and discontinuities.

Dualizing the walls yields a convex objective formed from costate norms. A costate is the objective's sensitivity to that tick's acceleration after subtracting priced constraint sensitivities. The circle and disk have the same support function; therefore this dual also describes a disk relaxation. **Convexity of the dual does not guarantee physical recovery or zero duality gap.** With an attained exact dual optimum and nonzero costates for every free positive-magnitude control, KKT does give feasible globally optimal full-magnitude recovery. At zero costates, directions must be coordinated using the trajectory constraints.

Tiny costates from objective/wall cancellation create smoothing and conditioning trouble. The existing smoothed derivative can use subphysical acceleration while decoding returns full magnitude. Stable internal controls are not a feasibility/stationarity certificate. More iterations, smaller epsilon or more precision alone do not solve true degeneracy. The measured Linkcraft failures have **not** established a positive optimum-value duality gap.

Primary theory and the applicability review:

- [Boyd/Vandenberghe, Convex Optimization](https://web.stanford.edu/~boyd/cvxbook/bv_cvxbook.pdf): duality/KKT and numerical Newton methods.
- [Boyd et al., Subgradients](https://web.stanford.edu/class/ee364b/lectures/subgradients_notes.pdf): nonsmooth maxima and nonunique inner solutions.
- [Luo/Echigo/Acikmese, Revisiting Lossless Convexification v2](https://arxiv.org/html/2410.09748v2): nonzero-costate sufficient conditions and discrete-time exceptions. Read [our theorem/provenance review](solver-traces/closedform-convergence-and-fallbacks.md); its assumptions have not been verified for arbitrary Minecraft walls/inertia.
- [Adaptive Smoothing Algorithms](https://arxiv.org/abs/1509.00106), and [our recovery-options review](costate-cancellation-recovery-options.md): possible approaches, not implemented fixes.

## Reuse the evidence, then design

- **Canonical route:** [archived real11 TAS](solver-traces/jump39-baseline-handoff-2026-10-24/real11-input.json.gz), 808 modeled ticks / 76 solver segments / 636 compiled scalar constraints. [All authored constraints](linkcraft-real11-all-jumps-input-constraints.md), [compiled rows, physics and boundaries](solver-traces/jump30-50-route-map-2026-10-24/README.md). Segment numbers need not match human jump counts.
- **Best focused numerical case:** [20–22 Z/max](solver-traces/jump20-22-zmax-2026-10-24/README.md) and [costate decomposition](solver-traces/jump20-22-zmax-2026-10-24/costate132-explanation.md). Cancellation, internal versus recovered controls, residuals, inertia checks and cap controls are preserved. `Jump20ZMaxProbeTest` reproduces it.
- **Acceptance counterexample:** [20–22 SLP replay-valid / continuous-invalid result](solver-traces/jump20-slp-replay-validation-2026-10-24/README.md), reproduced by `Jump20WindowComparisonProbeTest`. Threshold rounding changes the path; do not reject a game-valid path merely because another simulator follows a different regime.
- **Long-route baseline:** [staged-cap run](solver-traces/full-route-commit1-staged-caps-2026-10-24/README.md), 93.032 s, commits through 38 then fails at 39; [exact stopped-state handoff](solver-traces/jump39-baseline-handoff-2026-10-24/README.md). This predates restored SLP acceptance; no full-route rerun establishes current behavior. Earlier normal-margin success covered 30 jumps, not the full map.

Start by specifying the new method's variables, physical constraints, treatment of inertia, feasibility certificate, objectives and window interface. Compare on identical exact entry states and original constraints; keep feasible witnesses and separate solver failure from unreachable committed states. Objective placement near a turnaround or a bounding wall can be poor, and locally valid commits do not prove future feasibility. Automatic turnaround detection remains unimplemented.

## Workspace and experiment rules

Branch `codex/first30-margin-trace-bookkeeping`; recorded production baseline `02844f45872604b21d0d139736d3ae0163454156`. Current code contains experiments: changed epsilon/caps, disabled tiny-costate decoder substitution, intermediate retention, conditional inertia multiplier seeding, validation and objective-placement changes. The checkpoint inventories exact defaults versus probe options. Do not silently treat them as production or discard them.

Keep the user's unrelated modified `core/src/test/resources/captures/hpk/d9/j127_Head_to_Head_Neo.json` untouched. Read `git status` first. Run only relevant focused probes when requested, background Gradle with output to persistent files, and preserve each run under a new label. Log exact start state, window/objective/commit ticks, method, inertia assumptions versus actual checks, every rejected row and timing. Update notes and commit separate changes so experiments remain recoverable. Core must stay Java 8 compatible and use the existing deterministic math conventions.
