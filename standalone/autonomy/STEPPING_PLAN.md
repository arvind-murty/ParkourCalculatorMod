# Case-based stepping plan

Latest user direction, 2026-10-29: represent named movement cases with their Y schedules, movement regimes and positional constraints. The orchestrator does not need to reproduce or enumerate Minecraft's internal step-assist algorithm. The canonical definitions and tick mapping are in [STEPPING_CASES.md](STEPPING_CASES.md).

This supersedes the earlier plan centered on exact A/B step-branch generation and returned-leg bookkeeping. The shared collision resolver remains available for validation and investigation; its internals are not the movement-type catalogue. Production stepping behavior has not been changed in this planning pass.

## Current code to reuse

- `stair_jump_profile` already derives the ordinary two-input slab/stair boost using the existing Y simulation. The support graph automatically proposes it with four entry faces, keeping ordinary jump alternatives.
- A one-input grounded `step` contract and finite contact composition exist. Automatic normal-step discovery and the common registered stepping type still need integration.
- Normal air Y simulation, jump/run-off inputs, original-state numerical compilation, support/obstacle constraints and the common unit generator interface already exist.
- `ceiling_profile` models ordinary ceiling impacts. It must not silently stand in for a grind: hitting the ceiling plane can leave positive Y displacement, whereas the specified grind has zero rise and an outside-to-under entry condition.
- Blip, blip-up and grind templates are not yet represented by dedicated automatically generated case profiles. The current scalar positive-rise step contract cannot encode every zero-rise/elevated-ground outcome.

## Profiles to implement

| Case | Y/result conditions | Movement and continuation |
| --- | --- | --- |
| Normal stepping | Grounded rise through 0.6; before/after block constraints | Run inputs throughout |
| Slab/stair boost | Before J on lower level; J on higher level, rise about 0.42-0.6 | Jump movement, then one air input; usual two-input cycle |
| Blip | L on lower landing region; 0 <= higherHeight-yL <= 0.6; H on higher region at its height | Air movement into H; ordinary movement follows |
| Blip-up | yL > higherHeight; obstacle-free yFreeH < lowerHeight; yH=yL | Air movement into H; grounded output at elevated height; type ends, then jump or normal run-off |
| Grind | Before J outside ceiling; J underneath low ceiling; yJ=yBeforeJ | Jump movement with cancelled rise, then one air input; usual two-input cycle on flat ground |

Keep lower-landing and higher-hit constraints as separate anchors. Tick names refer to position states; input coefficients still use their actual starting ground/air state. A ground flag is not a claim that a physical surface exists at that feet height.

## Common contract

A candidate records its named case, relative position/input anchors, incoming Y/VY, ground/air schedule, jump/run/Sneak choices, Y outcome, lower/higher block regions and any ceiling condition. Use one original-state compiled trajectory; never pin an intermediate optimized X/Z position or velocity.

All position, input and contact anchors retime with inserted runs. Preserve the numerical worker's existing input-index meaning of `jumpTick`; introduce explicit case-level jump-input/jump-position and landing/hit anchors. Include `jumpInputs` in generic retiming when integrating the new type.

The blip-up output needs a legal grounded-but-unsupported state. The next normal jump starts at that Y, or normal run-off uses one ground-regime movement followed by air as needed. Do not bind either to a fabricated support top or force a special blip-up tail. The same full-history compiler supplies actual inertia/sprint effects and any real wall/ladder contacts.

## Discovery and constraints

1. Normal steps and slab boosts are proposed from nearby lower/higher collision geometry and the specified height windows.
2. For a descending ordinary schedule, compute L and obstacle-free H using the existing Y simulator. Find the real lower landing region and higher candidates. Select blip versus blip-up using their different height conditions, preserving both X/Z anchors.
3. For grind, generate the outside-before-J and underneath-on-J ceiling conditions, the low head-clearance condition and the zero-rise schedule. Derive following landing/air duration from the actual floor geometry.
4. Before/after constraints must make each event occur on its intended input and preserve real obstacles and contacts. Reuse the existing continuous solver and acceptance helpers; do not substitute arbitrary endpoint positions or smooth over discrete cases.
5. Update existing time/height/reach bounds to be sound for the generated profiles. The ordinary jump-height gate must not discard valid composite step/blip schedules. Aggressive route/input pruning remains deferred.

## Implementation order

1. Register `stepping` and migrate the existing normal-step/slab-boost contracts, including correct controls, timing, retiming and legacy compatibility.
2. Add case-level Y outcomes and distinct L/H/J constraints for blip and blip-up. Generalize open composition to retain actual ground state independently of support ownership.
3. Add the grind template. Initially share ceiling geometry/contact helpers with ordinary ceiling impacts. A separate ceiling movement type is an open design option, not a prerequisite.
4. Compose cases with ordinary movement, runs, following jumps and ladder units. Ensure the blip-up type ends cleanly and grind/boost recovery uses actual following geometry.
5. Check fresh complete numerical solves plus independent scalar/scene acceptance, then run the relevant regressions. Commit features independently in the isolated branch.

The user is still reviewing whether the extra observed cases in STEPPING_CASES.md should be included, generalized into a jump-cancel family or excluded. Do not silently promote their descriptive probe names into accepted types.

## Validation matrix

- Normal steps at several heights, all X/Z approaches, one-input grounded control history.
- Existing +0.5 two-input boost and five-input staircase, with correct jump impulse, one air input and inserted runs.
- Blip: lower constraint on L, higher constraint on H, height-window rejection, air coefficients into H and correct ordinary continuation.
- Blip-up: strict height ordering, hypothetical H below the lower level, unchanged hit Y, immediate elevated jump and no-jump run-off continuation without a fake support.
- Grind: outside/under timing, low ceiling, zero rise, air recovery and flat/non-flat following floor; ordinary ceiling impacts remain separate candidates.
- Missing/extra/mistimed contact rejection, actual retained wall effects, ladder boundaries, carried state, jump release/cooldown through repeated short cycles, and full original-state solve.

Water and performance pruning remain separate work. The reference collision probes are documented in [experiments/stepping-mechanics](experiments/stepping-mechanics/README.md); they do not by themselves certify new case-generated routes.
