# Existing knowledge: angle-solver failures and recoveries

Date: 2026-10-24. Follow-up to the [convergence investigation](angle-solver-convergence-investigation-plan.md). Scope: fixed inputs, Minecraft 1.8 per-axis inertia threshold checks, fixed start unless explicitly labelled otherwise. Focus: reuse the existing research and code before inventing another solver. Read-only review; no tests or new solver experiments were run.

## 1. What to read, and what each source actually gives us

| Priority | Existing source | Useful content | Status / caution |
|---|---|---|---|
| 1 | [Dual Newton iteration audit](dual-newton-iteration-audit.md), especially sections 2–5 and 7 | Measured nonconvergence; cap/early-stop experiments; contributing recovery stages; impossible margins; smoothing versus conditioning; existing successful fixes | August measurements at cap 100, not current cap 10000. Its negative results supersede the preceding Hessian handoff's proposed optimizations. |
| 2 | [Angle-solver design record](angle-solver.md), sections 2.1.1, 10–12 and 16 | Objective/wall cancellation; primal SLP recovery; disk relaxation; inertia patterns; basin escape; historical bake-offs and dead ends | A chronological record, not an accurate current pipeline specification. Several named stages were deleted. Unconditional global-optimality/zero-gap language is too strong. |
| 3 | [Issue 422's binding engine-fix plan](https://github.com/Leg0shii/ParkourCalculatorMod/issues/422#issuecomment-5428709916) and [PR 427](https://github.com/Leg0shii/ParkourCalculatorMod/pull/427) | Why the newer disk/fold/replay, wall homotopy and certified search were built; measured interior-control and recovery failures; limits of the replacement | Reports historical experiments. The comment references `global-solver-2026-08/ARCH2-STEP1-SIMPLIFY.md`; that directory is absent from this checkout. Use the archived comment, not an imaginary local file. |
| 4 | [Dual Newton/Hessian handoff](dual-newton-hessian-handoff.md), sections 3 and 6; [PR 397](https://github.com/Leg0shii/ParkourCalculatorMod/pull/397) | LP degeneracy, bad step-variable representation, the misleading LP-vertex diagnosis, and the inertia-survival constraint that actually restored loopmm | The update and measured follow-through matter more than the initial hypothesis. |
| 5 | [Nix setup-tick failure](nix-setup-tick-dual-failure.md), especially findings F1–F8 and “Fix shipped” | Known feasible witness; one-tick start-shift asymmetry; failed pattern/pin repairs; successful prefix-angle sweep | The empirical record is valuable. Its claim that weighted costates cannot express redirects is not established: combinations of wall normals can produce other directions. Old engine wiring and CMA references are obsolete. |
| 6 | [PR 393](https://github.com/Leg0shii/ParkourCalculatorMod/pull/393) | A necessary single-tick zeroing that candidate-derived discovery could not find; existing pattern-enumeration fix; separate first-tick seed-normalization fix | Suffix patterns alone were insufficient. Facing support has advanced since the PR; inspect current eligibility rather than copying its old restrictions. |
| 7 | [Nix solver handoff](nix-solver-handoff.md) and [ALM/snap review](alm-snap-stage-b-design.md) | Earlier ALM+BFGS and discrete-repair research; neighborhood falsification; distinction between refining a good basin and discovering one | Mostly razor/lattice research, lower relevance to our current large continuous violation. The ALM/snap implementation was built and later deleted, not an untried proposal. |
| 8 | [Nix full-route record](nix-full-freestart.md), sections 3b and 8.6 | Seam position and velocity must work together; why position-only surrogates can produce unusable entries; failed B&B-in-window attempt; SMT limits | Free-start case is adjacent evidence, not our fixed-start diagnosis. Its historical solved headline is superseded: current expectation is accepted-fail. |
| 9 | [Split-solver review](split-solver-grill-brief-2026-07-19.md), [matrix archive guide](data/README.md), [node-budget decision](node-budget-decision-2026-08-28.md) | Stage order/budget can determine success; old benchmark summaries; actual budget bugs | Old matrices reference removed algorithms. Do not replay their prescriptions as current configuration advice. |

The previous [paper and fallback-provenance review](solver-traces/closedform-convergence-and-fallbacks.md) supplies exact commit/PR provenance. The new [convergence plan](angle-solver-convergence-investigation-plan.md) separates measured facts, algebraic facts and hypotheses for our particular jump6 failure.

## 2. Which solves fail, and what already worked

### A. The dual is still far from stationary

**Signature:** appreciable projected-gradient residual, an iteration-cap/input-stall/no-descent exit, and a trajectory that violates even its assumed affine model. This does not require inertia-pattern disagreement or tiny costates.

**Existing evidence:** the August audit counted 9,551 dual calls, 53% capped; changing caps changed downstream solve outcomes non-monotonically. Our current fixed jump6 Z/MIN stops at 1264 with residual 0.0394 and continuous violation 1.5053; its inertia assumptions hold. Disabling the control-change exit reached 10000 without solving it. These facts identify nonconvergence, not the precise conditioning/globalization cause.

**Existing ways around it:** `RelaxationRecovery` deliberately has a separate primal disk solve (augmented-Lagrangian/FISTA), rather than relying exclusively on the production dual's recovered angles. The newer `FoldReplayDriver` uses `DiskSocpKernel`. These are existing alternative numerical paths to study on the same subproblem before implementing another generic optimizer.

**Limit:** neither is an independent mathematical oracle merely because it is a second implementation. Check original-unit residuals and the physical path; current E1's tightened affine feasibility is still unclassified.

Sources: iteration audit sections 1/3/5; design record section 10.1; [current code: RelaxationRecovery](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/RelaxationRecovery.java), [FoldReplayDriver](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/FoldReplayDriver.java).

### B. A good dual bound does not yield a feasible angle sequence

**Signature:** optimization of the relaxed controls can be accurate, yet normalizing to fixed acceleration magnitude fails. Costates may cancel to zero; disk controls may need to be shorter than physical acceleration. This is separate from A. An infeasible recovery alone does not prove a strict optimal-value gap.

**Existing evidence:** j021's early record reports residual 5e-9 but recovered violation about 0.34 even after 20000 iterations. Same-axis objective/wall cancellation appeared in 12/17 captures in an early survey. Issue 422 reports a thousand-class disk optimum with interior controls on ticks 0–15, and a much larger full-magnitude recovery error than its inertia contribution.

**Existing ways around it:**

- `SlpSolve` adjusts actual yaw variables rather than selecting each tick solely by its costate direction. Its feasibility phase can coordinate undetermined directions.
- A feasible solution from an alternate axis/sense seeds SLP on the original objective. The alternate is a seed, not proof that the original objective was optimized.
- `RelaxationRecovery` converts disk controls into projection/dither yaw seeds for SLP.
- `FoldReplayDriver` solves a disk problem, adds chord restrictions through its kernel path, decodes angles, replays, updates the assumed zeroing pattern and applies anchor-based local repair. It is not another name for `ClosedFormSolve`.

**Recorded successes:** design section 2.1.1 records SLP solving j021's feasibility failure; section 12.1 records interior-margin relaxation seeds moving j129/j135/j335/j344/j717/j828 onto cheaper recovery paths, and j318 from miss to solve. Those counts are historical, not a fresh current benchmark.

Current contracts in [FoldReplayDriverTest](../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/FoldReplayDriverTest.java) require a feasible loopmm result, feasible j003 reaching its old objective class, and repeated deterministic thousand-class feasibility. These are executable reuse leads; the tests were read, not rerun here.

### C. The required inertia pattern is missing from candidate discovery

**Signature:** the no-zeroing model is infeasible or sends every useful candidate into another pattern. Deriving a new mask only from its output cannot reliably discover the necessary regime.

**Decisive existing witness:** PR393's neo needs one X zeroing with velocity approximately 0.00498. Its effect at landing is about 0.026. Suffix masks cannot represent a one-tick zeroing followed by re-acceleration. The fix was single-tick masks in `BoundPrunedRecovery`; the PR reports a cold solve around 130 ms, through a patterned closed-form incumbent before tree subdivision even ran.

**Existing ways around it:** explicit single-tick/suffix pattern hypotheses, their threshold-band constraints, and per-pattern recovery; `CertifiedBnb` also branches unresolved inertia threshold checks. `GateFoldFinder` supplies specialized near-threshold discovery for foldable facing-chain cases, not a universal replacement for positional-window solving.

**Separate fixed bug:** if the input state's velocity is already below threshold at tick 0, normalizing it before building linear models fixes the model constant. That is not a pattern search variable.

Source: PR393 and `problems/solve/inertia-1tick-neo` / `inertia-1tick-neo-t31` expectations. The user's current E1 does not show a pattern mismatch; don't apply this explanation without evidence.

### D. The path must deliberately avoid zeroing on a particular side

**Signature:** candidate variations agree almost everywhere but one threshold check destroys useful carried velocity. Merely assuming no zeroing does not constrain that velocity to survive.

**Decisive existing witness:** loopmm's old successful trajectory enters tick4 with Z velocity +0.00505825; a missing trajectory has -0.00101368 and is zeroed. Their final Z differs by about 0.00055. The investigation initially blamed LP vertex selection, then identified the missing constraint.

**Successful existing fix:** `keepAliveWall` and `BoundPrunedRecovery.keepAlivePatterns` explicitly require positive or negative threshold survival. PR397 reports the positive tick4 case landing at about -279.299859. It restored three regression expectations. This is a particularly strong example of replacing a lucky numerical trajectory with the constraint the physics actually requires.

Sources: Hessian handoff section 6, PR397, [InertiaFoldingTest](../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/InertiaFoldingTest.java).

### E. A grounded redirect prefix produces a bad entry for the tail

**Signature:** starting on one grounded setup tick fails, but starting immediately after it works. A feasible continuation is known from an appropriate exact entry state.

**Successful existing fix:** `SetupPeelNode` sweeps a constant facing for the leading grounded, non-jump prefix, simulates it, and calls `LongRunSolver` on the resulting tail entry. It checks the assembled full path. The nix-t25 record reports success after the ordinary chain failed, while the next-tick variant continued to use the cheap path.

**Limit:** this is a structured prefix search, not arbitrary per-tick run-up optimization, and the node requires a multi-jump span. Do not generalize it into a theorem that all ground prefixes need peeling. Its class already has `solve/nix-t25-setup-tick.expect.json` with `shouldSolve: true`.

Sources: setup-tick doc “Fix shipped”; [SetupPeelNode](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/graph/nodes/SetupPeelNode.java). Prefer code for present wiring and limits.

### F. Local repair cannot find the needed basin / narrow feasible intersection

**Signature:** repeated local methods stall with similar residuals; more time in that basin does little. A known witness can be refined, but a cold candidate generator does not reach its neighborhood.

**Existing successful approaches, for different subclasses:**

- `WallHomotopyLadder`: relax wall RHS by 0.05, 0.01, 0.002, then 0, carrying the previous yaw and inertia pattern through the fold/replay driver. This is an **outward relaxation** ladder; it differs from inward robust safety margins. Final acceptance uses the original spec. [WallHomotopyLadderTest](../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/WallHomotopyLadderTest.java) requires j154's ladder to beat plain folding, land and reach the witness objective.
- `BoundPrunedRecovery`: pattern hypotheses plus intermediate-position cells and local recovery. Loopmm established why cell subdivision without the right inertia pattern did not suffice.
- `CertifiedBnb`: yaw intervals and inertia threshold branches, with node relaxations and exact candidate checks. It offers global search and, when bounds close, a certificate. Deadlines can leave a miss or a feasible incumbent with a loose bound.
- `IlsPolish`: existing feasible-route basin improvement. The j021 study records a large objective improvement, but that is evidence for polishing an incumbent, not a general cold feasibility solver.

**Limit:** success in one class does not make methods interchangeable. PR427 explicitly retained old relaxation/pattern recovery because certified search did not replace all their wins.

### G. A committed prefix is feasible, but the next solve fails

**Signature:** lookahead solves, commits, then the shifted window misses. This can reflect an unsuitable entry for newly introduced constraints OR a weak inner solver. The miss alone distinguishes neither.

**Existing findings:** design section 2.1 records why long monolithic solves were replaced by receding horizon; the historical nix full-route record demonstrates that seam position and velocity must work together. Our fixed jump6 entry also admits a physical feasible route under another candidate-generation margin, so its current miss must not be presented as proof that the committed entry is impossible.

**Existing workarounds:** window-size ladder, smaller commit retry, alternate objectives, centered feasibility. These alter either the local problem or its candidate generator; their success is not evidence that previous constraints were intrinsically difficult.

**Important correction to older prose:** overlap guarantees a previously found continuation only inside the already solved horizon, assuming that continuation really was validated. It does not guarantee feasibility of newly introduced jumps, nor that a later numerical method can rediscover the old continuation. Current relaxed lookahead validation makes the distinction between checked prefix and proposed future path especially important.

**Existing warning:** `nix-full-freestart.md` records a last-window B&B rescue that was implemented and reverted: it did not solve the target and could stall `VelocityFinder`'s per-cell solves for 10 seconds each. Any future per-window heavy recovery needs caller-aware cost control, not an unconditional insertion.

### H. The solver is solving the wrong problem, or a surrogate is impossible

Before assigning a mathematical failure class, the repo already says to check:

- **Fixture construction:** design section 10.2 found 12/13 apparent misses were artifacts of wrong sprint/input scenario construction. Use the capture/debug-aware fixture path.
- **Indexing and exact entry:** positions are pre-tick; absolute-facing versus delta confusion caused major divergence. Our current rounded seed audit also shows that exact reconstruction matters to numerical reproducibility.
- **Constraint truth:** a known path violating authored rows does not prove those rows collectively infeasible; it first proves that the witness is not a witness for that spec. Some historical docs overstate this distinction.
- **Tightening:** opposite inertia-band rows become contradictory when both are tightened by margin greater than 0.005. The free-start opposing-pair cap already prevents this in its ladder; fixed-start handling needs a separate audit.
- **Budget/eligibility:** a “relaxation start” trace can still mean immediate decline on equality constraints. More seconds cannot repair an eligibility guard.

These are not excuses to weaken real map constraints. They identify which problem the alleged witness, bound or failure actually concerns.

## 3. What is already available inside long-run windows?

Current code, not the older design narrative:

| Location | Recovery actually called |
|---|---|
| Nonterminal `LongRunSolver.solveWindow` | Robust closed form in primary plus three alternate directions; then one centered SLP call. SLP has its internal retries, but this is not the full outer recovery graph. |
| Terminal window | `RecoveryLadder`: closed form, SLP, relaxation recovery if time allows, alternate closed-form seeds feeding original-objective SLP; then objective hugging/top-up. |
| Top-level graph after horizon/dual-chain attempts | Other guarded nodes, including setup peel, fold driver, homotopy ladder, certified B&B and pattern B&B. They receive the graph's scenario, not each failed horizon window. |

Sources: [LongRunSolver.solveWindow](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/LongRunSolver.java), [RecoveryLadder](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/RecoveryLadder.java), [BuiltinGraphs](../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/graph/BuiltinGraphs.java).

Present restrictions to preserve when reusing code:

- Relaxation recovery refuses any equality row and unsupported facing forms; it is not universally available after SLP. `RecoveryLadder` also requires at least 3 seconds remaining when a deadline exists before starting it.
- Fold-driver and wall-homotopy graph nodes decline facing-wall specs, including forms other stages might fold. Their default graph tick cap is 256. Fold solving supports a pinned start; free start is not required.
- Certified/pattern B&B graph nodes also default to 256 ticks. Short windows inside an 800-tick route do not automatically receive them.
- Graph execution and a helper called directly have different eligibility/budget surfaces. Reusing a helper in a window must preserve our chosen retained constraints, shared validation policy and exact committed-state chaining.

**Most relevant reuse lead for our positional fixed-start case:** compare existing relaxation recovery, fold/replay and wall homotopy on the exact failing local window before designing another solver. Their historical purpose directly matches the failure classes above. This is a proposed focused comparison, not a claim they already solve Linkcraft jump6.

## 4. Work already tried: avoid restarting these blindly

| Proposal | Existing result | What would justify revisiting it |
|---|---|---|
| Increase the dual cap globally | August: expensive and non-monotone corpus outcomes. Current E1: removing input-stall and allowing 10000 still misses. | A diagnosed numerical mechanism and same-data residual/step evidence. |
| Earlier residual-stall exit / lower cap | Historical different captures were lost; capped iterates feed useful downstream candidates. | Separate a valid bound from candidate recovery, and preserve the latter through another mechanism. |
| Globally sharpen smoothing | Free-start precision improved only in a range, then conditioning worsened; different captures regressed. | Controlled continuation or a demonstrated recovery-bias diagnosis. Free-start support smoothing is distinct from our fixed-start EPS2. |
| Replace simplex with an equivalent LP formulation | Equal optimal LP values produced different step trajectories; shifted variables caused uninvolved ticks to move to the trust-region boundary. Split variables fixed that case. | Check recovery behavior and physics, not only LP objective equivalence. Loopmm's eventual fix was an explicit survival constraint. |
| Add ALM+BFGS plus snap as though untried | Implemented, investigated, moved out of production, then deleted in PR382. | A new measured role and failure-specific advantage; consult its old reviews and git history first. Deletion alone is not proof ALM cannot help. |
| Add tiny local bucket repair to a large continuous miss | Several neighborhood searches were explicitly falsified; some cases needed another basin/pattern. | Evidence that the neighborhood contains a feasible point and the error is local. |
| Turn on SMT-FP as a generic cold route solver | Historical Bitwuzla results mostly verified narrow tubes around known yaws; full-range search struggled even at 3 ticks. | A substantially new encoding/search result, not the seeded verification timings. |
| Give every stage more wall time | Old records show starvation, eligibility declines and wrong-basin plateaus; a budget-control bug even made optimize stages exit immediately. | Confirm the intended method actually executes and improves. |

Upstream [PR379](https://github.com/Leg0shii/ParkourCalculatorMod/pull/379) and [PR382](https://github.com/Leg0shii/ParkourCalculatorMod/pull/382) were fetched with `gh` for this review and [archived](solver-traces/existing-recovery-research-2026-10-24/README.md). They document algorithm removals, contribution audits and failed LP changes; they prevent treating superseded research proposals as missing implementations.

## 5. Implication for this investigation

We have substantial existing machinery for **getting around** a failed closed-form solve. The missing information is not “has anyone thought about primal feasibility or another inertia pattern?” They have, repeatedly, with concrete successes and falsifications.

For the current E1, the useful order is:

1. Keep its exact input/state/objective/rows/margin fixed; preserve the distinction between physical original feasibility and tightened fixed-model feasibility.
2. Classify its numerical failure using the existing convergence research; do not assume a recovery kink or missed pattern solely from the old literature.
3. Reuse the existing alternative primal/fold/homotopy candidate generators in a focused comparison, reporting their model residual, inertia consistency and replay acceptance under the same policy.
4. If they rescue it, decide how to expose that recovery inside windows with bounded cost. If not, try the existing global/pattern search on that same short window, rather than the full 808-tick route.
5. Only then design new numerical machinery for the uncovered failure class.

No such comparison was executed in this documentation pass. This file maps prior results and current code, rather than claiming a new solve. Current expectations and unit-test assertions are evidence of intended contracts; they were not rerun and are not a fresh green-suite report.
