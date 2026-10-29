# Multiple ladders and mixed contact schedules

2026-10-29. Implementation plan following the single-interaction prototype in [LADDER_STRATEGY_PLAN.md](LADDER_STRATEGY_PLAN.md). This document describes planned work, not delivered capabilities. User scope now includes multiple ladder interactions and deliberate wall/ceiling contacts composed with climbing. Diagonal-only ladder adjacency, switching climb-wall orientation within a ladder episode, and re-entering the same ladder after leaving remain deferred.

## Existing foundation and shared-source context

The current prototype discovers one ladder interaction between ordinary route prefixes/suffixes. It generates fall/Sneak/climb/stop/exit schedules, uses the shared movement compiler, and validates accepted continuous trajectories against actual finite scene geometry. Existing ordinary-route wall reversals, ceiling profiles and blockage branches provide reusable event constraints. The numerical worker already supports prescribed horizontal/vertical contacts, component-cap regimes and multiple occupied ladder states.

Reviewed the published integration tip `e09f2a7956d3ef2cd36d53cfedb5faa9c0adc096` using Git and GitHub CLI. Its cumulative `RECENT_REPLAY_HANDOFF.md` is dated 2026-10-28. The update after our pinned `f057c0c` contains documentation only; shared movement implementations have no newer code changes to import. Relevant files are `FixedMovementCompiler`, `ReplayInputState`, `ReplayRows`, `StaticScene` and the existing Python model. Continue using them, not a second sprint/input/physics implementation.

The other layer provides local exact replay, angle repair and SaveIO export; the mod performs final actual-world validation. Declared-contact replay does not establish arbitrary swept clearance or finite face coverage. Our finite-scene checks remain part of continuous acceptance. Portable mid-route checkpoints are unfinished, so every orchestration solve remains rooted at the original initial state. A temporary optimizer X/Z position/velocity is not an executable checkpoint.

## Target behavior

Given fixed initial state, start/goal supports, actual collision boxes and climbable cells, choose intermediate supports, ladder regions, approach/entry/exit timing, run allocation, Sneak/climb/fall schedule, deliberate hits and blockage passages. Reoptimize all earlier facings and connecting momentum in the same continuous solve. Rank complete candidates by total elapsed inputs, then the requested numerical objective.

Multiple interactions include ladders separated by real landings and ordinary movement, and a transfer from ladder A to a separate ladder B without an intervening landing. In an airborne transfer there is no new ground jump impulse, ground friction or velocity reset. Adjacent climbable cells that form one region remain one ladder episode; crossing an internal cell boundary is not a detach/re-entry.

## Geometry and ladder regions

1. Group face-adjacent climbable voxels into connected regions. Vertical and horizontal neighbors are connected; diagonal-only contacts do not join regions. Preserve all original cells and physical solid provenance.
2. Describe the exact union at each relevant Y height. A solid rectangular strip can use a merged rectangle. Irregular regions use an exact rectangle/cell cover, without filling holes or concave gaps using the region bounding box.
3. Select compatible coplanar wall pieces from actual collision geometry. A connected region can expose several wall alternatives, but a generated episode chooses one climb orientation/plane; turning onto another wall remains deferred.
4. For an irregular region, choose a bounded sequence of exact occupied corridors outside IPOPT. Each fixed corridor becomes smooth positional rows. Internal shared boundaries remain legal, without margins that create fictional gaps between adjacent cells. The scene checker verifies actual union membership at every state.
5. Region bounds are only optimistic reach screens. Body clearance and collision-face extents continue to use the real solids, not the climbable-voxel volume. Thin ladders retain thin solids; vines add no invented collision boxes.

## One relative event contract

Represent each candidate using a common ordered timeline with stable region/physical-solid IDs and relative input/state anchors. Events include region entry/exit, climb hit, deliberate reversal, ceiling hit, landing, ground run, jump/run-off/step profile and blockage gates. A direct airborne transfer joins two region episodes inside the same flight, rather than inventing a support transition.

Materialize the timeline into global key rows, occupied-state masks, horizontal and vertical contacts, positional/corridor/avoidance rows and Y references. Run insertion or a changed duration retimes every later event, mask, reference and objective together. Compatible duplicate physical hits at the same tick/axis merge; incompatible planes, directions or event demands reject that skeleton. Retain a diagnostic role without letting that label override movement physics.

For every fixed timeline, replay Y with the existing scalar engine from the original initial Y/VY, using actual occupancy, keys and selected contacts. Recompile the complete key/ground/contact history with the shared compiler before numerical solving. Recompute earlier-floor checks, active body-height intervals, landing time and admissible time bounds whenever Y changes. Cache by complete relevant discrete/initial-state assumptions; do not reuse coefficients solely because the local ladder shape is unchanged.

