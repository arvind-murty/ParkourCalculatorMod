# Joint model scaling comparison

2026-10-31. Source: `edcdc12`, isolated branch `spike/ipopt-mixed-integer`. [Protocol and reproduction](../../mip_spike/SCALING_CAMPAIGN.md). Six cases, two repeats, alternating method order, 24 measured runs. All cases are synthetic finite-box extensions of existing fixtures. Production planner code is unchanged.

## Finding

The current joint formulation works on the simpler routes but does not improve this campaign's solve rate or speed. The existing scheduler accepts 10/12 runs; the joint model accepts 8/12. Existing search is faster on all four cases both methods solve, and reaches better positional objectives on two of them. It also solves the five-jump wide-neo route, where joint search times out without an incumbent. Both methods remain unknown on the three-jump, two-wall case.

A separate four-run check keeps the same 20-second total budget and raises only the joint per-horizon cap. It solves the two-wall case in 36 ticks, where the original scheduler stayed unknown, with a first scene-valid candidate at about 13 seconds. The five-jump wide neo remains unknown. Thus horizon restart policy matters, and there is a useful joint-solver success beyond the baseline, but neither configuration is a consistent replacement. These results support work on budget policy and continuous primal candidate generation before expanding the movement catalogue. They do not establish that unknown cases are infeasible.

## Results

Values below are medians of two repeats. Each method has the same nominal 20-second budget; joint horizons receive at most six seconds. Shared preparation is measured separately. The first-time column is time to a scene-valid candidate: the baseline measures its actual scene check; joint availability is reconstructed from a captured SCIP timestamp, estimated process overhead and measured validation cost, with validity checked after horizon optimization. Actual validation completion times are also retained in the data. The joint backend does not yet stream those early candidates to the caller.

| Case | Jumps | Potential allocations | Existing ticks | Joint ticks | Existing first / total seconds | Joint first / total seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Flat | 3 | 72 | 36 | 36 | .296 / .304 | .394 / .409 |
| Flat | 5 | 432 | 60 | 60 | .257 / .263 | .528 / 6.149 |
| Required run | 3 | 72 | 37 | 37 | .175 / .179 | .380 / .388 |
| Different heights | 3 | 32 | 31 | 31 | .309 / .312 | 3.661 / 6.129 |
| Cross plus wide neo | 3 | 32 | unknown | unknown | — / 19.056 | — / 19.618 |
| Wide neo | 5 | 432 | 62 | unknown | 7.871 / 19.103 | — / 19.617 |

Every case repeats the same acceptance outcome and accepted duration. No minimum-duration or global physical objective certificate is claimed. The potential-allocation counts describe the allowed run-count domain, not the number either algorithm enumerates. In particular, the successful zero-run flat cases do not require examining all allocations.

Configured objective is final X, maximized after elapsed ticks. On shared accepted durations:

| Case | Existing objective | Joint objective |
| --- | ---: | ---: |
| Flat, three jumps | 11.718729645 | 11.718729645 |
| Flat, five jumps | 20.351631420 | 20.101635084 |
| Required run, three jumps | 12.478317241 | 12.478317241 |
| Different heights | 9.611672238 | 9.605147071 |

The baseline wide-neo result uses allocation `[2, 0, 0, 0, 0]`, 62 inputs, and objective 20.794248114. Both repeats pass full acceptance. The nominal search budgets include startup, model building and final acceptance, with a small validation reserve; actual elapsed times are retained. The two methods may stop early or consume different amounts of their available budget.

## Where joint search spends time

Under the primary six-second horizon policy, the failure is not a demonstrated explosion of branch-and-bound nodes:

- Five-jump flat, zero added runs: only five departure selectors, one fixed departure per leg. SCIP finds a valid candidate in about half a second, then remains at the root for its six-second budget. In the first repeat it solves 205 LPs with 119,077 LP iterations, ending about .25 below the existing objective. Schedule choice is already fixed in this horizon, so the inertia/control formulation and continuous optimization are a bottleneck even here.
- Different heights, three jumps: the first valid joint candidate appears at about 3.66 seconds. It builds the model in about .04 seconds and explores roughly 200 nodes during the six-second horizon budget. It does not close the small remaining objective gap.
- Two-wall case: joint horizons 36, 37 and 38 each spend six seconds at one root node without a solution. The final partial horizon also remains unknown. Models contain 3,740 through 4,673 original SCIP variables, with only 3 through 9 departure choices. The original baseline explores 14 contact branches in repeat 0, finding collision-free prefixes/continuous candidates but no fully accepted route before its deadline.
- Five-jump wide neo: horizons 60 and 61 are quickly rejected within the represented model. Horizons 62–64 each spend six seconds at the root with no incumbent. At horizon 62 there are 13 departure choices, 7,390 original SCIP variables and 46,955 LP iterations in repeat 0. The existing scheduler finds a complete scene-valid trajectory after 8.65 seconds in repeat 0 and 7.09 seconds in repeat 1.

