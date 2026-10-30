# First-class stepping movement

Next after the ladder/vine checkpoint in LADDER_COMPLETION.md. Water, pruning, exact-Y caching and resumable generation remain deferred. Keep orchestration in this worktree and reuse the shared movement/compiler/scene implementation.

## Existing implementation to reuse

- `block_profiles.stair_jump_profile` produces the current two-input jump/step-cancellation profile. Its first input requests a jump, steps onto a support and cancels Y carry; one airborne input then lands. It uses the existing scalar `vertical_step` and `vertical_carry`, and currently admits rises above the jump impulse through widened `0.6F`.
- The supplied `step` transition and `contact_profile` with a Y `role: step` already represent a one-input grounded non-jump step. They must remain distinct from the two-input boost.
- `block_search.build_transition_graph` currently proposes stairs separately and skips the registered normal generator for them. This is the integration point to migrate.
- `contact_combinations.py` carries finite step contacts and associated X/Z contacts. `StaticScene.move` evaluates the legacy Y-X-Z ordinary branch, two candidate step paths and their horizontal-distance selection. Its selected path retains the actual final contacts. Reuse it; an initial blocked horizontal sweep is not automatically a retained wall hit.
- Existing staircase, explicit step, same-input step plus X/Z contacts and floor/ceiling acceptance tests are migration gates.

## Implementation sequence

1. Register a `stepping` generator beside the normal and ladder generators. Initially wrap the working profiles without changing physics. Emit one-input grounded-step and two-input jump-cancel choices with explicit mode, jump inputs, step input, source/target support, entry face, required contacts and exit conditions. Preserve legacy saved profile/transition formats.
2. Route automatic stair/step proposals through this generator. Keep ordinary jump and run-off as competing alternatives to the same support. Generate candidate heights/entry faces from actual collision boxes and the existing step-height rule, rather than supplying guessed airtimes or replacing the shared step resolver.
3. Carry stepping units through common validation, absolute run retiming and full-history compilation. Include every new control/contact anchor in retiming; cover existing normal jump anchors too. Validate unit controls against the actual profile and compiled plan. Do not reset velocity, sprint state, Y carry or ground state at a unit boundary. Compose with ordinary and ladder legs through the existing support/airborne history contracts.
4. Exercise the migrated generator and acceptance on real scenes: one step, stair/slab boost chains, a following normal jump, a following/preceding ladder interaction, X/Z approaches, simultaneous retained wall contacts and blocked/ceiling-limited step alternatives. A valid result requires a full IPOPT solve plus independent scalar and shared finite-scene acceptance from the original initial state.

## Physics and acceptance rules

A step is not a vertical teleport. The ordinary branch must have an actual horizontal blockage, stepping must be eligible, the selected step path must clear the real scene and its horizontal progress must beat the ordinary path. Exact ties and which step candidate wins belong to the shared resolver. The height limit is the widened float `0.6F`, not an independently rounded decimal constant.

A Y `step: true` event encodes the selected step outcome for the scalar schedule. It does not by itself prove the horizontal trigger, intermediate clearance or selected branch. The shared scene check must confirm these. Preserve actual retained X/Z collisions and their velocity/sprint consequences; do not synthesize contacts from discarded step-trigger clips. A failed proposed step remains unknown unless an existing sound screen establishes a contradiction.

The one-input grounded case and two-input jump-cancel case have different ground/jump histories. Keep those histories exact so the next unit's acceleration, inertia and jump eligibility remain correct. Use the existing finite support/entry/avoidance requirements and collision order instead of enforcing an arbitrary endpoint on a completed optimizer result.

## Completion gate

The existing staircase behavior and saved witnesses remain accepted after migration. New generator tests verify the two modes, their height eligibility, control history, entry faces, masks and retiming. Fresh numerical/scalar/scene cases demonstrate the new type in complete routes, including ladder/normal boundaries. Run the orchestration regression once the final code is stable; repeat numerical/shared-source suites when their implementation changes. Commit each feature independently in the isolated branch.
