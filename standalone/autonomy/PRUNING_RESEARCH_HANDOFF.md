# Solver and schedule-generation research handoff

2026-10-30. Current user direction: defer water and focus on making the existing solver faster. This checkpoint documents implementation and research leads only. No new pruning, profiling run, schedule reduction or search-policy change is authorized by this documentation task.

## Start here

Workstream: orchestration within the continuous model. The other workstream owns replay/game integration. Success here is valid IPOPT with the correct movement, inertia, sprint, contacts, ladders and geometry, accepted by scalar validation and the shared finite-scene checker. Game replay and mod Java test suites are outside the current gate. Read `standalone/AGENTS.md`, repository `CONTEXT.md` and [GENERIC_COMPOSITION.md](GENERIC_COMPOSITION.md).

Current isolated worktree: `/Users/arvindmurty/CS/ParkourCalculatorMod-run-ticks`, branch `codex/run-ticks-planning`. Commit finished features separately. Use a separate worktree for parallel research/implementation; do not edit the integration worktree. The first orchestration commit is `e2a7b22`; its parent, `769c632778bdef254b2105201adf2f4b732c6b45`, is the comparison base for the full orchestration diff. Comparing this branch to `main` includes substantial unrelated inherited solver history.

## What is implemented

| Family | Current coverage |
| --- | --- |
| Normal | Height-derived jumps, ground/run inputs, no-jump run-offs, free turning and carried momentum |
| Obstacles/contacts | Finite boxes, directed blockage avoidance, cross/wide neos, signed wall hits, ceiling hits and mixed X/Y/Z combinations |
| Ladders/vines | Rectangular regions, front/side entry, climbing/falling, Sneak schedules, endpoint stop-climbing inputs, top/side/bottom exit and repeated airborne ladder interactions |
| Normal ceilings | Ordinary ceiling impacts and grind, the ceiling jump-cancel case |
| Stepping | Grounded step, slab/stair boost, blip, blip-up, X-facing corner jump cancel and corner blip-up |
| Ladder ceilings | Climbing-owned finite ceiling effects, repeated clips, endpoint stops and required subsequent climbing ownership |
| Composition | Open movement choices chained through a common interface without an artificial intermediate landing |

Automatic discovery exists. Given actual collision boxes, climbable cells and initial/goal conditions, it proposes intermediate supports, event types, schedules and contacts. It can return unknown because generation/queue/numerical budgets run out. Ability to compile and solve a prescribed route does not establish that the cold search discovers that route efficiently.

Repeated visits and multiple jumps on one block are allowed. There is no global visited-block exclusion. A recent generator/composer check constructed three jumps on the same support, at inputs 0, 12 and 24, with grounded states after each. This was a generation check, not a fresh numerical witness. Extra jumps can change momentum or position and must survive any proposed pruning unless a valid dominance argument applies.

## Actual search paths

The current collision-box CLI selects the generic path by adding `movementSearch: {}`. Python `find_movement_blocks` uses it directly; `find_blocks` dispatches to it only when `movementSearch` is present. `--support-search` selects the legacy support-profile route graph. Distinguish these paths in every benchmark and proposed improvement.

Generic flow:

1. `movement_search.iter_movement_programs` pops an open prefix from one queue ordered by elapsed inputs.
2. Registered generators propose normal, stepping, ceiling, `ladder_climb` and `ladder_fall` choices. The two ladder registrations share per-prefix schedule pools.
3. `movement_composition.append_choice` retimes controls/contacts/anchors and replays the full scalar history from the original start. Intermediate X/Z position, velocity and yaw remain unfixed.
4. `entry_possible` checks initial-state positional contradictions; a serialized full-history key removes exact duplicates. `screen_program` uses the existing conservative horizontal contradiction screen where applicable.
5. A completed grounded goal program reaches `solve_contact_branches`, which solves the whole trajectory and branches on floor, obstacle and contact consistency. Accepted solutions are ranked by total elapsed inputs, then configured X/Z MAX/MIN objective.

