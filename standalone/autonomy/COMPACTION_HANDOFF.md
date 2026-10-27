# Orchestration compaction handoff

Checkpoint, 2026-10-26. Implementation is committed through `7bf8263` on `codex/run-ticks-planning`. This document is a subsequent documentation checkpoint. No unfinished implementation or uncommitted experiment needs recovering.

## Resume here

1. Read repository `AGENTS.md` and `CONTEXT.md`, then this file.
2. Read [OBSTACLE_SEARCH.md](OBSTACLE_SEARCH.md) for the implemented interface and [OBSTACLES_AND_BLOCKAGES.md](OBSTACLES_AND_BLOCKAGES.md) for the contact-family design.
3. The next implementation task is intentional X/Z contact generation through the integration agent's shared movement compiler. Then add Y/ceiling-contact profile generation and update time bounds. Avoidance and interleaved blockage search are already implemented.

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
- Existing Java `ConstraintDeriver`/`SupportOverlap` produce support footprints; `JumpPhysicsInputs`/`JumpLinearModel` compile current collision-free movement arrays.
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
- The persistent Java compiler also exposes `sceneCheck`. Python supplies intended X/Z displacements and raw Y movement. The shared helper resolves Y-X-Z collisions and both step alternatives; resulting positions/ground state must agree with the declared profile, with no unplanned horizontal collision.
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

## Concrete gaps

1. **Intentional X/Z hits are not generated.** The numerical worker supports prescribed contacts, but orchestration still needs finite-face reach/overlap guards and shared wall-aware movement compilation. Current geometry acceptance rejects horizontal hits.
2. **Intentional ceiling/Y-hit profiles are not generated.** Ceiling avoidance is implemented; bonks and resulting shorter air times are not. Existing stair profiles remain a limited selected family, not general automatic contact discovery.
3. **Shared movement compiler not wired here yet.** The current `CompileRunTicksPhysics` assumes a fixed effective sprint policy, suitable for the accepted no-horizontal-hit family. `FixedMovementCompiler`/`ReplayInputState` on the other branch handle contact-driven sprint changes and must be reused before enabling wall hits.
4. **Restricted state/mechanics.** Collision-box mode declares a clear initial input state; extra resume fields, incoming wall-hit/cooldown history, ladder/special-surface/fluid/callback declarations are rejected. Only normal static inert geometry is modeled by this adapter.
5. **Bounded search, local numerical optimization.** Branch/queue/candidate/timing caps and timeouts can miss valid routes and interleavings. Complexity/side scores are ordering hints. There is no global objective certificate.
6. **Proof scope.** `minimumTicksProven` only means a witness reaches the current profile graph's lower bound. It does not cover omitted intentional-contact families or every game strategy. `objectiveOptimalityProven` is false.
7. **Benchmarks remain small.** New finite-geometry obstacle cases are synthetic. The interleaved case covers two obstructions, but dense multi-obstacle courses and full Linkcraft geometry remain untested in this mode.
8. **Generic unknown diagnostics.** A deliberately blocked control uses one jump and no runs, with a wall too tall to clear and too wide to go around. The numerical path hits it, no passage survives screening, and the outer status is generic `unknown/contact_search_exhausted`. A more specific no-passage-in-family reason would improve reporting; no global infeasibility certificate is produced.

## Next steps in order

1. Review/import the integration agent's shared movement compiler and necessary dependencies. The user authorized this. Read its latest handoff and avoid duplicating its input/sprint/float arithmetic.
2. Wire the orchestration adapter to that compiler; add an X-hit and Z-hit profile, finite tangential overlap at the correct stage, actual clipping/reach guards and normal reset. Test hit/avoid alternatives, sprint cancellation and air-factor recovery, including simultaneous X/Z events.
3. Add underside/top Y contacts, regenerate the remaining vertical trace and air time, and recheck every affected blockage window. Rebuild admissible time bounds over those shorter profiles so an ordinary-jump bound cannot prune a faster bonk route. Keep horizontal alternatives.
4. Broaden route/contact/obstacle coupling benchmarks, then replace supplied Linkcraft profiles with generated profiles where actual geometry is available. Reuse the mod's geometry/constraint helpers. Do not infer absent solids from recorded trajectory points or supply recorded facings as cold inputs.
5. Measure candidate counts and numerical recovery before redesigning search. Improve unknown reasons and targeted retries if measurements justify them. Keep replay/UI/export work with the other agent.

The integration branch reviewed here is `f057c0c`. Important source commits are `133b48c` (shared scene/replay machinery) and `af12801` (wall-aware compiler/input-state fix). `FixedMovementCompiler` and `ReplayInputState` depend on that branch's newer effects/input APIs and exact movement callback; inspect the dependency diff rather than blindly replacing unrelated native solver files. Read from our worktree with `git show feature/ipopt-mod-integration:standalone/ipopt/RECENT_REPLAY_HANDOFF.md`, then inspect `MOVEMENT_PORT.md` and the shared Java implementations. Isolated reference copies are in ignored build storage, and [integration-reference.json](experiments/blockage-order/integration-reference.json) records the reviewed source hashes.

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

The latest full run passed **110 tests in 71.243 seconds**; focused obstacle/cap tests passed 18 afterward. Without the two environment variables, important integration cases are skipped. Do not describe that smaller run as full acceptance.

The native `:core:test` run completed 798 tests with the same three pre-existing failures: `CrossJvmDeterminismTest`, `InertiaFoldingTest` and `CostateSmoothingScaleTest` (33 skipped). Shared scene tests pass. The standalone worker and native `core/.../anglesolver/` sources have not been modified by this work. The imported/extended shared `core/.../replay/StaticScene` is the deliberate core change.

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
| `block_profiles.py` | Existing-Y-model ordinary/stair/run-off profile generation and jump-height bound |
| `blockages.py` | Directed face chains, caps/physical-block usage, gate intervals/timeline, pruning and candidate generation |
| `obstacle_geometry.py` | Actual sub-box normalization/provenance, body expansion and shared scene-check adapter |
| `block_contacts.py` | Floor/geometry consistency, bounded joint floor/blockage branching and acceptance |
| `route.py` / `search.py` | Relative schedules, run allocation, support/exit rows and numerical requests |
| `ranking.py` / `screen.py` | Lexicographic ranking, bounded geometry seeds and conservative inertia/reach screening |
| `compiler.py` / `java/CompileRunTicksPhysics.java` | Persistent Java footprint/coefficient/scene-check bridge |

Recent durable implementation commits: `b554ddf` (tick/objective ranking), `ede6b01` (automatic run-offs), `41439f5` (run-off conditions), `8cd2aaa` (shared scene import), `f010496` (shared collision bridge), `1fa1472` (finite obstacle avoidance) and `7bf8263` (physical-block caps/interleaving). Older run-ticks and route/height/stair commits remain on this isolated branch. Continue from it, rather than recreating the implementation on another checkout.
