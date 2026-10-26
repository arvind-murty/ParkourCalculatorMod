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

## Commit 3: capture attachment, fixtures and final verification

Added public `attach_capture(request, capture, start=None)` and CLI `--ladder-capture` / `--capture-start`. The helper validates the source profile, input span, initial horizontal state and captured records, then returns a prepared copy. It infers ladder occupancy from the recorded climb signature, membership in climb-proven cells and authored LADDER overrides. It attaches prescribed vertical collision/step events, initial ground/collision/velocity, sneak and jump impulses, and hard recorded Y/vertical-velocity references. Malformed authored override records raise structured input errors. Source metadata retains the canonical capture hash and collision evidence; replay reconstructs horizontal collisions from the candidate rather than accepting those recorded flags.

The user explicitly said to assume tapped jumps. Held-jump cooldown is deliberately absent. Real11 has no grounded Jump tick blocked by cooldown, so this assumption reproduces its captured Y trajectory. The earlier Y mismatch was due to prescribed step events, including steps during a jump or landing.

Created separate `real11-200-ladders.json.gz` and `real11-400-ladders.json.gz` requests. Every original physical field, positional/facing constraint, inertia setting and prescribed horizontal contact remains identical to the original fixture. Added a reduced `real11-ladder-evidence.json.gz` containing only the recorded fields required to reproduce the first 400 states' ladder/Y attachment. Full original capture and reduced evidence produce the same physical problem hash. The original capture file SHA-256 is `af5d2d1bc6e0ab466f1b6cf309f3a26e76e4a63765ac8b15ff02fb4ad55d28bf`.

The 200-input request has 22 ladder input ticks, 50 prescribed vertical contacts and 8 steps. The 400-input request has 34 ladder input ticks, 81 prescribed vertical contacts and 9 steps. State 400 remains on a ladder and carries a previous horizontal collision and +.11760000228881837 stored Y velocity for resuming a later span.

Final CLI results, each with a 10-second total budget:

| Span | Initialization | Time | Final X objective | Validation |
| --- | --- | --- | --- | --- |
| 200 | captured facings | .750 s | 737.514258802979 | feasible, zero row violation |
| 400 | captured facings | 3.561 s | 691.581020917188 | feasible, zero row violation |
| 400 | earlier no-inertia facings with captured fallback | 3.677 s | 691.581020916180 | feasible, zero row violation |

All three saved results were read back from JSON and independently replayed. Returned X/Z positions and stored velocities match replay exactly. Y, stored Y velocity and grounded flags match the original capture exactly throughout each span. The warm result confirms that an earlier no-inertia path remains a useful initialization when adding the new physics. These timings are individual local measurements, not a reliability benchmark over unrelated routes.

All 47 standalone tests passed in 24.231 seconds with the actual CasADi/IPOPT runtime. Coverage includes the analytic cap optimum, 200/400 ladder solves with all original constraints, exact captured Y replay, entry and collision timing, cap ordering, fixed branches, missed walls, steps, resume state, original no-ladder requests, warm-start recovery and capture attachment through the CLI. Five capture tests passed again after adding malformed-override rejection. No native solver/model/capture resource was edited, so the native slow solver suites were not required.

The requested native fast gate `:core:test` ran as well: 791 tests, 3 failures, 33 skipped. It reports the same three previously recorded baseline failures: `CrossJvmDeterminismTest.searchPathIsBitIdenticalAcrossJvms`, `InertiaFoldingTest.loopmmInertiaTermDropsOutOfFoldedModel`, and `CostateSmoothingScaleTest.bothRecoveryPathsUseObjectiveDirectionOnlyForVanishingCostates`. Core, Forge and Fabric source trees have no changes. This Java gate is not passing, despite the standalone suite passing.

Logs, full result trajectories and a provenance/fixture-hash manifest are archived in [ipopt-ladder-2026-10-26](solver-traces/ipopt-ladder-2026-10-26/provenance.json). Usage and the schema are documented in [the standalone README](../../standalone/ipopt/README.md). Work remains isolated on `feature/ipopt-standalone`; commits 1 and 2 are `15dc24b` and `052595b`, followed by the capture/fixtures commit containing this record.

The current model deliberately fixes the discovered ladder and contact schedule. It does not validate supporting block geometry or rediscover ladder occupancy after the optimized route changes. General world replay and schedule discovery remain deferred after the user's instruction to use climbing ticks from the JSON. The NLP remains local optimization with bounded cap-regime trials, not a global optimum or infeasibility certificate.