## Movement order and contact composition

Retain the legacy order: pre-movement inertia/control and ladder caps, collision resolution Y then X then Z, then post-movement climb carry and normal gravity/drag. Sneak scales input; it does not scale all carried horizontal velocity. Ladder descent is capped at -.15 and Sneak arrests negative Y only; positive carry persists. Horizontal component caps apply independently before clipping.

- A reversal is an actual one-input horizontal clip, with finite reach/tangent overlap and before/after avoidance. It cancels the normal stored component and changes later sprint/air-factor compilation. Its next input moves outward in that normal direction. Separate climb hits do not inherit that outward guard.
- A ceiling hit clips upward Y against an actual finite underside. Its footprint is evaluated before horizontal movement. X contact uses resolved Y/old Z; Z contact uses resolved Y/new X. Keep those stage coordinates in the generated constraints and independent acceptance.
- Any horizontal clip ending inside a climbable region refreshes upward carry, even if its diagnostic purpose is reversal. Entering uncapped on that input can still refresh carry. Do not switch this rule off based on the event's role.
- A ceiling clip and ladder wall clip can occur on the same input. Y clips first; if the horizontal clip ends inside the region, climb carry can then replace the cancelled Y carry. A ceiling hit therefore does not always make the next input descend or shorten the climb.
- Ordinary ceiling contacts are usually one input. During sustained climbing, refreshed upward carry can cause another ceiling clip on the next input. Generate the necessary bounded repeated ceiling events or an alternative stop/exit; do not impose an unconditional one-ceiling-per-flight rule on that family.
- A reversal on another axis may coexist with climbing. Merge the axis contacts into one physical movement and require all reach/avoidance rows to agree. Same-axis competing contacts require one compatible final clipping plane.

Maintain the existing per-physical-block blockage caps: two horizontal, two vertical, three combined. They apply across episodes and interleaved obstacles, and count blockage transitions rather than climb/contact ticks. Keep independent reported work budgets for route expansion, ladder episodes, deliberate hit events and schedule generation; never reuse the reversal-hit cap as a climb-duration cap.

## Search and schedule generation

Use a shared best-first route/event work queue rather than an outer ordinary-prefix/one-ladder/ordinary-suffix enumeration. A node retains its discrete support/region/event history and timing alternatives. It does not fix an arrival position, velocity or facing from a previous solve. Expanding a node appends an ordinary profile, a ladder episode, an airborne region transfer or a permitted contact variation; numerical evaluation solves the whole resulting prefix from the original state.

Prioritize optimistic complete elapsed time: committed input count plus an admissible remaining-time bound. Ordinary jump-only height or time bounds cannot prune ladder paths. Until a stronger bound is justified, use a looser bound that permits climbing/contact-shortened durations. Among complete accepted routes, total elapsed inputs dominate objective. Numerical objective and cheap progress scores order work within comparable timing candidates; they are not feasibility certificates or sound dominance rules.

Retain routes reaching the same support or region with different histories. A fastest local climb can lose overall because of exit momentum, exit height, later runs or another ladder. Bound the queue explicitly and report dropped candidates; do not use a single visited-support bit or freeze the first feasible ladder schedule. Retry earlier unknowns that could improve the incumbent; a timeout is not a proof that later entry or more run ticks is necessary.

For each incoming event/contact context:

1. Use height/body overlap and conservative movement envelopes to find candidate regions, walls, ceilings, entry channels and transfer windows. Entering movement is uncapped if the input started outside. Inside-region movement uses the actual component caps.
2. Probe front and both signed side entry channels in increasing time from the original route start. Retain no-hit/hit-on-entry and relevant contact-assisted approaches. Keep earlier unknowns, then generate zero through five delayed entry times around validated entry timing. Ceiling/contact variants that change the approach or Y trace need their own entry context; a no-contact entry result cannot exclude their earlier entries.
3. Retain the existing schedule variety: variable climb windows, fall intervals, neither/beginning/end/both optional one-input stops, arbitrary Sneak patterns during non-climb inputs, and the actual first-clamped-input Sneak toggle. An end stop omits the final planned climb-refreshing hit. Internal stop periods remain canonicalized to beginning stops under the user's chosen family.
4. Add bounded reversal and ceiling alternatives on the approach, within the interaction and during exit/transfer. Retain avoidance alternatives too. Regenerate Y, occupancy windows, landing/transfer timing and downstream constraints after every changed contact set. One selected schedule is a fixed smooth branch; discrete alternatives stay outside IPOPT.
5. Rank comparable schedules by the existing zero-momentum straight-line progress benchmark, retain timing/entry/exit/contact diversity and backups, and interleave families before caps. This score does not replace the full solve's actual incoming velocity or whole-route elapsed-time/objective ranking.
6. Solve and independently validate promising complete prefixes/routes. Keep an accepted incumbent through deadline/cancellation and preserve unresolved/dropped work in diagnostics. Bounded search/local IPOPT still does not prove global optimality or infeasibility.

