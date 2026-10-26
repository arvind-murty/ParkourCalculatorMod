# Standalone ladder support, implementation plan

Research and design only. The horizontal solver, mod and other agent's files are unchanged in this turn. Target the route's Minecraft 1.8.9 behavior first, rather than mixing modern climb-by-jump rules into legacy physics.

## What the mod already provides

- `MinecraftAccess.isClimbable` and collision-box queries expose block classification and actual collision shapes through an MC-free port. Forge implements vanilla ladders/vines; modern Fabric uses the climbable tag. Ladder activation and the thin solid ladder collision shape are different data. Vines have the activation behavior without a solid ladder shape.
- Forge `SimulatorEntity.moveEntityWithHeading` / `travel` samples `isOnLadder` before delegating to the Minecraft superclass. Actual collision and climbing are implemented by Minecraft, rather than by an independent Java solver module.
- `SimulationRunner.snapshot` stores the captured medium and friction in `TickState`. The sample in state `t+1` describes the movement of input tick `t`.
- `Application.applyPathSurfaceState` already copies those samples into per-tick medium/slipperiness overrides. Those overrides can be saved and later read through `physics.surfaceAt(t)`. This is a ready source for a prescribed initial ladder schedule.
- `ExactJumpModel` implements ladder horizontal caps, the downward Y floor and sneak holding on explicitly authored ladder ticks. Its movement is collision-free, so it is not a complete ladder/world replay implementation and cannot supply the collision-triggered legacy climbing rule by itself.

The saved `DebugTick` currently includes position, velocity, ground, sneak and collision flags, but not the live medium/friction sample. `Fixtures.buildBoxes` therefore does not reconstruct that sample from debug JSON. The standalone exporter currently emits neither ladder flags nor Y/world geometry. A wall-contact flag alone cannot reconstruct ladder occupancy reliably.

## Version-specific movement order

Local 1.8.9 Minecraft/Forge bytecode was inspected, without fetching a web mirror or launching a client. `EntityLivingBase.moveEntityWithHeading` performs the following normal, non-fluid sequence after the earlier inertia/jump/input work:

1. Add horizontal input acceleration.
2. Query ladder occupancy at the current feet/center cell. In vanilla/default Forge behavior the cell is `(floor(posX), floor(boundingBox.minY), floor(posZ))`.
3. If on a ladder, clamp X/Z to `+/- (double) 0.15F`, limit negative Y to `-0.15` and prevent negative Y while the player sneaks.
4. Call `moveEntity`, which clips movement against actual world geometry and updates position and collision flags. The normal collision path clips Y, then X, then Z; stepping and sneak edge handling also matter where applicable.
5. Query ladder occupancy again at the moved position. If horizontally collided and still/on-now a ladder, set Y motion to `0.2`.
6. Apply gravity and drag. The climb case carries approximately `(0.2 - 0.08) * (double) 0.98F = 0.1176` into the next tick. That is a next-tick velocity, not an immediate additional upward displacement of the tick that just moved.

Forge's `fullBoundingBoxLadders` option changes the activation query; the inspected version defaults it to false. The profile should record which predicate is intended. The simple `isClimbable` block query does not by itself encode every loader/configuration-specific entity predicate.

### Correction to the initial entry claim

The initial commentary stated that a player can enter a ladder and receive the climb impulse on a tick with uncapped horizontal movement. This was initially inferred from the two queries without checking a concrete trajectory. The user challenged it. It should be qualified rather than treated as the ordinary entry sequence or a fact about Real11.

A small geometry probe used the actual cached Minecraft `AxisAlignedBB.calculateZOffset`, a 1.8 player half-width `(double) 0.6F / 2`, starting center `z=2.01`, and a ladder voxel `z in [1,2)` with its solid face occupying `z in [1,1.125]`. With X centered in the ladder block and feet Y in its cell:

