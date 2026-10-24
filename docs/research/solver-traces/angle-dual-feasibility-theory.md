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
