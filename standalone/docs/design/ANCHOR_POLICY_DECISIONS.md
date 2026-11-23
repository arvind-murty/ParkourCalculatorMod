# Anchor search decisions: DFS, binary turns and observability

Current decisions through runtime `dd738c98` (2026-11-11). The
[execution reference](../ANCHOR_SEARCH_EXECUTION_REFERENCE.md) is the current
combination/pruning/IPOPT and implementation-owner table. This O1–O7 record
explains the choices; [current acceptance](../../research/hard-jump-comparisons/README.md)
records600 native tests,131 known-witness prefix checks and80 retained-witness
replays. The feature is complete in the session checkout; old private-worktree
paths and planning-only authorization notes have been removed from this entry.

Feasibility scheduling advice is in [O4: exact DFS node feasibility and cadence](#o4--exact-dfs-node-feasibility-and-solve-cadence). Current visit scope is in [O6](#o6--revisit-generation-deferred-same-block-jumps-required); the full future revisit design is preserved separately. Other sections record the newly settled decisions and their implementation implications. Suggested implementation details are labelled recommendations; the user's explicit policies are adopted as policies rather than presented as mathematical completeness claims.

## O1 — Hard-jump status is a solve-wide latch

**Corrected hard-jump contract (user clarification,2026-11-04):** a hard jump has one transfer jump between S and E. It can have any allowed number of setup jumps that land back on S before that transfer. This applies to every starting anchor, including the implicit opening support. The earlier implementation counted all span impulses and rejected same-source returns; that was an implementation/design-record error, not the user's intended restriction.

A validated one-transfer S→E witness latches the ordered pair globally for the solve, across changed incoming histories/backtracking. Preparatory S→S hops remain legal after the latch. Ordinary runs and setup jumps remain subject to the original domain/caps, and all requested grounded impulses still count in J for ordering. No initial-span exception is needed.

The transfer starts after the last completed source contact before the target arrival. Earlier impulses are preparation; the final transfer has exactly one requested grounded impulse. An unfinished prefix can be preparing or in flight without already committing to target versus source landing. A source contact in the target-arrival input belongs to that same physical transfer case rather than resetting preparation. Aliased patches use physical owners.

Examples after a latch:

```text
S → S → S → E    allowed: setup hops on S, one transfer to E
S → air → S → E allowed when the final S→E transfer jumps
S → C → E        outside the hard transfer domain: intermediate support C
S → S → E        excluded if the final transfer is only a run-off, without a jump
```

Initial-0 Real11 jumps at0 back onto B1@12 and at12 to B2@21. The first is preparation; the second is the hard transfer. A latch learned from jump8→B2@17 must preserve that initial-0 program. Preparatory hops on later anchors receive exactly the same treatment.

Global latches survive certifier abandonment. Current hard-arrival comparison is
frame-local and broader than the older matching latest-jump run family. A slower
compatible arrival stays first-class if it has **more J OR fewer R ordinarily /
fewer A in turnaround**. Otherwise it can be recoverably deferred against one
validated earlier peer. Earlier preparation histories/actual programs are not
merged. Recovery preserves parent/rows and uses a scoped bypass. Optional inter-block revisits remain deferred; same-source setup does not consume the required-anchor cursor.

**Declared assumption (2026-11-11):** hard transfers between anchors never involve
stepping; only ceilings can shorten the ordinary-flight bound. Completed stepped
transfers cannot learn/satisfy a latch. Closed or pending source-local preparation
is not blanket-rejected. Unknown shortening ceilings contribute zero to the bound.

Hard-jump policy version3 and shared comparison policy1 are part of the outer/DFS/stage/movement semantic context. Old exact checkpoints are rejected because the wrong policy may already have discarded setup choices. Latch/arrival records expose preparation impulses, transfer impulses and the source-contact boundary. No movement physics or continuous objective equation changes.

## O2 — Requested grounded impulses are the jump count

**Settled:** absolute T; J/R/A local to the current required-anchor span; J counts requested grounded jump impulses. The user clarified that named jump cancels retain the horizontal jump impulse while losing the vertical effect. They still count as J.

```python
J = count(grounded[t] and jump[t])
R = count(grounded[t] and not jump[t])
A = count(not grounded[t])
span_ticks = J + R + A
```

There is no separate effective-launch ranking metric and no need to reopen this question. Preserve the named case's existing compiled horizontal/vertical behavior and independent validation. An ordinary jump with false support assumptions remains invalid; the named cancellation is an explicitly modeled outcome rather than evidence of such a mismatch. [Existing counter](../../src/parkour_solver/search/identity.py), [named vertical validation](../../src/parkour_solver/physics/vertical.py), [case regressions](../../tests/unit/movement/test_stepping_cases.py).

Same-block hops do not reset the span start. Only a required-anchor advancement does. Whole inputs retain their existing phase/counting convention, including landing then horizontal run-off during that input.

## O3 — Finite extended B7, backward guides and onward objectives

### New Real11 task

**Settled:** extend B7 one block forward in +Z and use actual landing on B7 instead of the unbounded virtual +Z finish. Keep minimum landing time, then MAXZ at that landing time.

The stored box in [the accepted event fixture](../../research/event-route-discovery/real11-auto.json) is:

```text
old B7: [729, 0, -1095, 730, 7, -1094]
new B7: [729, 0, -1095, 730, 7, -1093]
         xmin ymin zmin xmax ymax zmax
```

The terminal guide point is the physical +Z edge, X midpoint:

```text
q_finish = (729.5, 7, -1093)
```

X midpoint is a neutral guide choice because the objective has no X preference. Actual finite support still imposes horizontal landing bounds. The old extra task inequality X<729.7 and the old virtual height-region goal are replaced by the finite B7 landing predicate in the **new** fixture; do not accidentally keep them as additional constraints.

Existing support semantics allow player-body overlap rather than requiring the center strictly inside the solid. Consequently the physical guide edge Z=−1093 is not necessarily the maximum legal player-center Z: the ordinary center bound extends by the player half-width, approximately 0.3, subject to clearance/exposed-region checks. Reuse [`route.center_bounds`](../../src/parkour_solver/scene/route.py) and existing finite contact regions. Do not add a new center cap at the guide point or remove normal overhang support merely to make the picture and optimum coincide.

This changes collision geometry and terminal validation: B7 is now a real extended solid with real contact. Use a fresh fixture/report path under the next experiment directory. Do not edit archived old fixtures/evidence or present their virtual T67 results as validation of the new finite task. The new physical fixture is implemented; archived original-box reports remain unchanged.

### Guide recursion and objective

**Settled:** infer guide points backward from this finite endpoint, projecting each successor guide point into the previous anchor's allowed landing cover. Reuse clamping/nearest-patch helpers; retain physical owner/height provenance, stable ties and original initial position. Do not average disconnected covers into blocked space.

At arrival E, optimize in the onward direction to the next guide point N:

```python
d = normalize_xz(q_N - q_E)
objective_E = maximize(d.x * X[T_E] + d.z * Z[T_E])
```

Reuse [`Checkpoint.objective`](../../src/parkour_solver/spatial/checkpoints.py) and existing POSITION/customYaw. Physical landing constraints remain independent. No waypoint equality, progress-based pruning, slack work or constant FEASIBILITY objective is introduced.

A decision check, and the relaxation screen of a child when it is created, optimize the program's own
decision objective (`AnchorAdapter.program_objective`, `anchor_guides.decision_objective`; user,
2026-11-20). The program ends on block X: the block its last movement landed on, or the block it took
off from when it ends airborne (a ceiling contact). X's point is its guide point when X is the start, an
anchor or the task, otherwise its top-face centre. The objective aims from that point toward the next
required anchor's guide point. When X's point is inside that anchor's turnaround boundary, it aims at the
anchor after it instead. Landing on a group's last anchor therefore aims like that group's end, and
landing on the task keeps the scene objective. Guide points are a deferred improvement
([P14](../DEFERRED_WORK.md)).

**Settled design evidence:** the user reports a measurement of approximately equal speed for this objective with early stop. Adopt that as the design input; this session did not reproduce or inspect the other agent's measurement. No further approval or constant-objective comparison is required to proceed with this direction. Implementation/regression/cold acceptance is recorded in the current evidence linked above.

Use stop-on-feasible and no intermediate polishing/equal-cost variant drain. Keep native scene/anchor acceptance, repair, feasible-yaw seeds and original-state re-solves. A warm seed may already be feasible and return without improvement; it remains a valid early-stop result. Final actual landing uses real MAXZ optimization.

## O4 — Exact DFS node feasibility and solve cadence

### Settled rejection rule

There is no separate ancestor-testing or prefix-check subsystem. We choose **when to run the normal continuous solver on the exact current DFS node**. That model contains the node's full fixed history from the original initial state through this node and nothing later. Earlier facings remain free. It contains all required past contacts/anchors and the selected physical alternatives, without adding a future end-anchor condition at this node's clock.

- If that exact node's designated solve is infeasible under the adopted IPOPT assumption, reject that node and its descendants and backtrack.
- If a descendant is infeasible, do not mark any untested ancestor infeasible. Return to its parent and try another child.
- A failed longer end-anchor program says nothing negative about a shorter ancestor unless the solver actually solved that shorter node's own model.
- If one physical alternative fails, only that alternative fails; unresolved sibling alternatives remain.
- If all represented children are exhausted, DFS may backtrack from the exhausted parent. That is search exhaustion, not a new continuous-infeasibility result for the parent.

Example: P has children P+A and P+B. Solving P+A and finding it infeasible rejects P+A. P and P+B remain eligible. Only solving P itself, with neither A nor B in the model, can give an infeasible result for P. Parent links store ordinary DFS ancestry; there is no attempt to infer a failed ancestor from the failed child.

A completed designated no-solution attempt is an operational rejection under the user's solver-accuracy assumption. Record the actual status/allocation. Cancellation, unstarted work, errors and incomplete model construction do not pretend to be a completed node solve. Cache results by exact node/model/scope so a checked node is not redundantly solved after resume. Do not put changing repair progress into its semantic physical identity.

### Implemented default: solve each completed movement decision

The default performs a guided feasibility solve after every **completed meaningful movement decision**, always at required anchors and the finish. This is a clear initial baseline: an impossible selected jump/contact is rejected before extending it, and rejection has an unambiguous exact-node owner. It also fits the existing stop-on-feasible/feasible-yaw warm-start machinery.

A decision level here means a completed jump/landing or selected physical contact/case choice. It does **not** mean a simulation input or a raw one-input run-tick construction node. Consecutive ordinary ground-run inputs are preparation of a run-length choice and are included in the next completed movement decision's model. Do not add an IPOPT call merely because the generator extended an ordinary run from R2 to R3. If a whole run-duration action is exposed as a meaningful decision in a different generator, count that action once, not once per input.

For example, run3→jump→land on the same block is one completed movement decision including its run preparation. A second setup jump is another level. Required anchor arrival is always solved even if it occurs before the next scheduled level. Existing [`append_choice`](../../src/parkour_solver/movement/program.py), [`normal_choices`](../../src/parkour_solver/movement/normal.py) and named-choice boundaries provide the underlying actual choices; distinguish raw construction steps from this solve cadence explicitly.

Each feasible node retains the selected physical inequalities and the exact avoidance/contact rows of every stretch it settled ([side families](AVOIDANCE_PASSAGES.md#side-families-in-search-branches)), plus a yaw seed; no fixed X/Z/velocity seam is created. `evaluate_node` calls `commit_arrival`, whose `retain_prefix` preserves those selected rows before the next movement extension. Later node/anchor solves still optimize all earlier facings jointly from the original state, within the retained constraints. This does not introduce optional-support **comparison goals**: actual time comparisons/promotion remain at required anchors/final task. An internal feasible node is not a new required anchor. The [avoidance follow-up plan](AVOIDANCE_PASSAGES.md) separately records the need to specify recoverable passage-alternative retention; continuous reoptimization alone does not reopen a selected discrete side.

### Alternative: every 2 or 4 decision levels

Make cadence a small deterministic interval, measured in those completed decision levels since the last actual solve. Implemented default interval is1;2/4 are configurable, unselected alternatives for future matched cadence measurements. An interval 2 or4 can reduce NLP launches when most choices are feasible and solving dominates runtime. Its cost is exploring more discrete work below an impossible, still-untested ancestor before reaching the next scheduled solve.

When a scheduled deeper node fails, reject only that solved node. Do not walk upward running extra ancestor tests or label its unsolved parent infeasible. Resume ordinary DFS alternatives there. Changing cadence changes when we spend solver work, not what a child rejection logically establishes.

Always force the normal solve at required anchors/final task regardless of cadence, and avoid a duplicate solve if it is already the scheduled node query with the same objective/policy/scope. The final task still needs actual MAXZ optimization; an intermediate early-stop result does not replace that final work. Existing cheap scalar/physical screens remain during generation independently of continuous solve cadence.

### How to choose after the baseline works

Compare intervals 1,2,4 and anchor-only as matched diagnostics using the same new finite task/domain/budget. Measure total wall time, solver calls/worker launches, time spent constructing subsequently rejected descendants, first full finish and final T/Z. Do not choose from number of solves alone. If interval 1 has cheap warm-start returns and prevents expensive dead work, keep it; if calls dominate and nearly all nodes are feasible, increase the interval. No adaptive controller or separate probing mechanism is needed initially.

Cadence1 is implemented and covered by current tests/cold runs. A matched cadence1/2/4 study has not selected a replacement; no claim that1 is fastest follows. There is no unconfirmed cadence question blocking implementation.

### Implemented full-finish pruning and lazy checks

One validated full incumbent bounds new/selected candidates. Compute optimistic
finish `T+max(H,V)` from phase-correct landing-state covers and applicable latched
ceiling-aware flight minima, subtracting already-paid ordinary flight inputs.
Exclude only if strictly later than the incumbent. Equality remains eligible,
including unfinished work; a separately justified positive remaining bound can
then exclude it. Guide distances are estimates, not these pruning enclosures.
This bound overrides local count protection. Incumbent updates do not rescan
queued bounds. A different earlier history is a different candidate.

## O5 — Preliminary binary turnaround heuristic and observability

**Settled latest correction:** each decision is either turnaround or ordinary. Classify its current decision point against the preprocessed approach-facing edge plus margin, then select the corresponding existing heuristic. Do not split a decision into input contributions, aggregate different mode costs or introduce a third membership class. This is a preliminary heuristic and must be observable/tunable.

### One approach edge, including an imaginary diagonal edge

Detect guide turns >110° as already agreed. For E between S/N, construct one horizontal normal pointing away from their approach side:

```python
u_S = normalize_xz(q_E - q_S)
u_N = normalize_xz(q_E - q_N)
n = normalize(u_S + u_N)  # approach bisector; away from S/N
c = min(dot(n, v) for v in physical_top_footprint(E))
m = 0.3  # initial spatial margin; configurable and logged
is_turnaround = dot(n, decision_point_xz) >= c - m
```

The line n·p=c is E's approach-facing support edge. If the approach is diagonal, it is the imaginary diagonal edge supporting the footprint on that side. Move it toward S/N by m. At an axis-aligned reversal with E starting at X=10, the boundary is X=9.7. For a diagonal normal `(1,1)/sqrt(2)` and footprint `[10,12]×[10,12]`, the boundary is X+Z=20−0.3sqrt(2). Use physical footprint geometry, without adding player half-width a second time. The margin is heuristic geometry, not a new collision clearance.

The one-edge/bisector construction replaces the earlier two-plane wedge. It gives a deterministic binary answer for every finite decision point; equality belongs to turnaround. A zero horizontal guide leg cannot define the110° cue, so no turn boundary is registered for that triple; the decision uses ordinary behavior unless another applicable boundary contains it. Do not add an undefined/possible membership label.

Use only the relevant route neighborhood `(S,E,N)` for a boundary; a past turn's unbounded half-space must not change heuristics in unrelated later spans. Multiple applicable boundaries still produce one boolean (inside any), with matched boundary IDs recorded.

### Decision location and heuristic selection

The initial implementation uses the actual current DFS decision location: original initial X/Z at the root, then the current node's validated witness position. That point is a reference for choosing a heuristic, not a fixed continuous endpoint. Children still re-solve all earlier facings from the original state.

Choose one mode for the decision and use it for ordering that decision's alternatives:

```python
if is_turnaround:
    key = (absolute_T, -span_J, span_A, stable_id)
else:
    key = (absolute_T, -span_J, span_R, stable_id)
```

All J/R/A counts remain local to the current required-anchor span. There is no R_outside+A_inside score. A jump/compound choice crossing the boundary is not partitioned; the next decision is classified again at its own current point. Pending symbolic construction work uses the owning decision's already selected mode rather than inventing an uncomputed landing location. Actual node solves/anchor comparisons retain their existing scopes.

The default solve cadence 1 supplies a validated node point before its next movement decision. If later benchmarking a larger cadence, explicitly define/log the deterministic seed/reference position used at an unsolved node; the classifier still accepts a point and returns one boolean. Do not substitute reachability-envelope membership or branch the heuristic into several possibilities.

In turnaround mode, use T/−J/A and protect more J OR fewer A. Outside, use T/−J/R and protect more J OR fewer R. Local hard-arrival deferral applies in either mode only without the corresponding independent protections; raw extra R/A alone is not protection. Global hard topology stays single-jump wherever already latched. Do not split one run of ticks into differently ranked pieces.

The initial margin is approximately0.3 blocks. The previously suggested1-input margin is an optional later tuning representation, not an automatic temporal dilation or an extra membership state in this initial edge-distance classifier. Any later change must remain a documented binary rule.

### Required observability for this preliminary rule

Persist/log a compact record whenever a decision's mode is selected:

| Field | Purpose |
| --- | --- |
| Decision/node/parent IDs, anchor span, decision depth and absolute input/state | Identify the exact choice and its owner. |
| Decision X/Z and position source | Reproduce which point was classified; distinguish original state, validated witness and any later batching reference. |
| Boundary/guide IDs, source S/E/N, edge normal/c, margin/value units and policy version | Reconstruct the axis/imaginary-diagonal edge without copying geometry into every row. |
| Signed edge distance n·p−c and signed margin-boundary distance n·p−(c−m) | Explain how close the decision is to changing its binary label. |
| isTurnaround, selected heuristic and actual priority tuple with T/J/R/A | Verify the label selects precisely one agreed rule. |
| Hard-latch state, effective run deferral, actual deferred/restored alternatives and reason | Verify turnaround preparation is not suppressed accidentally. |
| Feasibility/selection outcome and solver/worker timing references | Relate heuristic choices to search behavior and cost. |

Later full-history solves can move the earlier decision location. Record its replayed position/distance and whether the same boundary rule would flip the label. This is a diagnostic boolean, not a third mode or an extra hard constraint. Keep keys stable for already-owned work; a deliberately reclassified decision is a versioned service event with an explicit reason, not silent in-place mutation.

Add aggregate per-span/per-turn counts for the two labels, mode selections, run deferrals/restorations and later label flips. Include a distance histogram and solver calls/time/accepted continuations by mode. Near-boundary distances are diagnostic measurements only; do not create an uncertain category or blend scores.

New diagrams show the physical edge, margin-offset edge, shaded turnaround side, guide points and decision points colored by their two labels. Show the imaginary diagonal boundary directly. A decision timeline can show its binary label versus input/depth. Mark replayed label flips as diagnostic changes. Historical evidence/diagrams remain untouched.

Store boundary geometry once and reference its ID in compact events. Resume needs a stable logging cursor so the same event is not emitted twice; diagnostics must not change ordering, constraints or consume numerical-solve budget. Avoid copying full trajectories/NLP requests into every mode event. Test that logging on/off leaves selected keys and physical rows identical.

## O6 — Revisit generation deferred; same-block jumps required

**Settled current scope:** do not generate inter-block revisits now. Preserve their design for later in [REVISIT_DISCOVERY_DEFERRED_PLAN.md](REVISIT_DISCOVERY_DEFERRED_PLAN.md). That document records complexity, lazy repetition-family generation, future milestones/tests and resumption requirements; it is not current implementation work.

Multiple jumps on the current block remain supported:

```text
A → A → A → B       allowed: setup jumps on A, then forward
A → air → A → B    allowed: local hop, no other support visited
A → C → B          allowed if C is a permitted new optional support
A → C → A → B      not generated now: inter-block return
A → B → C → B      not generated now: inter-block return
```

Use stable physical support provenance: a visit names the merged top face its contact stands on (`face_namer`, `search/anchors/policy.py`). A face's name joins its blocks' owners, so a block whose top touches no coplanar neighbour is named by its owner, and run ticks across a flush seam stay on one face. Anchors, the start and the goal are whole faces (user, 2026-11-22): `settings` rejects one whose top face holds another block, since its arrival would otherwise never be seen. Ignore transient air; compress consecutive same-face occurrences only for eligibility. Keep every jump event, input count, physical patch/case and actual program in the solver model. Moving among decomposed patches belonging to the same block is not automatically a different block visit.

Track departed faces: the current face remains eligible for repeated local jumps; after another face is visited, the departed face is ineligible for every interaction at the feet (landings, lower landings and the blocks a stepping case steps onto, over or around), while ceiling hits, grinds and wall contacts with it stay allowed. Apply this before expensive contact generation (the `feetAllowed` context predicate) and audit emitted programs as well. Initialize from the original support and reconstruct the same state on resume; supplied roots violating this selected domain must be identified explicitly. Do not use a visited set that rejects the second landing on the **current** face. A feet touch that is itself a visit at that tick is that arrival, not a return: a blip that lands on a lower face and steps onto another in one input visits both.

A required-anchor cursor advances only on its next actual required contact, never from a same-block setup hop. Optional supports are not globally disabled. Required order remains enforced; do not enter a later required anchor prematurely if that would require a currently unsupported return to fulfill the required sequence.

Global hard latches intentionally restrict the transfer to one jump. Same-block setup preparation remains available on the departure anchor in both latched and unlatched spans, including recovery. Do not accidentally remove all same-block hops from the generator while enforcing no inter-block returns. At an ordinary decision, a setup jump that does not naturally fit its merged launch face is held as a reserve, not dropped ([O11](#o11--jumps-back-onto-the-launch-face-must-fit-it)).

Current tests must assert both sides: several legal same-block jumps are generated/validated, while A→C→A is excluded as deferred feature scope. Logical DFS backtracking is allowed and is not a physical revisit. No revisit-pair source/family cursor is implemented now; positive-duration horizon and ordinary source limits still bound local chains.

## O7 — Turnaround heuristic means momentum ordering

**Settled:** one turnaround decision uses `(T,-J,A,id)`. At equal T/J, fewer A
favors R. For a *slower* candidate the confirmed protection is **more J OR fewer A**,
not raw more R. Extra run time with unchanged J/A can therefore defer against an
earlier compatible hard peer. Ordinary mode analogously protects more J OR fewer R.
Equal T remains live. This replaces the earlier ordinary-only run-deferral exemption.

Global hard topology and regional mode are independent: a latched span still has
one actual transfer after allowed source setup hops. Restore an unprotected slower
candidate only on explicit failed-continuation recovery, with scoped bypass and
all bounds/domain limits still enforced. No O7 design question remains.

## O8 — Encounter groups

**Settled (user, 2026-11-18):** consecutive anchor pairs whose encounters are
near each other form one group. `encounter_groups(scene, names)`
(`search/anchors/policy.py`, stored in `guide_plan['groups']`) starts at a pair's
end block and follows solids that touch (the obstacle-cluster test,
`SceneGeometry.touching`). A touching solid joins when some landing block of the
chain can hit it (`blocks` in its [block table](OBSTACLE_RELEVANCE.md)), and every
member extends the chain (user, 2026-11-19); the kinds only label members:
blocks a landing block can land on (`landing`) become landings, the rest are
hit/ceiling obstacles. The group then runs through the last later anchor among its
landing blocks, and the next group starts at the following pair. Real11: B4 → B7
is one group (B5, B6, B7); in the extended task B4 → B22 is one group (anchors
B5, B7, B20, B22), reached through the hit-only blocks B6 and B8 that B22 touches.

A frame searches one group (cursor `anchorIndex` … `groupEnd`). Anchors inside it
stay required and ordered (`audit`); a group-end arrival must have visited them
all, and feet stay forbidden on anchors after the group's end block. Alternatives
are compared only at the group's end. Inside a group the hard latch is per pair:
a validated group-end arrival latches every pair whose transfer is one jump
(`transfer_structure(..., until_arrival=True)` ignores the later pairs' history),
and every latched pair the program has entered is enforced. Slower-arrival
deferral at a group end applies only when every pair of the group is latched, so
a one-pair group behaves as before. Hard policy4 and search policy6 carry this.

## O9 — Minimum run ticks for advantageous jumps

**Settled (user, 2026-11-19):** a run tick (a grounded tick without a jump) leaves
the player on the same block, so outside turnarounds it is generated only as the
minimum that lets the jump after it be **advantageous**: the jump lands on the
next required anchor (anchors are visited in order; the task block after the
last), or it hits a ceiling, or it is a `grind`, `slab_boost` or
`corner_jump_cancel`. A run-off or walking step-up onto another block is itself
a run tick, kept when it reaches that block sooner than jumping would (Real11's
run-off from B7 onto B11 under B8's 2-block ceiling). This mirrors minimum run
ticks for hard jumps (`run_bound`, O7), which is unchanged.

An **ordinary decision** P is a grounded program whose last decision is not a
turnaround (`decisionTurnaround`). Its expansion generates jump-now children only
(`runTickChoices=False` in the generator context: `choice_approaches` and the
stepping generator's `normal_step`); ladder walk-ins and virtual-goal finishes are
unchanged. `minimum_runs` (`search/frontier.py`) then adds run ticks, bounded by
time (user, 2026-11-19/20):

- **Airtime** counts the jump input and the air inputs after it through the
  landing input: 12 for a flat jump, 3 for a jump under a ceiling 2 blocks above
  the feet, 2 for a grind or slab boost, and for a corner jump cancel 2 onto a flush
  ledge or the jump input plus the fall onto a lower one. A run-off has no jump
  input, so its airtime is its air inputs. (The comparison counts' A is different:
  air inputs only, with the jump input counted in J; a profile's `duration` counts
  all of its inputs, run ticks included.)
- **Built landings.** A ceiling hit's built choice stops at the hit, and a grind's,
  slab boost's or corner jump cancel's at its jump tick, so each lands in its own
  child's choice. Its landing on each block is the earliest of those choices that
  passes the screens (`landings_of`; these screens are not recorded, and the
  child's own expansion repeats them). These count as jump-now landings below:
  a grind, slab boost or corner jump cancel is a 2-tick jump, whose input after the
  jump tick is always airborne (user, 2026-11-21).
- **N** is P's fastest jump-now landing: on the next anchor if any child lands
  there, else on any face other than P's own (a flush neighbour of P's block is P's face). "N is a heuristic for how many run
  ticks we tolerate": it bounds run ticks only, and a built move that lands past it
  is kept (user, 2026-11-21).
- **Candidates** come from `advantageous_candidates` (`search/anchors/policy.py`)
  without building anything: the launch tables (`face_table`, measured from every block of P's face) give the blocks and
  undersides, vertical replays give each move's airtime A (A = 2 for slab boosts;
  a corner jump cancel's is its fastest landing on a ledge past one of the block's
  convex corners; a ceiling or grind A is the earliest descent through any
  landing top of the launch table, a lower bound since it ignores where the player
  can be after the hit). There are three kinds (user, 2026-11-20):
  1. **Advantageous jump**: k same-face run ticks then the jump, built while
     k + A < N, cheapest (total, A) first, at most
     `MAX_ADVANTAGEOUS_RUN_TICKS = FLAT_JUMP_TICKS − 1` = 11 run ticks (a flat jump
     lasts 12 ticks), on top of the request caps and the time bound. One beaten on
     both total and A by a kept one (advantageous jump-now children count) is not
     built. A kept one beats with its built landing (total and A), not the table's.
  2. **Run-off onto another face B**: R run ticks, the last walking off the edge
     (it moves as a run tick: a grounded input), then r airborne inputs (0 when it
     lands within the walk-off input). The fewest R, from 1 to 11, with R + r < J_B,
     the airtime of a jump from P onto B: the vertical replay of an unhit jump,
     lowered to any jump-now landing on B (built landings included), so
     it is how long reaching B takes (user, 2026-11-21; it may exceed N). Offered
     only when a jump-now child lands on B, since a run-off reaches only blocks
     close enough to jump onto. The run-off must leave its launch block: see
     walk-off faces below.
  3. **Walking step-up onto another block**: its step input is a run tick too, so 1
     run tick (the step on the first tick), else 2 (one run tick then the step);
     more run-up than that is a slab boost.

  Run-offs and step-ups take no part in beating and hold no reserve. The first run
  count whose children pass the frontier's screens keeps a candidate; the landing
  block's own expansion then finds the jumps after it.
- **Walk-off faces.** A run-off's player centre must be off its launch block's
  exposed top at the walk-off tick (the Y sweep of that input starts from the
  previous state's X/Z). The launch block's faces are tried by where the landing
  block lies: beyond one face, that face and the two side faces (the one toward
  the landing block first), the opposite face only when those three are blocked;
  diagonal, the two facing faces, the two opposite only when both are blocked;
  overlapping, all four, nearest first. A face is blocked when a neighbouring
  solid covers it entirely (`SceneGeometry.sides`, as for obstacle sides). The
  generators drop a run-off that cannot reach any open face, the creation screen
  picks the first face whose disk relaxation is not infeasible (both faces in a
  diagonal tie), and the node solve carries that choice instead of generic floor
  checks ([obstacle relevance](OBSTACLE_RELEVANCE.md#run-off-walk-off-faces)).
- **Reserves** (advantageous jumps only) are held in the frontier's `alternatives`
  with the options they wait on (`watch`), restored when every watched option's
  branch died (its node check failed or everything below it died), and never once
  a watched branch reached a validated group-end arrival. A kept candidate reserves
  its next run count while the total stays below N; beaten candidates wait for
  every candidate that beat them (one reserve per set of beaters); when every child
  landing on N's block died, that block is a dead end for this decision and N
  widens to the next landing (the next anchor is never a dead end).
- **Without N** (no jump-now landing on another block), run counts are scanned
  until a jump (by its built landing) or run-off lands on
  the next anchor, else on another block; that sets N and its next count is
  reserved. A run-off's walk-off input counts as a run tick here too, so its A is
  its airborne inputs.

Every resolution is logged in the frontier's `minimumRunCandidates`. This mirrors
minimum run ticks for hard jumps (`run_bound`, O7), which is unchanged.
Turnaround decisions generate run ticks as before and still prefer them
(`(T,-J,A)`). `searchPolicyVersion` 17 carried this, the decision objectives, O10 and O11; 18 adds the
same-tick arrival exemption of the departed-owner audit (O6, above); 19 makes visits, the launch tables and the
candidate keys face-based (O6), with `HARD_JUMP_POLICY_VERSION` 5 for the latch that reads those visits; 20 widens
O10 to every generated move; 21 takes a corner jump cancel's airtime from its ledges; 22 holds the other sides of
steps and slab boosts behind their forward side (O12).

## O10 — No backward moves at ordinary decisions

**Settled (user, 2026-11-21; widened to every generated move 2026-11-22):** an ordinary
decision (not a turnaround) generates no move that makes backward progress along its
guide leg; turnarounds are where backward moves belong (O5, O7). The DFS gives the
generator context the decision's validated point, the moves its current guide leg (the
span's guide point to the next, horizontal) allows, and the turnaround margin (0.3
blocks). A leg of zero horizontal length gives no filter.

The allowed moves come from the leg's direction class (`leg_class`,
`spatial/anchor_guides.py`, shared with O11; its one knob is `AXIS_LEG_DEGREES`, 22.5°).
Within 22.5° of an axis the leg is that axis and allows moving forward and both ways
sideways, so only straight back is disallowed; otherwise it is its quadrant's diagonal
and allows only its two forward axis directions. This is a heuristic for when the
landing is not known; run-off avoidance, which knows its landing, keeps its own
beyond-on-both-axes test (`walk_off_faces`, `constraints/contacts.py`).

A generated choice is skipped, or loses the rectangles concerned, for:

- **a region behind the decision**: a rectangle the move must reach (a landing, lower
  landing, step or flight-case top, ceiling underside or grind underside), clipped to the
  reachable envelope at its input, that lies more than the margin beyond the decision
  point in a disallowed direction (`behind_decision`, `reachability/service.py`). The
  check runs in `reachable_option` (`movement/contact_combinations.py`), which every
  region-carrying generator already calls, so a behind rectangle is dropped like an
  unreachable one and a move goes only when none is left;
- **an entry by a disallowed move**: a move that crosses a block's side, or contacts
  a side wall, from outside it moves against that side's outward axis
  (`entry_backward`). It covers grinds (`movement/ceiling_generation.py`), normal steps,
  slab boosts, blip-ups, corner jump cancels, corner blips and corner blip-ups (`movement/stepping.py`;
  corner rows cross the X side and only pass the Z side). A blip's wall avoidance rows are
  not entries.

Both skips are logged with the generators' reachability exclusions (reasons
`behind_decision_point` and `entry_against_guide`), so the solve report lists them per
frame. Run-tick bases and the peeked children of `landings_of` use their decision's
point and leg, so minimum-run candidates are filtered the same way. Run ticks are not
generated moves and are not filtered. `searchPolicyVersion` 20 carries the widening.

## O11 — Jumps back onto the launch face must fit it

**Settled (user, 2026-11-21):** at an ordinary decision P, a jump that lands back on
the face P stands on is generated only when it naturally fits on that face. Otherwise
it is held as a reserve in case P's run-tick moves fail. Faces are the merged top
faces landings see (`scene_contact_options` on Y, +1): coplanar touching tops merge
into one face across blocks, every solid through the plane clips it, and it is widened
by the player's half-width. After merging, the individual blocks no longer matter
here, only faces (anchors stay blocks, each a whole face, O6). On a long face (Real11's B1) a hop back onto
it is natural; on B4 the natural jump overshoots, and landing back needs braking, so
the B4 hop waits while the 3-run-tick jump onto B5 lives.

A program stands on the merged top faces, at its feet, named by the face it last visited
(`launch`: landing contacts and case events, so a blip-up or corner blip(-up) that sets no
`terminalSupport` counts). P's launch face is the one of those nearest its validated
position; with none (feet on no top with that name, or a ladder tick) nothing is held.
A child of P lands back on it when it jumps on its first input and every landing
`landings_of` finds for it (its own, or a ceiling hit's, grind's, slab boost's or
corner jump cancel's earliest landings) stands on exactly that face. A support whose
top solids cut into several faces is never counted, since an unsolved landing's piece
is unknown.
Minimum-run candidates (O9) keep their own reserves and are not tested. The test
(`same_face_fit`, `search/anchors/policy.py`):

- **Direction:** the heading of the direction class of P's current guide leg d (the class
  O10 uses, `leg_class`): within 22.5° of an axis it is that axis; otherwise it is the
  diagonal of its quadrant. A leg of zero length gives no test.
- **Room R:** from P's validated position to the face's farthest corner along that
  direction. In Real11, B12's face is only 0.4 deep (z −1091.7 to −1091.3) because the
  higher B11 and B13A wall it in.
- **Natural jump D:** full acceleration along d from P's validated velocity, through
  the landing's movement magnitudes and frictions (`travel_radii`).
- **Fits** when R > 0.7·D (`SAME_FACE_FIT_FACTOR`; the factor may change).

A held jump is not queued, sets no N and beats no run-tick candidate. When P queued
or reserved nothing else it is queued at once; otherwise it is reserved
(`state['alternatives']`) and restored once nothing below P lives and every node P
queued or reserved has died, never after a validated group-end arrival below them.
A run-tick reserve restored in the same pass counts as alive, so it goes first.
Each held jump is logged in `minimumRunCandidates` (key `["same_face", <face
name>]`) with outcome `held` (or `queued`), its room and its jump. Restoring it
earlier, once the run ticks spent below P reach its airtime, is deferred
([S9](../DEFERRED_WORK.md)).

## O12 — Other sides of a step or slab boost wait for the forward side

**Settled (user, 2026-11-22):** a normal step or slab boost onto a block is generated once
per exposed side its decision allows (O10). The side the decision faces moves forward over
the block; entering another side gains only distance, and needs moving one way then back.
So the forward side is queued as usual and the others are held until it is limited.
Grinds, stair boosts, blips, blip-ups and corner cases are unchanged: an edge grind or
stair boost is a real option.

- **Move and sides** (`side_variants`, `movement/stepping.py`): one move is a case onto one
  merged face with one set of top and ceiling-clearance rows; its sides differ only in
  their `case:outside` row. A side is **forward** when the decision point already
  satisfies that row. A move with a forward side and at least one other side tags each
  choice `sideVariant` (`forward`, the row, the case, face and side). A move without a
  forward side (it is covered, or O10 dropped it) or with one side is not tagged.
- **Hold** (`queue_child`, `search/frontier.py`): at the end of the expansion
  (`hold_sides`) a move's other sides are reserved behind its queued forward nodes
  (`state['sideReserves']`, one per parent program and move). They are queued at once
  when no forward was queued (screened, or already queued elsewhere), when a forward is
  complete (it lands on the goal and has no node solve) or when the search has no node
  solves.
- **Forward outcome** (`side_outcome`, `search/dfs.py`): once the forward node is fully
  solved, its kept solution's distance from its own row at the take-off is read; within
  1e-3 (`SIDE_SLACK`) it is `tight`, else `slack`. A node that is not solved (solve
  interval) counts as tight. An oracle-rejected, exhausted or deferred (numerically
  unknown) node, or one dropped when taken from the queue, has failed.
- **Promote or reserve**: once every forward of the move has an outcome, its sides are
  queued when none is slack. Otherwise each is held like a node's alternatives
  (`state['alternatives']`, watching the slack forwards and their held node alternatives):
  restored once those branches died, never below a validated group-end arrival, with
  deferred nodes counted live.
- **Run ticks**: a held side counts live in `restore_alternatives`, so a run-tick reserve
  or held jump watching it, or its decision, waits until the side variants are exhausted.
  A held side otherwise counts as a queued child of its decision: its landings set N and
  it can beat run-tick candidates (O9).

Why a slack forward may wait: the forward and another side are the same program but for
one row, so for the convex disk relaxation of one avoidance family, a forward whose row is
inactive has the optimum of the program with neither row, which bounds the other side's
node objective. The node keeps the best of several families and the bound covers the
node, not its subtree; restoring when the forward's branch dies covers the subtree.

Each held side is logged in `state['sideVariants']` (its decision, case, face, side,
forwards, their outcomes and its alternative entry); the solve report lists them under the
decision's expansion.

## Settled versus remaining implementation choices

Settled: DFS; solve-wide single-jump hard latch; requested grounded impulses; extended finite B7/onward guides/early stop;110° turn with approach-facing boundary and regional margin; no separate ancestor-testing subsystem; a failed descendant cannot invalidate an untested ancestor; inter-block revisit generation deferred; multiple setup jumps on the current block preserved even after a hard-transfer latch.

Implemented default: normal feasibility after each completed movement decision, with anchors/final mandatory; benchmark 2/4 later if solving dominates. Turn mode is a preliminary binary edge-plus-margin decision using its current point, with compact diagnostics, plots and outcome counters. The blended regional-score and uncertain-membership proposals are removed; no new turn-policy question needs reopening.

The [deferred revisit plan](REVISIT_DISCOVERY_DEFERRED_PLAN.md) preserves future cyclic-route work in full. The current feature is implemented/validated; cadence comparison, revisit generation and turn tuning are future work, not pending steps from this plan.
