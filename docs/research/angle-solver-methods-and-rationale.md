# Existing angle optimization methods and why they exist

2026-10-24. Source review at `91a9cca`, using current code, Git blame, introduction commits, archived issue/PR bodies, and freshly fetched PR descriptions. No numerical experiments or tests were run. The current task is the initial fixed-start yaw solve, with inertia threshold handling deferred. Later recovery, discrete search and route scheduling are enumerated for context, not added to the current scope.

## 1. Formulation that motivated the methods

With authored keys, contact schedule and entry state, and without inertia threshold checks, the existing continuous horizontal model is affine in per-tick rotated acceleration vectors. Each vector has a prescribed magnitude and a free direction. Position constraints become linear walls in those vectors; the fixed magnitudes remain nonconvex circle equalities. Sprint-jump contributions must come from the existing model, rather than a replacement assumption about the direction of thrust.

This supports three useful representations: wall multipliers, actual yaw variables, and relaxed acceleration vectors in disks. The fourth numerical implementation below solves the disk problem through its cone dual. These representations explain why multiple methods coexist even before inertia is introduced.

The original design's unconditional lossless-convexification claims are too strong. Circle and disk support functions agree, so the wall dual is also the disk relaxation's dual. Nonzero costates at an attained exact dual optimum provide a sufficient physical-recovery condition; zero costates need coordinated direction choices. A failed recovery does not alone establish a positive optimum-value duality gap. See [theory](solver-traces/angle-dual-feasibility-theory.md) and [applicability/provenance review](solver-traces/closedform-convergence-and-fallbacks.md).

## 2. Core continuous numerical methods

### 1. Wall-dual optimization: damped projected Newton, with projected-gradient fallback

**Code:** `ClosedFormSolve`, `CostateDualSolver`, `JumpLinearModel`.

**Variables:** one multiplier per compiled wall, unrestricted for equalities and nonnegative for inequalities. Costates combine the objective sensitivity and priced wall sensitivities. For fixed multipliers, the full-magnitude maximizing direction is analytical wherever the costate is nonzero. The multipliers themselves require iterative optimization; “closed form” does not mean a one-shot solve.

**Why this formulation:** exploit the affine trajectory map and separable support function instead of searching every yaw blindly. It gives an optimistic objective bound for the corresponding model and often a useful candidate from the same computation. This is the original fast path.

**Why Newton plus gradient:** code documents curved directions alongside locally flat or nearly flat multiplier directions. The implementation solves a damped free-set Hessian system by Cholesky, adjusts damping, projects multiplier candidates and line-searches. Gradient fallback provides a direction when the Newton trial cannot decrease the objective. This rationale is documented in the original code; it is not evidence of a measured comparison against every alternative optimizer.