These are original-model variable counts; SCIP can eliminate fixed variables in presolve. They are not counts of independent schedule decisions. Root LP statistics identify where computation occurs, but do not by themselves separate nonlinear primal heuristic time, cut generation, and other root processing.

## Supplemental horizon-budget check

The primary campaign exposed a confound: a short per-horizon cap repeatedly restarts the root search at longer durations. Four additional joint-only runs keep the same source requests and 20-second total budget, with a 20-second per-horizon cap. They are not included in the primary 24-run counts. No baseline result, seed, selected duration or allocation is supplied to these runs; horizon search still starts from each case's own lower bound.

| Case | Duration | First valid candidate / total seconds | Objective |
| --- | ---: | ---: | ---: |
| Five-jump flat | 60 | .638 / 19.723 | 20.101635084 |
| Different heights | 31 | 3.460 / 19.678 | 9.610116324 |
| Cross plus wide neo | 36 | 13.048 / 19.667 | 10.581372034 |
| Five-jump wide neo | unknown | — / 19.624 | — |

The two-wall result is raw-scalar/scene accepted with zero run ticks. It is found while SCIP is still at the root. The longer five-jump wide-neo attempt gets through the root and explores 197 nodes at horizon 62, but still has no incumbent. The flat objective does not improve even with the longer budget. The height objective improves, while remaining below the existing result. All three new accepted witnesses are freshly revalidated.

SCIP's installed wheel already includes Ipopt 3.14.19, with NLP enabled and its sub-NLP heuristic at the default frequency of one. The measurements are not explained by an absent nonlinear solver. [backend-stack.json](backend-stack.json) records the inspection. A domain-specific continuous seed/heuristic or stronger formulation is a distinct follow-up experiment.

## Comparison scope and validation

Both methods receive the same route, finite geometry, original state, ordinary sprint-jump/sprint-45 inputs, shared footprints/coefficients, run bounds, objective and per-block passage caps. Each constructs its own seeds; neither receives saved angles or allocations. The baseline uses the existing prefix run search, bounded passage generation and production objective/equal-cost settings, with no optional hits. Full position, velocity and ground scene acceptance is enabled. Every accepted final witness is freshly recompiled/replayed and independently checked again.

The candidate representations and bounded policies differ: existing search generates per-leg passages; joint search uses global face slots and increasing horizons. This is a comparison of those implementations, not proof that their represented search spaces are identical. An unknown remains unknown. The synthetic plane extrusions/extensions are explicit in provenance; these are not real Linkcraft geometry tests. Existing Linkcraft fixtures include movement outside this subset.

All 18 primary and three supplemental accepted final witnesses, plus all 10 baseline first-scene records, pass fresh independent checks. The focused gate passes 38 methods, including source physics, 729 one-input clearance controls, instrumentation, domain counting and every saved campaign witness. No mod Java suite or game replay was run.

## Artifacts

- [summary.json](summary.json): aggregate times, objectives and acceptance counts.
- [measurements.json](measurements.json): all 24 measurements, complete bounded-search diagnostics, incumbent validation records, root/LP statistics, source provenance and witness checksums.
- [manifest.json](manifest.json): the 18 accepted witnesses and their case checksums.
- `requests/`: all six authored campaign requests.
- `cases/`: normalized shared scene, route, profiles, coefficients and footprints for each request.
- `witnesses/`: accepted typed plans, full original-state numerical problems, trajectories and shared-scene acceptance, retained for both repeats and methods.
- `first-scenes/`: baseline's first full-scene-valid candidates and velocity contracts, independently rechecked after measurement.
- [supplemental/measurements.json](supplemental/measurements.json): the four longer-horizon-budget results, diagnostics and three additional witnesses.
- [backend-stack.json](backend-stack.json): installed nonlinear backend and unchanged relevant defaults.

## Recommended follow-up

Keep this campaign as the regression/performance reference. The supplemental check supports allowing more root-search time at promising short durations. The next useful experiments are: use the existing fast continuous optimizer's domain-specific seeds as a primal heuristic on solver-selected discrete assignments; test a feasibility-focused SCIP policy; and reduce or strengthen the inertia/control formulation, using the zero-run five-jump flat case as an isolated control. Preserve joint integer freedom after producing a continuous candidate. Only the per-horizon budget variation was tested here; none of those backend/formulation changes was implemented. Optional wall-hit/ceiling/step/ladder scheduling remains deferred.
