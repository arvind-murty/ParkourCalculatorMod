# Prescribed ladder support, 2026-10-26

## Scope

Implement in new commits in the isolated standalone worktree. The user redirected the initial geometry investigation to use climbing ticks in the original Linkcraft Real11 JSON. Work therefore targets captured, prescribed ladder/collision schedules; general world geometry discovery is deferred. No mod, native solver, source capture or other agent file is changed.

The original capture `/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json` has the same debug states as the existing separate no-facing copy. Stored Y velocity `(0.2 - 0.08) * (double) 0.98F = 0.11760000228881837`, together with the recorded horizontal collision, identifies climb states. Through state 400 these are 136-152, 190-193, 356-360, 362 and 395-400 (zero-based). Climb-proven cell membership additionally identifies ladder tick 189 and the no-collision tick 361, giving ladder input ranges 136-152, 189-193, 356-362 and 395-399 in a 400-input solve. Final state 400 is also on the ladder. These are inferred cells from the recorded route, not an exhaustive world snapshot.

## Commit 1: scalar physics and state contract

Added optional `ladderTicks` with n+1 state flags and optional fixed `ladderPattern` branches. Scalar replay clamps each axis after inertia and acceleration, before wall reach and normal collision reset. It verifies explicit cap branches without strict separation at the continuous cap boundary.

Added an optional Minecraft 1.8.9 `vertical` problem: initial Y/velocity/ground/previous-horizontal-collision, per-input jump impulse/sneak, and prescribed vertical contacts. Replay handles downward ladder caps, sneak holding, gravity/drag, floor/ceiling clipping and explicitly declared step contacts up to the player step height. Constructing the next state uses the horizontal collision from the completed movement and the ladder occupancy of that next state to establish climb carry. The current movement never receives that future upward displacement. The first state's supplied Y velocity is already stored carry; seam output preserves both it and the previous collision flag.

Optional vertical position/velocity references are hard validation rows. A prescribed vertical contact or step is part of the supplied model, not evidence of a geometry collision. This keeps the scope consistent with the existing prescribed horizontal contacts. The scalar result includes Y, vertical velocity, ground, horizontal collision and resume state.

Twenty targeted scalar tests pass, including independent positive/negative caps, cap-before-wall reach, inertia ordering, entry timing, tangential capping on the next tick, missed-wall rejection without granting climb, Y floor/ceiling/sneak/jump, prescribed steps, boundary branches, malformed fields and seam equivalence. Worker integration and captured export follow in subsequent commits.

## Commit 2: smooth cap branches and optimization

The sparse NLP now applies fixed negative/free/positive cap branches after acceleration and before contact reach, position updates and friction. Each phase starts from the seed's inferred cap pattern, retains its best nominally feasible path and explores bounded adjacent branches at cap boundaries, plus a free-branch recovery after a failed attempt. An explicit `ladderPattern` is respected. The existing continuation still preserves/polishes the best independently validated actual-inertia candidate. Scalar cap-branch membership uses the ordinary tolerance because adjacent cap branches agree continuously, while actual replay always applies the exact clamp.

A one-tick analytic case proves this is optimization rather than just feasibility: the initial Z-saturated pattern restricts maximum X to about .1322876, and adjacent branch exploration reaches the true .15000000596 cap optimum. Freezing the explicit original pattern returns its lower conditional optimum as expected. Existing 39 standalone tests passed after the initial worker changes; the new analytic test and nine ladder scalar tests pass together after the subsequent adjustments.

The captured Y comparison exposed step-up cases, including steps during an otherwise upward jump and steps while landing. The first hypothesis about cooldown was not the cause: Real11 has no cooldown-blocked grounded Jump ticks. Explicit prescribed step contacts now retain the original movement's ground outcome, rather than forcing grounded after every step. Using captured collision flags, the Y replay exactly matches all recorded positions and stored vertical velocities through 200 and 400 ticks. Jump impulses require the supplied prior grounded state.

Initial end-to-end prescribed-ladder runs are feasible with every original compiled row, inertia and horizontal/vertical contacts: 200 inputs in .777 s (X 737.5142588030), 400 in 3.746 s (X 691.5810209172). Both validate the recorded Y reference. The 400-input objective differs from the no-ladder model because caps change the physics; this is not an objective-quality comparison on an identical problem. Captured request export, saved fixtures and final regression/CLI checks are included in the next commit.
