# Compile complete fixed branches before continuous solving

Planning record, 2026-11-01. User selects this work before the time/progress frontier and requests the full plan and limitations. This is a proposed implementation sequence, not a claim that the compiler or every guarantee below already exists. Existing isolated worktree/branch: `ParkourCalculatorMod-ipopt-cell-routes`, `research/ipopt-cell-routes`.

## Contract and scope

The orchestrator selects movement inputs, movement/contact cases, relevant obstacle interactions and cell/portal timing before each IPOPT call. The numerical problem contains those fixed selections in its existing coefficient, wall and contact formats; every facing stays variable. No integer route variables, collision-engine embedding, disk relaxation, solver redesign or frozen intermediate X/Z state is introduced. Every solve starts at the original full position and velocity.

Required implication for the supported branch family:

```text
candidate satisfies the supplied fixed continuous model
    => candidate follows its declared geometric/contact/movement branch
       under the shared scene model, within the explicit numerical tolerances
```

A fully specified branch can still have no feasible facing sequence. Numerical unknown remains unknown. The contract does not promise every branch solves, that IPOPT proves infeasibility, or that the orchestrator enumerates every legal game trajectory. Soundness of a compiled branch and completeness of branch generation are separate properties.

Scope remains normal momentum carry with velocity thresholds zero; ordinary air, run inputs, blockages/avoidance, landing, ceiling and the approved stepping catalogue. No ladders, water, intentional retained wall-hit family or inertia-threshold pass. Temporary horizontal clips that trigger stepping are still necessary even though intentional final wall-hit routes are excluded.

## Current gaps found in the implementation

`cell_routes/local.py` currently conditions generation and audits a solved trajectory, then generates missing corridor bounds. `block_contacts.py` also discovers earlier floors, finite-scene collisions and passage alternatives after solving. `route.py`, `contact_combinations.py` and the named movement generators already compile selected events into existing walls/contacts. Reuse that compilation, but complete its geometric obligations before accepting a branch for IPOPT.

`StaticScene.move` is the collision oracle: normal Y, then X, then Z movement; stepping additionally evaluates paths from the original box, selects a step candidate by horizontal progress, and chooses it only if it improves on the normal path. `obstacle_geometry.check_geometry` correctly passes raw requested movement and expected stored velocity/contact/ground outcomes. Endpoint occupancy alone is therefore insufficient. Reuse the shared resolver/checker and existing named movement cases; do not port a second collision engine into orchestration.

There is an additional expressibility limit: existing walls support coordinate/facing constraints and the existing special modes, not arbitrary weighted X/Z halfspaces. A custom-direction objective does not create a corresponding custom-direction constraint. The current `Checkpoint.stage` includes oblique progress halfspaces. Exact generic oblique stage predicates cannot simply be promised as axis-aligned bounds. The initial strict mode should use explicit cell/portal/contact-defined readiness where exact, while retaining directional progress as the score. Existing oblique readiness semantics must be explicitly marked conservative/unsupported or kept diagnostic until a documented equivalent branch representation is available; do not silently change them or leave them as hidden post-solve feasibility checks.

## 1. Make the selected branch and its obligations explicit

Add an orchestrator-side fixed-branch description containing:

- Scene/physics/version identity, original initial state, spatial route and horizon/comparison event.
- Per-input controls, declared ground/air regime, jump inputs and the selected ordinary/named movement case.
- Selected finite landing/ceiling/step contacts with input index, physical solid/allowed coplanar owner set, contact face and direction.
- Ordered corridor/gate transitions, assigned input/collision phase or remaining timing domains while construction is in progress.
- Compiled guards and a ledger of outstanding geometry/movement/stage obligations with provenance.
- Conditional alternatives, resume state and facing guesses, separated from any accepted incumbent.

A branch is either unresolved, contradictory, unsupported, ready for continuous solve, or accepted after replay. It cannot be sent to IPOPT while mandatory obligations remain unresolved. Geometry-incomplete diagnostic solves, if retained for research comparison, use an explicit separate mode and cannot count as strict-mode progress.

Guard provenance connects each emitted row/contact to a selected decision and its covered input/phase/solid. Redundant inequalities can be compressed, but provenance and all logical obligations must survive. Pending state stores choices/constraints, not optimizer endpoint commitments.

Deliverable: branch schema, coverage ledger, exact indexing contract and an IPOPT-entry guard that refuses incomplete strict branches.

## 2. Build phase-specific geometry from the scene and fixed movement history

Use existing player dimensions and collision boxes to construct configuration-space geometry. The fixed input/contact history supplies the vertical trace and collision/ground regimes conditionally; these are not trusted independently of the geometric guards that justify them.

For ordinary non-step input t, distinguish:

```text
start:     (X[t],   Y[t],   Z[t])
after Y:   (X[t],   Y[t+1], Z[t])
after X:   (X[t+1], Y[t+1], Z[t])
after Z:   (X[t+1], Y[t+1], Z[t+1])
```