| Incoming Z move | Allowed move | End center Z | Ladder before | Ladder after | Horizontal hit |
| --- | --- | --- | --- | --- | --- |
| -0.2 | -0.2 | 1.81 | false | true | false |
| -0.4 | -0.4 | 1.61 | false | true | false |
| -0.8 | -0.5849999880790708 | 1.425000011920929 | false | true | true |

The last geometry case satisfies the post-move climb condition in the inspected control flow even though the pre-move cap condition was false. It requires enough incoming movement to enter the voxel and reach the collision face in one tick. This is a geometry/control-flow check, not a complete Minecraft entity replay or a demonstration that Real11 does it. The implementation should preserve both query timings and test the actual captured transition rather than assume an entry impulse.

The probe source and stdout are archived as `solver-traces/ipopt-standalone-2026-10-25/LadderGeometryProbe.java` and `ladder-geometry-probe.txt`. The inspected cached mapped jar SHA-256 is `ef018cf97218f594f7eff3501f91a76136797bf8af43e5f67821a7c3ffefd012`, under the original checkout's `.gradle/unimined/net/minecraft/minecraft/1.8.9/MinecraftForge-FG2/1.8.9-11.15.1.2318-1.8.9/mcp-stable-22-1.8.9-searge-1.8.9/`. Relevant `moveEntityWithHeading` bytecode offsets: first ladder query 234, move call 357, second ladder query 368, climb assignment 375-378. No full Minecraft class dumps are committed.

## Implementation work

### 1. Export a sufficient standalone problem

Extend our own exporter/request format with the game profile, initial Y/vertical velocity, jump/sneak inputs and any needed resume state; prescribed ladder-before-move flags; and a small world snapshot containing climbable cells, collision AABBs, friction properties and player dimensions. Represent ladder occupancy once per state boundary, `L[0..n]`, and preserve the horizontal collision flag for each movement, `H[0..n-1]`. The occupancy after movement `t` is the same state as the occupancy before movement `t+1`; it need not be a separate event schedule. Existing live surface overrides can seed the before-move schedule without changing the mod's UI.

The world snapshot must cover the trajectory region to be validated, including alternate paths; leaving its coverage cannot be treated as entering known empty space. Existing 200/400 compiled fixtures lack this geometry. Their recorded Y/velocity can seed a first experiment, but a recorded Y trace is an explicit fixed-path assumption, not complete vertical validation after horizontal changes.

### 2. Implement scalar world/vertical replay first

Add a standalone replay module, separate from the smooth IPOPT equations. Match the selected game's activation predicate, inertia checks including Y, jump, ladder caps, collision movement, collision-triggered climbing, sneak holding, gravity and drag. Reuse the mod's constants and per-tick state conventions and compare against the actual simulator's captured trace. Include ground/ceiling clipping, stepping and sneak edge behavior for the world features supported by the fixture; unsupported features must not be silently modeled as air.

Replay must report ladder occupancy at each state boundary, saturation branches, per-movement collision flags/contacts, ground/jump state, positions and stored velocities. Changes in ground/jump/sprint state can require recompiling the horizontal acceleration/friction arrays rather than merely changing ladder flags. This is the main additional physics work beyond the current horizontal replay.

### 3. Add fixed cap branches to the horizontal NLP

Keep Y outside the horizontal optimizer initially. Given a prescribed ladder schedule, one horizontal component computes `r = zeroedCarry + control`. On ladder ticks use `d = clamp(r, -c, c)`, where `c = (double) 0.15F`, before testing wall reach and moving. A no-wall component then stores `friction * d`; a reached wall clips position and resets the normal component afterward.

Use three fixed branches: negative saturation (`r <= -c`, `d = -c`), free (`-c <= r <= c`, `d = r`), positive saturation (`r >= c`, `d = c`). This preserves the existing smooth fixed-regime solve. Constraining raw velocity to the interval would incorrectly exclude legitimate saturation. Independently replay the real clamp for acceptance.

