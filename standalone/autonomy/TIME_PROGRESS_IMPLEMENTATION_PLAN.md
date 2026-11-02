# Implementation plan: comparable-event time/progress frontiers

2026-11-01. User selects this next and requests a plan. No implementation is shipped by this record. Work remains in isolated `research/ipopt-cell-routes`; reuse the existing movement/constraint builders, numerical solver, physical checker and saved-search-state machinery.

## Preserve the existing search model

Within one spatial route, enumerate close/coupled discrete choices jointly and normally handle separated choices sequentially. Commit/retain symbolic input and constraint choices within a branch. Every later solve starts at the original full state and can change all earlier facings; no solved intermediate position, velocity or facing becomes fixed.

Weak dependence of earlier discrete choices on separated later choices is a working hypothesis. Keep alternatives and the ability to reopen earlier choices; benchmark against joint controls on maps that challenge the hypothesis. This frontier is heuristic retention, not a proof of state dominance or global optimality.

No new movement physics, mixed-input relaxation, objective bounds, velocity-weighted score, ladder family, intentional final wall-hit family or inertia-threshold logic. Normal momentum carries and current velocity thresholds remain zero. The recent fallback/incumbent/queue/adjacent-cell/resume fixes remain in use.

## The implementation gap to solve

`plan_windows` currently creates a single fixed comparison tick before the next supplied opportunity. `find_checkpoint_route` generates/truncates candidates to that tick, evaluates horizontal position, and calls `retain_candidates` with one score per candidate. Final ranking is already ticks-first.

Simply adding time buckets to those same records does not solve the problem: all scored prefixes in that batch have the same tick count. In the ceiling/step examples, the faster goal completion can be discarded at a preceding progress-only checkpoint before the final ticks-first comparison occurs.

The change must affect which complete event/time candidates reach retention, not merely sort final accepted solutions differently. A required final goal landing is an event to compare, not automatically another independent control decision that forces stopping all candidates just before it. Avoid classifying opportunities using fixture names such as "finish"; represent terminal goal requirements explicitly or derive them from the declared goal contact/terminal support.

## 1. Define comparable stages and event labels

Add a stage description containing spatial-route identity, event kind, target/required passage signature, common horizontal direction/anchor, accepted readiness condition, allowed absolute tick range and relationship to the decision cluster. Initially support:

- Actual required goal landing, validated by the existing finite contact and terminal ground conditions.
- Existing explicit portal/outgoing-route stages after a turn, with their required crossing order and declared boundary/readiness conditions.
- Fixed-tick checkpoints as a compatibility mode: a single time bucket, with no claim that this alone corrects timing errors.

Never compare an airborne state merely above the goal with a completed goal landing. Do not force intermediate routes to land if their stage is a portal/free-space event. Y contributes to required feasibility, never a height reward. Do not silently treat different contact requirements, spatial routes or outgoing directions as the same stage.

For an input t that completes an event, use absolute state tick t+1, preserving the current pre-input indexing. Record the collision phase for validation where relevant. A selected first-crossing event must be validated as such; do not label arbitrary later samples of an already-passed event as new arrival times. An outgoing evaluation region can instead deliberately permit a range of evaluation ticks, but that is a different declared stage contract.

For a fully accepted label store:

```text
route/stage identity; absolute tick tau; horizontal progress S;
symbolic decisions and constraints; physical/contact/method identity;
accepted witness and optional facing guess; parent provenance;
validation/search status and resumable pending alternatives
```

Use S = d dot (p[tau] - common_stage_anchor) with one normalized direction d. This is the existing horizontal projection with a common offset; no new solver objective is needed. Store numerical velocity/contact information for diversity/diagnosis, without freezing it or adding a velocity bonus.

## 2. Generate actual event-time candidates before pruning

Reuse the current generators and composition. Given a stage, produce supported discrete programs that complete/satisfy it at different admissible tau values. Derive candidate timings from existing event schedules, opportunity/reachability domains and goal/cell conditions; do not require a solved trajectory to define all time buckets.

