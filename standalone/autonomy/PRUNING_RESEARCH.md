# Pruning research and search framework

2026-10-30. Living research record for the consolidated standalone solver. Update this document after each research discussion with relevant findings, user decisions, recommendations and open questions. Canonical branch: `feature/ipopt-consolidated`. Initial research checkpoint: `65db8c5`; latest checkout confirmed during planning: `99ecfe9`. No runtime behavior changes or new performance measurements accompany this record.

Read [PRUNING_RESEARCH_HANDOFF.md](PRUNING_RESEARCH_HANDOFF.md), [GENERIC_COMPOSITION.md](GENERIC_COMPOSITION.md) and [SEARCH_OBJECTIVE.md](SEARCH_OBJECTIVE.md) for implementation context and movement requirements.

## User direction and decision status

- User direction: decide the overall search framework before selecting individual pruning optimizations. The earlier pruning recommendations are examples to retain, not an agreed implementation sequence.
- User instruction: persist relevant information and decisions after every turn.
- Existing objective remains minimum total elapsed ticks, then the configured X/Z MAX/MIN objective among equal-duration solutions.
- Water remains deferred. Preserve the supported movement families, self jumps, revisits, free turning and optimization from the original full initial state.
- User-endorsed conceptual direction: partition free space into 3D cells; broad paths are cell/interface sequences; several ticks can stay in one cell. Local optimization chooses crossing/contact timing and movement methods, so the same spatial path can have different elapsed costs. Baritone motivated formalizing this broad layer. No particular partition or search implementation has been chosen.
- Revised hierarchical proposal: an untimed graph of spatial regions/passages selects route families; the sparse timed constraint-event graph realizes and optimizes each family. Retain sufficient previous-movement information across both levels. The previous single-level timed graph is now the local layer, not the entire spatial route representation.
- Implementation checkpoint: isolated branch `research/ipopt-cell-routes`, experimental source `3257571`. [Non-ladder benchmark results and recommendations](experiments/cell-routes/README.md) cover 309 capped runs, 22 replayed witnesses and forward-propagation ablations.
- Latest user decision: defer bound research. Plan and benchmark cell construction, spatial route generation and the local decisions within each selected route; determine whether that hierarchy sufficiently reduces search or another abstraction is needed. Optimize the small selected route pool individually and compare best found time/objective.
- Before writing implementation code, use a separate branch and worktree. The active staged plan is [CELL_ROUTE_EXPERIMENT_PLAN.md](CELL_ROUTE_EXPERIMENT_PLAN.md), with prescribed-route and automatic-route comparisons to separate local complexity from route-selection failures.
- Source findings and proposed selection architecture are persisted in [HEURISTIC_SOURCE_REVIEW.md](HEURISTIC_SOURCE_REVIEW.md).
- Revised research recommendation: derive a graph of free-space regions and finite passages from expanded collision geometry, attach contact/movement opportunities, compress incidental subdivisions, and shortlist diverse spatial passage signatures. Search local methods/timings inside those families using the timed event graph and whole-history solver. This concrete geometry abstraction is recorded in [SPATIAL_ROUTE_RESEARCH.md](SPATIAL_ROUTE_RESEARCH.md); it is proposed, not implemented. One scalar arrival distance remains insufficient in general, and B&B is optional refinement machinery.
- User direction supports designing heuristic candidate retention. No particular weights, pool cap, multiplier-elimination rule, resumable-generator implementation, search backend or global continuous solver has been selected or implemented.

## What the framework must represent

A route is a hybrid planning problem: discrete event/control/contact choices coupled to continuous positions, velocities and facings. Landing on the same block does not make two prefixes interchangeable. Their arrival momentum, feasible position regions, sprint/input state and contact obligations can permit different continuations.

The representation must preserve the effect of earlier constraints and optimize horizontal motion consistently with the original state. That information can live in a reachable-state relation with shared provenance rather than a separate graph vertex for every prefix. Where a sufficient summary is unavailable, retain shared constraint-history handles as a correctness fallback. Scalar Y/VY and ground/collision state can remain exact where the discrete prefix determines them. If a search node leaves vertical choices unassigned, it needs a set/enclosure or a conditional evaluator, not an invented single Y state.

A cached region is an outer approximation used to reject contradictions. It is not an exact reachable-state summary. Inclusion between two outer boxes does not establish dominance between their actual feasible sets.

Future constraints may reoptimize earlier facings. They cannot repair an impossible already-required prefix when the impossibility is established over all its allowed controls. Failure of one numerical trajectory does not establish that impossibility.

## Shortest path versus branch and bound

These are overlapping families of search methods, not an exclusive mathematical dichotomy. A* orders path prefixes by elapsed cost plus an admissible remaining-cost estimate. Branch and bound maintains an accepted incumbent and excludes candidate families whose valid bounds cannot improve it. A best-bound branch-and-bound implementation on a path-prefix tree can use the same ordering as A*.

The consequential choices are node semantics, valid bounds, when choices are materialized, and what establishes infeasibility.

| Approach | Strength | Difficulty in this solver | Recommended role |
| --- | --- | --- | --- |
| Dijkstra/A* on supports or named movement events alone | Small graph, reusable reverse distances and intuitive route proposals | A support/event ID does not determine a Markov state. Edges that are individually feasible may require incompatible arrival states. Closing a block after its cheapest arrival loses valid routes. | Optimistic abstraction for bounds and proposals; not the authoritative feasibility search. |
| Shortest path on full physical states | Correct state can include velocity and discrete movement/input state | State is continuous. A finite grid adds approximation and can miss narrow solutions. Exact symbolic reachable regions are expensive and require correct label dominance. | Possible longer-term formulation; no evidence it is cheaper than retaining constrained prefixes now. |
| A* on complete symbolic prefixes | Retains history and can prioritize promising completions using admissible time estimates | Loses most graph merging benefits. Unknown numerical leaves and capped generators prevent first-goal optimality claims. Equal-time objective improvement still requires further work. | Compatible queue policy for the recommended framework. |
| Branch and bound on movement/contact/schedule decisions | Explicit incumbents, family-level contradictions and time/objective bounds; fits whole-history numerical solves | Still exponential without useful bounds. IPOPT does not supply global bounds. Branch coverage and unresolved work must be tracked. | Earlier primary-framework recommendation; now a possible backend/refinement strategy for the sparse event graph. |

The generic queue already performs uniform-cost expansion of whole prefixes, exact-history deduplication, conservative screening in supported cases and pruning when elapsed prefix ticks exceed the accepted incumbent. It is therefore already close to a weak branch-and-bound tree with remaining-time bound zero. Rebranding it cannot produce a speedup.

A support-only graph can be useful even when some paths in it are physically impossible: that makes its lower bound optimistic. Conversely, a graph that omits feasible ladder/ceiling/stepping connections can overstate remaining time or falsely report disconnection. A graph used for rejection must cover every allowed real continuation through a suitable abstract path. Uncovered cases need a safe fallback bound, potentially zero.

