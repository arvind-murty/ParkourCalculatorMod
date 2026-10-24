# Plan: why similar angle solves converge very differently

Date: 2026-10-24. Scope: Minecraft1.8, fixed start, inputs through a continuous feasible path, with per-axis inertia threshold checks. Byte-exact conversion is considered only to distinguish an unrelated rejection cause. User request: research first; record potential leads before pursuing them; then flesh out each lead toward a holistic explanation. No solver changes or new tests/probes in this pass.

## Question and working standard

Why can a window with apparently generous spatial corridors and a known physical feasible path cause an extreme convergence miss, while a nearby window or larger margin returns quickly? Do not assume inertia is responsible or that the physical problem is intrinsically hard. Distinguish physical feasibility, feasibility of a particular tightened fixed-pattern affine subproblem, optimality of a disk relaxation, full-magnitude angle recovery, and the numerical path taken by the implementation.

## Phase 1: cursory leads recorded before deeper research

The following list was written after an initial source/history scan and a primary-source search. These are leads, not diagnoses.

| ID | Lead | Initial reason to investigate | Follow-up |
|---|---|---|---|
| P1 | Compare the exact same problem | Normal/fixed histories differ in seeds, rows, margins, epsilon, caps and pattern evolution; rounded seeds already changed outcomes | Build a comparability/evidence ledger from saved artifacts |
| P2 | Feasibility is being sought indirectly through an optimization dual | Code follows costates toward a coordinate extremum; it does not maintain feasible iterates or solve a pure feasibility problem | Inspect objective/recovery/acceptance and feasibility-restoration alternatives |
| P3 | Constraint ticks change coefficient geometry | Nearby state rows share long friction-weighted prefixes; moving a tick is not merely relabeling rows | Derive coefficient/Hessian geometry; inspect active-set rank, duplication and scaling leads |
| P4 | Margins can change the surrogate problem, not just safety slack | margin is subtracted from linear bounds; zeroing bands are much narrower than0.05 | Inspect opposing-wall and inertia-band feasibility, margin continuation and warm-multiplier provenance |
| P5 | Costate cancellation, smoothing and full-length recovery | Existing first-window and jump6 audits measured large recovery discrepancies | Separate exact degeneracy from numerical regularization/fallback; tie to primary LCvx theory |
| P6 | Newton/gradient globalization, damping and stopping | Current fixed miss persists after removing one early stop; line searches and Hessian solves have bounded retries | Inspect code and blame for numerical safeguards, stationarity criteria and unverified guarantees |
| P7 | Small inertia effects can change active-set geometry | Replay-derived fixed patterns cut coupling after a zeroing tick; previous extension probe had exactly identical coefficient vectors | Determine when physical perturbation becomes a large representation/conditioning change |
| P8 | Data scaling and floating-point sensitivity | World positions, row norms, huge multipliers and tiny tolerances coexist; rounded entry seeds have divergent solves | Check current centering/normalization and consult primary numerical solver documentation |
| P9 | Provenance of alternative solvers | SLP, relaxation recovery, pattern Bnb and CertBnb address historically different failures | Read commit/PR/issue records, avoid equating every miss with a duality gap |
| P10 | Independent convex oracle and primal certificates | A stalled dual cannot distinguish feasible, infeasible, recovery-degenerate and poorly conditioned cases | Specify a minimal same-data oracle workflow; inspect existing disk kernel rather than suggest replacement blindly |
| P11 | Holistic hypothesis ranking and discriminating future measurements | Several mechanisms coexist; previous parameter sweeps changed trajectories without isolating causes | Rank established facts versus plausible explanations; write a small ordered future experiment plan without running it |

## Initial source pointers

Repository: `solver/CostateDualSolver.java`, `ClosedFormSolve.java`, `JumpLinearModel.java`, `FacingPrefold.java`, `WindowCandidateValidation.java`; `docs/research/dual-newton-iteration-audit.md`, `dual-newton-hessian-handoff.md`, `angle-solver.md` sections2.1.1/10; saved `solver-traces/jump-six-*`, `initial-seven-*`, and `linkcraft-real11-jump-two-fixed-zmax-violations.md`.

