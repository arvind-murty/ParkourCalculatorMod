# Orchestration compaction handoff

Updated checkpoint, 2026-10-29, on `codex/run-ticks-planning`. Run ticks, support routing, blockages, one-input wall/ceiling contacts, multiple ladder transitions and multi-episode airborne transfers are implemented. `39d4ede` added the common contact contract and `a8b659c` added full ladder/contact composition and step diagnostics. The latest reviewable state is committed in our isolated worktree.

## Stepping planning investigation

The user asked what stepping exists and what Minecraft/mod behavior the next type must handle. STEPPING_PLAN.md has been expanded from the earlier two-mode migration sketch. Verified the cached mapped 1.8.9 Entity movement branch against shared StaticScene and ran six collision probes, saved in experiments/stepping-mechanics.

Current automatic coverage is the two-input grounded jump/stair boost; one-input grounded steps and combined contacts work through supplied contracts. General landing-step/blip discovery is absent. Important additional gaps: scalar step events force positive net rise/zero Y carry, and the scene-contact contract requires a final top surface. They do not cover unsupported/zero-net-rise steps or all actual collision-derived ground/carry outcomes.

The step resolver resets to the original tick-start box and compares two raised alternatives against ordinary Y-X-Z movement. A/B ties choose B; ordinary/step ties choose ordinary. Eligibility includes a real downward floor clip during the input even when initially airborne. Returned Y for collision flags is the final downward leg, not net feet displacement. Probes confirm supported-but-not-grounded and grounded-but-unsupported outcomes, plus an equality case retaining incoming negative Y carry. These are shared-resolver probes, not full-route numerical or game witnesses.

Proposed type: one physical stepping input with grounded/jump-start/landing mode and actual branch metadata; keep the usual two-input slab boost as step plus normal recovery. Generalize open composition to retain onGround separately from physical support. Add shared diagnostics/exact scalar event handling before claiming full stepping coverage. No production stepping implementation was changed in this planning pass; water and route pruning remain later work.

## Ladder closeout and next movement type

The user asked to close ladders and move toward stepping. Read LADDER_COMPLETION.md and STEPPING_PLAN.md. The audit found one new functional gap: falling-only discovery depended on choosing a climb wall. The implementation now supplies a wall-independent `fall_only` context, all four horizontal entry sides, no fabricated primary hits/obstacle plane and unchanged ladder caps/Sneak. Both initial and appended discovery use it. New cold hanging-vine acceptance: 17 inputs, entry 1, exit 6, no wall hits. Saved scalar/scene evidence and fixture are in the contact-combinations experiments and fixtures folders.

Final closeout regression passed: all 265 orchestration tests across 50 completed cases and a 215-case continuation (661.037 seconds). The first process was interrupted by a native crash in the optional Python diagnostic stack dumper; continuation without that timer passed. All eight hanging-vine cases passed, including fresh cold discovery and saved scalar/shared-scene revalidation. No shared Java or numerical-worker arithmetic changed. The wall-independent discovery feature and completion/stepping plans are saved in `78c8f13`. The ladder mechanics milestone is closed for the documented scope. Stepping implementation has not started; its plan reuses the current one-input grounded step, two-input stair/jump-cancel profiles and shared selected-step collision resolver. Water/pruning/cache/resumable generation and rare ladder strategies remain deferred. The completion document also makes the current first grounded-jump approach and horizontal-entry search-family limits explicit; it does not claim universal route coverage.

## Latest refactor: generic movement units

The user requested the two speed fixes, actual common movement-type generation and consistent initial/appended entry handling. All three are implemented. Stepping and water remain deferred.

- `3a8d8cd`: stop already-invalid occupied-Y traces and skip duplicate floor prediction for open prefixes. Fifty-five focused regressions pass. Representative append median: 2.849 to 1.849 seconds, identical serialized continuations, 256 generated schedules and unchanged mask work. See GENERATION_PERFORMANCE_PLAN.md.
- `2cee238`: registered generators for `normal`, `ladder_climb` and `ladder_fall` produce interval/duration/control/contact choices before Y replay. Normal support generation reuses existing jump/run-off profiles. Composition retains the five ladder phase grammars, endpoint-stop ownership, optional phases and immediate hit-on-entry behavior. Nine new generator tests and 72 focused regressions pass.
- `a3a01f7` (plus `bb01f61` for independent scalar acceptance): entry unification. `movement_entry.py` builds initial and appended entry probes from the same original-state route context. Full prior support/run, airborne, input, collision and Y histories are present. No X/Z/VXZ endpoint is pinned. Production chain expansion passes this evaluator to every append; pure low-level generation remains explicitly unprobed. Approach ceilings are derived from existing scalar/contact helpers. Open probes end inside the next ladder without fabricating a landing or exit.

Read MOVEMENT_REFACTOR_STATUS.md and MOVEMENT_UNITS.md for contracts and remaining limits. Both support routing and airborne composition use the registered generators; their family-specific grammars and queues remain separate. This is a shared generation/compiler interface, not one monolithic route graph.

