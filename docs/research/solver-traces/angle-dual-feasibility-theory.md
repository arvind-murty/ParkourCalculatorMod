# Why dual angle candidates can fail continuous validation

Scope: pinned start, one fixed inertia pattern, coordinate objective, affine position/threshold rows, independent yaw variables after valid facing folds. This is the closed-form subproblem, not the entire nonsmooth Minecraft route.

## Primal and dual

Writing each tick's chosen acceleration as a two-vector u_t with prescribed magnitude m_t, positions are affine in u when the inertia pattern is fixed. The subproblem has objective c.u, linear rows A.u<=b, and ||u_t||=m_t. Those equalities define circles, not convex disks. Angle variables are continuous but the fixed-length problem is nonconvex.

Dualizing the linear inequalities gives costates g_t=c_t-sum_j lambda_j A_jt and dual D(lambda)=sum_t m_t||g_t||+lambda.b, minimized over nonnegative inequality multipliers; equalities carry unrestricted multipliers. This is a convex dual objective that upper-bounds a maximization primal. Each independent circle maximization is analytic. Standard weak/strong duality and KKT distinction: [Boyd/Vandenberghe, Duality slides](https://see.stanford.edu/materials/lsocoee364a/05Duality.pdf), slides5-3 and5-9 through5-13.

## Why global zero gap is not automatic

For every g, max over ||u||=m of g.u equals max over ||u||<=m of g.u, both m||g||. Therefore this same unsmoothed dual is also the dual of the convex disk relaxation. Its value cannot distinguish the physical circle from allowing reduced acceleration. Under suitable constraint qualification it solves that disk relaxation, not automatically the circle problem.

Counterexample within this abstract model: maximize z with x^2+z^2=1, -.5<=x<=.5, z<=0. The circle optimum is -sqrt(3)/2; the disk relaxation attains0 at(0,0). Its dual also attains0 (take multiplier1 on z<=0 and0 on the X walls), giving a genuine positive gap of sqrt(3)/2. These are all linear positional walls plus a fixed magnitude. Thus the unconditional zero-gap claim in the header of CostateDualSolver.java is not a general theorem. This example establishes possibility, not a measured gap in Linkcraft.

At an attained exact dual optimum with all relevant g_t nonzero, each inner maximizer is unique, u_t=m_t g_t/||g_t||. Dual KKT then supplies linear feasibility and complementary slackness for those same controls. In that regular case recovery does certify a primal optimum. At g_t=0 the inner objective has no direction preference: every circle direction is optimal locally, and the disk can also choose an interior vector. Choosing a direction independently does not enforce the coupled trajectory rows. Even a zero objective gap need not make an arbitrary degenerate recovery feasible.

## Implementation adds two further approximations

`CostateDualSolver.costate/grad` uses sqrt(||g_t||^2+EPS2); EPS2 is currently1e-24. The gradient uses u_grad=m_t*g_t/sqrt(||g_t||^2+EPS2). Near zero this is shorter than the prescribed acceleration. However `FacingPrefold.expand` converts the direction into full-magnitude acceleration; below ||g||<1e-9 it substitutes the raw objective direction. Consequently dual gradient slack and replayed-angle slack can describe different trajectories. Numerical smoothing is not an inertia threshold check, and continuous angle recovery is already a potential mismatch before Minecraft quantization.

The solver can also return before KKT convergence: iteration cap, no-descent, divergence stall, or recovered-input stall. The last tests successive changes of the internal u_grad against1e-9. Stable inputs do not prove linear feasibility. `solve` returns a Result at those stops; callers must validate it. The comment describing input stability as the real optimum is not a general certificate.

Separately, full continuous replay uses actual per-axis inertia threshold checks. If those disagree with the assumed pattern, its recurrence differs from the affine subproblem. That can invalidate an otherwise good affine solution and motivates inertia rediscovery. This mechanism is distinct from duality gap, numerical convergence, and yaw recovery.

## What is established for the current jump6 fixed Z/MIN miss

The1264-iteration candidate stops at RECOVERED_INPUT_STALL with projected residual0.0394, not a converged KKT solution. No inertia assumptions fail, and assumed affine path versus actual continuous path agrees to around1e-12 on constraint values. The large violations already exist before the Minecraft replay conversion. Thus nonconvergence/recovery is the first issue to diagnose; no true optimal-value duality gap has been established.

The0.05-margin candidate found from the SAME fixed entry satisfies all original physical rows. Its assumed-model row values differ from its actual continuous row values by up to0.015718118409. Therefore physical feasibility of that candidate alone does not certify feasibility or zero gap of the unzeroed tightened affine subproblem. The existing saved normal success likewise uses a different inertia pattern, entry state, retained rows, epsilon and cap.

## Diagnostic order

1. At one returned dual iterate, compare all rows evaluated using internal derivative controls u_grad against rows evaluated using full-magnitude recovered angles in the fixed model, and then the actual continuous threshold simulation.
2. Record min||g||, controls below1e-12 and below the1e-9 fallback, gradient residuals, and exact stop reason. This separates convergence failure, smoothing, arbitrary recovery, and inertia mismatch.
3. If needed solve the matching disk relaxation independently and inspect which acceleration vectors are inside their disks. A saturated feasible solution points toward implementation/convergence/recovery trouble; interior controls identify where physical full-magnitude recovery needs additional work. Interior controls alone do not prove a strict optimal-value gap.
4. Compare valid dual bounds with feasible primal values for the SAME fixed model and constraints. A gap between approximate iterates is not proof of a true duality gap; one must establish sufficient optimality/bounds on both sides.

Code sources: `solver/CostateDualSolver.java` header, solve, costate, grad; `solver/FacingPrefold.java` expand; `solver/ClosedFormSolve.java` runLadder/solveWithPrefold; `solver/WindowCandidateValidation.java` evaluate. Empirical sources: [fixed audit](jump-six-window-endpoints-2026-10-23/fixed-violations.md), [controlled margin probes](jump-six-margin-continuation-controls-2026-10-23/README.md). This theory review adds no production changes or probe runs.


## Precise sufficient theorem and provenance follow-up

Physical acceleration magnitude is fixed once keys, sprint/contact/potions are fixed. In `JumpLinearModel.precompute` mMag=hypot(fF+boost,sF); air sprint45 is approximately0.026 (AIR_SPEED_F=0.025999999F), straight W is scaled by0.98 and jump ticks combine the fixed boost with the input vector before rotation. The disk is a mathematical relaxation, not a physical degree of freedom.

**Theorem for the independent-vector affine subproblem:** suppose the exact UNSMOOTHED dual has a finite attained minimizer lambda*, and every free control with m_t>0 has g_t(lambda*)!=0. Then u_t=m_t*g_t/||g_t|| is feasible and globally optimal for the fixed-modulus primal, and the duality gap is zero. Equality multipliers may be included freely. This is sufficient, not necessary. Exact zero costates may still admit feasible optimal circle recovery; arbitrary independent fallback is not sufficient.

**Proof:** with all relevant costates nonzero, D is differentiable and gradient_j D=b_j-(A.u)_j. KKT of minimizing convex D over nonnegative lambda implies gradient>=0 and lambda_j*gradient_j=0. These are exactly primal wall feasibility and complementarity; free equality multipliers give equality residual0. Circle constraints hold by construction. Since D(lambda*)=c.u+lambda*.(b-A.u)=c.u, weak duality proves primal and dual optimality with equal values. This derivation is specific to the displayed affine fixed-modulus problem. Actual inertia-pattern consistency is a separate requirement; fixed-pattern modeling alone does not enforce every assumed nonzero check unless those rows are included.

Consequently: under dual attainment, a genuine strict circle-versus-disk optimal-value gap requires at least one positive-magnitude free control to have zero costate at EVERY dual minimizer. Merely SMALL but nonzero costates do not create a mathematical gap. They create conditioning/smoothing/recovery trouble in floating point. Costates being nonzero at the returned iterate does not imply that iterate is the dual optimum. Numerical algorithm failure can occur even when costates are large and stable. A fixed pattern makes the model definite; it does not make product-of-circles constraints convex.

The cited discrete-time LCvx paper has a closely related sufficient nonzero control-costate result (Theorem3.12), with explicit assumptions on its own problem class: [Luo/Echigo/Acikmese, 2410.09748v2](https://arxiv.org/html/2410.09748v2). It does not supply an unconditional all-ticks losslessness guarantee for arbitrary Minecraft positional rows. Its abstract explicitly distinguishes continuous-time guarantees from discrete-time exceptions. The direct KKT proof above avoids assuming the paper's full control problem matches this implementation.

### Git and gh provenance

- Original implementation commit [8263d65c, June8](https://github.com/Leg0shii/ParkourCalculatorMod/commit/8263d65c20464d73ed574ad5d38c1a9d7a3f12ed), within [PR122](https://github.com/Leg0shii/ParkourCalculatorMod/pull/122), explicitly asserts zero gap because the per-tick inner maximization is exact. This reasoning misses the need to choose jointly feasible inner maximizers when costates vanish. It also introduces input-change convergence as protection against wandering multipliers. PR122 has an empty description and no comments; the substantive explanation is in this constituent commit message. Squash9a22bdbe (June10) is what local header blame identifies.
- Commit95d44c06 (June11, documentation update) introduced `docs/research/angle-solver.md` section1.1 and its LCvx references: Acikmese/Blackmore2011, Harris/Acikmese2014, Luo2024/2025. That section contains both the broad zero-gap claim and a caveat about discrete-time losslessness. GitHub commit-to-PR lookup returns no associated PR.
- Commit6d5b3b75 (June11, better long solver) adds section2.1.1 qualifying the claim: unique recovery is required; j021 is recorded with projected residual5e-9 but recovered infeasibility0.34 even at20000 iterations; SLP is added as recovery. The measurement establishes a failed recovery, but on its own is not a formal proof of a positive optimum-value duality gap. GitHub commit-to-PR lookup returns no associated PR.
- Commitb13bce07 (June12) expands the same discussion for objective/wall cancellation: 12/17 surveyed single-jump fixtures had a failing objective direction, motivating SLP and alternate-direction seeds. No associated PR returned by lookup.

[Archived gh responses and blame](dual-gap-provenance-2026-10-23/). No external linked Claude session was accessed. No production changes or tests for this provenance review.

### What input-change convergence literally checks

At each multiplier iteration k, grad computes ux_t,uz_t=m_t*g_t/sqrt(||g_t||^2+EPS2). The solve loop compares those components against uPrevX/uPrevZ from iteration k-1. If k>0 and max over ALL ticks and both axes of abs(component change)<=1e-9, it stops with RECOVERED_INPUT_STALL. These are added-horizontal-velocity control components across optimizer iterations; they are NOT player velocity at adjacent simulation ticks, yaw changes, or constraint slack. The threshold check does not additionally require projected-gradient convergence or primal feasibility. Radial changes of a costate can barely change its normalized control even while the multipliers still need to move far enough to satisfy constraints. Stable controls can therefore be an early stop, not a mathematical convergence certificate. Current fixed1264 stop retains projected residual0.0394, far above GRAD_TOL1e-8.