**Provenance:** blame attributes the damped-Cholesky implementation to `9a22bdb`, [PR #122](https://github.com/Leg0shii/ParkourCalculatorMod/pull/122). That PR has an empty description, so the rationale comes from code/design records, not a nonexistent PR explanation. The class header still says truncated-eigen Newton; the actual method is damped Cholesky Newton.

**Limits:** finite caps, smoothing and line searches need not reach dual stationarity. Small recovered-control changes do not certify feasibility. Near-zero costates can make derivative-implied controls shorter than physical controls and recovery unstable. [PR #385](https://github.com/Leg0shii/ParkourCalculatorMod/pull/385) measured capped solves as load-bearing, with non-monotone downstream outcomes under different caps. Current branch cap/epsilon experiments differ from production; consult [current state](angle-solver-investigation-current-state.md).

### 2. Direct yaw optimization: trust-region sequential linear programming (SLP)

**Code:** `SlpSolve`, with `TrustRegionLp` solving each local LP.

**Variables:** per-tick yaw changes, folded where facing constraints require it, plus a worst-violation variable in the local LP. Linearize trajectory walls with respect to yaw, restrict the size of the yaw step, solve, evaluate the proposed path, and adjust the trust region. Phase 1 restores feasibility/clearance; phase 2 improves the requested objective.

**Why:** coordinate actual full-magnitude directions when independently decoding costates does not produce a feasible trajectory. This addresses a continuous-space recovery problem, not just sine-table rounding. The original j021 record reports a dual residual around `5e-9` yet a recovered miss around `0.34` blocks, persisting after 20,000 iterations. Same-axis objective/wall cancellation motivated alternate-objective seeds while still optimizing the user's original objective.

**Provenance:** introduced by `6d5b3b7` (June 11); expanded by `b13bce0` (June 12). The introduction diff explicitly describes a post-failure closer. Historical wording calls it a genuine duality-gap closer, which exceeds what the recovery failure alone proves. See [existing failure review](angle-solver-existing-failures-and-recoveries.md).

**Why LP and trust regions:** affine wall sensitivities give linear local constraints; bounded yaw steps limit the error of the trigonometric linearization. The current custom bounded-variable simplex, `TrustRegionLp`, entered the squashed `538c92b` cleanup lineage. Earlier versions used Apache Commons Math simplex. This is an inner numerical solver, not a separate trajectory formulation. [PR #382](https://github.com/Leg0shii/ParkourCalculatorMod/pull/382) records a reverted faster equivalent LP formulation that changed downstream trajectories; [PR #397](https://github.com/Leg0shii/ParkourCalculatorMod/pull/397) later showed that loopmm's decisive landing difference was an unmodeled inertia survival event, correcting the simpler lucky-LP-vertex explanation. Other SLP quality sensitivity remained.

**Limits:** a local method with seed/trust-region/budget dependence. It scores candidates using game replay even when derivatives come from a continuous model. Current full-window replay acceptance was restored in `2a218a0`; continuous disagreement alone must not invalidate a game-feasible result. That production behavior is distinct from the acceptance policy we will choose for a new inertia-free initial solve.

### 3. Primal disk relaxation: augmented Lagrangian with projected accelerated gradient (FISTA)

**Code:** `RelaxationRecovery.relaxedPrimal`.

**Variables:** two acceleration components per tick, with magnitude at most the authored magnitude. Wall multipliers and penalties are updated outside the projected accelerated-gradient inner loop. Projection onto each disk is inexpensive. Warm dual restarts, margin ladders, projection/dither decoding and SLP surround this numerical solve.

**Why:** obtain an actual relaxed trajectory when the production dual's directional recovery or early stopping gives an inadequate seed. The design explicitly states that production dual solves stopped too early, so this separate primal solve was added. Relaxed controls expose interior-magnitude requirements instead of immediately forcing every direction onto a circle.

**Provenance:** blame traces the FISTA updates and disk projection to `d35c286`, [PR #209](https://github.com/Leg0shii/ParkourCalculatorMod/pull/209). The PR body only says “Closes #204”; [issue #204](https://github.com/Leg0shii/ParkourCalculatorMod/issues/204) and design section 10.1 contain the actual motivation. The issue separated bound computation, continuous feasible recovery, and Minecraft realization. Its loopmm example had about `0.245` blocks of continuous wall violation before rounding.

**Why this numerical algorithm:** disk projection and smooth penalized wall terms fit projected first-order updates. That is a structural explanation supported by the code. This review found no controlled FISTA-versus-interior-point bake-off establishing it as the best algorithm for our new initial solve.

**Limits:** disk-interior acceleration is not executable with fixed full-strength keys. Decoding and SLP are separate recovery operations. This is not the deleted ALM+BFGS angle solver described below.

### 4. Disk/cone relaxation: log-barrier interior-point Newton

**Code:** `DiskSocpKernel`, used by `FoldReplayDriver` and certified search.

**Variables:** wall multipliers plus per-control cone epigraph variables. A decreasing barrier parameter enforces cone interiors and appropriate multiplier signs. Newton directions use a Schur-complement system and Cholesky, with line search. The emitted result includes relaxed controls and bound information.

**Why:** the ARCH-2 replacement needed a disk trajectory, chord-restricted subproblems and bounds, rather than only full-magnitude costate decoding. The [binding issue #422 plan](https://github.com/Leg0shii/ParkourCalculatorMod/issues/422#issuecomment-5428709916) reports interior controls and persistent full-modulus recovery errors, motivating a disk-solve/replay driver plus global search.

**Provenance:** blame attributes the barrier/Newton loop to `be584ed`, [PR #427](https://github.com/Leg0shii/ParkourCalculatorMod/pull/427). The driver, kernel and certified search arrive together in the squashed history. The PR reports certified small captures, loose bounds on larger ones, and retention of both older recoveries because they remained load-bearing.

**Limits:** a more accurate disk solve still does not impose circle equalities. The issue plan supports the need for the disk/chord machinery; it does not establish interior-point Newton as universally superior to AL-FISTA or the existing wall-dual Newton implementation.

## 3. Other existing optimization and search mechanisms

| Mechanism | What it does and why it was added | Provenance / qualification |
| --- | --- | --- |
| Level-set objective ascent | Add a movable goal wall and bisect between a feasible witness and an optimistic target, using SLP to seek feasibility. Helps when direct objective ascent stalls despite having a feasible seed. | `d88bc36`, [PR #339](https://github.com/Leg0shii/ParkourCalculatorMod/pull/339): j003 X/MIN around -27 versus -31.3; 72 capture/direction improvements reported. Failed local feasibility shrinks a search interval heuristically; it is not an infeasibility proof or a certified bisection bound. |
| Fold/replay fixed-point driver | Solve the disk model, decode/replay, update the assumed inertia pattern and local corrections, then retry. | `be584ed`, #422/#427. Addresses model/replay consistency and recovery; deferred for our current scope. |
| Wall homotopy | Solve relaxed wall RHS values `0.05, 0.01, 0.002, 0`, carrying the previous result forward. | `WallHomotopyLadder`, `be584ed`, #422/#427. Motivated by cold discovery in very narrow corridors; intermediate relaxed paths do not establish original feasibility. |
| Pattern and position-cell B&B | Enumerate inertia hypotheses, branch on intermediate position cells, bound and run local recovery. | `BoundPrunedRecovery`, `d35c286`, #204/#209; #393 adds single-tick patterns, #397 complementary survival hypotheses. Distinct from certified yaw-interval search. |
| Certified yaw/lattice B&B | Split yaw intervals, inertia alternatives and supported facing-order overlaps; bound relaxations; seek incumbents; enumerate small leaves. | `CertifiedBnb`, `be584ed`, #427; [PR #447](https://github.com/Leg0shii/ParkourCalculatorMod/pull/447) adds explicit lattice intervals and leaf handling. Needed because local solvers cannot guarantee basin discovery or global objective quality. Deadline expiration can leave a loose gap. |
| GateFoldFinder | Search near-threshold pattern basins, with steered SLP and lattice polish. | #447. A targeted inertia discovery mechanism, deferred. |
| Coordinate/bucket ascent and ILS | Locally improve game-replayed yaws; perturb and reclimb to escape a local family. `BucketAscentPolish`, `IlsPolish`. | ILS introduced in `be18592`; design section 16 records gains beyond local polish. Current climbs use bucket polish after CMA removal. No global guarantee. |
| Wrap-window ILS | Search equivalent yaw wraps and nearby realizable facings, accounting for float accumulation. | `WrapWindowIls`; #382 documents correcting its scoring to match reaccumulated execution. A realization/near-miss mechanism, separate from the continuous initial solve. |
| Leaf snapping/enumeration | Try small neighborhoods of realizable sine-table controls around a feasible seed. | `LeafSnap`, #427, replacing `SphereDecodeSnap`. Local enumeration cannot repair an arbitrarily distant continuous miss. |
| Setup/run-up facing sweeps | Enumerate selected early facings and solve the remainder, altering seeds/basins. | `SetupPeelNode`, `FacingStepNode`; [nix setup-tick record](nix-setup-tick-dual-failure.md) and `2d25661` (#469). Search wrappers around existing solves. |
| Receding horizon, multi-start, margin ladders, alternate objectives | Decompose large routes or diversify calls to existing optimizers. | `LongRunSolver`, `SeedSweepNode`, `RecoveryLadder`. Scheduling/seed policies, not additional numerical optimization algorithms. Long-route commits and free-start search are outside the current task. |
| Facing folds/order DP; smoothing | Fold equalities/pins, use chain structure for supported facing orders; separately smooth a solved path under feasibility/objective checks. | `YawTies`, `FacingPrefold`, `YawOrderDp`, `SmoothingPolish`. Constraint structure and a secondary objective, not replacements for the initial position solve. |

## 4. Retired methods that older notes still mention

- **CMA-ES:** formerly the derivative-free multistart/race and inner climb. [PR #375](https://github.com/Leg0shii/ParkourCalculatorMod/pull/375) records removal after kill-audits, deterministic replacements and explicit capture dispositions. Removal was an engineering decision supported by the then-corpus, not a proof that evolutionary optimization cannot help. Current ILS should not be described as running CMA climbs.
- **ALM+BFGS plus snap:** `AlmBfgsCore`, `SmoothJumpProblem`, `FacingReconstruction`, `SnapRepairPolish`/`AlmSnapStage` were implemented, moved out of production and deleted through #379/#382. They are not an untried suggestion. Their removal does not remove the separate, live AL-FISTA disk solve.
- **Other deleted recoveries:** #427 removes `GateMip`, `ResidualRescue`, `SeamSweepRecovery`, `SphereDecodeSnap`. Do not enumerate them as current production stages.

## 5. What this establishes for our initial solve

The relevant comparison is between formulations as well as algorithms: wall-dual Newton, direct-yaw SLP, primal disk AL-FISTA, and cone-dual barrier Newton. There are two separate questions: how accurately/quickly does a method solve its mathematical subproblem, and does its output satisfy the original fixed-magnitude trajectory constraints? Solving a convex relaxation accurately answers only the first.

The history explains why the stack accumulated: cheap bounds and easy recoveries first; direct yaw when normalization failed; explicit disk trajectories when dual recovery was inadequate; continuation and global/discrete search when local recovery or realization remained insufficient. It does not justify copying the entire stack into the new initial solve, nor prove that these are the best numerical choices for that scope.

## 6. Evidence and audit trail

Fresh [PR JSON and blame excerpts](solver-traces/optimization-method-provenance-2026-10-24/) preserve #122, #339, #375 and the four numerical implementations. Earlier [failure-provenance archive](solver-traces/closedform-failure-provenance-2026-10-23/) preserves #204, #209, #385, #393, #397, #422, #427 and #447; [recovery archive](solver-traces/existing-recovery-research-2026-10-24/) preserves #379/#382. PR bodies describe historical intent and reported results; no old benchmark was rerun in this review.

Git commands used: path-filtered `git log --all --reverse`, `git blame` at the actual update loops, `git show 6d5b3b7` for the SLP introduction, and `git show -s` for introduction metadata. PR #122's empty description is preserved explicitly. No unrelated capture edits were staged.