Entry scanning retains earlier numerical unknowns, prioritizes validated windows and reports other unvalidated channels as deferred. If nothing validates, unprobed windows remain candidates. Up to five delayed entry times remain proposals, not independently certified witnesses. Appended probes receive at most three seconds and 35 percent of the remaining expansion budget; window/program/queue drops and numerical unknowns still preclude a global minimum proof. The old hard 20-input append horizon is removed. No route/input dominance pruning was added.

Focused entry evidence includes fresh IPOPT and full-scene checks for immediate initial entry, second airborne entry, two launch run ticks, an approach ceiling, and entry after four prior climbs. The numerical suite passes all 51 tests in 25.045 seconds. All 257 orchestration tests pass across two batches: 176 completed cases from the initial sweep and 81 remaining cases in 64.006 seconds. The final focused entry gate passes 11 tests in 11.970 seconds, including worker-success/scalar-failure rejection. All cold geometry cases, mixed contacts, long chains and legacy witnesses pass. No Java or numerical-worker arithmetic changed. The current feature work is complete; stepping/water and the already-deferred pruning/cache/resumable proposals remain future work.

The user wants resumable generation written down only as a potential improvement and remains unconvinced. It is explicitly optional, unimplemented and not a prerequisite for finishing the generic interface. Keep it separate from the two accepted implementation efficiencies and the deferred multiplier-based pruning proposal.

Latest performance discussion: see [GENERATION_PERFORMANCE_PLAN.md](GENERATION_PERFORMANCE_PLAN.md). The user accepts early termination of already-invalid Y traces and skipping duplicate movement evaluation for open prefixes. These are implemented in `3a8d8cd`. The user proposes best-multiplier input-schedule pruning for the later pruning pass; preserve distinct entry-face geometry unless equivalence/dominance is established. Exact-Y caching is also deferred. Resumable generation is being explained as finer work scheduling, not a new movement tick, strategy deletion or permission to abandon tick-first ordering. Profiled append: 24,510 Y traces, 22,126 inside-Y rejections, 256 generated schedules, eight continuations, 8.417 seconds under cProfile.

Latest follow-up: automatic mixed wall/ceiling generation is now connected to the first open episode, every appended approach/ladder and the final air interval. Read [MIXED_MOVEMENT_CONTACTS.md](MIXED_MOVEMENT_CONTACTS.md). Contacts enter before Y schedule generation, so entry/exit timing can change. Mask/output budgets are shared fairly; skeleton counts must leave mask work to emit schedules. Contact/entry families survive ranking caps, and transfer windows distinguish contact histories while counting distinct exit states. No route-specific pruning was added. New accepted generated continuations: approach ceiling 29 inputs, second-ladder ceiling 29, final-air ceiling 27. Fresh full-scene IPOPT tests pass for all three, alongside existing cold/two-region/five-fall/bottom regressions. The generated continuations start from prescribed episode prefixes, not cold whole-route discovery.

The user authorized this contact work first, then geometry-only discovery of longer climbing chains, and explicitly deferred pruning until movement-type coverage is complete. Mixed contacts are committed in `a9074fd`. Read [LONG_LADDER_VALIDATION.md](LONG_LADDER_VALIDATION.md) for the next delivered results: cold three-region climbing discovery in 50 inputs, and a supplied five-region climbing contract accepted in 99 inputs. Both pass fresh IPOPT and full scene checks, with one launch and no intermediate ground. An initially-in-ladder seed wrongly aimed back at the cell centre; its tangential reference now starts at the actual initial position, without adding facing constraints. Shared initial ground friction remains intact.

The cold five-region search still returned unknown/context_limit in 131.180 seconds, despite the accepted 99-input contract. Its fixture/settings/counters are saved in `experiments/contact-combinations/five-climbing-search-limit.json`. Do not claim cold five-region discovery or infeasibility. No route-specific pruning was added. The full orchestration suite passes 228 tests in 525.911 seconds; the two subsequently added long-chain regression tests also pass in 2.491 seconds. The numerical worker and shared movement arithmetic were not changed.

Latest user decisions are recorded at the top of LADDER_COMPOSITION_PLAN.md. Treat `normal`, `ladder_climb` and `ladder_fall` as first-class orchestrator-visible movement types sharing one unit interface. Shared ladder mechanics remain in one implementation. Later add `stepping` for existing step-up/slab boosts and `water`. Ladder rectangles are sufficient; stop expanding irregular geometry.

Within one ladder hitbox there is at most one climbing unit, optionally preceded/followed by falling; falling-only and direct-climbing-on-entry cases are valid. Never generate `ladder_climb -> ladder_fall -> ladder_climb` in that hitbox. The climbing unit internally owns its four endpoint-stop variants: neither, one beginning stop, one end stop, or both one-input stops. These are Y/height and duration choices, not separate falling phases. Preserve the final-hit omission semantics and actual carried VY.

