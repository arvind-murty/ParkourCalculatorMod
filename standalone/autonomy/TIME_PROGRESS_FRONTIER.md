# Time and progress comparisons: two distinct goals

Authoritative user clarification, 2026-11-01. The new path is to generate the supported candidates and evaluate them in two complementary ways. We are not trying to generate candidates specifically at different completion times. See [the implementation plan](TIME_PROGRESS_IMPLEMENTATION_PLAN.md) and [implemented behavior and measurements](experiments/two-goal-comparisons/README.md). The separate comparisons are now the default checkpoint-search policy.

## Spatial goal: compare elapsed time

Hold a physical target fixed: for example, land on the same block or complete the same passage. Determine the earliest validated achievement time found for each candidate. This comparison helps identify the fastest potential route.

Landing requires the actual declared finite contact; being airborne over the goal is insufficient. Preserve input/state indexing: an event produced by input t is observed at state t+1. Earlier unresolved times remain unknown. A found time is not automatically a proof of minimum time.

## Time goal: compare objective

Hold the absolute elapsed state tick h fixed across candidates. Optimize/measure the same horizontal objective S(h), usually progress/distance along the shared outgoing route direction. Compare after turnarounds where the direction is meaningful. Y is feasibility, not a height reward.

This is a proxy for which candidate goes farthest. It is not equivalent to the shortest time to a spatial goal. Do not compare progress sampled at each candidate's own arrival time and call it a same-time objective comparison. Record spatial-goal times and fixed-clock objectives separately.

## Retention

Preserve similar candidates and time/progress tradeoffs. A slightly slower or slightly shorter-progress history is not a reason to discard it. Only mark a candidate heuristically worse when the evidence shows a meaningful disadvantage under comparable conditions, without an offsetting advantage. Unknown measurements do not count as bad measurements.

Do not construct a false dominance argument from the fastest candidate and a different farthest candidate. A genuine tradeoff may sit between those two. Even comparison with one apparently better peer is heuristic because physical/contact/velocity history can affect later choices.

Keep near alternatives, symbolic diversity, accepted incumbents and resumable search state. Resource-cap omissions are reported independently from comparisons that classify a candidate as markedly worse. At the final goal, the earlier accepted completion remains the fastest even if a later route has a larger horizontal objective.

## Commitment and provisional assumptions

Commit/retain discrete inputs and constraints within a spatial-route branch. For later decisions, re-solve from the original full state with all earlier facings free. Do not fix optimized intermediate positions or velocities. Nearby coupled decisions remain jointly considered; weak coupling of farther decisions is provisional and can be overturned by maps.

Retain alternatives and support reopening/expanded clusters when evidence warrants it. A failed numerical continuation is not a proof that the earlier decision was wrong. Successful-but-suboptimal commitments must also be checked through reference comparisons.

## Regression is part of the design

Full regression must additionally optimize/simulate the continuations of near and would-be-discarded candidates, including apparently inferior ones, to discover whether they produce the best final route. Replaying only retained results does not validate pruning.

Use unpruned/joint or exhaustive references over declared finite test domains, independently validate returned witnesses, and record every discarded candidate that ties/beats the heuristic result. Give this reference evaluation sufficient separately reported work; unknown or unevaluated branches leave the audit incomplete. On larger domains explicitly report incomplete coverage instead of claiming optimality preservation.

This is the selected new search path, not an opt-in rollout project. Existing movement/constraint builders and IPOPT remain unchanged. No runtime implementation is claimed by this planning update. The earlier source-specific failure reports remain in PRUNING_RESEARCH.md and the experiment directories; the earlier unknown grind/turn failures have already been fixed (current controls find 11 and 13 ticks respectively).
