# Research: failure patterns relevant to window selection

Date: 2026-10-24. User priority: understand general failure patterns and existing knowledge, so window selection can avoid fragile subproblems. Jump6 is one example; repairing it is secondary. No new solver runs, tests, numerical sweeps or implementation changes in this pass.

Companions: [source/recovery catalog](angle-solver-existing-failures-and-recoveries.md), [convergence plan](angle-solver-convergence-investigation-plan.md). This review adds upstream experiments that were not covered in detail in those documents and develops their implications for window selection. Minecraft1.8 per-axis checks are the target; modern-era results are explicitly scoped.

## 1. New upstream evidence worth preserving

Three detailed issue422 comments survive even though their referenced research directories are absent from this checkout. They are [archived here](solver-traces/window-pattern-research-2026-10-24/README.md).

### The simplification study tested the tempting shortcuts

[Step1 verdicts](https://github.com/Leg0shii/ParkourCalculatorMod/issues/422#issuecomment-5428306439) report:

- Forward/backward interval propagation left almost every inertia check ambiguous at the root across 30 exported problems: examples include 90 ambiguous checks on j1150 and 76 on j021. Position-only bounds did not infer a tiny set of inertia decisions. A velocity disk contains zero; an interval cannot express the disjunction “negative survival OR positive survival” as a single surviving interval. Replay-derived folding worked much better than the proposed root propagation.
- The pointwise costate formula was checked against an adjoint recurrence to about 6e-12. The issue was not that the friction-chain derivative was inherently absent. Nevertheless, recovered full-magnitude paths violated walls by 0.08–12.29 blocks on seven studied captures.
- A tight bucket neighborhood was useful within a committed solution family; across families the solutions could differ by hundreds or thousands of buckets. Local precision work did not substitute for structural discovery.
- Interface errors were amplified by factors 6.5–10.1 in the studied examples. A dense entry-velocity grid became prohibitive, and velocity alone was not enough to characterize the usable interface.

These are historical measured reports, not universal numerical constants. The source's stronger “recovery floor” assertions do not prove that every current stalled solve has a true gap. Our projected residual remains a valid stationarity diagnostic for the implemented smooth dual; small residual, valid full-magnitude recovery and physical feasibility are separate conditions.

### Disk-to-angle realization was tested, and the successful alternative exists

[M0 falsification experiments](https://github.com/Leg0shii/ParkourCalculatorMod/issues/422#issuecomment-5430366350) found:

- Direct normalization of disk controls failed: about 1 block of violation on thousand and 0.055 on j716.
- Greedy velocity/position tracking intended to reproduce shortened disk controls also failed. Thousand's relaxed solution shortened large jump-boost inputs, while later small air inputs had insufficient spare correction authority. Local SLP did not generally repair this tracking construction.
- Greedy chord restrictions inside the disk solve worked on the studied cases. They restricted each tick's direction to an arc and progressively required its acceleration magnitude toward the physical magnitude, re-solving between restrictions. This changed the optimization problem toward realizable controls instead of asking a decoder to manufacture them afterwards.
- The existing fold/chord/repair pipeline landed thousand, j716 and j1150 in Minecraft1.8. The separate 176-tick j003 success was on a modern combined-threshold model, so it is scaling evidence rather than a direct 1.8 result.

This is relevant to high-level “these should be easy” reasoning: a generous positional corridor can admit many physical paths while an optimizing disk relaxation chooses a very different, throttled path. The numerical solver and the realization policy matter as much as corridor width.

### The later implementation report gives limits, not just success headlines

[M1 results](https://github.com/Leg0shii/ParkourCalculatorMod/issues/422#issuecomment-5433258957) describe the Java port behind current `FoldReplayDriver`, `DiskSocpKernel`, `AnchorSlp` and `BucketWalk`. They also record:

- Replacing SLP's default seed with the proposed new seed was implemented, measured harmful on j021, and reverted. Better seeds for one method are not automatically better for another.
- New candidate generation removed some feasibility failures but introduced objective/latency tradeoffs; it did not supersede every old recovery.
- A kernel had previously reported convergence on infeasible affine specifications; a weak-duality check was added. Solver status must be checked against residuals and the exact model.
- Some knife-edge witness values were invalid under corrected authored constraints. The exact spec is part of every result.

The later PR427 retained `RelaxationRecovery` and `BoundPrunedRecovery` as load-bearing stages. The historical story is complementary methods, not one universally superior replacement.

## 2. Patterns a window policy could detect

“Detectable risk” means a mathematically or empirically motivated warning. It does not mean the window is infeasible or guaranteed to fail.

| Pattern | Evidence / mechanism | Detection available | Candidate response to study |
|---|---|---|---|
| Objective pushes directly into an endpoint wall | Exact same-axis objective/wall normals can cancel; common historical failure class | Static axis/sense/tick inspection; `ObjectiveBounds.directCap` already does it | Choose another provisional objective or boundary. Preserve real map constraints. |
| Objective almost duplicates an earlier same-axis bound | Long friction-weighted prefixes can be nearly proportional, leaving only a short tail to distinguish them | Compile coefficients and compare signed, magnitude-weighted normals; activity still unknown | Compare nearby endpoints/objectives; do not merely check equal tick numbers. |
| Tight corridor or crossing plus little remaining adjustment authority | Opposing time-indexed bounds force a particular displacement or reversal; relevant variables may be concentrated on a few ticks | Static constraint cluster plus exact friction/input coefficients; stronger evidence from a candidate or relaxed solve | Include enough lead-in and continuation to distribute control; avoid treating nearby wall pairs independently. |
| Disk solution relies on shortened physical inputs | Existing 1.8 M0 failures and chord-narrowing successes | Requires a relaxed solve, not just constraint counting; inspect magnitude deficit, especially large takeoff inputs | Try a different endpoint/objective, or use existing chord/fold recovery. |
| Boundary lands on a redirect, braking or near-threshold episode | Nix setup-tick asymmetry; single-zeroing and threshold-survival witnesses | Ground/key schedule is static; threshold proximity needs a candidate | Keep the coupled episode inside the horizon or use setup/pattern recovery; avoid committing through it blindly. |
| Provisional endpoint is spatially good but dynamically poor | Nix lead-in position/velocity counterexample; exact-state chaining research | Requires a path and some next-horizon constraints; position alone is insufficient | Compare candidate entry states against future requirements; reduce commit or change horizon when justified. |
| Duplicate/dependent wall representation | Exact duplication confirmed in jump6; dual multiplier nonuniqueness | Static compiled-row comparison; numerical rank requires an active/free set | Normalize/deduplicate for diagnosis; this is not inherently a reason to move the window. |
| Robust margin makes a model contradiction | Opposing-band algebra; existing free-start pair cap | Static check of each tightened opposing pair, after folding when needed | Fix the margin/model attempt, not the physical window. |
| Recovery unavailable or starved | Current equality/facing guards, graph tick caps, deadline gates | Static code/config eligibility plus trace attribution | Use an eligible existing recovery or choose a different decomposition; extra time alone may do nothing. |

### How to make the second row precise

Write the maximization objective as c·u and each wall as a_j·u <= b_j. The costate vector over all ticks is c minus a nonnegative combination of inequality-wall normals. A wall is directly cancellation-capable when a_j is a **positive** multiple of c. For near-parallel vectors, compare their residual after the best nonnegative rescaling. Weight each tick by its available movement magnitude, since ticks with no input have no control authority.

A same-axis wall with the opposite orientation is not the same warning: maximizing Z toward Z<=hi is the obvious case; maximizing Z with only Z>=lo is not. Several walls can cancel collectively even when no individual wall matches, so pairwise similarity is a limited warning rather than a complete test.

Moving a boundary by a jump changes the row set, objective coefficients and control horizon. Moving it by one tick usually retains much more of that geometry. Neither should be assumed to remove cancellation without checking compiled coefficients. A velocity objective also has constraints and can be cancellation-prone; it is not a guaranteed cure.

### Corridor width is not a hardness classifier

A narrow band can have an easy stable feasible response; a wide corridor's coordinate optimum can be degenerate. The useful quantities are what becomes active, which controls can distinguish those active rows, and whether a physical candidate exists away from the selected optimum. Static width and tick count are descriptors, not reliable failure predictions.

A pair of adjacent bounds can constrain the intervening displacement. It is tempting to call that a velocity target, but stored velocity, inertia zeroing, acceleration and position-update order matter. Use the model's compiled state-difference row rather than an informal velocity formula. A row can be weak because most older-input effects cancel in this difference, leaving only a small set of useful controls.

## 3. What windowing can address, and what it cannot

There are three distinct targets:

1. **Numerical tractability:** avoid an objective/row geometry that the current dual handles poorly. Another boundary may help without changing the route's physical feasibility.
2. **Physical continuation:** avoid committing an entry state that cannot negotiate later obstacles. This needs information beyond the current endpoint coordinate.
3. **Recovery coverage:** give an inner problem access to a method that can find its required inertia pattern or basin. Merely making the window shorter does not ensure the current CF/SLP pair covers it.

The repo has evidence for all three, but not a validated policy combining them. A window can be numerically easy and commit a poor continuation, or numerically difficult while admitting an easy physical route.

### A feasible overlap should be preserved as evidence

Standard MPC has a useful precedent: its suboptimal formulations carry a feasible control sequence forward and construct a successor warm start, with terminal conditions supporting continued feasibility. See Rawlings/Mayne/Diehl, [section2.7, especially Algorithm2.43](https://sites.chemengr.ucsb.edu/~jbraw/mpc/MPC-book-2nd-edition-1st-printing.pdf#page=196). This is a design analogy, not a theorem for arbitrary Minecraft routes.

For this mod, keeping a validated uncommitted yaw suffix would distinguish “cannot rediscover a known continuation” from “new horizon constraints need a different continuation.” Extending that suffix to new jumps is still unsolved work; carrying it does not prove the new horizon feasible. Under our relaxed lookahead checks, the suffix first needs validation: only the committed prefix is guaranteed to replay, even when the entire continuous window passes. Exact start-state chaining alone does not retain a feasible future control sequence.

The mod's existing record already proposes backward-reachable seam constraints for longer coupling horizons. Dense velocity grids were tested and found impractical in the upstream study. A usable state region or retained witness is different from pinning velocity to one recorded value, which previous experiments rejected.

## 4. Evidence standards for a useful window rule

Before promoting a warning into a rule, require:

- A clear mechanism and an exact feature definition. “Lots of walls,” “100 ticks” and “inertia happens” are not enough.
- Feasible witnesses or appropriately scoped certificates distinguishing solver failure from true infeasibility.
- Matched nearby cases holding entry state, margin, solver version and retained constraints constant where possible.
- More than one capture family, plus examples where the feature is present and the solver succeeds. Otherwise we cannot estimate false alarms.
- Measurements of solve success, candidate clearance, continuation and cost. Optimizing only local success can worsen the route after commitment.
- A declared scope: pure positional rows, tied facings, 1.8 per-axis thresholds, pinned/free start, and terminal versus provisional objective are materially different populations.

Existing generated-mutation benchmarks could support this later, but their old run matrices contain removed algorithms. The survey's corrected results also show why a small easy development corpus is insufficient: an initially claimed feasibility-dominant preset was refuted when omitted hard cases were restored. See [survey sections8b–8c](solver-graph-learning-survey.md#8b-generated-corpus-extension-2026-07-1819).

## 5. Jump6's role in the broader investigation

Jump6 establishes one useful counterexample: the exact fixed entry admits a valid physical 6–12 path, yet the cold0.0003 Z/MIN solve is not stationary and violates its own continuous model while satisfying its inertia assumptions. This disproves an explanation based only on impossible entry or replay threshold mismatch.

It also contains duplicate X87 rows and nearby conflicting Z requirements, but neither is yet isolated as the dominant cause. The endpoint matching wall was already omitted in that attempt, so “avoid direct endpoint caps” cannot be the entire policy. No evidence yet says that shifting a jump specifically fixes its underlying numerical problem; later experiments should test that hypothesis rather than silently replacing its problem.

The broader research priority is therefore:

1. Consolidate mechanism-specific existing findings and their falsifications, including the newly recovered ARCH-2 comments.
2. Specify warnings in terms of signed objective/constraint geometry, available control authority, candidate magnitude deficits and boundary continuation.
3. Audit which warnings are cheap static facts, which need one relaxed solve, and which require future-state evidence.
4. Use a small, representative matched-case study before implementing adaptive window selection.
5. Use jump6 as one deliberately difficult case in that study. Rescue comparisons are diagnostic evidence, not the objective of the whole investigation.

No general success/failure classifier has been established. The strongest immediately reusable findings are direct objective-wall detection, analytical margin consistency, existing primal/chord/fold recovery, explicit threshold-survival/zeroing hypotheses, and retaining exact continuation evidence.
