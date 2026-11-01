# Time-first progress frontiers at meaningful events

2026-11-01. User identifies the earlier goal landing behind the ceiling/stepping counterexamples and proposes retaining the farthest progress for each tick count, keeping later candidates that gain progress over earlier ones, and trying shorter times first. This is the next design direction. The existing shared-tick objective-band implementation is unchanged in this discussion turn.

## What the counterexamples actually establish

Both pairs complete the required goal contact earlier with the smaller total tick count. Ceiling: jump input 0, goal contact state 12; the other history runs for four inputs, jumps at input 4, hits the ceiling at state 8 and lands at state 13. Its final flight is shorter, nine inputs versus twelve, but its total elapsed time is longer. Stepping: normal step input 0 then jump input 1 lands at state 13; slab boost input 0, landing state 2 and jump input 2 lands at state 14. The last flight has the same duration, with a later takeoff.

Thus total elapsed time to a required event, including run-up, contact resets and flight, is the relevant quantity. These terminal examples do not by themselves establish a need for a more elaborate horizontal score. The previous prefix comparisons optimized horizontal progress while leaving the different remaining times to actual goal contact outside the value being compared. Final route ranking already prefers ticks before progress; early retention did not carry that criterion.

## Comparable labels

Use a meaningful route stage/event sigma and absolute elapsed tick tau. A label contains the symbolic prefix and selected decisions, completed stage/contact conditions, tau, horizontal progress S, solver/validation status and optional trajectory/yaw seeds. Every solve retains the original full initial state and free earlier facings. A stored numerical intermediate state is not a committed starting point.

For each tau, optimize progress among candidates that actually satisfy that stage's conditions at tau. A goal-landing candidate must satisfy the actual finite goal contact; an airborne candidate above the block has not completed that stage. Use the existing Y/X/Z phase-aware acceptance rather than an endpoint-only box test. Other stages may be portals or the boundary before a new interaction; do not force an artificial landing on a block a spatial route can skip.

This replaces comparisons across unlike readiness states with a time-indexed frontier for a common stage. Within each tau bucket, siblings still share an absolute tick and horizontal direction. Across buckets, elapsed time is an explicit objective component, so different evaluation times are no longer being compared as equal-cost progress scores. Normalize direction/progress consistently across the same stage.

Let F_sigma(tau) be the best progress found for a valid label at that stage and time. Preserve a few similarly good but meaningfully different histories within each tau bucket. Maintain the cumulative earlier envelope:

```text
M_sigma(tau) = max F_sigma(s) over completed/validated earlier times s < tau
```

A later label earns a place on the basic time/progress frontier if it provides more progress than the earlier envelope. Compare against all earlier times, not only the immediately preceding tick; some time buckets may be empty. No numerical improvement tolerance or per-bucket near-tie threshold is selected here.

For an intermediate stage, earlier/lower-progress and later/higher-progress labels are both useful tradeoffs. Try the earlier label's continuation first and preserve the later one as a fallback. At the actual final goal, an accepted earlier completion wins under the current ticks-first objective, even when a later completion is farther along the goal axis. Continue improving progress among tied earliest-time completions. Earlier unresolved times stay unknown; finding a valid completion does not certify every earlier time impossible.

Time/progress alone is still a heuristic compression of intermediate labels: different carried velocity and contact state can affect the suffix. Keep near-ties or a small state/contact diversity reserve where justified. Do not call scalar frontier exclusion a proof of physical dominance. Y remains in stage feasibility, not a height reward in the objective.

## Scheduling and generator consequences

Schedule complete event/cluster candidates by increasing tau, with bounded work per time bucket and per retained parent. Keep unknowns explicit and avoid starving later fallbacks indefinitely on an unresolved early bucket. Retain every accepted incumbent before polishing it.

This is different from expanding all shorter partial prefixes first. The grind generator already does the latter: in the failing recorded search, its generation records reach only prefix tick 9 before 512 expansions are consumed, while a known full witness finishes at tick 11. It generated 2704 choices and dropped 230 queued prefixes; no complete numerical proposal reached the solver. A terminal frontier alone would not help that run because no terminal labels are produced. Apply useful compaction at completed intermediate interactions, and construct a bounded set of complete event-time plans early rather than exhaustively expanding all short histories.

The proposed frontier addresses the time-versus-progress retention mismatch. Within-cluster proposal ordering, contact/phase-aware label grouping, and feasible-incumbent preservation remain separate required work. A richer weighted score is deferred.

## Severity of the current discovery failures

The current late-window cold prototype fails two of six small supplied-route scenes, repeatedly. This is a material robustness problem for this prototype. The earlier manual-window pipeline found known 11-tick grind and 16-tick turn witnesses, so those physical solutions are representable and accepted by the shared model. Different domains/settings prevent claiming a controlled overall regression rate, but the new policy is not ready as a robust replacement.

Grind: about 4.2 seconds, 512 expanded prefixes, 2704 generated choices, 230 queue drops, zero numerical solver calls. This is short-prefix enumeration and cluster granularity. More total solver time alone does not help when the prefix cap stops generation first.

Turn: about 8.6 seconds and 63 solver calls in the fixed control, with zero accepted prefix scores. Twelve of 16 partial candidates hit a local cap of four calls, three exhaust contact search, and one fails the post-turn stage. The global 128-call budget is not exhausted. The same scene therefore has both local repair-allocation and candidate-handling issues.

Additional single-case probe at source e24e61e8: truncate the known accepted 16-tick turn program at state 10 and reuse its facings as a seed. The prefix passes scalar physics, finite scene replay, corridor audit, the new post-turn checkpoint and opportunity ranges. Nevertheless the existing optimize/repair path returns unknown after four calls in about 0.64 seconds. Its first optimized output violates the corridor, then it spends the remaining work on repairs. It fails to return the already-valid seed as an incumbent. This shows that missing initial facings or an impossible checkpoint alone cannot explain this case. Validate and preserve feasible seeds before optimization, and return them when later polishing/repair runs out of work.

The [probe artifact](experiments/decision-windows/evidence/turn-known-prefix-probe.json.gz) preserves its input/control and output evidence. This is one controlled diagnostic, not a new repeated benchmark. Its conclusion is incumbent preservation needs correction; it does not establish all turn failures have the same cause. No solver/runtime fix or time-frontier implementation was made in this explanatory turn.