Use raw requested displacements to establish whether a declared contact occurs and resolved displacements for the subsequent phases. A selected floor/ceiling clips the Y request; do not require the unclipped request beyond that selected stopping face to be obstacle-free. Conversely, an unselected nearer face must not clip first.

Construct phase-specific admissible horizontal regions, contact patches and crossing options. The 3D cells are useful geometry/route context; their horizontal projection alone is not a full collision proof. Vertical-phase footprints depend on the swept Y interval and selected stopping face. X/Z phases use the resolved Y and the correct pre/post horizontal coordinates. Whole axis segments must be covered; preserve multi-cell movement within a single phase using the corrected adjacent-region coverage.

Consider every supplied solid unless conservative motion/geometry envelopes prove it irrelevant to every trajectory allowed by the branch. Include source/target support, other floors, ceilings and solids affecting a step trial even if they do not intersect the guide polyline. Include scene-query bounds, not just final player occupancy, because the shared checker queries swept bounds.

Use necessary position/velocity envelopes and forward/backward propagation to shrink timing and region domains. They start from the full original state and fixed compiler coefficients. This is feasibility screening, not the deferred objective-bound/B&B work. Geometric proximity to the guide may rank an obstacle but cannot prove it irrelevant.

Deliverable: scene-to-phase obligation builder independent of solved facings, with explicit relevant/irrelevant reasoning per solid.

## 3. Compile ordinary movement, run, landing and ceiling branches

For each relevant input/phase/solid, discharge an obligation by selecting and compiling one admissible alternative:

- No contact: a selected separating face/connected free axis region ensures the whole applicable phase is unobstructed.
- Selected vertical contact: the raw Y request reaches the declared face in the correct direction, its finite footprint overlaps at the Y phase, and no other face clips earlier.
- Ground run: the selected descending support clip explains the declared ground flag and next-input friction/regime. Prevent unintended extra landing, ceiling or step outcomes.
- Leave support or pass an optional floor: specify the relevant footprint avoidance at the actual contact phase, rather than rely on a later floor audit.
- Portal/gate transition: select crossing phase/timing and appropriate before/after/finite opening constraints, including required ordering and readiness at the comparison event.

Merge coplanar simultaneous contacts as an allowed contact set when physically equivalent; do not choose arbitrary single ownership that excludes valid edge/bridge contacts. The compiler still checks the correct stored velocity resets, ground flags and movement coefficients.

Use the existing selected-contact and passage compilation where its guard conditions meet the contract. Replace candidate-distance-based post-hoc passage discovery with geometry/timing alternatives generated from route context and necessary envelopes. A numerical trajectory is not a prerequisite for creating those alternatives. The selected passage/face/tick combination is fixed before IPOPT.

Compress long unchanged intervals into one decision/window and emit required per-state rows internally. A player may stay in one cell for many inputs or cross several portals within an input; neither needs an artificial contact or a new spatial route.

Deliverable: strict ordinary/run/landing/ceiling pipeline with the shared scene checker serving as verification rather than ordinary missing-constraint discovery.

## 4. Add complete guards for the approved stepping cases

Treat each approved named case as a guard template producing fixed inputs, vertical/contact effects and geometric prerequisites. Reuse STEPPING_CASES.md and the existing case generators; do not turn raw resolver A/B bookkeeping into new primary movement names.

Audit each template for the conditions making the shared resolver select that outcome: trigger eligibility, approach/corner conditions, clearance and swept-body occupancy of the relevant paths, required lower/upper contacts, absence of intervening solids, declared final ground/velocity result, and dominance/tie conditions where another normal/step path competes. Include non-selected obstacles affecting counterfactual step paths. Ordinary paths and step paths need different obligation traces.

Start with normal step and slab boost, then grind/ordinary-ceiling interaction, blip/blip-up and the two approved corner cases. Do not merely add a final height or endpoint box and assume that establishes the case. Blip-up/corner-blip-up can end grounded at an elevated height; do not invent support at that height or extend a special mode into the ordinary continuation.

Most template prerequisites are face/interval constraints. Some ambiguous clipping/step-winner comparisons involve expressions not representable by existing coordinate rows in full generality. Under the unchanged-solver requirement, first split into fixed geometric subcases and derive sufficient existing-format guards. If a region still has no sound representation, mark it unsupported with its coverage gap; do not silently overconstrain it, send an incomplete branch, embed the collision engine, or claim complete arbitrary-AABB stepping coverage. Full-family coverage is a separate deliverable to measure.

Deliverable: per-case guard contracts and tested coverage/status, progressing from sound supported subfamilies toward the existing catalogue.

## 5. Enumerate and schedule complete branches in the orchestrator

Within a selected spatial route, maintain a resumable decision graph over movement events, portal/cell transitions, contact ownership and guard alternatives. Select timings in windows using route order, vertical events and necessary reachability domains. Propagate each choice against neighboring decisions; branch on real remaining alternatives and backtrack on contradictions.

Rank likely choices using the existing cell-route guide and compatible solved guesses. That ranks proposals; it neither pins the polyline nor certifies alternatives impossible. Do not generate a Cartesian product of every block at every tick or require one original cell per complete tick. Unchanged stretches share choices/constraints until geometry or a selected interaction changes.