Primary external leads: [Boyd/Vandenberghe optimization text](https://www.stanford.edu/~boyd/cvxbook/bv_cvxbook.pdf), [Luo/Echigo/Acikmese discrete LCvx](https://arxiv.org/html/2410.09748v2), [SCS equilibration](https://www.cvxgrp.org/scs/algorithm/equilibration.html), [SCS residuals and certificates](https://www.cvxgrp.org/scs/algorithm/index.html), [OSQP residuals and polishing](https://osqp.org/docs/solver/). These are conceptual references; a solver's documented behavior does not establish the same behavior in this implementation.

## Phase 2 status

Initial plan committed as `98113dd` before deeper research. P1–P11 are now expanded below as a research review. This completes the requested literature/code/history pass; diagnostic experiments remain explicitly proposed, not executed. All conclusions distinguish measurements already archived, algebra/code facts, historical reports and untested hypotheses.


## Research conclusion before the details

A physically feasible window and a numerically easy multiplier solve are different properties. This implementation seeks feasible angles indirectly by pushing a coordinate objective toward its constrained optimum and normalizing the resulting costates. The input-to-position map is affine for a fixed pattern, but its dual can contain flat multiplier directions, cancellation points, nearly dependent rows and abrupt changes in the chosen primal response. A small change in tick, right-hand side, margin or entry state can change which of these regions is visited without making the physical route appreciably harder.

The existing evidence supports a formulation/conditioning/recovery explanation for the class, not a blanket claim that the parkour is hard, that every miss is an unavoidable duality gap, or that inertia is always responsible. It does not yet isolate the dominant numerical mechanism of the current jump6 cold0.0003 miss. The first useful next step is to classify that exact affine subproblem independently and observe its numerical iterates, not add more undirected margin/cap sweeps.

### Evidence ledger: keep these cases distinct (P1)

| Evidence | What is actually established | What it does not establish |
|---|---|---|
| E1: current fixed6–12, Z/MIN,0.0003 | Stops at1264 input-stall, residual0.0394; continuous violation1.505305. All66 assumed inertia checks satisfied; assumed and actual continuous values agree to roughly1e-12 | No proof of infeasibility, dual optimality or true positive duality gap |
| E2: same case, input-stall disabled | Initial solve hits10000, residual0.207; discovers X63 zeroing; retry stops NO_DESCENT547, residual0.5352, continuous violation1.190397 | Removing one bad exit does not make the remaining algorithm converge |
| E3: SAME exact fixed entry, ordinary0.05-first ladder | Accepts first0.05 attempt with zero initial multipliers; full replay satisfies all53 original rows, minimum clearance0.034189 | Does not show that the0.05-tightened affine model is feasible or optimized; its returned affine X58 misses the requested0.05 buffer by0.000448329 |
| E4: SAME fixed entry, forced continuation to0.0003 | Warm ladder still fails (best continuous1.600640); cold ladder reproduces1.505305 | Multiplier reuse alone is not a demonstrated explanation for normal's success |
| E5: saved normal6–12 | Different exact entry,53 rather than52 retained rows, EPS2=1e-14, cap100; margin ladder then X63/X81 pattern succeeds at0.0003 | Its same accepted margin does not make it a same-data comparison with E1 |
| E6: historical earlier jump2 probes | Misses with minimum costate magnitudes0.128–0.172; major violations already in assumed affine model; all hit cap100 | Not every failure requires tiny costates or the fallback |
| E7: initial-window smoothing audit | At EPS2=1e-14, normalized recovery violated rows despite tiny dual residual; derivative control length and physical control length differed | Not automatically an explanation at EPS2=1e-24 or for every later window |
| E8: endpoint reconstruction | Using seeds rounded to12 decimal places changed jump6's solver outcome; sequential exact replay recovered the original trace | Shows implementation sensitivity; does not show physical feasibility changes at1e-12 |

Sources: [E1/E5](solver-traces/jump-six-window-endpoints-2026-10-23/README.md), [E2](solver-traces/jump-six-no-control-stop-2026-10-23/README.md), [E3/E4](solver-traces/jump-six-margin-continuation-controls-2026-10-23/README.md), [E6](solver-traces/linkcraft-real11-jump-two-fixed-zmax-violations.md), [E7](solver-traces/linkcraft-real11-initial-seven-dual-iteration-convergence.md). Read-only CSV re-analysis for the new buffer/duplicate observations: [saved-artifact-audit](solver-traces/convergence-research-2026-10-24/saved-artifact-audit.txt). No new solves were executed.

## P2 — Finding a feasible path is not the problem this fast path directly solves

**Code fact.** `ClosedFormSolve.runLadder` constructs an objective vector, minimizes the costate dual, recovers a single angle sequence from its returned costates, then validates it. It does not maintain a feasible trajectory while updating multipliers. The margin is a change to linear bounds; it is not a direct penalty minimizing physical constraint violation. A returned trajectory can therefore improve the dual bound while worsening physical feasibility. The initial-window cap audit actually recorded this: continuing from iteration100 to125 improved the dual but worsened the recovered path; the earlier candidate was not retained within that one dual solve.

**Why it matters.** A window may have many feasible interior routes while its coordinate-maximizing boundary solution requires very accurately coordinated directions, or has undetermined directions. We are asking the optimizer to solve that boundary/recovery problem as the way to find ANY feasible route. That is a stronger and sometimes less stable task than feasibility restoration.

**Primary lead.** Parshakova et al.'s [MARA manuscript](https://arxiv.org/pdf/2503.12221) directly studies the separation between dual progress and practical primal recovery. It generates multiple approximately optimal responses rather than a single exact response, then reduces coupling residuals. Its convex blending is not automatically legal here: averaging fixed-length accelerations can shorten them. AppendixB treats nonconvex agents with discrete response selection instead. This is a useful design lead, not a ready-to-port guarantee.

**Existing capability.** `SlpSolve.optimizeCentered` already supplies a primal phase1, but normally seeds it from another dual recovery. A poor dual direction can make that seed's yaw Jacobian unhelpful: d(u_x,u_z)/d(theta)=(-u_z,u_x), so for purely axis-aligned acceleration the same-axis position change is second order in a small yaw perturbation. A linear feasibility step may see little useful change. This is an algebraic possible failure of the seed/linearization, not a proven diagnosis of all recorded SLP misses.

**Verdict:** established mismatch between the feasibility goal and the fast path's optimization/recovery mechanism. Future feasibility-first or bounded-suboptimal recovery merits study; no claim that a new phase1 automatically solves fixed-length nonconvexity.

Sources: [ClosedFormSolve](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/ClosedFormSolve.java) `runLadder`; [SlpSolve](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/SlpSolve.java) seeding and LP row construction; E7.

## P3 — Moving constraints between ticks changes numerical geometry

For constant friction f, one acceleration at tick s contributes to position k with coefficient

    C(s,k) = 1 + f + ... + f^(k-s-1).

Moving that constraint one tick changes old-input coefficients by f^(k-s). Nearby position rows therefore share long, almost-identical prefixes. They can differ mainly in the small set of recent inputs that can alter velocity between the two states. With varying ground friction and a changed endpoint, the coefficients change again. This is not simply permuting a row label.

For the unsmoothed fixed-magnitude dual, write B_t as the matrix mapping multipliers to the two costate components. Its Hessian is

    H = sum_t (m_t/||g_t||) B_t^T (I - ghat_t ghat_t^T) B_t.

In two dimensions each tick contributes at most rank1. Directions changing only costate magnitude have zero curvature. Nearly dependent active rows give nearly flat directions in multiplier space. The count of active/free multipliers matters; total row count alone does not determine singularity.

**Concrete redundancy already present.** The saved jump6 spec contains both `X@87` and `X@87hi`, the identical upper bound X87<=732.699999988079. With the same coefficient row and RHS, transferring multiplier weight between these rows leaves both costates and the dual value unchanged where allowed by nonnegativity. That creates nonunique multipliers; whether both rows are in a particular iteration's free set is not logged. The compiler appends these rows rather than deduplicating them. This is established redundancy, not a measurement that it dominates the stall.

At a cold axis objective, costates initially point purely along that axis. Same-axis wall changes are then radial; their unsmoothed curvature can be zero even when the costates are nowhere near zero. This gives a concrete reason a seemingly easy axis-constrained solve may need careful handling of flat directions before Newton reaches a curved region.

**Verdict:** mathematically established structural mechanisms; actual spectrum, free-set rank and dominant rows of E1 remain unmeasured. The nearby-tick explanation should be verified using exported active rows, not inferred from map appearance.

Sources: [JumpLinearModel](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/JumpLinearModel.java) `coef`, `coefAxis`, `compileWalls`; [CostateDualSolver](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/CostateDualSolver.java) `buildHessian`; saved-artifact audit; [Boyd/Vandenberghe, Newton and conditioning](https://see.stanford.edu/materials/lsocoee364a/10UnconstrainedMin.pdf).

## P4 — Margin attempts can be mathematically infeasible even when the route is easy

**Established code chain:** `ClosedFormSolve.runLadder` compiles position rows, adds `lin.velocityWalls(0.005)` for assumed zeroing, and passes ALL those rows to `CostateDualSolver`. Its `solve` replaces every inequality RHS b_j by b_j-margin. There is no antipodal-pair cap in this fixed-start path. The similarly named "normalized" wall representation means inequality sign normalization, not unit-norm scaling.

For an assumed inertia zeroing, the two velocity rows are

    v <= 0.005 - margin
    v >= -0.005 + margin.

Thus any margin>0.005 makes that pair contradictory; margin0.005 collapses it to an exact velocity0 condition. The robust ladder contains0.05,0.02 and0.01 before lower rungs. A patterned pass that includes a nonconstant zeroing band can therefore try impossible surrogate problems, independently of whether the original positional route is generous.

There is an exact dual explanation for extreme behavior on these rungs. The two row normals are a and -a; raising both multipliers by K leaves EVERY costate unchanged. Their dual linear term changes by

    2*K*(0.005 - margin).

For margin>0.005 the dual decreases without bound along this direction while recovered controls remain unchanged. That is an infeasible tightened subproblem, not a positive duality gap of a feasible one. A control-change exit can return a finite Result long before the multiplier limit recognizes divergence. That vector is then reused at the next margin by the ladder. This is a concrete mechanism for enormous multipliers and bad warm starts without requiring a physically difficult route.

**Provenance:** `FreeStartSolve.jointLadder` DOES compute an analytic opposing-pair margin cap. Blame identifies e171cc52; [PR387 addendum3](https://github.com/Leg0shii/ParkourCalculatorMod/pull/387) and the [iteration audit section5](dual-newton-iteration-audit.md#5-precision-experiments-same-day-prompted-by-the-4x2-field-pair) explain why: slow-diverging infeasible margins were being mistaken for feasible margin-bisection results. That protection does not appear in fixed-start `ClosedFormSolve.runLadder`.

**Critical limit:** E1 is a single unpatterned0.0003 solve. It has neither a zeroing-band pair initially nor margin>0.005. This finding does NOT explain E1 by itself. It applies to other patterned ladder passes, including the forced continuation experiment's X63 retry.

**Acceptance semantics:** E3's nominal0.05 success validates original physical rows, not every row tightened by0.05. The saved assumed-model X58 clearance is0.049551671155:0.000448328845 short of the requested inward buffer. Its actual continuous minimum is0.034316913340. Therefore passing E3 does not certify optimization or even feasibility of its tightened affine surrogate. It still proves a real feasible original route from the fixed entry.

**Verdict:** contradictory patterned large-margin rungs and the missing fixed-start pair cap are established from algebra and code. Their contribution to each run's overall cost/trajectory remains to be measured. Do not remove the physical validator or call a good original-feasible candidate invalid just because the intermediate optimization surrogate failed.

## P5 — Cancellation makes the angle map much more sensitive than the physical dynamics

At fixed pattern,

    g_t = c_t - sum_j lambda_j A_jt.

If objective and active wall coefficients nearly cancel, recovering a direction requires resolving the small remainder of larger quantities. The direction map has transverse sensitivity proportional to1/||g_t||. A tiny change in multipliers or input data can rotate a nearly canceled costate substantially, selecting a very different physical acceleration.

**Established measurements:** an earlier extended jump6 retry had exactly identical Z89 and Z90 coefficients for27 controls because an assumed zeroing at82 erased their later velocity influence. A wall multiplier near1 canceled the objective over that block. The same-iterate audit found continuous violation0.05077 using the actual costate direction versus16.91609 with the raw-objective fallback. However the pre-recovery controls were already infeasible, so bypassing the fallback alone was not sufficient. [Exact earlier experiment](solver-traces/linkcraft-real11-jump-six-10k-zmin-recovery.md). This was the superseded extended-endpoint case, NOT E1.

**Smoothing tradeoff, derived.** With delta=sqrt(EPS2), the norm contribution has tangential curvature m/r and radial curvature m*delta^2/r^3, where r=sqrt(||g||^2+delta^2). For ||g|| much larger than delta, their ratio is about||g||^2/delta^2. Near g=0, peak curvature is m/delta. Sharpening EPS2 from1e-14 to1e-24 reduces recovery bias but increases peak curvature100000-fold and changes this ratio by ten orders at a fixed nonzero g. Better approximation and easier numerical optimization are not the same direction of change.

`FacingPrefold.expand` also switches to the raw objective direction at ||g||<1e-9. That is a separate recovery threshold, not delta. The solver can thus optimize one smooth control map and emit another full-magnitude direction map.

**Theory boundary:** the exact attained unsmoothed optimum with all free positive-magnitude costates nonzero has zero gap and feasible recovery; [proof and LCvx provenance](solver-traces/angle-dual-feasibility-theory.md). At a degenerate optimum, loss of unique recovery or an interior disk optimum can invalidate this route to angles. Infeasible recovered angles alone do not prove a strict optimal-value gap.

**Verdict:** cancellation/smoothing/recovery are measured failure mechanisms in this codebase. The actual minimum costates and fallback count for E1 are not currently archived, so they remain hypotheses for that specific stall. E6 demonstrates that large nonconvergence errors also occur with comfortably nonzero costates.

## P6 — The numerical method and its stopping rules are not protected by the paper

The implementation is damped projected Newton over a free multiplier set, with Cholesky, then a projected-gradient fallback. The header's truncated-eigen wording is stale. It uses one damping scale based on the largest Hessian diagonal and bounded retry counts. Inspecting the bodies gives8 damping retries,22 Newton line-search trials per retry, and40 gradient expansion/backtracking trials. A successful objective decrease is not a feasibility step. The code returns a Result at an iteration cap, no-descent or input-stall rather than requiring primal/KKT convergence.

**Established:** E1 input-stall happens at a residual far above tolerance, and E2 still fails after removing it. [PR385](https://github.com/Leg0shii/ParkourCalculatorMod/pull/385) recorded widespread capped solves and non-monotone outcomes when the cap changed. Those are historical observations at cap100, not present-cap statistics.

**Numerical leads requiring measurement:**

- Is the active Hessian nearly singular from redundant or radial wall directions?
- Does a few-tick near-zero costate make the largest diagonal so large that uniform damping slows every other direction?
- Does Cholesky lose positive definiteness from cancellation, and how often is damping raised?
- Is the finite Armijo search too restrictive after projection clips an unconstrained Newton direction? It computes directional slope using d while evaluating the projected candidate P(lambda+t*d); the projected displacement can differ substantially. This is a code-review lead, not a demonstrated bug or proof of nonconvergence.
- Are valid small decreases lost when the dual value sums and cancels large positive/negative terms? Strict comparisons can then report NO_DESCENT although residuals remain large.
- Do stable Hessian expressions agree with the implementation near radial alignment? Terms such as1-hat^2 and dot-hat_i*hat_j can suffer cancellation even when the symbolic Hessian is PSD.

Do not replace control-change with multiplier-change as another heuristic. The existing projected-gradient residual is the right stationarity diagnostic for this orthant-constrained dual. Better diagnostics should report it alongside physical primal feasibility, not call all exits convergence. Standard Newton convergence rates require regularity and suitable globalization; no verified rate or finite-iteration guarantee for this hybrid was found. [Primary Newton reference](https://see.stanford.edu/materials/lsocoee364a/10UnconstrainedMin.pdf).

**Verdict:** stopping and finite search behavior are established; which numerical bottleneck dominates E1 remains unknown. Simply trying another global cap/damping constant repeats the historical failure pattern without answering this.

## P7 — Inertia can be physically small but structurally large

A per-axis zeroing removes a small velocity. Its direct displacement effect is a decaying carry over later ticks, often a few hundredths of a block. That may be small compared with a pad width.

But the fixed-pattern matrix changes discontinuously: all earlier inputs lose their influence on later velocity along that axis while retaining an equal accumulated position offset. Different future position rows can acquire exactly identical early-input coefficients. New threshold-band rows are introduced as well. Their relation to the objective, matrix rank and active rows can change much more than the visible route.

This supplies a mechanism for a small physical threshold event producing a large change in the multiplier landscape. It does not imply every threshold event is important, or that the current miss is due to threshold mismatch. In E1 the affine assumed path and actual continuous path agree. In E3 the same comparison differs by up to0.015718118409 on rows, despite a valid original physical path. The two attempts are not solving/recovering identical surrogate trajectories.

The active-set pattern loop can also fail as discovery: an infeasible initial model may supply no useful candidate; a candidate can identify another pattern that worsens the objective/violation and terminates the loop; and forced zeroing without complementary survival constraints is not a complete enumeration of regimes. [PR393](https://github.com/Leg0shii/ParkourCalculatorMod/pull/393) and [PR397](https://github.com/Leg0shii/ParkourCalculatorMod/pull/397) document those omissions concretely.

**Verdict:** structural changes are mathematically/code established and previously measured; threshold mismatch is ruled out as the direct E1 replay discrepancy. Do not blame all difficult convergence on inertia.

## P8 — Scaling, precision and invariance tests are credible leads, not diagnoses

**What is already done correctly:** `JumpLinearModel.compileWall` subtracts the known start-position/initial-velocity contribution from each bound. World-coordinate values around1000 are not simply fed unchanged into every dual RHS. This reduces one obvious scaling risk.

**What is absent in this path:** `CostateDualSolver` copies wall coefficients/RHS directly; no row equilibration, multiplier preconditioning or explicit deduplication was found. Its projection uses an absolute unit step in the residual diagnostic and gradient fallback begins with an O(1) multiplier probe. These operations are sensitive to how rows are represented. Position rows and velocity-band rows have different coefficient norms and physical units; the same numeric margin applies to both.

**Existing alternative:** `DiskSocpKernel` scales each row using its largest coefficient/RHS magnitude before solving, and maps multipliers back. It also has numerical caps and failure heuristics; it is not automatically an independent correctness oracle. General solver practice is consistent with this lead: [SCS equilibration documentation](https://www.cvxgrp.org/scs/algorithm/equilibration.html) explicitly treats scaling as important and converts residuals back to original coordinates. [SCS stopping criteria](https://www.cvxgrp.org/scs/algorithm/index.html) track primal residual, dual residual and gap rather than small iterate change. These are methodological references, not guarantees about this code.

**Precision evidence:** E8's rounding sensitivity is already demonstrated. A robustly feasible physical path need not change discontinuously with a1e-12 entry perturbation, while a degenerate multiplier/angle recovery and its branch decisions can. This distinction argues for numerical diagnosis, not a claim that Minecraft's feasible set moves by blocks at that precision.

**Additional lower-priority lead:** prefix products/sums compute C(s,k) as(sPre[k]-sPre[s])/fPre[s]. Extremely late ticks can suffer subtraction cancellation when prefix sums saturate. Current receding-horizon windows are much shorter than the whole808-tick route, so long-route prefix underflow is not an established explanation here. Any replacement must preserve represented physics; compare against a stable recurrence only after quantifying error.

**Verdict:** lack of equilibration/deduplication and rounded-seed sensitivity are established; current row-norm spread and floating-point loss in E1 are unmeasured. Scaling changes must preserve physical margins (scale the already-tightened row consistently) and circle/cone geometry; blindly rescaling X and Z independently changes the acceleration constraint.

## P9 — Provenance: recovery exists because the failure modes differ

| Record | Relevant observation | Why it matters here |
|---|---|---|
| [Original closed-form commit8263d65c](https://github.com/Leg0shii/ParkourCalculatorMod/commit/8263d65c20464d73ed574ad5d38c1a9d7a3f12ed), PR122 | Tiny per-wall dual, fast empirical convergence, exact inner maximization used to claim zero gap | Expectations originated as a fast-path formulation; not a general numerical convergence theorem |
| [June11 design record2.1.1](angle-solver.md#211-the-duality-gap-on-cross-seam-coupled-windows-and-the-slp-closer) | j021's small dual residual coexists with recovered infeasibility; SLP introduced | Convergence and primal recovery were already separate |
| [Issue204](https://github.com/Leg0shii/ParkourCalculatorMod/issues/204), [PR209](https://github.com/Leg0shii/ParkourCalculatorMod/pull/209) | Continuous recovery violations and inertia-model mismatch measured separately; disk/primal recovery and pattern B&B added | Lattice precision was not the primary cause of those misses |
| [PR385](https://github.com/Leg0shii/ParkourCalculatorMod/pull/385) | Many unconverged dual calls, unsafe simplifications/constant sweeps, deterministic non-monotone outcomes | Merely increasing/decreasing work is not a reliable repair strategy |
| [PR387](https://github.com/Leg0shii/ParkourCalculatorMod/pull/387) | Smoothing bias versus conditioning window; infeasible margin bisection fixed analytically | Representation and infeasible surrogate attempts can matter more than physical corridor width |
| [PR389](https://github.com/Leg0shii/ParkourCalculatorMod/pull/389), [PR390](https://github.com/Leg0shii/ParkourCalculatorMod/pull/390) | A fresh witness defeated a witness-fitted ladder; another miss was a silently caught indexing crash | Historical "solver hard" explanations also included ordinary implementation bugs and fragile fits |
| [PR393](https://github.com/Leg0shii/ParkourCalculatorMod/pull/393), [PR397](https://github.com/Leg0shii/ParkourCalculatorMod/pull/397) | Necessary one-tick zeroing and survival branches absent from candidate generation | Pattern completeness is independent of convex convergence |
| [Issue422 binding plan](https://github.com/Leg0shii/ParkourCalculatorMod/issues/422#issuecomment-5428709916), [PR427](https://github.com/Leg0shii/ParkourCalculatorMod/pull/427) | Recovery floors, interior controls, basin trapping; CertBnb added; older recoveries retained | Global search addresses unrepresented choices and certification, not only impatience with Newton |

**Source quality warning:** some older prose overstates its theoretical conclusion. In particular `nix-setup-tick-dual-failure.md` records valuable controlled feasibility witnesses, but its statement that a redirect cannot be represented because it is neither an objective direction nor an individual wall normal is not a general theorem. Weighted combinations of the wall gradients can point in other directions, and downstream walls already enter earlier costates through the friction map. Keep the measured success/failure asymmetry; do not import that geometric explanation as proof. Likewise a failed recovered candidate does not itself establish a strict optimal-value duality gap.

[Prior provenance archive](solver-traces/closedform-failure-provenance-2026-10-23/), [new source archive](solver-traces/convergence-research-2026-10-24/). These historical cases inform hypotheses; none was rerun in this research pass.

## P10 — Independent problem classification before choosing a new algorithm

A useful future workflow should export ONE exact subproblem, not compare whole routes with changing seams:

1. Record exact-bit entry state, inputs, friction schedule, objective, fixed zeroing mask, threshold-band rows, original b and tightened b, all folds, and any survival-sign constraints. Include a data hash and distinguish original physical rows from surrogate additions.
2. Solve a **phase1 disk SOCP**, minimizing a scaled maximum violation with ||u_t||<=m_t. Separately solve its coordinate objective if phase1 is feasible. Use an independent solver with residual/certificate checks; SCS supports second-order cones and primal/dual diagnostics. OSQP is a QP solver, not an exact SOCP replacement—its residual/polishing documentation is conceptual only.
3. Compare with the in-repo `DiskSocpKernel` on the SAME exported rows as a second implementation. Check the original unscaled residuals independently; do not trust status names, a decreasing barrier parameter, or a large-multiplier heuristic as a proof.
4. Inspect ||u_t||/m_t. If a well-converged disk solution saturates all positive-magnitude controls and satisfies pattern consistency, a physical fixed-pattern solution is directly available. If some controls are interior, that relaxation needs constrained full-magnitude recovery; this does not alone prove no feasible full-magnitude path exists or a strict objective gap.
5. Feed an actual known physical witness, if available, through the fixed mask and tightened rows. A physical0.05 candidate can satisfy originals while failing those added rows; E3 already illustrates the distinction. This is the right place to determine whether the affine subproblem is simply the wrong target.

No new external runtime dependency is proposed for the shipped mod. The oracle is a diagnostic tool. Existing `RelaxationRecovery` and `DiskSocpKernel` should be understood before designing a replacement; both have their own iteration/regularization choices. A trusted convex phase1 can classify the relaxed subproblem, not prove general nonconvex infeasibility if the disk is feasible.

**Verdict:** high-value discriminating next step; deliberately not executed in this research-only pass.

## P11 — Ranked investigation and a small future experiment plan

### Ranking by present evidence

| Priority | Lead | Evidence strength | Applicability to current E1 |
|---:|---|---|---|
|1| Poor dual convergence/globalization interacting with flat or dependent rows | E1/E2 residuals, current code, PR385; duplicate row confirmed | High; dominant numerical detail not identified |
|2| One-response optimal-direction recovery used as a feasibility finder | Established architecture, E3 physical witness, primary primal-recovery literature | High; explains why an easy feasible route need not be found by this path |
|3| Tightened-model feasibility and harmful warm starts | Exact zeroing-band contradiction, existing free-start fix | High for patterned larger-margin retries; does not directly explain unpatterned0.0003 E1 |
|4| Cancellation/smoothing/fallback near weakly determined controls | Earlier same-iterate measurements and exact geometry | Plausible for E1, not yet measured there; disproven as sole explanation for E6 |
|5| Scaling/numerical sensitivity and redundant representation | No equilibration, duplicate row, E8 | Credible; need row/curvature/step diagnostics before assigning blame |
|6| Missed inertia regime or actual pattern mismatch | Many historical witnesses | Direct pattern mismatch ruled out for E1; may matter in other attempts |
|7| Intrinsic physical infeasibility or an unavoidable true objective gap | No certificate for E1; E3 proves original physical feasibility | Do not use as the default explanation |

### Future measurements, in order—not run yet

**V1. Export and classify E1 and the nominal0.05 attempt.** Same-data phase1/objective SOCP, original/tightened rows separated; compare in-repo versus independent solver, report certificates/residuals and physical witness margins. First determine whether we are trying to optimize a feasible affine surrogate.

**V2. Explain the actual numerical stop.** Trace ONLY the exact E1 trajectory and its first problematic region. Required fields: D and its separately accumulated terms; raw/projected gradient; lambda norms and per-wall active set; costate norms/directions and fallback counts; derivative-control, full-magnitude fixed-pattern and actual-continuous residuals; Hessian diagonal range/effective rank/linear-system residual; damping, step norm before/after projection, accepted step and line-search rejection cause; per-row margin and threshold velocities. Log enough iterations around stalls/line-search failures rather than dumping an entire map.

**V3. Audit derivatives and algebra at saved iterates.** Directional gradient checks against D, Hessian-vector checks against the gradient away from nondifferentiable/active-boundary cases, compare stable norm-Hessian formulas and finite-precision evaluations. Repeat selected checks in higher precision to distinguish implementation mistakes from conditioning. These are future focused checks, not an instruction to run a full suite now.

**V4. One representation-preserving experiment at a time.** Start with duplicate-row removal preserving physical rows, then positive row equilibration, then appropriate variable preconditioning if needed. Map multipliers/warm starts and margins consistently; compare projected residuals in original units. Same feasible set should not become impossible merely through relabeling/scaling in exact arithmetic, but an imperfect algorithm can change; do not interpret one successful perturbation as a general fix.

**V5. Cap impossible margins analytically and scope warm starts.** Extend the existing opposing-pair reasoning to the fixed-pattern ladder as an experiment; never tighten a0.005 zeroing band past its half-width. Do not carry an iterate from a certified-infeasible tightened problem as if it were a solved warm start. Measure this on patterned retries separately from E1, since E1 initially has no velocity-band rows.

**V6. Test feasibility recovery separately from optimal-bound recovery.** Try a direct primal phase1 from a nondegenerate seed, or a bounded-suboptimal response recovery guided by the dual while accepting real full-magnitude controls. Start from existing SLP/relaxation capabilities. For zero costates, coordinate directions jointly instead of independently choosing the raw objective. A local method can still fail; a passing case must be replayed under the actual threshold checks. Compare feasible success/time and retained constraints before asking for full objective precision.

Only after this classification should broader route experiments or implementation changes be selected. Retain the fixed0.0003 policy when comparing optimized commits; larger margins can remain controlled candidate-generation evidence without silently replacing that user requirement.

## Holistic answer

The physical routes can be easy while the representation chosen by this fast path is fragile. Most ticks and constraints look similar in map space, but their *active coefficient geometry* need not be similar: moving a bound changes the effective authority of early/late ticks, which constraints bind, and whether objective and multiplier forces cancel. A fixed magnitude converts those forces into directions; normalization can amplify a small algebraic change into a large angle change. A dual-bound method does not promise feasible iterates, and optimizing into a boundary can require much more price accuracy than merely producing a good interior route.

The implementation then adds sources of variability: unscaled redundant rows, global damping, finite projected searches, early exits that are not stationarity certificates, and a margin policy that can create contradictory inertia bands in patterned passes. Continuation changes the numerical path and the selected pattern, sometimes helpfully and sometimes harmfully. Actual inertia changes can alter matrix coupling as well as trajectory carry. These mechanisms are sufficient to explain why "same shape, different ticks" need not mean similar numerical convergence, without attributing every miss to Minecraft discontinuities or worst-case nonconvex hardness.

For the current E1, we know the route is physically feasible from the same entry (E3), the small-margin candidate is already infeasible before any Minecraft conversion, its assumed inertia checks hold, and its dual is not converged. What remains unresolved is whether its particular tightened affine model is feasible and which numerical/recovery mechanism is preventing convergence. That is a sharply defined research question now; the plan above avoids treating another cap/margin win as its answer.

## Source and work log

- Initial leads committed in `98113dd` before this expanded review.
- Code inspected read-only at branch state following `0f72262`; current cap10000/EPS2=1e-24. Temporary isolated experiments from earlier turns are historical evidence; none was run again here.
- Read primary LCvx literature, standard Newton material, official SCS/OSQP documentation and the MARA primal-recovery manuscript. The numerical solver references suggest methods of diagnosis, not a proved performance guarantee for Minecraft or a proposed library migration.
- Queried upstream PR bodies with `gh`; prior archived PR122/209/385/393/397/427/447 and issue204/422 provide history; new PR387/389/390 bodies and damping/margin-cap blame are archived with the read-only CSV audit.
- Preserved historical context and identified overclaims rather than treating all old prose as a theorem. No production changes, test execution, benchmarks, new fixtures or full suite runs. The user's modified j127 fixture was untouched.

## Follow-up: reuse existing findings before new numerical work

The [existing failure/recovery catalog](angle-solver-existing-failures-and-recoveries.md) maps the old documents, measured failures, successful workarounds, obsolete approaches and current code eligibility. In particular, `RelaxationRecovery`, `FoldReplayDriver` and `WallHomotopyLadder` already provide alternative primal/fold/continuation paths. Nonterminal long-run windows currently do not run that full recovery portfolio. Compare those existing paths on the exact failing window before creating another solver; preserve retained rows, margin policy and shared acceptance checks. An earlier unconditional B&B window rescue was reverted because it missed its target and risked stalling velocity-finder callers. See the catalog for source details and limits; no new experiments were performed for this follow-up.

## Updated priority: general window-sensitive failure patterns

User steering on 2026-10-24 prioritizes research/investigation across solve families; jump6 is one facet, and making that exact window solve is secondary. The [window-pattern review](angle-solver-window-failure-patterns.md) now leads the next research phase. It recovers ARCH-2 falsification/M0/M1 comments, separates numerical tractability from physical continuation and recovery coverage, and distinguishes static warnings from candidate-dependent evidence. No new recovery comparisons or nearby-window experiments were started after this steering. The exact jump6 comparison remains an example within a future matched-case study, not the sole objective.

## Follow-up result: preserve the adjoint response when changing patterns

[Multiplier-seeding experiment](solver-traces/inertia-multiplier-seeding-2026-10-24/README.md) identifies a concrete avoidable failure in shifted end9/end10 windows. Naively carrying positional multipliers does not preserve angles after the affine coefficients change. Adding compensating prices on the new opposing inertia-band rows preserves costates, hence the prior angle response; both shorter inertia-aware retries then converge in12 iterations and pass continuous and full replay checks. Longer end14/end15 retries remain unresolved. This is test-side evidence, not a production default change or a complete solution of the original end12 case.