The unit interface, bottom exits and open scalar prefixes are committed in `6fd1131`. Generic append/queued chain expansion now replaces the two-region transfer template and surfaces relative/retimed units in routes/plans. Read [MOVEMENT_UNITS.md](MOVEMENT_UNITS.md) for exact APIs, limits and evidence. New accepted contracts: five falling-only airborne ladders in 58 inputs, one launch and no interior ground; bottom exit at state 15, landing at state 21. Both pass fresh IPOPT and shared scene checks. A five-prefix queue test uses predetermined scalar prefixes; the real append generator is separately tested across five climbing regions. Five narrow climbing prefixes were generated through the real append API, but attempted full numerical solves returned unknown, so they are not accepted witnesses. The existing cold two-region fixture still finds 28 inputs after transfer-window fairness was corrected to sample distinct exit states.

New limits: `maxTransferPrefixes` (64), `maxTransferQueuedPrefixes` (256), existing extension-program/window budgets; explicit queued drops and unknowns remain unresolved. Each full flight stays within 200 inputs, configurable interactions within that horizon. Open nodes are Y/phase-valid proposals, not independently accepted X/Z trajectories; complete proposals solve jointly from the original start. Same-region re-entry, wall-orientation changes, exhaustive appended mixed-contact generation, stepping/water and route-specific pruning remain later work.

Implementation commits: `6fd1131` adds movement units/open/bottom exits; `092317a` adds repeated chain expansion, retimed unit plans and saved accepted contracts. Final focused gate: 27 tests passed in 18.487 seconds, covering new units/composition, legacy contract checks and all saved-route witness methods. Numerical gate: 51 passed in 24.973 seconds. The full 215-test sweep completed in 460.748 seconds, with only five old-snapshot metadata subcase errors; the fallback and affected methods pass in the focused rerun. Three later-added tests are included in that focused gate. Do not claim a clean full-suite rerun after the metadata fix, or an accepted five-climbing-region trajectory. Current native core/shared movement arithmetic was not changed by this refactor.

## Latest composition work, 2026-10-29

The user prioritized supporting contact types before route-specific pruning. Read [CONTACT_COMBINATIONS.md](CONTACT_COMBINATIONS.md), [LADDER_COMPOSITION_PLAN.md](LADDER_COMPOSITION_PLAN.md) and [the saved evidence](experiments/contact-combinations/README.md). `39d4ede` introduced the common finite contact contract. The subsequent work adds multiple ladder transitions, full-flight multi-episode contracts and automatic two-region transfers, mixed ladder contacts, exact face-connected region metadata, finite coplanar alternatives and step contact diagnostics. Numerical/scene acceptance remains strict and facings stay free.

Current witnesses: cold two-ladder route 42 inputs; cold ladder/ceiling route 12; explicit and independently discovered airborne transfer 28, with one jump and no intermediate landing; wide-neo passage plus Z contact 12; same/consecutive ceiling/X/Z contacts 3. Step plus X/Z and occupied ceiling/X/Z followed by ceiling-only cancellation and landing also pass actual IPOPT/shared scene tests. The worker/Y arithmetic is reused, not duplicated.

Normal coupled generation spans one input or two consecutive inputs; normal events with a free gap compose independently in the full solve. Ladder windows retain full sequences. No new route-specific rarity or reversal-direction pruning was added. Work caps remain explicit. Same-region re-entry, changing climb planes, diagonal-only continuous adjacency and exhaustive irregular-corridor enumeration remain later work. Automatic airborne expansion now repeatedly appends regions using open prefixes, rather than requiring a two-region template or provisional landing.

The integration branch is idle. Published tip `e09f2a7` was reviewed using Git/GitHub CLI; its update beyond our pinned `f057c0c` is documentation only, so the pinned movement sources already contain the fixes. Full-prefix solves preserve carried state until a portable replay checkpoint API exists. Shared StaticScene arithmetic is unchanged; only chosen-step collision records were added, with native and Python coverage.

## Resume here

1. Read repository `AGENTS.md` and `CONTEXT.md`, then this file.
2. Read [OBSTACLE_SEARCH.md](OBSTACLE_SEARCH.md) for the implemented interface and [OBSTACLES_AND_BLOCKAGES.md](OBSTACLES_AND_BLOCKAGES.md) for the contact-family design.
3. Read [ONE_TICK_CONTACT_PLAN.md](ONE_TICK_CONTACT_PLAN.md): wall reversals and ceiling profiles retain their own contact families. Ladder consecutive hits bypass reversal guards/count caps.
4. Read [CONTACT_COMBINATIONS.md](CONTACT_COMBINATIONS.md) and [LADDER_COMPOSITION_PLAN.md](LADDER_COMPOSITION_PLAN.md) for current APIs and limits. The single-interaction record in LADDER_STRATEGY_PLAN.md is historical. Complete candidates retain free facings and full-prefix movement compilation; time ranks before objective. Earlier unknowns and work limits remain unresolved rather than infeasibility claims.
5. Inspect [current composition evidence](experiments/contact-combinations/README.md) and the earlier [single-ladder experiments](experiments/ladder-search/README.md). They retain continuous problems, trajectories and shared-scene checks. Geometry-only automatic examples and prescribed-contract coverage are distinguished explicitly.

