# Ladder and movement-refactor status

2026-10-29. This audit distinguishes the implemented shared representation/compiler from the remaining shared search/generation interface. The two are not the same milestone. See [MOVEMENT_UNITS.md](MOVEMENT_UNITS.md), [MIXED_MOVEMENT_CONTACTS.md](MIXED_MOVEMENT_CONTACTS.md) and [LONG_LADDER_VALIDATION.md](LONG_LADDER_VALIDATION.md) for accepted contracts and evidence.

## Implemented ladder family

Rectangular regions, optional climb/fall phases, the five agreed phase sequences, climbing-owned beginning/end stops, interspersed falling Sneak, actual component caps, immediate hit-on-entry carry, side/top/bottom exits, successive airborne regions, finite wall/ceiling contacts and whole-route carried-state compilation are implemented. Accepted continuous witnesses cover multiple grounded transitions, five airborne falling regions, three automatically discovered climbing regions, a supplied five-region climbing contract and generated extra contacts in connecting air, the second ladder and final air.

No new movement arithmetic is needed to claim those witnesses. They use the shared movement compiler and scalar/scene acceptance. The numerical worker remains unchanged. Replay/export and game end-to-end testing remain outside this layer's scope.

## Remaining ladder orchestration

1. The two agreed implementation efficiencies from [GENERATION_PERFORMANCE_PLAN.md](GENERATION_PERFORMANCE_PLAN.md) are complete: an already-invalid occupied-height trace stops early, and open prefixes avoid the duplicate floor-prediction movement call. The measured append is about 35 percent faster with identical emitted continuations. Generic generation and consistent entries below remain in progress.
2. Unify entry handling across the first and appended regions. The first interaction uses numerical entry probes; appended interactions currently enumerate explicitly unprobed entry times within a bounded local horizon. Their X/Z feasibility is checked only with a completed route. A shared entry/transition contract should preserve the original-state solve and avoid freezing an earlier endpoint. This is a functional interface gap, independent of rare-route pruning.
3. Improve long-chain discovery reliability and retain honest coverage reporting. Cold three-region discovery succeeds; the cold five-region benchmark exhausts generation time before a full numerical attempt. Its supplied 99-input structure is accepted. Those facts do not prove cold five-region coverage or infeasibility.

Same-region re-entry, changing climb-wall orientation inside a region and rare diagonal transfers remain explicitly deferred. Additional irregular-region work is not a priority; rectangles suffice for the requested scope.

## Remaining general refactor

`movement_units.py` currently provides typed interval descriptors, phase grammar, occupancy/hit/Sneak materialization and retiming. These are real compiler/validation inputs. However, search still uses separate ordinary-support routing and ladder-episode expansion paths.

In particular, `generate_ladder_schedules` constructs an entire ladder schedule and `ladder_units` then identifies its climb/fall units. `iter_chain_programs` appends complete ladder episodes. The orchestrator does not yet independently expand `normal`, `ladder_climb` and `ladder_fall` through one common movement-type generator interface. Earlier descriptions of first-class units refer to representation/compiler visibility, not a completed generic search architecture.

Finish that interface so each movement type supplies candidate durations/inputs, entry/exit conditions and the relative contact/positional requirements used by the common compiler. The route orchestrator should compose those choices while preserving the agreed per-region phase grammar. Endpoint stops remain internal climbing alternatives; separating the types must not permit climb/fall/climb inside one hitbox or introduce a fake falling tick on entry.

Normal intervals and contacts already share a full timeline with ladder episodes, so reuse that contract. Remove ladder-specific assumptions from the common expansion path as needed. Keep one complete input/collision history and reoptimize the full trajectory from the original state; a unit boundary is not a portable frozen replay checkpoint.

Move the existing step-up/stair/slab-boost profiles behind a first-class `stepping` type once the common generator interface is in place. Their current working profile implementation has not yet been migrated. Add `water` later through the same interface; it is not currently implemented.

The common generator interface can use the current eager generation policy. It does not require the optional resumable-generation proposal. Tick-first and then objective ranking remain unchanged.

## Deferred optimization work

Multiplier-based input-schedule pruning, exact-Y caching, route-specific pruning and optional resumable generation remain separate follow-ups. The user is unconvinced about resumable generation; record it as a possible improvement rather than an agreed next step. Finish movement-type coverage before the pruning pass.
