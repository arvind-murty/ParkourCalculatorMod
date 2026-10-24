# Closed-form convergence guarantees and fallback provenance

## What the cited paper actually proves

Paper: Luo/Echigo/Acikmese, [Revisiting Lossless Convexification, v2](https://arxiv.org/html/2410.09748v2). This is a model/relaxation-validity analysis, not a convergence proof for `CostateDualSolver`'s implementation. Theorem3.10 supplies primal/dual optimality structure under Slater; Theorem3.12 gives a sufficient nonzero control-costate condition for a relaxed optimum to satisfy the nonconvex control constraint at that grid point. Theorem4.27 bounds invalid grid points by nx-1 almost surely after a prescribed small random perturbation and under its assumptions (controllability, normal case, full-rank equalities, Slater and objective/control regularity). Even that is not all-ticks feasibility. Theorem4.32 bounds final-state error of control-magnitude correction as the discretization is refined; this is not a rate for Newton iterations, nor a guarantee of satisfying intermediate positional walls after correction. Long-horizon cases use a separate two-phase/bisection construction in section5, absent from our costate solve.

Its stated class uses particular dynamics/boundary/objective structure. The Minecraft subproblem has time-dependent friction, zeroing patterns and many intermediate state walls; no verified assumption mapping was found. Therefore neither the nx-1 count nor any all-ticks guarantee can be claimed for this code merely because its position map is affine. The exact nonzero-costate KKT result for our displayed fixed-pattern subproblem remains valid independently; see [proof](angle-dual-feasibility-theory.md#precise-sufficient-theorem-and-provenance-follow-up).

## Why current code need not reach the dual optimum

The exact dual is convex. With EPS2>0 its implemented norm is smooth, but potentially extremely ill-conditioned and not strictly convex (many wall multipliers can generate the same costates). Standard convex optimization algorithms can be designed to converge under suitable assumptions. This does not supply an algorithm-specific guarantee for a capped finite-precision Newton/gradient implementation.

`CostateDualSolver.solve` returns a best-effort Result on projected-gradient tolerance, input-change stall, no descent found by its finite line searches, divergence stall or the iteration cap. A lambda magnitude/NaN limit instead returns null. Current cap10000; projected tolerance1e-8; input-change tolerance1e-9. Newton tries at most8 damping retries and22 line-search trials per retry; gradient fallback expands/backtracks at most40 times. Strict objective-decrease comparisons can fail numerically even with a nonzero residual. Input-change is not a KKT/primal-feasibility certificate. The current fixed jump6 candidate stops at1264 iterations with residual0.0394, so simply increasing10000 cannot affect that exit.

There is also smoothing/recovery mismatch: gradient controls can be shorter than full magnitude; angle recovery restores full magnitude and can substitute raw objective direction below1e-9. A small residual for the smoothed gradient need not certify the emitted angles or the actual inertia-threshold simulation.

Historical audit is unusually explicit: [PR385](https://github.com/Leg0shii/ParkourCalculatorMod/pull/385) recorded9551 dual solves,53% hitting the then-cap100, with non-monotone downstream outcomes when truncating at95/90/75/50. This is older code/settings, not a new benchmark. It establishes that incomplete convergence was intentional architecture, not an unrecognized theoretical guarantee. On sound node relaxations, an unconverged dual iterate still gives a valid looser bound; it need not give a feasible primal trajectory.

## What closed form can fail at

1. Numerical optimization: cap/stall/no-descent before dual KKT; feasible fixed-pattern solutions may exist.
2. Degenerate primal recovery: zero/near-zero costates; local maximizers not jointly feasible; disk relaxation may prefer sub-modulus controls. A strict optimal-value duality gap is a distinct possibility, not established by an infeasible returned candidate alone.
3. Wrong inertia pattern: affine proposal not reproduced by actual threshold checks. At most4 rediscovery passes can stop or cycle.
4. Discovery failure: the initial no-zeroing affine model can be infeasible even though another inertia pattern admits a physical solution. If initial solve yields no useful candidate, naive replay-derived discovery cannot find it.
5. Model versus Minecraft realization: float/trig/lattice differences matter on sufficiently narrow corridors, separate from continuous infeasibility.
6. Additional constraint structure: facing ties/order require folds/cascades; unsupported forms or a failed cascade may decline without establishing infeasibility.

## Why the recovery stages exist

### SLP: optimize yaw directly after dual recovery fails

Introduced6d5b3b75, June11. [Design notes2.1.1](../angle-solver.md#211-the-duality-gap-on-cross-seam-coupled-windows-and-the-slp-closer) record j021: dual residual5e-9 but recovered path~0.34 blocks infeasible, persisting at20000 iterations. June12b13bce expands this to same-axis objective/wall cancellation (12/17 then-surveyed single-jump captures had a failing direction). SLP operates on the primal yaw variables, minimizing constraint violation then improving objective. It can choose angles where costate normalization fails; it remains a local budgeted method and does not guarantee global recovery. The documentation calls this a duality-gap closer, but failed normalization alone is not a mathematical proof of strict gap.

### Relaxation recovery: recover a physical path from disk controls

[Issue204](https://github.com/Leg0shii/ParkourCalculatorMod/issues/204) and [PR209](https://github.com/Leg0shii/ParkourCalculatorMod/pull/209) separated bound, continuous recovery and Minecraft realization. Loopmm's recovered continuous candidate violated a wall by0.245 blocks. Its inertia error was~0.033 versus much smaller sine error. Design notes10.1 say a separate augmented-Lagrangian/FISTA disk solve plus warm restarts was introduced because the production dual stops too early, then projected/dithered controls and SLP/pins recover full-magnitude paths. This specifically acknowledges numerical nonconvergence separately from nonlossless recovery.

### Pattern Bnb: discover patterns and move beyond one local trajectory

BoundPrunedRecovery first appears in d35c286/PR209 (dated July3 in author history). [Notes10.3](../angle-solver.md#103-bake-off-results-all-falsified-except-the-relaxation) record that seam-cell B&B alone failed; enumerating inertia zeroing patterns was the breakthrough. Loopmm uses X velocity0.00465 before a threshold check to get a free brake, which the no-zeroing affine model cannot represent. Per-pattern dual bounds plus cell subdivision and local recovery landed it. PR209 body only says closes204; issue204 and the in-repo record hold the explanation.

[PR393](https://github.com/Leg0shii/ParkourCalculatorMod/pull/393) gives a particularly decisive witness: a one-tick inertia zeroing is necessary, the no-zeroing relaxation is infeasible, closed-form discovery depends on a candidate the initial diverging solve does not provide, and suffix-only enumeration misses the one-tick pattern. The fix adds single-tick masks. [PR397](https://github.com/Leg0shii/ParkourCalculatorMod/pull/397) adds complementary survive-positive/survive-negative hypotheses; previously only zeroing was constrained and the free model did not enforce threshold survival. These fixes address omitted regimes, not inadequate dual iteration budgets.

### CertBnb: global search and bound certificates beyond local recovery

Introduced be584ed/[PR427](https://github.com/Leg0shii/ParkourCalculatorMod/pull/427), Aug28, from issue422's ARCH-2 work. The [binding plan](https://github.com/Leg0shii/ParkourCalculatorMod/issues/422#issuecomment-5428709916) names several measured failures: full-modulus recovery floors even over300k iterations; interior-control relaxations; knife-edge no-solves; local pattern/SLP/ILS basin trapping; budget overruns. These are historical measurements, not reproduced here. A certified search was added to split yaw ranges and inertia threshold outcomes, tighten sound relaxations, discover incumbents and close a global objective bound gap where affordable.

PR427 explicitly limits the achievement: certified small captures, larger multi-jump/free-start cases may retain a loose bound; BoundPrunedRecovery and RelaxationRecovery stayed because they were load-bearing. Finite angles do not make a deadline-limited search complete. [PR447](https://github.com/Leg0shii/ParkourCalculatorMod/pull/447) subsequently introduced explicit lattice intervals/leaf enumeration and GateFoldFinder for otherwise inaccessible threshold basins. CertBnb is therefore not simply a more patient closed-form dual solve.

[Archived gh issue/PR bodies](closedform-failure-provenance-2026-10-23/). No production changes or tests were made for this review.