## Earlier whole-prefix framework, retained for comparison

The next section records the initial recommendation. The sparse event graph proposal below supersedes its choice of primary topology. Its incumbent, pruning and proof-accounting rules still apply.

1. **Search node:** a constrained movement prefix plus its unresolved discrete alternatives and remaining budgets. Keep the full horizontal constraint history or a proved equivalent representation. Exact scalar boundaries, bounds and compiled data are cached summaries, not frozen horizontal arrivals.
2. **Branch:** select a next movement family/region, timing or contact/control domain. Initially reuse the existing movement grammar. Where justified, represent a family of entry ticks, contacts or masks before enumerating its concrete members. Children must cover the parent's alternatives; disjoint branches are preferable but exact overlap deduplication is also possible.
3. **Bound:** compute an optimistic completion time `L_T = elapsed + h`. Cheap geometry or interval contradictions can exclude a family before full schedule generation. Derive `h` from safe dynamics bounds and, later, an optimistic event graph. Do not use a candidate trajectory or an observed run count as a lower bound.
4. **Incumbent:** accept only a complete scalar/shared-scene-validated numerical witness. Rank it by `(ticks, -value)` for MAX or `(ticks, value)` for MIN. IPOPT produces candidate witnesses and useful seeds, not global rejection proofs.
5. **Prune:** reject a sound contradiction, an exactly equivalent/dominated family, or a node with `L_T > incumbent_ticks`. At equality retain it for objective improvement unless a valid objective bound proves it cannot improve the incumbent. For MAX, a safe upper bound on objective can exclude a same-time family; for MIN use a lower bound. A local IPOPT value is neither.
6. **Select work:** start with smallest `L_T`, then deterministic heuristic tie ordering. Best-bound selection is a policy, not a requirement of branch and bound. If finding the first incumbent is slow, evaluate a separate promising-completion probe without removing the unresolved frontier. Whether such scheduling is beneficial remains an experiment, not an agreed resumable-generator refactor.
7. **Track coverage:** distinguish exhausted/proved-impossible, accepted, numerical-unknown, deferred and heuristically dropped work. Include ungenerated alternatives and numerical-unknown leaves when computing a global lower bound. A discarded family can retain its inherited lower bound for reporting; it cannot silently disappear from the proof accounting.

This is an outer discrete search with conservative bounds. It is not a proposal to branch on every facing or globally solve all continuous variables. A continuous global optimizer could be a later backend for difficult fixed schedules, but is a separate cost and certification question.

### Stopping and guarantees

A feasible incumbent is useful at any deadline. Minimum ticks are established only when all possibilities with fewer ticks have been ruled out or a valid global lower bound matches the witness. A numerical-unknown shorter leaf prevents that claim. A queued goal is not automatically an accepted goal.

Equal-time candidates remain relevant after the tick minimum is known. Global objective optimality additionally requires valid continuous objective bounds and coverage of all equal-time alternatives. Exhausting discrete schedules and obtaining local IPOPT solutions does not suffice.

All guarantees must name the declared movement grammar, horizon and physical assumptions. Work caps are not completeness proofs. The existing generic proof flags should remain false until the corresponding conditions are actually implemented and satisfied.

## Sparse timed constraint-event graph: revised primary proposal

### User proposal and assessment

The user proposes one shared vertex for a type of constrained interaction at a specific tick, such as running, jumping, landing, hitting or avoiding an obstacle, climbing/falling on a ladder, or stepping. A hit and an avoid of the same obstacle at the same tick are distinct alternatives. Normal airborne motion is represented by an edge spanning the unconstrained interval. The user recognizes that arrivals are not interchangeable and asks whether a sparse graph can retain sufficient previous-movement information.

Assessment: yes, this is a valid primary representation. The necessary distinction is between shared event topology and the arrival information attached to each event. A separate topology vertex per full history is unnecessary. A single distance and predecessor for each event is generally insufficient. This revises the earlier recommendation, which made complete prefixes the primary nodes.

This is a proposed design, not a measured performance result or an implemented solver. The user has directed the investigation toward this topology; they have not approved a particular reach-set representation or label-dropping rule.

### Event vertices and transition edges

A candidate key is `(tick, simulation phase, interaction predicate, geometry reference, movement mode)`. The predicate records the relevant physical block/region, face, direction and finite footprint, or references their shared geometry record. Include phase and mode only where they affect semantics; do not eagerly construct all combinations.

Examples: ground run on support A; jump eligible from support A; landing on B; signed X wall hit; outside a selected obstacle face; ladder entry/exit/phase change; named stepping case; ceiling hit. A predicate may constrain more than endpoint position, including velocity reset, required pre-input overlap and input eligibility.

An edge from state tick `t` to state tick `t+k` has weight `k`, not one unit per named interaction. It carries the complete transition relation: existence of controls and intervening physical states satisfying the movement dynamics, contacts and invariants. Normal air ticks need no separate topology vertices but remain in this relation and the final numerical solve. Different permitted durations are separate timed transitions or a duration domain, not one guessed local minimum valid for every arrival.

Running and ladder occupancy are interval conditions. Represent their boundaries/change points as vertices and enforce their per-input controls/membership on the intervening edge. If a contact or stepping effect changes the mode inside that span, split there. Obstacle avoidance may require a swept interval condition, not merely a point outside a box at the endpoint. An edge cannot silently skip an interaction that changes its dynamics.

Several compatible effects may occur on the same input. Use a bundle of predicates or ordered phase vertices. Preserve Y then X then Z collision semantics and existing support-check timing. Same-tick phase edges have zero elapsed cost and a fixed acyclic order. Do not force a combined ceiling/wall or step/ladder event to consume extra ticks, or treat compatible predicates as exclusive alternatives.

For a finite horizon, forward-time and ordered-phase edges give a DAG. A physical block can recur at later ticks, preserving self jumps and revisits. Construct nodes/transitions on demand from geometry and feasible time windows; naive all-event/all-tick expansion can still be large.

### Absolute ticks change what shortest-path distance means

If all routes start at tick zero and an edge weighs its actual time difference, every path to `(event, t)` costs exactly `t`. Edge costs telescope. Therefore two arrivals at the same timed event do not differ in elapsed cost; they differ in reachable state and continuation options. The time objective selects the earliest reachable goal event, then the numerical objective within that goal time.

If event templates omit absolute time and edges instead carry durations, the same event may be reached at different costs. A single cheapest arrival still cannot close it unless its state possibilities dominate the alternatives. Timed vertices and untimed templates are two representations of the same timing decision; do not count elapsed time twice.

### Sufficient arrival information

