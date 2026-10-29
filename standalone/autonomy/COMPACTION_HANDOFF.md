# Orchestration compaction handoff

Updated checkpoint, 2026-10-27, on `codex/run-ticks-planning`. Run ticks, support routing, blockages, one-input wall/ceiling contacts and one ladder/vine interaction per composed route are implemented. `46f0f52` added the shared ladder compiler/ranking foundation and `d6ee0a4` added the complete route feature, cold fixtures and saved evidence.

## New authorized work, 2026-10-29

Read [LADDER_COMPOSITION_PLAN.md](LADDER_COMPOSITION_PLAN.md) before implementation. The user now requests multiple ladder interactions and joint deliberate wall/ceiling contacts. Horizontally/vertically adjacent cells should represent one exact ladder region; diagonal-only adjacency, switching climb walls and same-region re-entry remain deferred. Preserve full-prefix solving and shared movement compilation. The integration branch is idle; published tip `e09f2a7` was reviewed with Git/GitHub CLI and its latest update is documentation only. The current source pin already contains the movement fixes. The composition extension is planned, not implemented yet.

## Resume here

1. Read repository `AGENTS.md` and `CONTEXT.md`, then this file.
2. Read [OBSTACLE_SEARCH.md](OBSTACLE_SEARCH.md) for the implemented interface and [OBSTACLES_AND_BLOCKAGES.md](OBSTACLES_AND_BLOCKAGES.md) for the contact-family design.
3. Read [ONE_TICK_CONTACT_PLAN.md](ONE_TICK_CONTACT_PLAN.md): wall reversals and ceiling profiles retain their own contact families. Ladder consecutive hits bypass reversal guards/count caps.
4. Read [LADDER_STRATEGY_PLAN.md](LADDER_STRATEGY_PLAN.md), including its numerical execution plan and delivered scope. `find_blocks` now discovers signed entry channels/timings, generates and ranks schedules, and solves an ordinary prefix, one ladder interaction and ordinary suffix jointly. Earlier entry unknowns remain unresolved; total elapsed time then objective still ranks complete candidates.
5. Inspect [five saved ladder experiments](experiments/ladder-search/README.md), especially side entry with two chosen Sneak inputs and the full `start -> pad -> wall -> goal` route. Every result has a geometry-only source request and independently checked movement/Y/scene contract.

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

## Ladder integration checkpoint

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
5. **Restricted state/mechanics.** Clear initial input state, normal static inert 1.8.9 boxes. Extra resume history, multiple ladder interactions, special surfaces, fluids and callbacks remain outside this orchestration adapter. Single-interaction ladder handling reuses the existing numerical clamp/Y/contact model.
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

The complete ladder integration gate passed **174 tests in 240.831 seconds**, including all five cold IPOPT/Java ladder scenes, old saved routes and existing obstacle/run/ceiling regressions. A final **13-case bridge/evidence gate passed in 6.495 seconds**, including the subsequently added coplanar-face regression; the current discovery count is 175 and every case has passed. The standalone numerical suite passed **51 tests in 26.077 seconds**. Generator coverage now includes 39 focused cases; a 43-case generator/contract run passed after reach-bound validation changes. Without both environment variables, important integration cases are skipped. Do not describe that smaller run as full acceptance.

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
