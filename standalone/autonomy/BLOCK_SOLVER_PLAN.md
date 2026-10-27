# Block solver: continuous-model orchestration

2026-10-26. Run ticks are [prototyped](RUN_TICKS_PLAN.md). This is the next orchestration task. Acceptance remains a validated solve within the declared continuous model; solver-to-replay export and game end-to-end testing are out of scope.

## Problem and boundary

Given an initial state, an ordered sequence of blocks/supports and relevant surrounding collision geometry, derive candidate transition profiles and the positional constraints needed to visit them. Pass each resulting route to the existing run-ticks search and numerical solver. The output is an accepted continuous trajectory with its chosen transition profiles and run allocation.

The original run-ticks API consumes an ordered route. A new [route-selection prototype](BLOCK_ROUTE_SEARCH.md) now chooses intermediate supports from an unordered coplanar set, with only the start and goal fixed. Inputs stay sprint-jump plus sprint 45; their facings remain free. Choosing a route through an arbitrary world and deriving richer contact/obstacle profiles remain later work. Block solving removes the remaining manually supplied block order, air times and contact/obstacle conditions in stages, rather than replacing the angle optimizer or run-ticks allocator.

```text
block/support geometry + initial state + ordered route
    -> candidate air times and contact/crossing conditions
    -> existing run-ticks allocation and schedule construction
    -> existing coefficient compiler + prepare / solve / validate
    -> best accepted continuous-model candidate
```

The three layers remain distinct: orchestration generates the discrete alternatives, the solver optimizes facings for one numerical request, and replay can consume a chosen result later. Orchestration owns the adapter between profiles and solver requests. It does not depend on recorded facings or a game replay service.

## What exists and what does not

| Capability | Current state |
| --- | --- |
| Ordered supports, supplied profiles, run search, free turning and continuous horizontal acceptance | Implemented by the run-ticks prototype |
| Single-support footprints and wall-face derivation | Existing Java `ConstraintDeriver` and `SupportOverlap`; reuse them |
| Scalar Y recurrence and prescribed floor/ceiling/step contacts | Existing `standalone.ipopt.ladder`; reuse it |
| Ordinary open-air air time from source/target heights | First profile-compiler slice implemented in `block_profiles.py` |
| Intermediate support selection with fixed endpoints and sprint-jump/sprint-45 inputs | Implemented for coplanar support planes in `block_search.py`, including repeated visits |
| Optimistic transition bounds, reverse goal reachability and a route-prefix pool | Implemented, with separate accounting for heuristic pool drops and unknown numerical attempts |
| Different-height route edges and earlier-floor consistency | Implemented with the existing Y simulation and bounded outside-face positional branching |
| Finite solid side faces, general obstacle corridors and collision consistency | Not implemented in the new block path |
| Ceiling placement/contact selection, jump cancellation and stair-boost constraints | Next profile families |
| Choosing among profiles with joint run-count backtracking | Not implemented |
| Arbitrary block-route discovery, ladders, fluids and effects | Later extensions |

## First slice: ordinary open-air profiles

`compile_open_air_route` accepts a request with initial state, rectangular supports and normal movement policy, without `airtimes`. The caller must explicitly declare `sceneModel: open_air_supports`. This is a support-plane scene: it does not assert that finite solid side faces or omitted surroundings are harmless.

For each consecutive pair, `ordinary_jump_profile` uses the existing vertical step/carry functions to find the first descending target-height contact. The resulting profile contains air time, Y/vertical-carry states, ground flags and the floor event. Normal height changes 0, +.5, +1 and -1 give 12, 10, 9 and 14 inputs respectively. It generates the existing run-ticks route and leaves horizontal feasibility to IPOPT. A missing profile within the configured horizon is `unknown`, not a whole-jump infeasibility claim.

This first slice deliberately rejects supplied air times, contact overrides, ceilings, obstacles, composite shapes and other unimplemented profile families. It cannot silently ignore them. It preserves one explicit support per route entry and does not merge separated blocks into a bounding rectangle.

The fixture [blocks-flat-two-jumps.json](fixtures/blocks-flat-two-jumps.json) has no recorded states, facings or air times. The continuous integration test compiles it to `[12,12]`, runs the existing search and validates a `[0,0]` allocation. Profile tests also check the established height tiers through the numerical model's vertical validator. The combined orchestration suite passes 37 tests with the actual Java compiler and IPOPT runtime.

Profile derivation is not yet a general block-scene acceptance test. Metadata explicitly records that solid side faces and contacts with other supports are not checked. For example, a lower destination requires conditions that prevent landing back on the source first. Generating those conditions belongs to the next milestone, not to the ordinary Y recurrence.

