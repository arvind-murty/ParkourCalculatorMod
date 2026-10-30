# Ladder-ceiling ownership and generation

2026-10-30. The user requested an implementation plan for the remaining ladder-ceiling work. This document is planning only. The generic open movement composer is already implemented; preserve that interface and the standalone-only acceptance scope.

## Scope and existing behavior

Ceiling clipping, ladder carry, repeated ceiling contacts and combined horizontal contacts already have scalar and numerical coverage. `ceiling_sequence` supplies shared scalar prediction; `mixed_schedule_pool` and `mixed_ladder.with_contacts` generate/replay variants. `open_ladder.ladder_choices` currently reaches them through the generic mixed-contact pool.

The remaining work is explicit ownership, one source of ladder-ceiling proposals, coherent regeneration of the schedule and integration with the generic composer's boundary resolution. Keep `ladder_ceiling` an internal effect of the owning ladder phase. Keep ordinary normal-air ceilings in the registered `ceiling` type. Preserve a positive-carry ceiling hit during a fall/stop phase without inventing another climbing phase.

## Physical contract

Reuse `vertical_move`, `vertical_step`, `vertical_carry`, shared finite geometry and the existing contact compiler. Do not add another movement equation or collision resolver.

Y resolves before X and Z. An upward ceiling clip cancels vertical movement. A horizontal hit on that same input, ending inside a ladder, can then refresh the stored climbing carry to about +0.1176. Without that refresh, the clipped carry after gravity/drag is about -0.0784. Sneak affects a following negative ladder movement according to the existing helper. It does not suppress a positive carry merely because a ceiling event was selected.

Continued wall contact can produce ceiling hits on consecutive inputs. Omitted beginning/end hits, Sneak, entry and exit can change the sequence. Normal-movement one-input contact restrictions must not cap a ladder ceiling sequence at one hit.

## Step 1: extract ladder-owned prediction and ownership

Add `ladder_ceiling.py` for ladder-specific candidate sequences, local effect ownership and schedule integration. Reuse the neutral ceiling primitives. Retain compatibility re-exports where callers/tests need the old names.

Represent a local effect with its input, feet plane, actual finite box/provenance and owning phase/region. Keep the low-level physical `role=ceiling`; add ladder-specific effect/owner metadata. Existing scalar/scene compilation continues to consume the same Y contact.

Resolve ownership from the composed units and actual pre/post membership. This includes entry-producing climb inputs and direct transfers. When appending a ladder resolves ownership of a previously open boundary, transfer the existing ceiling effect to the ladder owner without adding a second physical contact. A normal approach/exit air interval retains normal ceiling ownership.

Gate: existing scalar Y/VY/contact results are unchanged, and every physical ceiling contact has one resolved owner.

## Step 2: give ladder generation one ceiling provider

Have the ladder schedule path call the local provider with its existing climb/fall, stop and Sneak candidates. Reuse the current mixed scheduler for X/Z contacts and combinations. Route upward Y opportunities through the owning ceiling provider, deduplicating them against already selected contacts.

Keep simultaneous ceiling plus X, ceiling plus Z and ceiling plus X/Z combinations, consecutive contacts and repeated ceiling sequences. Generic contact compilation remains shared; it must not independently rediscover another copy of an owned Y event. Preserve current work limits and report truncation/cancellation as incomplete coverage.

Gate: existing repeated and combined contacts remain available, with no new fixed pair/route template or rarity pruning.

## Step 3: regenerate coherent schedules

Feed selected ceiling variants into the existing scalar schedule generator and replay. Recompute Y, VY, actual refreshes, phase masks and valid entry/exit times. A changed ceiling history may require a later exit; do not attach metadata to the old schedule or freeze its original exit tick.

Use scalar prediction to propose sequences and validate regenerated candidates against the selected finite contacts. Reuse existing enumeration/work limits for alternatives that require a changed stop/Sneak/duration choice. Preserve all positional and input constraints through retiming.

Ceiling overlap is checked at the pre-input X/Z state because Y resolves first. Enforce the chosen upward clip on its selected input. On other inputs, require avoidance only where their Y sweep could actually collide. Remaining beneath the ceiling at a lower height is legal. Use the existing obstacle/contact branch helpers for the required avoidance alternatives.

Gate: emitted schedules have matching controls, contact times, Y history, phase grammar and exits before numerical solving.

## Step 4: integrate the generic open program

Keep `append_choice` and the shared queue type-independent. Ladder-owned ceilings travel as effects on the existing ladder units; their open output can feed normal movement, stepping, another ceiling or another ladder through the existing interface.

Preserve complete scalar history, entry-producing carry and coincident effects during phase normalization. Keep finite X/Z wall contacts and shared sprint compilation. Never fix an intermediate optimizer X/Z position, velocity or facing.

Gate: a composed ceiling/ladder interaction can continue without an artificial landing or state reset, and the normal ceiling generator does not claim a ladder-owned input.

## Step 5: focused standalone validation

Scalar/generation tests:

- One ceiling hit, continued pressing and several consecutive hits.
- Beginning stop, final omitted hit and both stops, with their expected Y/VY and exit changes.
- Sneak/no-Sneak on fall and stop inputs, including positive incoming carry.
- Entry-input ceiling plus horizontal hit, inside-ladder hit, exit-input hit and direct-transfer ownership.
- Ceiling plus X, plus Z and plus X/Z on the same input, and consecutive-input combinations.
- Normal approach/final-air ceiling ownership, retiming and duplicate suppression.
- Missing, mistimed, extra and wrong-box contact rejection; finite footprint/stage constraints.

Fresh complete IPOPT plus scalar/shared-scene checks:

- Repeated ceiling contact while pressing a ladder wall.
- A stopped-hit variant whose height/carry/exit differs as predicted.
- A ceiling variant that needs a later exit and a following unit that consumes that actual state.
- Normal ceiling -> ladder-owned ceiling -> normal/stepping continuation through the common composer.
- Existing ladder-ceiling, appended-ladder-ceiling and final-air-ceiling regression witnesses.

Run focused standalone tests during the work. Run numerical regressions if shared scalar/worker behavior changes. Do not run the mod's Java test suite or game replay tests. Search speed/pruning and water remain separate work.

## Commits

1. Extract local helpers and explicit ownership, preserving behavior.
2. Integrate proposal deduplication, regeneration and generic boundary ownership.
3. Add new numerical witnesses, run relevant standalone regressions and update the handoff.