Prefer earlier complete candidates with a retained diversity/unknown reserve, reuse the corrected completed-candidate scheduling, and persist the new geometric frontier through the existing resume mechanism. Preserve a validated reference guess separately from rejected proposals. Extend context compatibility to cover compiler/schema versions and new guard decisions. Do not merge branches solely because their physical Y/control histories match if their geometric alternatives/pending work differ.

A proof of inconsistent interval/guard/contact requirements permits elimination. Numerical failure is not such proof. Generation caps and heuristic drops remain explicit. Aim for tens/hundreds of complete local candidates per route, but measure whether the upfront graph actually achieves that; no universal small-candidate guarantee is assumed. No new time/progress frontier, richer score or objective bound is implemented in this workstream.

Deliverable: complete fixed candidate models produced and resumed without learning their basic geometry from IPOPT output.

## 6. Integrate validation, tests and rollout

Keep scalar validation and independent shared finite-scene replay. In strict mode a model-feasible candidate with an unexpected contact, corridor/signature violation, movement-case mismatch or readiness failure is classified as an orchestrator branch-compilation defect. Save the branch, obligation ledger, compiled model and failing trace. Preserve any earlier accepted incumbent. Do not report the defective candidate as a successful solve or silently route it through the legacy repair pipeline.

Verification layers:

1. Geometry unit checks: both axes/directions; Y sweeps; multiple cells/portals per phase; genuine gaps; corners, thin boxes and equal boundaries; finite contacts and nearer competing faces.
2. Event/case differential checks: ordinary run/air, landing, ceiling, step/slab, grind, blips/corners and multiple nearby solids. Check raw requested movement, resolved movement, all phase positions, stored velocity, ground flags and intended contact sets against the existing shared checker.
3. Feasible-candidate adversarial checks: stress different objectives, boundaries and different feasible facings within the same compiled branch. The contract must hold for every feasible candidate, not only the optimizer's preferred trajectory. Finite tests support but do not replace derivations for the guard templates.
4. Cold end-to-end regressions: all six current window scenes and extra held-out geometry; no injecting the historical solved facings or oracle-only guard sets into a claimed cold run. Retain known accepted programs as separate coverage/quality controls.
5. Resume/isolation checks: same original state and free facings; later constraints can reoptimize earlier angles; pending choices survive extension; changed physical/compiler context invalidates incompatible state.

Measure: total orchestration work, complete models emitted, IPOPT calls, rows/contacts, unresolved/dropped branches, unsupported guard regions, ordinary versus numerical-tolerance mismatches, model-feasible-to-scene-valid conversion, elapsed time and best accepted completion time/objective. Include upfront compilation time; reducing IPOPT calls while hiding combinatorics in generation is not a win.

Acceptance target: zero unexplained geometric/contact/case/readiness rejections among model-feasible strict-mode candidates in the supported corpus; no loss of previously validated incumbents; original six-scene completion coverage at least maintained; no IPOPT model/interface change. Report unsupported cases and heuristic generation coverage separately. Do not label zero mismatches on a finite corpus a proof of complete global path coverage or optimality.

Keep the old lazy pipeline as an explicit historical/diagnostic comparison during rollout. Default the new strict path only for supported families after its gates pass. No automatic lazy fallback may mask an incomplete strict compiler.

## Limitations and explicit tradeoffs

- Upfront choices can be combinatorial, particularly several interacting contacts/portals on nearby inputs. Cells help organize them but do not guarantee a small graph.
- Sound fixed branches can be infeasible and IPOPT can return unknown. This is distinct from a feasible result violating omitted geometry.
- Existing row/contact expressiveness bounds what can be represented exactly without changing the solver. General oblique readiness regions and ambiguous step-winner conditions require an explicit exact/conservative/unsupported policy.
- Conservative guards, nonzero clearance and bounded enumeration may miss legal near-edge or unusual movement choices. Track those coverage losses; they are not infeasibility certificates.
- A fixed Y/contact history is conditional on its guards. Unexpected contacts invalidate the declared history and its later jump/friction regime; they are not harmless positional errors.
- Bounds on geometry relevance and state reachability must be conservative. “Not near the polyline” is not a valid exclusion argument.
- Current measurements use a limited corpus and standalone replay. Dense-map scaling, automatic route/opportunity coverage and actual-world validation remain unestablished.

## Proposed implementation checkpoints

A. Branch schema, phase-obligation ledger and strict entry contract.
B. Ordinary/run/landing/ceiling geometry guards with cold simple-scene acceptance.
C. Approved stepping guard catalogue with explicit representability/coverage gaps.
D. Resumable route/timing/guard enumeration and strict integration.
E. Full old/held-out control matrix, coverage/cost report and supported-family rollout.

Each checkpoint is a separate reviewable commit in the isolated worktree, with focused standalone tests and wider standalone checks when the affected scope warrants them. The event-ready time/progress frontier remains the next separate workstream after this contract is reliable.
