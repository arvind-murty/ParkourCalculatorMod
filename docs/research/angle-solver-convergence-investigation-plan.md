# Plan: why similar angle solves converge very differently

Date:2026-10-24. Scope: Minecraft1.8, fixed start, inputs through a continuous feasible path, with per-axis inertia threshold checks. Byte-exact conversion is considered only to distinguish an unrelated rejection cause. User request: research first; record potential leads before pursuing them; then flesh out each lead toward a holistic explanation. No solver changes or new tests/probes in this pass.

## Question and working standard

Why can a window with apparently generous spatial corridors and a known physical feasible path cause an extreme convergence miss, while a nearby window or larger margin returns quickly? Do not assume inertia is responsible or that the physical problem is intrinsically hard. Distinguish physical feasibility, feasibility of a particular tightened fixed-pattern affine subproblem, optimality of a disk relaxation, full-magnitude angle recovery, and the numerical path taken by the implementation.

## Phase1: cursory leads recorded before deeper research

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

## Phase2 status

All P1–P11 pending at creation. Each will receive a findings section with code/history/source citations and an explicit established-versus-hypothesis verdict. Initial plan is committed before those sections are added.