Our isolated worktree is `/Users/arvindmurty/CS/ParkourCalculatorMod-run-ticks`. The other agent owns `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-mod-integration`, branch `feature/ipopt-mod-integration`. The main `/Users/arvindmurty/CS/ParkourCalculatorMod` checkout also has concurrent work. Make our edits and commits only in our worktree. The user explicitly authorized importing the other agent's shared changes when needed; no additional permission is required. Do not modify that checkout or duplicate its physics/input implementations.

User rule: commit every added feature. Do not add code comments/docstrings or em dashes in repository writing. Preserve the native angle solver and unrelated agents' work.

## Ownership and accepted user decisions

- Three layers: orchestration, numerical solver, replay. We own orchestration. The other agent owns replay, angle realization and TAS export.
- An independently validated IPOPT solve under correctly generated continuous movement/geometry constraints is success. Game end-to-end testing and solver-to-replay export are not our current deliverables.
- Fixed start/goal supports; intermediate supports, order, transition profiles, passage choices and run allocations are search variables.
- Inputs are sprint jump on jump inputs and sprint 45 otherwise. Facings remain free on every input, including runs. No facing constraints are needed.
- Rank complete routes by total elapsed inputs first, numerical objective second. Do not rank different routes by run count first. Keep equal-time candidates for objective improvement.
- Ordered run allocation: try zero; increment the first failing transition one tick at a time; probe supported running and backtrack when it fails or a declared limit is reached. No binary search or monotonic feasibility assumption.
- Run-off exclusively means leaving a block without jumping and falling. Ordinary movement beyond an edge followed by a jump is normal tick order and has no special name.
- One horizontal blockage is a cross neo; two horizontal blockages are a wide neo. A horizontal blockage joins X/Z faces; a vertical blockage involves Y.
- Current hard caps per physical block: at most two horizontal, two vertical and three combined blockage transitions. All parts and selected route legs share the block's budget. Passing another block does not reset it. Settings may tighten, but cannot exceed, those caps.
- Different blocks can interleave on a common trajectory. Do not serialize complete obstacle passages or freeze an earlier passage's exit velocity.

## Accomplished

### Ordered run ticks and generated transition profiles

- Immutable route preparation, incremental/backtracking run search, independent candidate arrays, relative event retiming and conservative per-axis inertia/reach screens.
- Existing Java `ConstraintDeriver`/`SupportOverlap` produce support footprints; `JumpPhysicsInputs`/`JumpLinearModel` retain legacy ordered/support-plane compilation. Collision-box profiles use the corrected shared `FixedMovementCompiler`.
- Ordinary Y profiles derive from source/target heights using the existing scalar Y engine. Flat, +.5, +1 and -1 transitions use 12, 10, 9 and 14 inputs.
- Half-block stair boosts are two-input profiles: grounded takeoff plus one air input, with entry constraints and Y cancellation. This differs from the legacy supplied `step`, one grounded non-jump input.
- Automatic run-off profiles have no jump impulse, grounded incoming departure constants, following air inputs and Y-derived fall time. Drops of .0625, .125, .25, .5 and 1 use 1, 2, 3, 4 and 5 inputs respectively. Duration includes the first input, not just fully airborne inputs.
- Run-offs enumerate all four exit faces. Their route serialization remains `walk_off` and `walkOffExits` for compatibility.
- Motion-bound caching distinguishes jump/no-jump and duration. A run-off and stair profile with the same duration must not share coefficients.

### Route search and geometry

- `find_blocks` selects intermediate supports from an unordered set, including different heights, alternative profiles and repeated visits.
- Optimistic reach graph, reverse goal reachability/hop/time bounds and a full-prefix priority queue. No single visited-block flag or frozen arrival state.
- Earlier-floor exclusions and bounded outside-face branching; all positional conditions retime with added runs.
- `sceneModel: support_planes` and `coplanar_support_planes` retain their narrower floor-only scope.
- `sceneModel: collision_boxes` accepts actual six-bound sub-boxes once and derives candidate top supports plus a shared scene. Multipart parts remain separate, with physical `sourceBlock` provenance and stable solid indices. Current limit is 64 collision sub-boxes.
- Normal jump height is derived as 1.2491870787446813, then used for the cheap landing-height filter. Y masks prune unavailable over/under faces and passage windows. Conservative forward/backward and between-gate reach bounds remove impossible timing choices.
- Finite coverage bounds are explicit. A query outside supplied geometry is unknown, not assumed empty space.

### Blockage avoidance and interleaving