The queue's current bound is elapsed prefix inputs, without a generic geometry-derived remaining-time estimate. `maxAcceptedTicks` excludes prefixes longer than the incumbent; equal-time candidates remain eligible. Full history serialization is not a compact boundary-state dominance test. Prefix screening currently skips the horizontal contradiction screen when additional wall-hit branches could invalidate its fixed-contact assumptions.

The legacy support graph already has height/reach bounds, reverse reachability and shortest remaining flight-input estimates. It evaluates full prefixes through the run-ticks solver. These are useful references, but are not automatically shared by every generic event generator. The ordered run-ticks search starts with zero runs, increments at the first failing jump and backtracks; its standalone prototype settings and the outer generic search are separate layers.

## Current schedule generation

| Layer | What it enumerates / retains |
| --- | --- |
| Normal | From the current scalar boundary, jump/no-jump approaches and downward crossings of each support top; a finite landing contact and positional constraints for each chosen support |
| Stepping/normal ceiling | Named event cases, candidate boxes, orientations, event inputs and appropriate approach/avoid/hit alternatives, ending at the event through the common open contract |
| Ladder geometry | Rectangular region, candidate wall/fall-only context, front/side entry channel, already-inside versus entering, optional launch jump and exit face |
| Ladder skeleton | Entry state, exit state/kind, at most one contiguous climbing phase, optional beginning/end omitted primary hit, and leading/trailing falling phases |
| Sneak mask | Eligible falling/endpoint/entry inputs; seeded masks followed by combinations. Late Sneaks and the first-clamped-input exception influence enumeration order, not a proved dominance reduction |
| Mixed contacts | Horizontal/ceiling opportunities across boxes and inputs, same/consecutive/independent combinations, predicted repeated ceiling sequences and their prefixes, followed by scalar schedule regeneration |

One ladder hitbox permits falling only, climbing only, fall -> climb, climb -> fall, or fall -> climb -> fall. It never permits climb -> fall -> climb. One-input beginning/end stops remain owned by climbing, including the omitted final eligible wall hit. Top exit ends in climbing. Side/bottom exits can end in climbing or falling. The user wants these movement families preserved while improving speed.

`open_ladder.ladder_choices` currently builds candidate entry states explicitly up to its horizon, labels them `firstCandidateState`, and invokes the scalar generator with `entryDelay=0`. That is distinct from the legacy numerically validated earliest-entry + delayed-entry probe path in `ladder_transfers.py`, `climbable_search.py` and `movement_entry.py`. Do not describe all generic entries as numerically validated earliest entries.

`generate_ladder_schedules` builds entry/exit/kind/contact contexts, collects a bounded skeleton pool, then round-robins Sneak-mask work across skeletons. Each selected mask is replayed with the actual Y recurrence, ladder occupancy, contacts and refreshes. Invalid occupied Y is rejected during replay. Returned schedules are vertically validated; they still need X/Z/contact/scene acceptance.

`mixed_schedule_pool` proposes bounded contact variants from baseline and raw-approach traces, then calls the scheduler again with selected contacts. This recomputes heights, carry, phase validity and exit timing. A ceiling history cannot simply be attached to an old exit time. Repeated clipping sequences can exceed the ordinary contact-count combination cap. Keep this behavior.

`open_ladder` finishes a baseline pool and mixed pool before yielding choices for that local context. It sorts/interleaves schedule families and then expands exit faces. Its pool cache is shared between ladder types within one prefix expansion, not a persistent cross-prefix memoization cache. Contacts, repeated traces, full-history copying and compiler/screen calls are research targets, not newly measured dominant costs.

Current generic defaults: 200 inputs, 256 expanded prefixes, 512 queued prefixes, 64 choices per type per expansion, 200 interactions and 14 candidate entry inputs. Open ladder defaults include 2048 skeletons, 2000 schedules, 30000 mask-work steps, 16 contact variants and 3 mixed contacts. These are reported work bounds; increasing them is not itself a speed improvement or completeness proof. Lower-level/legacy scheduler defaults differ.

## Known performance evidence and existing optimizations