The physical boundary information potentially needed is joint position and velocity, ground/support condition, ladder membership, previous horizontal collision, and any input/sprint compiler state that affects future coefficients. The event itself may fix some components. Include future-relevant grammar/resource state as well: current ladder episode/phase, pending ladder-ceiling successor ownership, and per-physical-block event usage under the configured caps. These can be separate small discrete modes or labels rather than duplicated physical geometry.

Facing is not automatically an additional state dimension: current scope has free turning and no requested facing constraints. Carry a facing/history variable only if an actual future rule depends on it. Additional nonlocal constraints would also need their relevant boundary information.

For a fully sufficient state `s`, event transition relation `T_uv`, and exact reachable set `R_u`, propagation is:

```text
R_v = union over incoming edges (u,v) of
      { s_v : some s_u in R_u satisfies T_uv(s_u, s_v) }
```

The event predicate is included in the transition/result restriction. All histories can be forgotten for feasibility once their exact reachable union and all future-relevant mode/resource state are retained. Keep shared witness/provenance links to reconstruct a trajectory from the original start. The reachable set can be disconnected and strongly correlated. A symbolic union or shared constraint DAG can compress its representation, but does not automatically make membership or propagation cheap. The existing fixed-schedule IPOPT compiler does not directly solve an arbitrary disjunction of histories.

Practical implementation options to evaluate are multiple correlated arrival regions/labels, implicit reachable-set formulas with shared predecessors, and conservative outer regions with lazy refinement. A label need not contain a copied full path. Shared event/edge/constraint storage is useful even before proving that many arrival labels can be eliminated.

### Why one best arrival fails, and when merging is safe

Consider two paths reaching the same landing predicate at tick 12: one has eastward horizontal carry, the other westward carry. The predicate can hold for both, while an immediately following eastward gap may only be reachable from the first. They are equal-time arrivals, not a cheaper and more expensive pair. Keeping either one arbitrarily can lose the route. A wall hit resets its normal velocity component but usually leaves tangential velocity and the point of contact free; a landing or ceiling hit likewise does not reset all state.

At the same timed event and with identical future-relevant discrete state, label A subsumes label B for continuation feasibility if A's actual feasible boundary-state set contains B's. Exact union merging is also valid: preserve both sets as alternatives behind the shared vertex. A containment test between loose outer boxes alone proves neither subsumption nor feasibility. Discarding a known feasible B because its box lies inside A's mostly unreachable box is unsound.

True reset events can make merging easier because some earlier state components cease to matter. Derive those reductions from the complete post-event state and future rules. Do not assume that a named contact is a complete reset.

Checking that each edge has some feasible endpoint pair is insufficient. Adjacent edges must share the same event boundary state. An incoming edge that only reaches positive X carry cannot connect to an outgoing edge that needs negative X carry merely because both touch the same predicate. A composed solve or a valid reach-set propagation enforces this compatibility.

### Current design recommendation and next question

Use the sparse timed event graph as the primary shared topology, with a set/label of possible arrivals attached to each vertex. Use DAG/time-ordered or A*-style processing as appropriate, plus incumbent bounds. Branch and bound can remain a method for resolving event-path, mode or continuous-feasibility ambiguity; it need not define the topology.

The central unresolved problem is how to keep arrival information compact without silently losing useful states. Compare candidate representations by joint-state fidelity, exact merging/dominance opportunities, update cost, number of labels per shared event, and numerical solve latency. A fixed label cap or a single representative witness is an explicit heuristic and must report possible loss of coverage. Sparse topology does not guarantee polynomial total work: complexity can move into labels or reachable-set formulas.

A small framework experiment should contain two histories joining the same event/tick with incompatible momenta, an event requiring simultaneous predicates, an interval with no explicit air vertices, and a genuine reset allowing state merging. Verify edge-state continuity, unchanged elapsed cost, preservation of the useful arrival and reconstructable whole-history acceptance. This experiment is proposed, not run.

### Related primary research