- All six legacy edge timing templates, with signed faces and finite approach/exit intervals.
- Cross neo, wide neo and mixed horizontal/vertical chains. A face can change after a corner; there is no permanent departure half-plane wall.
- Collision-driven discovery: solve a proposed path, identify an unplanned collision, generate alternative chains and gate times, and re-solve the full prefix. Both sides remain candidates under explicit search budgets.
- Per-block horizontal/vertical/combined caps are checked during candidate generation and route preparation. Refining one chain replaces its old usage while preserving the other selected chains' usage.
- `best.passages` holds local chains; `best.blockageTimeline` merges absolute first/second gate states; `best.blockageUsage` reports physical-block counts. The merged timeline is diagnostic, not a replacement movement order.
- The accepted interleaved fixture has ceiling Y->X at state 3, wall X->Z at 4, ceiling X->Y at 7->8, and wall Z->X at 10->11. Four events overall are legal because the wall uses two horizontal and the ceiling two vertical blockages.
- Geometry-derived facing seeds reuse the scalar numerical model and compiled friction, preserve earlier non-obstacle prefix guesses, and add no facing pins. Seed work is bounded.
- Returned routes retain the compiler's exact footprint bounds. Reconstructing a request must preserve them to avoid one-ULP differences in generated support rows.

### Acceptance and reuse

- The integration agent's `core/.../replay/StaticScene.java` and seven tests were imported unchanged in `8cd2aaa`, then extended with clipping/step-trigger diagnostics. The movement algorithm is shared, not reimplemented in Python.
- The persistent Java compiler also exposes `sceneCheck`. Python supplies intended X/Z after the existing ladder component clamp and raw Y after its ladder descent/Sneak rules. The shared helper resolves Y-X-Z collisions and both step alternatives; resulting positions/ground state must agree with the declared profile and exactly selected hit mask, with no unplanned horizontal or upward Y collision.
- Every accepted collision-box trajectory passes the independent scalar model, strict support/exit checks, Y schedule and shared swept collision checks. Full-tick endpoint clearance alone is insufficient.
- Run-off sources remain ordinary finite solids throughout the route. Their avoidance is active only during actual body-volume Y overlap, including later legs. Passing below is allowed once the head clears the actual bottom. Legacy support-plane mode still lacks that volume.

## Physics/indexing rules to preserve

- `p[t]` is before input t; input t produces state t+1. Stored incoming velocity differs from intended displacement after inertia/acceleration and from clipped displacement.
- On a normal legacy movement, Y sees old X/Z; X sees updated Y and old Z; Z sees updated Y/X.
- X->Z, Y->X and Y->Z need an outside-both state. Z->X, X->Y and Z->Y allow first-face clearance at k and second-face clearance at k+1.
- Support is checked before horizontal movement. A jump may start beyond an edge using the previous input's ground flag. Do not add a source-support condition on that jump state.
- A run-off additionally requires preceding-state support and current departure clearance, then source collision avoidance while the full body overlaps in Y.
- Use widened legacy float player dimensions, not a rounded .3/1.8 rewrite of strict predicates. Incoming per-axis inertia remains .005.
- A normal clipped component is reset before drag; other components retain their movement. Y clipping is followed by normal gravity/drag, so next stored Y carry is not generally zero.
- Horizontal clipping cancels sprint through the next input's previous-collision flag. Legacy air-factor changes/recovery lag another tick. Do not accept a wall-hit branch using current constant-effective-sprint arrays.
- Current work is a continuous model. Do not upgrade that claim to byte-exact executable Minecraft/TAS acceptance; that is the other layer.

## Current intentional-contact checkpoint

- Reuse existing Real 11 numerical X/Z contact rows, scalar validation and Y recurrence. No duplicate contact integrator or sprint machine was introduced. Historical Real 11 arrays predate the other agent's sprint fix; generated collision-box schedules compile afresh through that corrected implementation.
- `materialize_movement.py` materializes unchanged shared Java sources from pinned commit `f057c0ce9b454ae1ce2706843bb141cce799b5cb` into ignored build storage. The compiler builds them in its isolated classpath, overlaying the local extended `StaticScene`. This avoids replacing native core APIs with the other branch's newer dependencies. The pinned Git object must be available to rebuild.
- Collision-box compilation uses actual keys, selected collision flags and shared `ReplayInputState`/`PlayerSprintMachine`. Wall hits cancel sprint on the following input and carry the delayed air-factor response. Ceiling-only hits do not cancel horizontal sprint.
- `hit_events.py` generates finite signed X/Z reversal faces/times, retimed numerical/scene contacts, tangential guards, same-side before/after avoidance and next-input outward movement. Contact plane arrival without actual clipping is rejected as a selected hit.
- `ceiling_contacts.py` uses the existing Y recurrence to generate one upward hit per ordinary-jump transition and simulate the resulting shorter landing. Before/after Y sweeps ensure no earlier/repeated hit; old-X/old-Z rows enforce finite underside overlap. Durations enter the graph before reverse time bounds are computed.
- Scene acceptance reconstructs intended X/Z movement before clipping from scalar carry/control. Using resolved endpoint differences would hide wall hits. Shared collision resolution must match selected state, axis, direction, plane and limiting box; extra hits, early/late hits, missed finite faces, nearer solids and successful step substitutions are rejected.
- `maxWallHits` defaults to two selected hits per route, `maxWallHitCandidates` to 24 retained candidates, and `ceilingHits` to true. They are separate from per-block H2/V2/combined3 blockage caps. `maxContactBranches` bounds the combined branch queue. Existing avoidance regressions disable intentional contacts explicitly to preserve their original scope.
- Output adds `best.hitEvents`, `best.ceilingEvents`, `best.plan.contacts`, `best.plan.sceneContacts`, scalar raw movement/movement assumptions and `attempts[].wallHitSearch` diagnostics.
- Cold wall reversals pass in X/Z and both signs. Saved geometry-only cases: a 12-input wall reversal (~6.96 s), three-input ceiling bonk (~.28 s), and three-input simultaneous Y/X hit (~2.14 s). All have zero added runs, independent scalar acceptance and shared scene acceptance. Found yaws in saved numerical requests are warm witnesses, not cold inputs.