- [GENERATION_PERFORMANCE_PLAN.md](GENERATION_PERFORMANCE_PLAN.md) records a historical first-region append profile: 24,510 scalar traces, about 1.6 million tick updates, roughly 74% of cumulative profiled time in Y replay, and 22,126 traces rejected for occupied Y. Those figures are from the earlier chain path, not a new profile of the generic queue.
- Two optimizations are already implemented in `3a8d8cd`: stop replay after an occupied-Y failure, and skip redundant floor-crossing prediction when no target floor exists. Median unprofiled time for that measured append fell from 2.849 to 1.849 seconds with identical serialized continuations. Do not propose these as unfinished work.
- [five-climbing-search-limit.json](experiments/contact-combinations/five-climbing-search-limit.json) records an older cold five-ladder search: unknown/context limit after about 131 seconds, 12,800 generated transfer candidates, 20 expanded transfer prefixes, 381 still pending and zero numerical attempts. It diagnoses generation starvation in that path, not the current generic queue's measured profile.
- The generic checkpoint also records an 18-second cold one-ladder smoke returning unknown before a complete numerical proposal. Prescribed five-airborne-ladder and mixed stepping/ceiling chains do have accepted full numerical witnesses. This difference motivates search research; it is not an infeasibility result.

## A few research pointers

1. **Measure where candidates disappear.** Separate geometry opportunities, skeletons, masks/Y replay, regeneration, append/copy/hash, coefficient compilation/screening, queue drops and IPOPT. Record time to first complete proposal and first accepted solution, not only raw generator throughput. Compare generic and legacy paths explicitly before transferring an optimization.
2. **Conservative geometry and time bounds.** Investigate forward reachable envelopes and backward continuation requirements for supports, ladder entry/exit and finite contact faces. Reuse the legacy reach/time-bound ideas while accounting for variable height, carried momentum, step/blip height gains, short run-offs and collision-induced velocity changes. A remaining-time lower bound could prioritize whole routes more effectively than elapsed prefix time alone.
3. **Schedule equivalence/dominance.** The user's suggested reduction groups schedules with equal total airtime and equal time after leaving ladder occupancy, then favors the best pure straight-line horizontal progress from zero momentum. Late Sneaks are usually favorable, with the first-clamped-input Sneak exception. Research this as a heuristic or prove a stronger sufficient condition; equal airtime alone does not preserve Y, finite contact timing, incoming inertia/caps, sprint state or feasible entry faces. Compare full compiled movement effects. Keep geometry alternatives even when they share input coefficients.
4. **Avoid repeated computation.** Explore compact canonical schedule keys, shared scalar prefixes, coefficient/geometry caches and a DP over truly sufficient Y/control/contact state. Preserve exact float/threshold behavior. Cross-prefix caches need the actual scalar boundary, horizon, incoming collision state and contact context; a block ID or Y position alone is not enough.
5. **Generate contact combinations locally.** Use conservative Y/time/finite-face overlap windows and route approach/exit requirements before taking products over boxes, axes and inputs. Retain all supported X/Y/Z families. Normal contacts separated by a free gap can be independent; ladder interactions require sequence-wide reasoning. Rarity alone is a ranking hint, not a rejection proof.
6. **Budget allocation and resumable generation.** Investigate whether yielding small batches lets useful continuations reach the common queue sooner and prevents one local context from consuming the deadline. The user previously questioned this benefit; it remains an optional research idea, not an agreed implementation plan. Preserve an admissible bound for unseen work and account for pending alternatives when reporting optimality.

Earlier related research: [STRATFINDER_INVESTIGATION.md](STRATFINDER_INVESTIGATION.md), [docs/noturn-global-solver-design.md](../../docs/noturn-global-solver-design.md), [BLOCK_ROUTE_SEARCH.md](BLOCK_ROUTE_SEARCH.md) and [SEARCH_OBJECTIVE.md](SEARCH_OBJECTIVE.md). The stratfinder review already checked our native implementation and Curryocity's tool, with source/history pointers. Its fixed-facing and approximate-inertia rejections are not automatically safe for this free-turn problem.

