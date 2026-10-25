# Options for small-costate handling in the angle solver

2026-10-24. Research/design follow-up to the [20–22 Z/max measurements](solver-traces/jump20-22-zmax-2026-10-24/README.md) and [exact tick132 cancellation](solver-traces/jump20-22-zmax-2026-10-24/costate132-explanation.md). No code changes or experiments in this follow-up. The observations establish severe first-control recovery/conditioning issues in this case; they do not classify every closed-form failure as costate cancellation.

## 1. What is currently inconsistent with physical recovery

Let m_t be the prescribed acceleration magnitude, g_t the costate and epsilon=sqrt(EPS2). Current code uses:

- Dual term: m_t sqrt(norm(g_t)^2+epsilon^2).
- Its mathematically consistent gradient control: u_t=m_t g_t/sqrt(norm(g_t)^2+epsilon^2).
- Physical angle decoding: magnitude m_t, direction g_t/norm(g_t).

The objective and gradient agree with each other; it is their recovered control that differs from the physical full-radius control. At tick132, norm(g)=1.588e-13 and epsilon=1e-12, so the gradient control has about15.686% physical radius. This is a measured discrepancy, not a proposed new optimizer.

Near-zero costates can be structurally real. g=c-A^T lambda is a net sensitivity; original objective and constraint prices can cancel even in exact arithmetic. At exact zero, the inner linear maximization has no preferred full-radius direction. Increasing precision does not supply the missing direction.

## 2. Force full physical magnitude: mathematically possible, but nonsmooth

For g!=0, the exact unsmoothed support term is m norm(g), with gradient m g/norm(g). It has exactly the physical magnitude. At g=0 it is nondifferentiable: the subdifferential is the closed disk of radius m. Any full-radius vector is an allowed subgradient too; choosing one does not guarantee the resulting full trajectory is feasible.