## Original single-ladder integration checkpoint

The first bullets describe the generator API; the subsequent integration bullets describe the now-implemented full route evaluator.

- `generate_ladder_schedules` and `trace_ladder_schedule` are exported from `standalone.autonomy`. The request is a pure vertical/timing component with caller-validated entry windows, one contiguous vertical region, exit options and a target plane. It declares vertical-only validation and does not claim a valid horizontal climb or solve earliest entry.
- Front/side entry times plus delays, variable climb/fall lengths, four beginning/end stop choices, arbitrary non-climb Sneak masks, first-clamped-input Sneak, top/side exits, canonical hit masks, per-context fair generation, explicit work caps and (total,post-exit) timing groups are implemented.
- Latest user correction: the end stop omits the final planned climb hit, never the penultimate. The final climb-refresh-eligible hit ends inside the region; the exit-producing input itself is no-hit. Omitting only a hit that ends outside cannot change Y. Tests check both interpretations and real carry/height effects rather than editing a height/count field.
- Saved [height evidence](experiments/ladder-schedules/README.md): no stops lands after 17 inputs, beginning/end after 16, both after 15. End stop lowers exit Y by about .080752 while leaving the last inside position unchanged. Incoming jump carry can reverse that comparison; exactly .2 can make the hit/no-hit Y identical. Those cases, fall caps, Sneak patterns and continuing hits are explicitly tested.
- Generator tests now total 39 cases, including the 37-case checkpoint, 500 independent-order random traces, 100 scalar-validator comparisons, exhaustive small schedule families, all 256 eight-input Sneak masks/70 four-Sneak arrangements, and exhaustive first-hit reach-bound filtering. The broader gate passed 155 tests before two more focused edge cases; numerical suite 51 passed. Native core repeats its three known baseline failures. No numerical worker or native source was changed by the generator.
- Numerical entry discovery, exact key/Sneak/sprint bridge, component-cap geometry checks, straight-line ranking and full-prefix route composition are now implemented in `climbable_search.py`, `ladder_events.py`, `ladder_geometry.py` and `ladder_ranking.py`. The numerical worker remains unchanged.
- `climbableCells` declares actual feet-center voxels. Contiguous columns preserve holes, climb faces use finite actual solids, thin ladders retain their real collision boxes, and vines create no fictional solids. All scene cells/solids are checked even if unselected.
- Entry probes increase state per signed front/side channel, retain earlier unknowns, then generate up to five delayed entries. A conservative first-hit interval bound avoids filling the pool with unreachable early hits. Contact-aware interval screening retains 300 generated valid cap/contact witnesses; small exhaustive families verify reach-bound filtering preserves all remaining schedules.
- Ranking interleaves timing buckets before its budget and benchmarks actual compiled zero-momentum horizontal progress with caps. Actual solves retain supplied initial momentum and reoptimize the entire ordinary prefix/ladder/suffix. Diverse candidates/backups survive heuristic ranking.
- Shared acceptance validates exact selected hits, no extra contacts, actual occupancy, cap/Sneak movement, Y/ground and stored velocity. Negative evidence tests reject removed/added/shifted hits, false occupancy and wrong climb carry.
- Saved cold witnesses: top exit 16 inputs; side entry 13 with Sneak at local inputs 2 and 10; side landing 16; thin ladder top 15; ordinary-prefix/ladder/suffix 39. These are accepted witnesses, not minimum-time proofs.
- One automatic interaction per route, one selected climb wall/contiguous column and one occupancy episode are current family limits. Independent reversal generation during the ladder-family solve, climb-time ceiling hits, multi-region/re-entry strategies and extra initial resume history remain later work. Ordinary pre-generated ceiling/step profiles can precede/follow the climb.

## Concrete gaps and next steps

