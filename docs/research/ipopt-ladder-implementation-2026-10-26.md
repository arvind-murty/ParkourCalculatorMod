# Prescribed ladder support, 2026-10-26

## Scope

Implement in new commits in the isolated standalone worktree. The user redirected the initial geometry investigation to use climbing ticks in the original Linkcraft Real11 JSON. Work therefore targets captured, prescribed ladder/collision schedules; general world geometry discovery is deferred. No mod, native solver, source capture or other agent file is changed.

The original capture `/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json` has the same debug states as the existing separate no-facing copy. Stored Y velocity `(0.2 - 0.08) * (double) 0.98F = 0.11760000228881837`, together with the recorded horizontal collision, identifies climb states. Through state 400 these are 136-152, 190-193, 356-360, 362 and 395-400 (zero-based). Climb-proven cell membership additionally identifies ladder tick 189 and the no-collision tick 361, giving ladder input ranges 136-152, 189-193, 356-362 and 395-399 in a 400-input solve. Final state 400 is also on the ladder. These are inferred cells from the recorded route, not an exhaustive world snapshot.

## Commit 1: scalar physics and state contract

Added optional `ladderTicks` with n+1 state flags and optional fixed `ladderPattern` branches. Scalar replay clamps each axis after inertia and acceleration, before wall reach and normal collision reset. It verifies explicit cap branches without strict separation at the continuous cap boundary.

Added an optional Minecraft 1.8.9 `vertical` problem: initial Y/velocity/ground/previous-horizontal-collision, per-input jump impulse/sneak, and prescribed vertical contacts. Replay handles downward ladder caps, sneak holding, gravity/drag, floor/ceiling clipping and explicitly declared step contacts up to the player step height. Constructing the next state uses the horizontal collision from the completed movement and the ladder occupancy of that next state to establish climb carry. The current movement never receives that future upward displacement. The first state's supplied Y velocity is already stored carry; seam output preserves both it and the previous collision flag.

Optional vertical position/velocity references are hard validation rows. A prescribed vertical contact or step is part of the supplied model, not evidence of a geometry collision. This keeps the scope consistent with the existing prescribed horizontal contacts. The scalar result includes Y, vertical velocity, ground, horizontal collision and resume state.

Twenty targeted scalar tests pass, including independent positive/negative caps, cap-before-wall reach, inertia ordering, entry timing, tangential capping on the next tick, missed-wall rejection without granting climb, Y floor/ceiling/sneak/jump, prescribed steps, boundary branches, malformed fields and seam equivalence. Worker integration and captured export follow in subsequent commits.