The grounded multi-ladder case is the first implementation slice. Direct airborne transfers follow in the same design before claiming general multi-ladder support. Their schedules start from the previous global Y/VY/collision/input state, without inserting a fictitious jump, landing or rest state. Changing the first episode's exit requires reevaluating the transfer and later episode jointly.

## Acceptance and tests

Keep continuous-model acceptance as this layer's delivery criterion. For each accepted witness independently check scalar rows, real support/exit conditions, exact union occupancy, all selected finite contacts, no unplanned collision, X/Z caps, Y/ground and stored velocity against the shared complete finite scene. Compiler movement assumptions must match the observed ground/horizontal-collision schedule. Reuse the existing clamp regimes; do not introduce a new approximate clamp or pass final displacement as intended movement.

Unit and contract checks:

- Face-connected vertical/horizontal regions, exact rectangular strips, irregular cross-sections, holes and diagonal-only separation. Internal adjacent-cell crossings stay occupied without a new entry event.
- Multiple episodes and relative event retiming after runs, earlier duration changes and later contacts. Inputs, masks, Y references, gates, contacts and objective move together; candidate arrays remain independent.
- Carry through two landings/ladder episodes and through an airborne transfer, including previous sprint/air factor and actual Y carry. No fabricated midair jump or per-segment rest/velocity reset.
- Ceiling plus climb on the same input: cancelled Y then refreshed climb carry; the no-horizontal-hit variant instead descends. Repeated ceiling clips during sustained climb and stopping the final climb hit produce different, correct Y timelines.
- Reversal before/after climbing, orthogonal reversal during climbing, uncapped hit-on-entry, finite underside overlap and Y-X-Z stage-specific face overlap. Wrong early/late/missing/extra contacts, false occupancy and wrong stored carry must fail acceptance.
- Small exhaustive families versus bounded enumeration, all existing Sneak/stop regressions, retained equal-time alternatives, time-first queue ordering, unknown retries and explicit budget/cancellation accounting.

Cold geometry-only acceptance fixtures:

1. Two distinct ladders with an intervening landing/run or ordinary jump.
2. Ladder A to ladder B without an intervening landing, then a real final support.
3. A horizontally adjacent ladder strip traversed within one occupied region.
4. A wall reversal on a ladder route and a ceiling hit on a ladder route, with before/after avoidance and corrected compiled sprint/Y state.
5. A simultaneous ceiling/climb case and a case whose faster exit requires omitting a climb hit.
6. One mixed course combining runs, ordinary movement, deliberate hits, ladders and blockage avoidance. Include a competing locally faster first episode with a worse full continuation.

Requests contain geometry and initial state only, without recorded facings, chosen hit times or picked schedules. Save accepted continuous problems/trajectories and independent scene checks. Record generated, screened, ranked, numerically attempted, unknown and dropped alternatives, plus time spent per family. Add a real course section using actual existing geometry/capture data when available, without making game replay a prerequisite or inventing missing solids from trajectory samples.

## Implementation sequence and feature commits

1. Introduce the shared relative event/episode representation and exact face-connected region geometry. Preserve old saved-route compatibility and existing single-ladder tests. Commit the feature with focused contract/geometry coverage.
2. Replace the single-interaction outer loop with common time-prioritized route expansion. Support multiple grounded ladder transitions, full-prefix recompilation and retiming. Save a cold two-ladder witness and commit.
3. Add direct airborne region transfers with continuous global Y/input carry and no fictitious ground transition. Save a cold transfer witness and commit; only then describe general multiple-ladder routing as delivered.
4. Compose deliberate one-input reversals and regenerated ceiling events with ladder schedules, including correct simultaneous/repeated ceiling/climb behavior and avoidance. Save positive and negative mixed-contact evidence and commit.
5. Run the mixed course and existing full orchestration/numerical gates, measure queue/family drops, and update the handoff with precise supported cases and remaining limitations. Broaden native checks only if shared/native solver code is changed. Keep known baseline failures separate. Commit final evidence/documentation.

## Deferred cases

Diagonal-only adjacency inside one ladder episode, changing climb-wall plane/orientation inside an episode, and leaving/re-entering the same ladder region remain explicit search-family gaps. Distinct region episodes along a route are supported by the planned composition; diagonal proximity does not imply continuous occupancy. General contact-selected stair/step discovery, effects/special surfaces, fluids/callbacks and extra initial resume state remain outside this extension. Reuse existing supported ordinary stair profiles and scene rejection rather than silently approximating these mechanics.