1. **Bounded reversal corridor.** The first wall-hit family stays on the same normal side during this leg's Y-overlap intervals. It does not yet generate rounding that same solid later in the leg. Generalize to before/after blockage chains if those strategies are needed; other solids already retain independent passage chains and can interleave.
2. **More coupled evidence.** Distinct episodes and later legs can carry selected hits, but current saved cold evidence is small. Add multi-leg/repeated-hit cases and measure candidate/branch drops before raising limits. Current bounds/caps and local IPOPT can miss valid strategies; unknown is not infeasibility.
3. **Broader vertical skeletons.** One ceiling hit per ordinary transition and existing selected stair profiles work. General contact-selected step/cancel choices and their collision competition remain later work. Keep time/reach bounds admissible as those families expand.
4. **Shared-source integration.** Replace the pinned isolated source dependency with common integrated sources when both branches are combined. Do not clone sprint/input arithmetic or edit the other worktree. The integration branch remains `feature/ipopt-mod-integration`, reviewed HEAD `f057c0c`; read its latest `RECENT_REPLAY_HANDOFF.md` and `MOVEMENT_PORT.md` before later imports.
5. **Restricted state/mechanics.** Clear initial input state, normal static inert 1.8.9 boxes. Extra resume history, within-region wall-plane switching, same-region re-entry, diagonal-only continuous adjacency, special surfaces, fluids and callbacks remain outside this orchestration adapter. Multiple-interaction ladder handling reuses the existing numerical clamp/Y/contact model.
6. **Benchmarks and diagnostics.** Broaden dense finite-geometry courses and eventually Linkcraft using actual geometry. Do not reconstruct missing solids from trajectory points or use recorded yaw as cold inputs. Improve generic contact-search unknown reasons if useful.
7. **Proof/ownership scope.** `minimumTicksProven` compares normal-family witnesses to the generated graph's optimistic bound, including ceiling profiles. Ladder-family results always set it false. It is not a certificate over omitted contact families or all game strategies. Objective optimality is never claimed. Replay, angle realization, TAS export and actual-world validation stay with the other layer.

## Tests and reproduction

Local runtime: `.venv-ipopt/bin/python`, Python 3.12.2. Do not call `Path.resolve()` on this interpreter path: resolving the venv symlink selects the base interpreter without its installed packages. Use the explicit path or `.absolute()`.

Prepare/rebuild the persistent Java helper after Java changes or moving worktrees:

```sh
./gradlew --configure-on-demand \
  -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home \
  -I standalone/autonomy/compiler.gradle :core:prepareRunTicksCompiler --console=plain
```

Run the real orchestration gate, including IPOPT/Java integration cases:

```sh
PKC_RUN_TICKS_COMPILER="$PWD/standalone/autonomy/build/compiler.json" \
PKC_IPOPT_PYTHON="$PWD/.venv-ipopt/bin/python" \
python3 -m unittest discover -s standalone/autonomy/tests -v
```

The previous single-ladder integration gate passed **174 tests in 240.831 seconds**, including all five cold IPOPT/Java ladder scenes, old saved routes and existing obstacle/run/ceiling regressions. A final **13-case bridge/evidence gate passed in 6.495 seconds**, including the subsequently added coplanar-face regression; the current discovery count is 175 and every case has passed. The standalone numerical suite passed **51 tests in 26.077 seconds**. Generator coverage now includes 39 focused cases; a 43-case generator/contract run passed after reach-bound validation changes. Without both environment variables, important integration cases are skipped. Do not describe that smaller run as full acceptance.

The initial broad run exposed missing optional `ladderEvents` in older saved routes and a stale test treating ladder metadata as unsupported. Both were corrected before the passing full rerun. Coplanar multipart clipping is accepted only when the selected finite part overlaps at the correct Y-X-Z stage and both face planes match; unrelated finite faces and wrong planes are rejected.

The latest native `:core:test` run was repeated and completed 798 tests with the same three pre-existing failures: `CrossJvmDeterminismTest`, `InertiaFoldingTest` and `CostateSmoothingScaleTest` (33 skipped). Shared scene tests pass. The standalone worker and native `core/.../anglesolver/` sources have not been modified. `standalone/ipopt/model.py` and its four movement-contract tests were imported unchanged from `af12801`; native solver classes are compiled from the pinned source only inside the isolated helper. The imported/extended shared `core/.../replay/StaticScene` is the deliberate core change.

Run the interleaved cold example:

```sh
PKC_IPOPT_PYTHON="$PWD/.venv-ipopt/bin/python" \
python3 -m standalone.autonomy.solve_blocks \
  --input standalone/autonomy/fixtures/obstacle-interleaved.json \
  --output standalone/autonomy/build/interleaved-result.json \
  --seconds 55 --route-seconds 45 --candidate-seconds 1 --retries 0
```

`build/`, `.venv-ipopt/` and temporary logs are ignored. Rebuild generated compiler configuration as needed. Durable results below are tracked. Their request yaw is the found witness for warm validation; re-solving it is not a cold-search reproduction. A new trajectory must still pass full scene checks.

## Saved evidence