For terminal goal comparisons, generate complete goal-contact programs directly. Do not insert a mandatory progress-only pre-goal cutoff merely because a terminal landing range is listed as an opportunity. This is the first integration milestone because it directly addresses the known ceiling/step mistakes.

For intermediate stages, permit multiple input counts for the same declared stage. Compile existing passage/case/region constraints and evaluate each selected program from the original state. An event cannot be called complete based only on its symbolic Y schedule; require the existing scalar, finite-scene, corridor and stage checks before it participates in the accepted frontier.

Use a common stage/timing specification for sibling comparisons. A candidate does not choose its own scoring direction, relax readiness or acquire an arbitrary extra horizon after seeing its result. If completing the stage crosses another genuinely unresolved decision, honor the existing cluster/dependency structure or report that the proposed comparison boundary is unsuitable. Do not silently optimize through a different unselected physical interaction.

This is potentially more work than one fixed-tick solve. Reuse already accepted witnesses and compatible symbolic contexts; count every generation/screen/solve attempt. Avoid solving every tick independently when an existing generated event already provides its exact completion time.

## 3. Implement the retention policy as a pure component

For one stage, group validated labels by tau. In each group find the best progress F(tau) and retain near-best, symbolically distinct alternatives within a configurable same-time band. Do not duplicate a physical/constraint/search context merely because it has multiple metadata descriptions.

Process time groups in increasing tau and maintain the cumulative earlier envelope:

```text
M(tau) = max F(s) for validated earlier times s < tau
```

A later candidate is core-frontier eligible when its progress exceeds M(tau) by the declared comparison tolerance. The earliest accepted group is eligible without an earlier reference. Check all earlier times, not only tau-1, since time buckets may be missing.

Example, one intermediate stage:

| tau | best progress | Core retention |
| --- | --- | --- |
| 8 | 3.0 | Keep: earliest |
| 9 | 2.9 | Behind the earlier envelope |
| 10 | 3.5 | Keep: later but farther |
| 12 | 3.4 | Behind the tau-10 result |
| 13 | 3.8 | Keep: later but farther |

A near-best candidate in a later bucket that itself fails to improve the earlier envelope is not automatically a core tradeoff merely because its bucket champion qualifies. It may earn a separate diversity-reserve slot, with the reason logged.

Separate four mechanisms in configuration and diagnostics: same-time objective band; cross-time improvement tolerance; physical/method diversity reserve; hard resource caps. Unknowns have their own reserve and never receive invented progress or infeasibility. Derive numerical near-equality handling from the existing tolerance contract and report sensitivity to the ranking band; do not tune constants to the two original fixtures.

A non-improving label may have better future potential because its contact/velocity/discrete history differs. Therefore cross-time exclusions are heuristic. Retain a bounded diversity reserve and park omitted labels with enough provenance/state to reconsider them. Hard-cap drops are recorded separately from envelope exclusions. A cap smaller than the eligible time frontier cannot truthfully preserve every eligible time group; report that loss explicitly.

## 4. Schedule earlier accepted event times first

Extend the protected completed-program scheduling rather than reverting to shortest-partial-prefix expansion. Queue ready event/time candidates and retained continuations primarily by increasing tau, with bounded work quanta across time buckets/parents and a discovery share for new alternatives.

At an intermediate stage, try the earlier useful prefix first while retaining later/farther fallbacks. At the actual final goal, a valid earlier landing wins regardless of the later candidate's larger horizontal progress; optimize progress among tied earliest completions. Preserve every accepted incumbent before further work.

Unknown earlier times do not block every later fallback indefinitely, and a found completion does not prove earlier unknown times impossible. Do not claim minimum time unless the requisite coverage/proof actually exists. Keep current shared solver-call/deadline counters and saved pending state so work cannot be hidden in retries or extra timing buckets.

## 5. Carry decisions forward and support revisiting them

Use existing `retain_prefix`/repair-state mechanisms to retain selected input and geometric decisions, not numerical endpoint values. When exploring B, re-solve the original full prefix with retained A decisions and B variants; all earlier facings remain free.