[Marcucci et al., Shortest Paths in Graphs of Convex Sets](https://arxiv.org/abs/2101.11565), particularly the [problem statement and control applications](https://arxiv.org/html/2101.11565v5), provides a close structural analogy: graph vertices carry continuous sets, and edge constraints couple their state variables through dynamics. The paper also establishes that the generalized problem is not ordinary polynomial-time shortest path in general.

Our event dynamics are not automatically convex: fixed-magnitude turning, inertia thresholding, caps and contact/phase alternatives matter. This is a conceptual reference and a possible source of conservative convex relaxations, not a claim that its convex solver applies directly or an endorsement of a mixed-integer backend at this stage.

## How branch and bound could help inside the sparse event graph

The user requested clarification of this role. Branch and bound is optional machinery for unresolved route/state alternatives within the shared graph; it does not require replacing each timed event vertex with a full-path vertex.

### Separate event topology from search subproblems

A graph vertex still means a timed interaction predicate. A branch-and-bound subproblem means a family of solutions in that same graph, restricted by selected event alternatives, timing domains, movement modes or continuous arrival-state conditions. Several search subproblems can share the same vertices, transitions and compiled templates. Splitting a subproblem is not a new physical event or a consumed movement tick.

Maintain an accepted complete incumbent with time `T_best`. For each unresolved subproblem, obtain a valid lower bound on earliest possible completion. An optimistic shortest path in a graph with relaxed dynamics can provide that bound, provided it covers every real completion in the subproblem and charges actual elapsed time. Relaxed adjacency can admit incompatible arrival/departure states: that weakens the lower bound but does not invalidate its direction. A relaxed path is not a feasible witness.

If a subproblem's bound exceeds `T_best`, discard all its event sequences and arrival histories together. A necessary-state contradiction also excludes it, even before any incumbent exists. If the bound can improve the incumbent but a coarse region admits incompatible states, refine only that unresolved region or event decision. The children must cover the parent's real alternatives. They continue to reference the original incoming feasibility relation; splitting an outer box does not make every state inside a child box reachable.

An IPOPT solve can supply a candidate witness and improve the incumbent after independent acceptance. Its local objective or failure cannot supply a global time/objective bound or infeasibility proof.

### Illustrative example, not measured results

Assume an accepted route finishes at tick 32. At a shared event at tick 12, an arrival family has both eastward and westward possibilities. Suppose conservative continuation analysis yields:

| Subproblem at the same event/tick | Optimistic additional ticks | Earliest possible goal bound | Consequence |
| --- | --- | --- | --- |
| Eastward arrival alternatives | 15 | 27 | Keep; actual feasibility and completion time remain unresolved. |
| Westward arrival alternatives | 23 | 35 | Discard the whole family because it cannot beat or match tick 32. |

Both arrivals already cost 12 ticks. Branch and bound helps by bounding the future, not by declaring the eastward prefix cheaper. The bounds must apply to every continuation, including allowed revisits and momentum-building detours. If a detour could reach the goal sooner than 35, then 35 was not a valid lower bound.

The same principle applies to a hit-versus-avoid decision, ladder entry/exit choices and groups of contact times. Use a coarse bound first, then split only where necessary. Merely branching into every concrete schedule before computing any useful bound recreates the original enumeration problem.

### What this buys and what it does not

Potential benefit: avoid generating, propagating or solving all members of a family once a family-wide contradiction or incumbent bound settles it. Regions that remain coarse and useful do not need refinement. Incumbent pruning, state-domain refinement and lazy event alternatives are independent mechanisms; implement only those whose measured savings exceed their costs.

It does not justify closing an event after one arrival, manufacture compact exact reachable sets, or guarantee a small number of search labels. Worst-case growth can remain exponential. If exact reachable-set propagation on the timed event DAG is already affordable, no additional branch-and-bound layer is required. Branch and bound becomes useful when unresolved state/mode correlations or event alternatives are too expensive to handle all at once, and sufficiently strong bounds exist.

For equal-time candidates, preserve the configured secondary objective: a bound equal to `T_best` cannot exclude a family on time alone. It can be excluded only by infeasibility, valid dominance or a certified bound showing no possible objective improvement. Track all shorter numerical-unknown and ungenerated possibilities before claiming the minimum.

Current recommendation: treat incumbent pruning as a generally useful facility; keep branch-and-bound state/domain refinement as an optional research tool. The primary framework remains the sparse timed event graph. No new user implementation decision, solver run or speed measurement accompanies this clarification.

## Candidate pruning and reuse mechanisms retained from the first discussion

These are mechanisms within the framework, not competing frameworks or an accepted build order.

| Candidate | Layer | Classification and assumptions | Survival checks |
| --- | --- | --- | --- |
| Forward reachable envelopes and backward necessary continuation conditions | Before ladder-entry, exit-face and contact-combination generation; prefix screening | Sound rejection only with outer bounds covering all allowed inertia, caps, collision resets and movement modes. Horizontal positions/velocities remain regions. An empty necessary intersection rejects; a nonempty intersection is not a witness. | Saved accepted trajectories; exhaustive short cases; incoming transverse momentum, braking and revisits. |
| Local finite-contact windows | `movement_contacts` and `contact_profiles` before products over boxes/axes/ticks | Sound when overlap windows include all possible prior contact histories. Baseline traces can rank opportunities but cannot rule out geometry that becomes reachable after an earlier clip. Regenerate Y/exit timing after selected mixed contacts. | Simultaneous/consecutive X/Y/Z; repeated ceilings; ladder-ceiling successor ownership; per-physical-block caps. |
| Shared Sneak-mask prefixes and exact vertical feasibility memoization | `ladder_schedules._mask_work` | Share simulation across identical control prefixes and reject all completions after a proven past Y violation. Key exact Y/VY, tick, ground/collision state, phase and future obligations. Equal Y alone does not permit dropping different horizontal histories. | Exhaustive emitted-schedule comparison at short horizons; first-clamped Sneak; endpoint stops; top/side/bottom exits. Measure cache reuse before committing to a large DP. |
| Exact schedule equivalence and computation caches | `open_ladder`, composition and compiler | Reuse movement computation for identical full movement/control semantics, including inherited input/sprint state. Retain distinct entry/exit faces and their constraints. Cross-prefix cache keys need all actual dependencies. | Compare full compiled effects and acceptance constraints, not only coefficients or final state; preserve phase/context boundaries. |
| Optimistic remaining-time bound | `movement_search` frontier and abstract event graph | Sound with an admissible estimate over every supported continuation. Use strict `>` time pruning and preserve equal-time objective alternatives. Shortest-path calculations are useful here. | Exhaustive small scenes; edge-support timing; short run-offs; airborne composition; self jumps/revisits. |
| Best straight-line multiplier per timing group | Schedule ranking or a future dominance filter | User-suggested grouping by total airtime and post-exit time is retained. Best progress from zero momentum is currently a heuristic score. It does not establish dominance with incoming carry, braking, caps, sprint state or intermediate geometry. Late Sneaks and the first-clamped exception need explicit treatment. | Search for counterexamples with transverse carry, required braking, tight contact footprints and exit-velocity tradeoffs. Initially reorder rather than eliminate. |
| Incremental scalar replay, compact canonical history and shared immutable prefixes | Composition, deduplication and cache storage | Behavior-preserving reuse, not infeasibility pruning. Preserve terminal carry changes when membership changes at the append seam, exact float behavior and all scene constraints. | Compare complete serialized programs, boundaries and compiled requests. |
| Small-batch/resumable generation | Work scheduling across local generators and global frontier | Optional latency/budget experiment. Does not itself reduce exhaustive work or prove any candidate infeasible. Track unseen alternatives and their lower bounds. User previously questioned its benefit; not approved as the next implementation. | Time to first full proposal and accepted witness, same-budget solution quality, coverage accounting. |

The existing occupied-Y short-circuit and omission of redundant floor prediction without a target floor are already implemented. Do not count them as new work.

## Heuristic shortlist priority and external source findings

Latest user direction favors selecting tens/hundreds of promising routes rather than making all route elimination depend on proofs. [HEURISTIC_SOURCE_REVIEW.md](HEURISTIC_SOURCE_REVIEW.md) records a fresh pinned-source inspection of Baritone and Curryocity's Stratfinder, plus our native Stratfinder comparison.

The central finding is structured candidate generation followed by cheap ranking and bounded evaluation. Baritone uses local movement templates, geometry checks and estimated-time A*; it merges by XYZ and does not represent our arbitrary arrival momentum. Curryocity searches backward through simple input segments and a small named strategy library, using approximate velocity transforms and selected pruning assumptions. The native tool adds a ranked screen/certification cascade and diversity-aware retention.

Revised practical recommendation: keep the sparse event graph, generate useful route families without eagerly multiplying all timings/masks, rank using estimated completion time plus momentum/constraint compatibility, and retain a diverse bounded pool. Compare 50/100/200 actual expensive proposals, not merely that many skeletons with uncounted internal attempts. A* ranking alone does not enforce a small candidate pool; a bounded best-first/beam retention policy does. B&B remains optional for sound family exclusions inside that search. No particular cap, score weights or implementation has been adopted or measured.

## Search terminology clarification: bounded best-first and beam search

2026-10-31. The user requested definitions of the proposed policies. No policy, beam width or numerical-attempt budget has been selected.

Best-first search keeps a frontier of partial candidate routes and repeatedly expands the candidate with the best heuristic score. Standard best-first ordering alone does not cap frontier size or total work.

In the earlier recommendation, "bounded best-first" meant a specific heuristic retention policy: maintain a global frontier of at most B partial candidates, select the best across all represented depths, add its successors, and discard the worst-ranked overflow entries (optionally using diversity rules). This phrase was descriptive, not the name of one uniquely defined standard algorithm. A work/time-limited best-first search is another possible meaning, so name the actual bound explicitly. A frontier cap is heuristic pruning and is distinct from branch-and-bound rejection based on a valid objective bound.

Classical beam search advances by layers. Expand every retained partial candidate in the current layer, score their combined children, and keep only the best B for the next layer. The width B is the beam width. A layer might be the number of event transitions, not elapsed ticks: our edges have variable durations, and a compound event need not be physically later than a simpler one merely because it has more predicates. Define layering explicitly if using beam search.

Example with B=3: the initial node produces eight children and only three survive. Expanding those yields, say, twelve grandchildren and again only three survive. Candidates discarded earlier are normally unavailable unless explicitly retained for backtracking/restarts. The policy can miss the only feasible or fastest route, which is the deliberate heuristic selection tradeoff. Diversity-aware retention reserves room for different mechanisms or arrival states instead of keeping three near-duplicates.

The global-frontier policy compares candidates at different event depths; layer-based beam search restricts competition to a common generation. They need not retain the same routes, and neither is inherently appropriate until the heuristic/layering is evaluated.

Keep three separate limits: live partial candidates B, candidate expansions E, and expensive complete numerical attempts K. A width of 100 does not mean only 100 routes or solves will be considered. The user's target of tens/hundreds of expensive proposals requires an explicit K budget; control expansion/generation effort separately. All policies continue to use the sparse shared event topology with the required arrival information, rather than a single closed distance per physical event.

## Hierarchical encoding: spatial route families and local realizations

2026-10-31. The user distinguishes two discrete optimization scales. Broad paths differ substantially in horizontal route, for example using/skipping an intermediate block or taking opposite sides of an obstacle. Local optimization changes interaction ticks or movement methods while following approximately the same broad route, for example tick 4 versus 5, earlier/later ladder entry, or flying over a block versus landing and running off. The user clarified that Baritone research was intended to inform this broad path representation.

### Encode the broad route without exact event ticks

Proposed outer graph vertices name geometric regions and passages rather than complete histories or every constrained tick. A passage is a finite area or crossing through which a trajectory can move: a side of an obstacle, a support neighborhood, a ladder approach/exit region, or a connection between open regions. They are regions, not exact waypoint coordinates. References retain actual block geometry and height extents.

A broad route family is an ordered sequence of meaningful passage choices, with a signature such as:

```text
start -> left side of wall A -> neighborhood of block B -> ladder L -> goal
```

Another family might take the right side of A or bypass B's neighborhood entirely. The signature intentionally omits exact contact ticks, run duration, Sneak masks and local movement method unless one of these changes the broad corridor itself. It also avoids recording every incidental contact as a new broad waypoint.

A family represents allowed full trajectories, not one approximate polyline to which the solver must adhere. A polyline can supply a search hint. Spatial passage restrictions used in actual acceptance must be explicit, finite and wide enough for the intended family. In particular, a horizontal obstacle projection must not act as an infinitely tall wall and remove a permitted overflight. Height bands and local contact possibilities matter.

The outer graph can use estimated minimum/typical transit time for each connection and geometry/momentum-aware continuation scores. Those are heuristic proposal costs; actual duration follows local realization. Do not claim the abstract shortest route is the globally fastest feasible route. Use one shared region/connection geometry record and carry a few selected arrival contexts when useful, rather than duplicating geometry by entire route history.

### Encode local alternatives as a graph or grammar inside a route family

Each broad connection references a small graph of permitted local movement choices. Nodes here are the user's timed constraint events. An edge can contain ordinary air or a controlled movement interval. Timing domains remain open until a candidate is materialized.

Illustrative record, not an implemented input schema:

```text
route family:
  passages: [start, wall_A_left, block_B_neighborhood, ladder_L, goal]

local choices:
  wall_A_left:
    crossing_tick: candidate window containing 4 and 5
  block_B_neighborhood:
    method:
      - fly_over
      - land -> ground_run(duration variable) -> run_off
  ladder_L:
    entry_tick: candidate window containing 10 and 11
    interaction: existing valid climb/fall/stop/contact grammar
```

The two block-B methods have different numbers of timed constraint nodes and different Y/velocity consequences. They still belong to the same broad family when they traverse the same spatial neighborhood. Local refinement must therefore permit adding/removing/replacing an event subsequence, not only perturbing numbers on one fixed event list.

Use the existing movement registry/generators for local method alternatives, scalar schedule generation for valid timings and contact regeneration, composition for whole-history requests, and IPOPT plus shared-scene validation for the continuous witness. A changed contact can alter downstream height, ground, momentum and duration: regenerate the affected schedule rather than merely changing its tick field.

### Define route identity operationally

Let a local program's spatial signature record its selected ordered passages/landmarks and corridor-side choices, excluding exact timing and incidental interaction modes. Local programs with the same signature compete within one family. Different signatures compete as broad routes. This is a practical planning abstraction, not a claim of exact geometric homotopy equivalence.

A block ID alone is not enough to define the boundary. Passing above a block with or without touching it can be local, while a detour to a different block that changes the corridor is broad. A required support landing, when explicitly chosen as a necessary landmark, must remain required; do not accidentally encode every block neighborhood as a mandatory landing. Repeated visits/loops remain possible and may be grouped as local momentum setup when contained in a neighborhood, or appear explicitly in the broad signature when they change its passage sequence. They are neither prohibited nor universally penalized out of the candidate pool.

The outer abstraction must expose method alternatives that materially change connectivity or plausible timing. For example, a corridor achievable only by a step or ladder contact must be discoverable before its precise local program is solved. Refining only ordinary-jump routes and hoping exotic movements appear later would undercut the intended coverage.

### Exchange boundary possibilities rather than frozen endpoints

Local connections communicate necessary/possible position and velocity regions, movement mode and relevant input/grammar state. They do not commit to the first solved arrival. A connection's time depends on its incoming state and chosen local method, so one scalar edge estimate is a ranking aid rather than an exact state-independent duration.

The final candidate is optimized from the original full initial state with earlier facings still adjustable. A windowed local edit may use the preceding incumbent as a seed, but changing a contact or ladder entry must be allowed to reoptimize earlier movement. Neighboring connections can be refined together when momentum or vertical timing couples them. Independently optimizing each corridor with fixed seam states is not sufficient.

### Search the hierarchy with feedback

1. Generate and rank a few geometrically distinct route families using a Baritone-inspired region/movement graph and coarse transit/momentum estimates.
2. Give each selected family an initial local-realization probe, so the first attractive corridor does not consume the entire budget.
3. Spend more local attempts on promising families: adjust timing windows, change local methods, and reuse good numerical seeds. This can use a small beam, best-first local search or another neighborhood policy; none is chosen yet.
4. Feed actual duration, constraint deficit, useful arrival momentum and numerical uncertainty back into outer ranking. A failed local probe is not a proof that the family has no valid realization.
5. Retain diverse broad families separately from nearby local variants, and compare all accepted results by actual elapsed ticks and then the configured objective.

Budget broad families, local proposals and actual numerical attempts separately. A global total numerical-attempt budget prevents a small family count from hiding an explosion of local timing variants. No exact family count or per-family allocation is agreed. This hierarchy directly expresses the user's two optimization scales; A*/beam/B&B are policies that can be chosen independently at either level.

### How Baritone informs the formalization

The inspected Baritone code supplies the architectural hint: share geometric location nodes, connect them through a finite library of movement actions and estimate costs before executing the details. Its dynamic movements can span multiple blocks. Our adaptation introduces a coarser route-family level over that idea, and explicitly leaves a set of local movement methods/timings behind a broad connection. Do not attribute this full two-level design to Baritone itself.

References: [Baritone PathNode](https://github.com/cabaletta/baritone/blob/c55a9c5d9a333c9f0336179a96c13a178fcbc2d4/src/main/java/baritone/pathing/calc/PathNode.java) and [Moves](https://github.com/cabaletta/baritone/blob/c55a9c5d9a333c9f0336179a96c13a178fcbc2d4/src/main/java/baritone/pathing/movement/Moves.java). Its location-only cost dominance is a proposal simplification for our problem, not a valid general arrival-state dominance rule. The present hierarchy is our design inference from the source and the user's examples.

### Open design work and a focused check

The next concrete design work is to specify how actual scene geometry produces passage regions/signatures, which interaction changes keep a family identity, and what cheap arrival context suffices for useful ranking. Test a small scene with left/right alternatives and a shared broad corridor that permits both overflight and land/run-off. Verify that timing changes and local method replacements stay grouped, that a genuinely different corridor has a different signature, and that a useful momentum setup survives. Compare grouping and shortlist quality before implementing a large new search engine. This check is proposed, not run.

## Geometry-to-route research checkpoint

The user asked for an abstract algorithm from relevant AABBs, independent of serialization. [SPATIAL_ROUTE_RESEARCH.md](SPATIAL_ROUTE_RESEARCH.md) compares primary work on cell decomposition, navmeshes, hierarchical entrances, topological and topo-geometric classes, and diverse top-K paths.

Concrete proposal: expand collision boxes for the player's reference point; create height-aware free cells and finite shared-face portals; annotate support/wall/ceiling/ladder/stepping opportunities; compress subdivisions while preserving branches; add coarse directed movement estimates; group and select routes by meaningful spatial passage signatures. Exact timing and local contact methods remain inside a family. A flat obstacle projection, floor-only navmesh, unrestricted flying graph, or strict 3D homotopy classification each loses an important part of this task. In particular, different useful sides of a finite box may be topologically equivalent in 3D.

The recipe is a design inference, not a benchmark or adoption decision. It can be prototyped without first solving exact reachable-set propagation. Important work still includes choosing the grouping scale, efficient decomposition, representative movement envelopes and validating shortlist quality. Geometry cannot itself guarantee only tens/hundreds of useful routes; that number comes from heuristic selection with explicit budgets.

## Evidence and next framework experiments

No new benchmark, solver run or implementation was performed for this discussion. The historical 74 percent Y-replay profile belongs to the earlier append path, not a fresh profile of the generic queue.

Before selecting an implementation sequence:

- Record a current generic-path breakdown: opportunities, skeletons, masks, Y replay, mixed regeneration, append/copy/hash, compiler/screens, queue drops and numerical solves. Include time to first complete proposal and first accepted solution.
- Compare the current elapsed-time queue against `elapsed + admissible remaining time`, with identical grammar, budgets and acceptance. Count both node work and bound-construction overhead. Weak bounds may cost more than they save.
- Compare bounding a candidate family before materialization against applying the same test after concrete generation. Branch and bound must avoid generation work to address generation starvation.
- Use exhaustive small scenes and saved witnesses to audit every proposed safe rejection. Include a cheaper arrival with unusable momentum, a more expensive arrival that completes sooner overall, self jumps, equal-time better objectives and shorter numerical-unknown candidates.
- Record cold discovery separately from prescribed-route acceptance. Prescribed five-ladder witnesses do not measure cold five-ladder search performance.

## Sources and source-code anchors

- `movement_search.py`: whole-prefix queue, elapsed-time priority, incumbent tick cutoff, full-history deduplication and horizontal screen bypass when extra wall hits are enabled.
- `movement_composition.py`: original-state replay and composition without freezing horizontal endpoints.
- `open_ladder.py`, `ladder_schedules.py`, `movement_contacts.py`: pool construction, mask work and mixed-contact regeneration.
- `block_search.py`: legacy optimistic graph and reverse shortest remaining flight times, within its covered movement family.
- [Russell and Norvig, Informed Search](https://aima.cs.berkeley.edu/4th-ed/pdfs/newchap04.pdf): best-first/A* evaluation and admissible heuristic context. The proposed node representation and comparison above are solver-specific design inferences.
- [IPOPT documentation](https://www.coin-or.org/Ipopt/): IPOPT targets local solutions, which does not supply the global bounds required for certified branch pruning.

## Discussion log

### 2026-10-30: framework before individual filters

Persisted the initial pruning/reuse proposals and their assumptions. User requested an explicit comparison of shortest path and branch and bound, plus document updates after every turn. Recommendation: constrained-prefix branch and bound, with A*-style time-bound ordering and shortest-path bounds from an optimistic abstraction. User acceptance of that recommendation is pending. No new implementation or performance claim.

### 2026-10-30: sparse timed constraint events

User objected that complete-prefix primary nodes are too numerous and proposed a time-weighted graph whose vertices are constrained movement events at particular ticks, compressing normal air into edges. They explicitly noted that prior movement still matters. Persisted the proposal and revised recommendation: shared event topology with sufficient continuous/discrete arrival information, rather than one graph vertex per full prefix. One-distance-per-event closure remains unsupported; at an absolute tick all arrivals already have the same elapsed cost. Exact reachable unions can merge histories in principle; practical compact representations and safe label dominance remain the key open research issue. No implementation, benchmark or numerical run.

### 2026-10-30: role of branch and bound within the event graph

Explained branch-and-bound subproblems as restricted families in the same shared event graph, distinct from graph vertices. Recorded optimistic shortest-path completion bounds, incumbent-based family rejection, optional local refinement of state/event ambiguity, and an illustrative same-tick arrival example. Exact timed-DAG reachability may need no additional B&B layer; benefits depend on strong bounds and avoiding materialization. No adoption of state splitting, one-label closure, implementation or performance claim.

### 2026-10-30: prioritize a heuristic shortlist; Baritone and Stratfinder review

User explicitly prioritized heuristic selection to tens/hundreds of potential routes and requested investigation of Baritone and the existing Stratfinder. Fresh pinned source review completed for Baritone `c55a9c5d9a333c9f0336179a96c13a178fcbc2d4` and Curryocity/Stratfinder `988695b9b23245ded9f8ffb07c7ffd984091225d`; native Stratfinder at local `dc10e68` was also read for comparison. Persisted actual mechanisms, transfer limits and a proposed bounded diverse candidate-selection architecture in HEURISTIC_SOURCE_REVIEW.md. No implementation or new performance experiment; exact scoring and budgets remain open.

### 2026-10-31: bounded best-first and beam definitions

Clarified that the proposed bounded best-first policy caps a global frontier and expands its best candidate, while classical beam search retains the best B candidates at each chosen layer. Defined separate live-frontier, expansion and complete numerical-attempt budgets. Event depth and elapsed ticks differ in the sparse graph; no layering or width was chosen. Clarified that bounded best-first is descriptive here, and frontier-cap drops are heuristic rather than branch-and-bound proofs. No implementation or benchmark.

### 2026-10-31: broad spatial path versus local interaction optimization

User introduced two optimization scales and clarified the purpose of Baritone research: formalize broad spatial path finding before local interaction/timing refinement. Persisted a hierarchical proposal with untimed spatial route families, shared finite passage/region references, and an inner timed constraint-event graph whose local grammar can replace event subsequences as well as shift ticks. Recorded route-signature semantics, optional contact versus required support, momentum coupling, original-state whole-history optimization, interleaved feedback and separate budgets. This revises the earlier single-level timed graph's role. The distinction is user direction; concrete portal extraction, schemas, algorithms and budgets remain proposals. No implementation or benchmark.

### 2026-10-31: abstract extraction of spatial routes from AABBs

User requested research on automatically extracting broad path families from reasonable block geometry, with no input-format work. Persisted a concrete expanded-space region/portal recipe, contact-opportunity annotations, coarse directed movement connections, spatial signature grouping and diverse shortlist selection in SPATIAL_ROUTE_RESEARCH.md. Primary-source findings include the failure of pure homotopy to distinguish relevant box-side alternatives in 3D and the near-duplicate problem in ordinary top-K routes. No implementation, benchmark or numerical tests; recommendation and exact grouping/compression choices remain unaccepted design proposals.

### 2026-10-31: explain height bands and cross-band connections

Clarified the proposed decomposition as stacked foot-position floorplans: critical expanded obstacle heights divide ranges with constant horizontal occupancy; each free region is extruded over a band; shared free interface patches connect neighboring bands. Added a low-box example, retained the exact top contact boundary, and distinguished geometric adjacency from physical jump/climb feasibility. Overhead obstacles use bottom-minus-player-height as a critical foot height. No design adoption, implementation or experiment.

### 2026-10-31: general cell-decomposition research before application

User explicitly requested background research first, then adaptation. Added a general-method review to SPATIAL_ROUTE_RESEARCH.md using LaValle, Hauser and Choset/Acar primary material: cells need not be prisms; exact versus approximate/adaptive methods; coverage/Morse context; incidence graphs; geometric connectivity versus shortest/time/dynamic guarantees; and decomposition nonuniqueness. Refined the proposed AABB application to exact geometry-aligned cuboids with local box subtraction/merging as an alternative to global height bands, explicit portal/contact strata and a movement overlay. Recorded that selected cells can simplify obstacle-avoidance OR constraints to linear region bounds, while neither cell hops nor portal crossings are Minecraft ticks. No production implementation or benchmark; the six-versus-26-cell example is analytic. No decomposition has been approved or adopted.

### 2026-10-31: cell sequences and schedule-dependent time weights

User endorsed the cell-route/local-realization direction and emphasized multiple ticks per cell and schedule-dependent edge weights. Persisted a spatial-itinerary versus timed-realization formulation in SPATIAL_ROUTE_RESEARCH.md. Clarified nonuniform cells, artificial interfaces versus real physical contact patches, state-dependent traversal costs, multiple cell crossings within one input, global original-state optimization and heuristic outer weights. Conceptual hierarchy is accepted; concrete decomposition/search/backend and cost approximation remain open. No implementation, tests or benchmarks.

### 2026-10-31: isolated mixed-integer spike coordination

The separate Locate block solver docs thread reported that the user authorized a generic mixed-integer spike and cross-thread coordination. Its implementation worktree is `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-mip-spike`, branch `spike/ipopt-mixed-integer` from `feature/ipopt-consolidated@99ecfe9`. Reported scope: experimental circle/disk controls and discrete finite-geometry choices, compared against existing IPOPT with scalar/shared-scene acceptance; no production search changes. That thread will preserve these uncommitted research documents. This research thread will avoid the spike worktree and has no overlapping solver implementation underway. Results and limitations are pending; no architecture or backend decision follows from the experiment being started. Confirmed the consolidated checkout is now at `99ecfe9`; earlier research source checkpoints remain historical provenance.

### 2026-10-31: enumerate a small route set, optimize individually, compare

User clarified that the intended spatial shortlist is small enough to optimize every selected route individually; precise estimated edge/route costs may offer little value. The primary architecture is enumerate spatial families, optimize local methods/timing/continuous movement for each, and compare accepted time/objective results. Persisted the distinction between estimates, validated incumbents and true lower bounds in SPATIAL_ROUTE_RESEARCH.md. Weak lower bounds retain too much work but do not over-prune; invalid high bounds do. Locally optimized values are best found witnesses rather than certified per-route global optima. No implementation, bound choice, benchmark or exact optimizer commitment.

### 2026-10-31: bounds deferred; staged cell-route experiment planned

User deferred lower-bound work and requested a plan to construct the cell decomposition, enumerate spatial routes, define local decisions and benchmark whether this hierarchy sufficiently reduces the remaining work. Prepared CELL_ROUTE_EXPERIMENT_PLAN.md with isolated worktree setup before coding, geometry/portal/contact tests, raw and normalized route generation, corridor-conditioned reuse of existing movement generators and original-state solving, and a three-arm benchmark: current generic, prescribed broad spatial route, automatic cell routes. Recorded metrics and decision criteria distinguishing geometric/selection failures, residual local decision explosion and numerical bottlenecks. Planned branch/worktree research/ipopt-cell-routes at /Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-cell-routes, based on the then-current consolidated HEAD (currently 99ecfe9), separate from the MIP spike. Names checked unused; no worktree or code created, no benchmark run. Update the docs after subsequent turns as requested.

### 2026-10-31: preliminary parallel MIP report

The separate spike thread reports an initial eight-case guarded run in its isolated worktree: both experimental SCIP variants with existing IPOPT refinement produced scalar/shared-scene-accepted witnesses on all eight fixed-schedule cases; the existing fixed-schedule baseline accepted five at a 12-second budget. These are preliminary cross-thread reports, not independently reproduced results. The candidate passage families differ, the objective is feasibility-first, and testing, acceptance strengthening and repeated measurements remain underway. This does not establish route/Y/contact-timing discovery or a production speedup. Keep the cell-route experiment independent and await durable results before drawing backend conclusions.

Follow-up from that thread: isolated spike commit `16bcc4c`; two reported eight-case runs with accepted witnesses for both guarded SCIP variants, and 12 focused tests passing. The initial baseline used feasibility-first `solve_contact_branches`, whereas production enables objective/equal-cost search. The spike owner is adding a production-policy measurement and separating first shared-scene acceptance from total optimization time. These remain unverified cross-thread observations. Requested that the spike/report stay separate from canonical integration during this research; no merge or backend choice is authorized by this coordination.

Further correction from the spike owner: three repeats of eight fixed-schedule cases and one production-policy baseline run are reportedly complete. On repeated support, production accepted in approximately 0.32 s, versus approximately 7.8 s for circle and 2.9 s for disk; the earlier feasibility-first baseline missed this case. The production-policy run reportedly missed the wide/interleaved case and both fixed ladder cases within its local attempts, while the experimental variants accepted all eight. Thus the preliminary feasibility-first comparison must not stand in for production performance. Timing aggregation, first-valid versus full optimization, objective quality and differing candidate families still require the durable report. No universal speedup or route-discovery result follows. The spike owner confirmed no canonical merge.

### 2026-10-31: implementation authorized in isolated worktree

User authorized implementing the staged experiment and requested no further communication with the other agent. Created branch `research/ipopt-cell-routes` and worktree `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-cell-routes` from canonical `99ecfe9`, copied this research snapshot, and began geometry/routes/local-search/benchmark implementation. Bounds remain deferred; preserve the production search default and original-state trajectory optimization.

### 2026-10-31: isolated geometry milestone

Implemented expanded-AABB subtraction/merging, explicit boundary strata, partial portals, physical contacts/provenance, ladder memberships and swept-segment coverage in `cell_routes/geometry.py`. Six focused tests pass, including exact floor/ceiling contact and equivalent split-box coverage. Built the worktree-local Java helper. Bounds deferred; routes/local adapter/benchmarks follow. Details live in `experiments/cell-routes/README.md`.

### 2026-10-31: fixed-route decisions should propagate forward

User clarified a key reason for spatial separation: within one broad route the geometry changes little while arrival ticks change, so evaluate decisions and constraints from the start with little backtracking. Added ordered cell-stage paths and a forward prefix-domain pass. It intersects known per-state position/contact constraints, propagates possible nondecreasing route progress through Y/X/Z phases, permits skipping cells within one tick and repeated interactions within a cell, and preserves explicit route returns. Earlier numerical facings remain adjustable. The pass is a necessary geometric/order relaxation, not a greedy commitment or new time bound. Benchmark with and without it, measuring prefix rejections, cache hits, phase updates and numerical work. Lazy numerical corridor repair remains heuristic.

### 2026-10-31: active tests restricted to non-ladder movement

User requested non-ladder tests for now: stepping, blockages, ceiling, run ticks, landing and hits. Exclude ladders from subsequent benchmark/regression runs and architectural conclusions for this iteration. Preserve earlier ladder observations only as historical diagnostics. Add explicit run-up, ceiling and wall-hit controls alongside open/landing, side blockages, stepping/blip and grind. Continue the isolated worktree; no cross-thread communication.

User follow-up: exclude explicit wall-hit tests because they are rare. Active scope is stepping, blockages, ceilings, run ticks and landing. Remove the new wall-hit benchmark control and exclude any exploratory wall-hit output from the report; production collision behavior remains unchanged.

### 2026-10-31: goal regions must not impose interior gate crossings

The non-ladder grind benchmark found that a landmark plane inside the goal landing footprint could exclude a valid earlier landing, changing the best result from five to six ticks. Fixed spatial routing to target every free cell incident to the valid finite landing region, with geometric distance to that region used only for ordering; a goal center may itself be blocked. Exclude midpoint gates whose crossing depends on the chosen terminal point inside the footprint. The corrected smoke retains the same five-tick objective in generic, prescribed and automatic arms. Added a blocked-goal-center unit case and a numerical grind regression. Restart final non-ladder measurements on this corrected code; earlier exploratory results are not final evidence.

### 2026-10-31: apply route constraints before collision-branch repair

Moved corridor auditing/repair ahead of expensive shared-scene collision-branch generation, after scalar/support/Y validation. Wrong-side provisional paths now receive the selected route constraint before enumerating unrelated physical avoidance branches. All accepted results still pass the full shared-scene check. The wide-neo smoke found a physically accepted twelve-tick automatic-route result, compared with thirteen ticks from generic/prescribed-left under the same nominal limits. This is a smoke, not final timing evidence or proof of optimality. Added a numerical acceptance/call-limit regression and will rerun the final non-ladder comparison on the committed source.

### 2026-10-31: non-ladder experiment completed and next recommendations

Implemented the experimental cell/portal/contact decomposition, normalized spatial walks, early local opportunity filtering, forward cell-stage constraint propagation, lazy timed corridor repairs before collision branching and benchmark tooling. Source measurement checkpoint is `3257571` on the separate branch/worktree. No other-agent communication after the user's isolation instruction. The final active scope excludes ladders and explicit wall-hit experiments.

Saved 309 capped runs, 21 cold accepted witnesses across seven numerical scenes and three arms, and an independently replayed authored multi-support witness. All 22 saved witnesses pass scalar/support/Y/shared-scene replay. The focused non-ladder gate ran 153 tests with 11 environment skips and no failures; ten selected numerical integration tests passed. A bounded one-input stepping grammar exhausts in A/B with the same complete program.

The grind's prescribed-route first acceptance improves from median 1.726 s without forward propagation to 0.833 s with it, retaining the same five-tick objective. Automatic routes find a twelve-tick wide neo versus thirteen ticks for the generic baseline. No uniform speedup: run-up repeats nine numerical calls across automatic families versus one for generic/prescribed at the same seventeen ticks. Current small scenes return one to three families under a 300-expansion limit; caps 10/50/100 do not establish larger-map scaling.

The same-corridor multi-support scene remains unknown in all timed arms under the 64-prefix limit, before any complete numerical proposal. A valid authored 24-tick start -> middle3 -> goal witness fits the prescribed corridor and forward pass. Thus residual local interaction/program ordering remains a real issue. Automatic geometry also misses that known through-family at its expansion cap, retaining a side family; this is a separate proposal-coverage limitation, not proof that the retained family cannot solve.

Recommendation: keep the two-level base and forward propagation; improve portal-patch ordering/normalization, prioritize untimed interaction sequences within ambiguous corridors, move useful checks earlier in choice generation, and reuse symbolic/Y/contact work across variants. Benchmark those changes before deciding on a mandatory third graph. Bounds stay deferred. Exact metrics, profiles, geometry/route dumps, validation IDs, reproduction commands and limitations live in `experiments/cell-routes/README.md` and its evidence directory. This is a completed experimental implementation/measurement milestone, not a production routing switch or a global optimality claim.