| Artifact | Result and scope |
| [One-input contacts](experiments/one-tick-contacts/README.md) | 12-input wall reversal; three-input ceiling and simultaneous Y/X cases; corrected movement compilation, exact hit timing and shared geometry checks |
| --- | --- |
| [Linkcraft j0-j16](experiments/linkcraft11-j0-j16/README.md) | 130 inputs, 11 added runs plus one mandatory grounded step, 80.63 s, zero scalar violation; supplied profiles/declared support regions, not full finite geometry or a minimum certificate |
| [Height and stairs](experiments/height-and-stairs/README.md) | +1/+1 route uses 18 inputs; two short stair profiles plus one run use 5; historical first-feasible checkpoint |
| [Tick/objective ranking](experiments/ticks-and-objective/summary.json) | Shorter elapsed routes beat longer zero-run routes; equal-time MAX/MIN objective checks |
| [Automatic run-offs](experiments/runoff-profiles/README.md) | Plane-model run-off and jump/run-off generation; historical geometry scope documented |
| [Minecraft edge-order probe](experiments/blockage-order/README.md) | All six blockage types, four sign combinations each, checked against cached Minecraft 1.8.9 AABB methods; 24 direct cases plus outside-both controls |
| [Finite obstacle cases](experiments/obstacle-avoidance/README.md) | Cross neo 12 inputs, wide neo 12, side run-off 5, ceiling underpass 12, deliberately blocked control unknown; independently checked requests and actual collision boxes |
| [Interleaved blockages](experiments/interleaved-blockages/README.md) | 12 inputs, zero runs, about 30.73 s; wall has two horizontal gates and ceiling two vertical gates, interleaved; zero scalar violation and shared scene acceptance |

The side-run-off case is a separate fixture with carried velocity and a nearby side edge, not a measured improvement to the earlier eight-input plane fixture. The ceiling-underpass case avoids contact; it does not demonstrate a bonk or shortened flight.

## Code map and commits

| File | Responsibility |
| --- | --- |
| `block_search.py` | Scene normalization, height/reach graph, full-prefix route queue, time/objective ranking |
| `hit_events.py` / `ceiling_contacts.py` | Signed reversal generation/retiming/guards and existing-Y-model ceiling profiles |
| `materialize_movement.py` | Pinned shared-source dependency, isolated from native solver sources |
| `block_profiles.py` | Existing-Y-model ordinary/stair/run-off profile generation and jump-height bound |
| `blockages.py` | Directed face chains, caps/physical-block usage, gate intervals/timeline, pruning and candidate generation |
| `obstacle_geometry.py` | Actual sub-box normalization/provenance, body expansion and shared scene-check adapter |
| `block_contacts.py` | Floor/geometry consistency, bounded joint floor/blockage branching and acceptance |
| `route.py` / `search.py` | Relative schedules, run allocation, support/exit rows and numerical requests |
| `ranking.py` / `screen.py` | Lexicographic ranking, bounded geometry seeds and conservative inertia/reach screening |
| `compiler.py` / `java/CompileRunTicksPhysics.java` | Persistent Java footprint/coefficient/scene-check bridge |

Recent durable implementation commits: `b554ddf` (tick/objective ranking), `ede6b01` (automatic run-offs), `41439f5` (run-off conditions), `8cd2aaa` (shared scene import), `f010496` (shared collision bridge), `1fa1472` (finite obstacle avoidance) and `7bf8263` (physical-block caps/interleaving). Older run-ticks and route/height/stair commits remain on this isolated branch. Continue from it, rather than recreating the implementation on another checkout.


## Final type-support verification, 2026-10-29

Feature commits are `39d4ede` (common signed contacts) and `a8b659c` (multiple ladder episodes, mixed schedules, finite unions and step diagnostics). The full Java/IPOPT orchestration gate passed **200 tests in 554.373 seconds** with both runtime variables set and no skips. A final focused run passed **23 tests in 4.284 seconds**, including the automatic airborne-transfer snapshot/hash and the step/ceiling/X/Z numerical cases. The unchanged standalone numerical suite passed **51 tests in 28.846 seconds**. Native core completed **799 tests, 33 skipped**, with only the three previously recorded failures in CrossJvmDeterminismTest, InertiaFoldingTest and CostateSmoothingScaleTest. StaticScene's updated step diagnostics test and new simultaneous step-contact test pass.

The first broad run found an ordinary-prefix pool ordering regression and a legacy test selecting the newly introduced general-contact family instead of its dedicated reversal family. Both were corrected; the final full gate includes the fixes. Native step clip-list expectations were updated to the newly retained actual step-path contacts, with no change to movement arithmetic. No acceptance tolerance was relaxed.

The type-support milestone is complete to the explicit search-family limits described above. Route-specific pruning/rarity heuristics remain deferred. Future work should improve bounded enumeration/continuation ranking and irregular-corridor choice coverage without deleting contact families or treating unknowns as infeasible. Final actual-world replay and executable angle/TAS acceptance remain with the other layer.