Retain non-winning A histories as bounded alternatives rather than treating score exclusion as a proof. The first version should store and expose parked alternatives and allow a bounded reopening/rescue pass when continuations remain unresolved or exhaust the active histories. Such a failure does not prove A wrong. Reusing an old known-worse numerical state is not the rescue; reopening changes the discrete/constraint history and re-solves continuously.

A successful but suboptimal commitment can go unnoticed at runtime. Test this explicitly against joint or small exhaustive controls; do not pretend a failure-trigger alone detects every coupling loss. Record when reopening or enlarged clusters rescue feasibility or improve final time/objective, and use that evidence to revise clustering/retention. No unconditional ten-air-input independence rule.

## 6. Reuse and concrete code ownership

- New pure `cell_routes/time_progress.py`: label grouping, cumulative envelope, reserves/caps, deterministic retention and explanatory diagnostics.
- Stage/validation extensions beside `cell_routes/checkpoints.py`: stage identity, event acceptance and absolute timing. Reuse current checkpoint/goal/contact checks.
- `decision_windows.py`: distinguish discrete opportunities from terminal event requirements; preserve existing cluster logic and compatibility mode.
- `movement_search.py` / existing generators: expose complete stage candidates with variable event times using existing physics/constraints and the protected completion queue.
- `checkpoint_search.py`: evaluate labels, apply the new frontier, retain original-state symbolic histories and schedule continuations. Keep current position-band policy as an explicit comparison arm.
- Existing experiment drivers/checker: collect all work and independently replay every returned accepted witness.

Keep the new mode optional until its acceptance gates pass. Do not silently claim variable-time behavior in the old fixed-checkpoint mode. No changes to IPOPT/physics are required.

## 7. Tests and measured acceptance

Pure tests cover cumulative-envelope retention, missing buckets, same-time ties, near-equality, opposite axis/directional objectives, coordinate translation, distinct stage keys, diversity reserves, unknowns, cap omissions and deterministic ordering.

Semantic/integration tests cover actual goal landing versus airborne-over-goal, different Y histories at the same valid portal stage, first-event input/state indexing, same-stage variable timings, no fake intermediate landing, multiple portal phases, unknown preservation, original initial position/velocity, free earlier facings and persisted repair state. Retaining labels cannot freeze numerical states or remove accepted incumbents.

Regression controls include the known earlier/later ceiling and step histories, revalidated under current zero-threshold physics. The new policy should expose the actual earlier goal completion before progress-only filtering can lose it. Final ticks-first ranking alone is insufficient evidence because it already exists. Separate controlled witness coverage from genuine cold search success.

Run the existing six cold scenes and additional independent scenes, including deliberately coupled distant decisions, under matched total solver/generation budgets. Preserve the current successful grind11/turn13 controls; investigate any quality regression generically. Compare position-band, time/progress frontier, and frontier plus diversity/rescue. Use joint/small exhaustive references where tractable to measure—not assume—the loss from sequential commitments. Sweep retention tolerances/caps without optimizing parameters to individual fixtures.

Report final accepted times/objectives, validity/replay outcomes, time groups proposed/accepted/missing, retained and parked histories, reasons for every omission, numerical calls, generation/compilation work, total time and rescue improvements. Finite tests establish observed behavior, not global completeness or optimality. All accepted witnesses must independently replay.

## Implementation milestones and scope boundary

1. Pure frontier policy and diagnostics/tests.
2. Terminal event generation/retention integration and ceiling/step regressions.
3. Intermediate comparable-event variable timing, original-state continuation and saved alternatives.
4. Bounded rescue plus coupled-map controls and matched-work benchmark.
5. Supported-mode rollout with accomplishments/limitations/next-step report.

This work does not rebuild the movement catalogue, change IPOPT, introduce a richer weighted score or redesign the initial route guide. It implements the agreed retention/scheduling change while measuring its provisional assumptions. Main risks are event comparability, generation coverage before retention, cross-time state differences, total candidate growth and sequential coupling. Each is logged/tested separately instead of attributed generically to insufficient budgets.