Y can be replayed outside IPOPT for a fixed event schedule. Correct occupancy/grounding must still be checked against the solved X/Z path; a captured Y trace alone is not enough for full ladder support.

### 4. Extend continuation to discover and repair schedules

Replay an initial path, infer ladder occupancy/cap branches alongside inertia and contacts, solve the fixed event pattern, then replay again. Check ladder membership at every state boundary, as well as unintended entry on states declared ordinary. Use the next state's occupancy and the preceding movement's horizontal collision to establish the climb carry for that next state. Refresh changed schedules and try adjacent regimes within the budget. Preserve the best fully validated incumbent and any alternative schedules; a failed fixed schedule is unknown for the broader problem.

The NLP can add membership/selected outside-face rows to reduce schedule drift, with scalar replay authoritative at cell boundaries. Actual vertical replay is required before claiming a physically valid full solution. A fixed-schedule horizontal-cap milestone is useful earlier, but its narrower assumptions should remain explicit.

### 5. Verify against Minecraft traces

Required cases include both signs and axes of saturation, free/saturated boundary values, inertia followed by caps, caps before wall reach, tangential carry at a wall, ordinary entry and exit, climbing initiation/termination, sneak holding, top and bottom of ladder runs, floors/ceilings, vines, and negative-coordinate/cell-boundary activation. Check the two occupancy timings with a full entity trace, including the challenged high-speed entry case if supported. Re-run the actual ladder sections of Real11 and all existing standalone regression tests with unchanged constraint tolerances.

Recommended order: export an explicit schedule and vertical reference, implement/test scalar ladder rules, add the NLP cap branches, then add geometry-driven discovery and coupled vertical validation. The first fixed-schedule milestone must not be presented as completed automatic world-aware ladder support.

## Confirmed entry timing, user clarification

Both example approaches start outside the ladder, so the entry tick applies no ladder horizontal caps. In the slower, non-colliding entry, normal air vertical movement applies. In the faster entry with a horizontal hit and ladder occupancy after movement, the climb rule sets Y motion to .2 before gravity/drag, carrying approximately +.1176 into the next tick. The climb rule does not retroactively change the entry tick's displacement.

Both examples end that tick on the ladder. Consequently the second tick applies the ladder horizontal cap independently to both X and Z after acceleration and before movement. This includes any large tangential X component, even if Z caused the wall hit. The wall collision separately resets its normal velocity component. The before-move ladder flag controls the horizontal caps; the post-move ladder/contact condition controls the climb velocity carried forward. This clarification changes no implemented physics or earlier probe results.

## Tick-boundary formulation, 2026-10-26

The user correctly pointed out that the climb displacement occurs on the following tick. The earlier explanation mixed the tick-end velocity assignment with the tick that uses that velocity for movement. We can use one ladder occupancy value `L[t]` at the start of each tick, rather than separate before/after schedules. Minecraft's query after movement `t-1` evaluates the same position as the start-of-tick query `L[t]`.

Horizontal caps on movement `t` use `L[t]`. The climb velocity used by that movement is the incoming `vy[t]`; the prior transition established it using `H[t-1] && L[t]`. In the loaded, normal 1.8.9 movement branch, that condition produces approximately `(0.2 - 0.08) * (double) 0.98F = +0.1176` as the incoming vertical carry. A replay can defer this climb-carry calculation to construction of state `t`, using `L[t]` and the previous collision flag, before applying this tick's inertia/jump processing. This is a reindexing of the same dynamics, not an immediate upward displacement on entry.

Ladder occupancy alone does not imply climbing: retain the previous movement's horizontal collision or the already computed incoming Y velocity. Using the current movement's eventual collision to alter its own upward displacement would shift climbing one tick early. With the tick-boundary formulation, we do not need an independently inferred after-move ladder variable or an extra physical event between ticks. No production physics changed for this documentation clarification.