The standard rule for subgradients of a pointwise maximum supports this oracle construction; see Boyd et al., [Subgradients, sections3.4–3.5](https://web.stanford.edu/class/ee364b/lectures/subgradients_notes.pdf). That is a statement about a valid subgradient, not convergence of our current Newton routine or physical-path recovery.

A consistent experimental variant would change:

1. Dual value to the unsmoothed norm.
2. Gradient to full-radius response for nonzero g.
3. Hessian/step handling consistently: classical curvature is singular near zero and undefined at zero; use a nonsmooth method or a suitable safeguarded fallback there.
4. The zero-costate response rule and stopping criteria. Arbitrary subgradient norm need not vanish at a nonsmooth optimum; a simple Armijo descent check on an arbitrary chosen subgradient is not a reliable replacement for bundle/proximal handling.
5. Continue selecting candidates by actual constraint validation.

Changing only `grad()` while keeping the smoothed objective and its Hessian is not a consistent gradient/Newton method. The resulting wall residual direction would no longer be the derivative of the function used for line-search acceptance. Such a hybrid could be a heuristic diagnostic, but must not be described as the original dual method with a better gradient.

No differentiable radially symmetric smoothing of this norm can retain a nonzero full-radius gradient at the origin: symmetry requires its gradient there to be zero. One must accept nonsmoothness, alter the model, or recover controls separately.

## 3. More accurate costates / smaller smoothing

Compensated summation, problem scaling and higher precision can reduce cancellation ROUNDING error in c-A^T lambda. They cannot eliminate exact mathematical cancellation. For the current saved norm, reducing EPS2 from1e-24 to1e-28 would raise the radius ratio from.1569 to about.9980 IF g stayed fixed. It will not necessarily stay fixed after reoptimization: the minimizing costate can move closer to zero as smoothing is reduced.

Smaller smoothing increases curvature/conditioning difficulty. The measured weight m/sqrt(norm(g)^2+epsilon^2) is already~1.28e11 at132 versus at most31.72 for other ticks. The current step damps all free multiplier coordinates by rho times the maximum Hessian diagonal. Better scaling/equilibration and damping are separate numerical leads; they do not resolve an exactly ambiguous recovery direction by themselves.

A more coherent numerical experiment is continuation: solve with easier/larger smoothing, warm-start successively smaller values, and measure physical feasibility separately at every stage. Adaptive/homotopy smoothing is an established approach for nonsmooth convex optimization; [Tran-Dinh, Fercoq and Cevher, Adaptive Smoothing Algorithms for Nonsmooth Composite Convex Minimization](https://arxiv.org/abs/1509.00106). Their theory is not automatically a guarantee for this implementation, its early stops, or its nonconvex physical recovery.

## 4. Can we force costates to stay large?

### Multiplying objective/units

Scaling objective coefficients and corresponding multipliers scales g and the relevant dual quantities. With consistent smoothing/tolerances this does not change the underlying cancellation. If smoothing is not rescaled, it is effectively another change in relative smoothing, not a proof that zero-costate situations are avoided.

### Imposing norm(g)>=delta

This removes legitimate dual multiplier configurations and is generally a nonconvex restriction in lambda. It changes the dual optimization problem and can exclude its optimum. It is not a numerically neutral safeguard.

### A small reference-direction objective

Adding delta times a reference control direction to the primal objective biases otherwise weak controls toward an existing good trajectory. Recompute the dual value, gradient and Hessian for that modified objective. This can be a practical continuation/secondary-objective strategy, but it changes the objective and still does not guarantee all costates stay nonzero after multipliers readjust. A lexicographic approach first protects the primary objective with a wall, then optimizes path preference; this also needs feasibility and objective accounting.

## 5. Recover the weak directions using the original constraints

The practical alternative to normalizing numerical noise is to treat weak-control angles as coupled unknowns:

1. Keep the strong-costate controls at their current or best validated values.
2. Free the weak tick(s), starting with132 here, and enough neighboring controls to restore feasibility.
3. Solve the original position constraints with each freed control's physical fixed magnitude. Do not solve each weak direction independently from its objective vector.
4. Prefer retaining a good previous path and improving actual objective/violation, rather than throwing away early directions solely because their costates cancel.
5. Validate the actual candidate, including inertia threshold checks. If no feasible weak-only repair exists, expand the set or move to broader recovery. A local failure does not certify infeasibility.

The near-zero part of the Lagrangian gives little direction information, while the full original constraints still provide strong information. This is why a small primal repair can be useful even when high precision cannot choose a unique costate direction.

For this case, pinning132 to the saved SLP angle reduces the initial continuous miss to.01024; pinning132 and133 gets.00160. Both controls still fail, so this is motivation, not a demonstrated repair. Earlier closed-form intermediate retention saves only fully valid candidates; no such candidate exists on this trajectory yet. A good seed must come from a separate candidate or an explicitly recorded near-feasible trajectory.

Existing code to study/reuse:

- [SlpSolve.optimizeLocked](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/SlpSolve.java): local yaw solve with a supplied seed and lock mask. Its locked overload is best-effort and can return a miss, so use explicit final validation. Its replay acceptance was just restored; decide explicitly whether a CF-specific recovery also requires the continuous check.
- [RelaxationRecovery](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/RelaxationRecovery.java): independent primal disk solve and conversion to yaw seeds.
- [FoldReplayDriver](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/FoldReplayDriver.java), [DiskSocpKernel](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/DiskSocpKernel.java): existing circle/disk geometry and replay recovery methods.
- [Existing failure/recovery inventory](angle-solver-existing-failures-and-recoveries.md), particularly sectionB: this problem class and relevant historical fixes are already documented.

## 6. Fundamental limit of solving only the dual

For each tick,

`max over norm(u)=m of g·u = m norm(g) = max over norm(u)<=m of g·u`.

Therefore this wall-Lagrangian dual has the same support term for a circle as for its filled disk. This is an algebraic fact for this model. The dual alone cannot guarantee it recovers a feasible circle trajectory when an optimum needs an interior disk response. A better nonsmooth dual solver can fix numerical convergence without automatically fixing primal recovery.

A toy example, not a diagnosis of20–22: one control norm(u)=m, ux=0, uz<=0, maximize uz. The circle optimum is uz=-m; the disk optimum is0. The dual support minimization attains0 with g=0. It cannot yield a zero-gap full-magnitude primal certificate in this example. Thus the CostateDualSolver class comment claiming automatic zero gap/feasible recovery in general is too strong. Our measured20–22 iterate is NOT stationary, so it has not demonstrated such a gap.

For the general background distinguishing dual bounds, primal feasibility and sufficient optimality conditions, see Boyd and Vandenberghe, [Convex Optimization, chapter5](https://web.stanford.edu/~boyd/cvxbook/bv_cvxbook.pdf). The identity/example above are our direct derivation for this solver's fixed-modulus model.

## 7. Suggested bounded experiments

These are proposed, not implemented or run:

1. **Consistent exact-norm response control:** isolate the132..178 fixed-pattern problem, ensure all gradient responses have physical magnitude, and use a nonsmooth-safe step strategy. Measure primal residuals, objective and iterations rather than merely costate size. Do not silently leave the old smoothed Hessian/value in place.
2. **Smoothing continuation plus scaled damping:** preserve consistent value/gradient/Hessian, warm-start stages, and export per-stage radius ratios and wall margins. This tests convergence separately from direction recovery.
3. **Restricted primal recovery:** free132 and133 plus neighboring controls as necessary, retain other useful directions, and use existing locked SLP or a small angle search with all original rows. Benchmark against the already working full SLP replay candidate. Do not alter constraints to make recovery pass.

For practical success, prioritize physical primal recovery and accurate residual checks; for diagnosing the dual, run the exact-norm/continuation controls separately. Forcing a positive lower bound on costates is not recommended as a correctness-preserving change. Nothing in this note changes current production behavior or claims a proven fix.