```python
import json
from standalone.autonomy.block_profiles import compile_open_air_route
from standalone.autonomy import find_route

request = json.load(open("standalone/autonomy/fixtures/blocks-flat-two-jumps.json"))
compiled = compile_open_air_route(request)
if compiled["status"] == "derived":
    result = find_route(compiled["route"], compiler, seconds=60)
```

`compiler` is the existing `JavaCompiler` described in README.md. The example demonstrates the orchestration composition; it does not introduce another worker or solver schema.

## Next milestones

The [Stratfinder investigation](STRATFINDER_INVESTIGATION.md) checks both our native search and Curryocity's external tool. Its first proposed experiment is now implemented: an optimistic support/profile graph plus a shared pool of route prefixes, using conservative reach checks and the current ordered-route solver as the inner evaluator. Different arrivals at a block remain distinct; neither a visited flag nor a no-turn failure removes a free-turn route. The coplanar prototype passes a comparison with exhaustive enumeration of a small explicit support set. Next extend contact consistency and the profile families below; route proposal itself is no longer missing.

1. **Ordinary profiles and support correctness.** Height-dependent route profiles and earlier-floor exclusions are implemented. Every descending crossing of an earlier support is checked, and a conflicting candidate branches over outside-face positional conditions. Extend from this support-plane model to explicit finite support side geometry separately. The standalone ordinary-profile helper still only supplies the destination floor event; scene-level contact checks belong to the route orchestrator.
2. **Ceiling and stair-boost profile candidates.** Derive possible ceiling contacts from head clearance and footprint overlap. Model the user's stair-boost abstraction as a short jump profile, often 2 inputs, plus the positional entry constraints that make it applicable. Keep ordinary, ceiling-cancelled and step-cancelled alternatives distinct where geometry permits more than one. The old fixture's 1-input grounded step is a separate non-jump event, not a substitute for this family.
3. **Generate the extra positional conditions.** Each event specifies when its collision/support overlap must hold, which block/face it concerns, and any states that must stay outside to avoid an earlier landing or cancellation. Anchor these conditions to transition-relative events so inserting runs retimes them correctly. Reuse `ConstraintDeriver.deriveWall` and the existing support/entry machinery. Do not copy authored per-tick X/Z tables.
4. **Finite obstacle corridors.** Branch over relevant passage sides and crossing intervals; use the 1.8.9 Y-X-Z ordering to construct constraints. A harmless boundary beside the path is not an infinite wall across unrelated inputs. Keep finite-face conditions and continuous clipping contacts consistent with the declared branch.
5. **Joint discrete search.** If an early profile choice blocks a later transition, backtrack across both profile choices and run counts. Reoptimize the full prefix so position and inertia remain coupled. Retain local unknowns and return the best accepted route under a deadline.
6. **Real-data progression.** Start with isolated short-hop, ceiling and stair-boost examples, then replace the corresponding supplied profiles in Linkcraft j0-j16. Geometry and initial state may be fixture inputs, but the generated profiles/constraints must not depend on recorded schedules or facings. Use recorded traces only as regression witnesses/diagnostics.

## Lessons retained from the shelved solver

The [historical post-mortem](../../docs/research/block-solver.md) reports an honest result of about 16-17/53 on its old solver, with both false-positive support and multi-jump convergence problems. Those counts are not a measured ceiling for the new IPOPT implementation.

- Never replace a disjoint set of supports with its bounding box. Select a support per branch or keep an explicit disjunction, and check strict membership in an actual support.
- Keep each obstacle's actual sub-boxes and finite geometry. Do not infer that a captured route being inside broad bounds prevents a solver from exploiting gaps.
- Preserve landing/run-off indexing and axis order. A support row on the wrong state changes which jumps exist.
- Avoid exact recorded-point pins as a normal input or seed. Keep yaw free; use only block-derived positional conditions.
- Numerical timeout is unknown, not a proof that a block route or contact profile is impossible.
- A future margin objective may help, but the first objective is an accepted route under correctly generated constraints. Do not redesign numerical optimization before measuring the new path.

The old blocks-only pipeline and probe screens were never committed and cannot be recovered. `BlockSolver`/`SweptCollision` are historical references; `ConstraintDeriver` is live. Reuse existing helpers, but do not revive the old inflated success criteria.

## Completion gates

- A fixture starts from block/support geometry and initial state with no supplied air time, recorded facing or authored per-tick rule table for the feature under test.
- Generated profiles expose their geometry/model assumptions and contact timing. All added conditions follow run insertion.
- Every accepted route passes the existing independent continuous-model validator and strict support checks, including the actual support in any alternative-support branch.
- Unsupported mechanics are rejected explicitly; bounded search failure is reported as unknown.
- At least one fixture exercises each newly implemented family and a control case rejects a geometrically inconsistent contact.
- Replay agreement, mod UI and global minimum certificates remain separate future work.