Code navigation: `movement_search.py` owns the generic queue/screen; `movement_generation.py` owns the type registry and ladder phase grammar; `open_movement.py`, `open_ladder.py`, `stepping_generation.py` and `ceiling_generation.py` produce open choices. `ladder_schedules.py` and `movement_contacts.py` own the main schedule/mixed pools. `movement_composition.py` and `movement_units.py` own full-history composition. `screen.py`, `block_search.py`, `blockages.py` and `block_contacts.py` contain existing bounds and geometric branch machinery. `search.py` owns ordered run allocation and native-worker evaluation.

## Constraints on any proposed improvement

- Minimize total ticks, then objective among equal-duration solutions. Preserve equal-time alternatives; the numerical objective is not the route-time bound.
- Optimize the full original-state X/Z history. A single locally solved arrival, visited block, preferred wall side or greatest speed is not a complete reachable-state summary.
- Keep self jumps/revisits and distinct entry/exit geometry unless a justified reduction covers them. No facing constraints are requested.
- Normal Minecraft support checks precede horizontal movement. A tick can finish beyond an edge while remaining eligible to jump on the next input. This is ordinary movement. Run-off means departure without jumping and needs source-block obstacle handling during Y overlap.
- Respect Y -> X -> Z collision order and finite contact footprint timing. Preserve simultaneous/consecutive contacts. Blockage caps are per physical block: 2 horizontal, 2 vertical, 3 combined, including interleaved events for different blocks.
- Ladder horizontal movement clamps at .15 per component when the tick starts inside. Entry-producing wall contact can refresh climbing carry when the result enters the ladder, without clamping that earlier input prematurely. Endpoint stops use normal Y evolution unless an actual clip/cap changes it. `ladder_ceiling` must be followed by a climbing-owned input, which can be an endpoint stop.
- Distinguish a sound rejection, a heuristic ordering/drop, and a local numerical unknown. Never convert timeout, queue truncation or IPOPT failure into infeasibility. Generic `minimumTicksProven` and `objectiveOptimalityProven` currently remain false.

## Validation checkpoint and reproduction pointers

Latest implementation commits: `b49a205` ladder-ceiling ownership and `b5d9095` saved endpoint-stop witnesses. Latest gate: 64 focused standalone methods in 54.220 seconds, plus saved-evidence validation, for 65 methods total. This includes fresh full-state solves, existing generated approach/second-ladder/final-air ceilings, generic five-ladder composition, stepping and combined contacts. No tests or new benchmarks were run for this documentation-only handoff.

Start with `tests/test_generic_composition.py`, `test_movement_generation.py`, `test_movement_contacts.py`, `test_ladder_ceiling.py`, `test_ladder_schedules.py` and `test_stepping_cases.py`. Saved evidence is under `experiments/ladder-climbing-ceiling`, `experiments/contact-combinations`, `experiments/ladder-schedules` and `experiments/block-route-search`. Some examples prescribe schedules, and some old request yaws are saved witnesses. Explicitly identify genuinely cold inputs.

```sh
PKC_RUN_TICKS_COMPILER="$PWD/standalone/autonomy/build/compiler.json" \
PKC_IPOPT_PYTHON="$PWD/.venv-ipopt/bin/python" \
python3 -m unittest standalone.autonomy.tests.test_ladder_ceiling \
  standalone.autonomy.tests.test_generic_composition -v
```

`build/`, `.venv-ipopt/` and generated compiler configuration are local/ignored. See the existing compiler Gradle helper if a new worktree needs preparation. Shared movement sources are pinned by `materialize_movement.py` to `f057c0ce9b454ae1ce2706843bb141cce799b5cb`; the local shared `StaticScene` is deliberately compiled separately. The native worker executable should retain its `.venv-ipopt/bin/python` symlink path. Do not rebuild helper classes during active solves or use Python's timed faulthandler diagnostic on this macOS native runtime.

Expected next research deliverable: a short ranked proposal identifying each candidate reduction's assumptions, whether it is sound or heuristic, the exact generation layer it affects and how to test that useful routes survive. Implementation and new performance experiments are deferred from this handoff task.
