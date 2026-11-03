# Time and progress comparisons: two distinct goals

Authoritative user clarification, 2026-11-01. The new path is to generate the supported candidates and evaluate them in two complementary ways. We are not trying to generate candidates specifically at different completion times. See [the implementation plan](TIME_PROGRESS_IMPLEMENTATION_PLAN.md) and [implemented behavior and measurements](experiments/two-goal-comparisons/README.md). The separate comparisons are now the default checkpoint-search policy.

Latest user refinement, 2026-11-02: retain the separate time/progress comparisons, and include both horizontal position and velocity in the fixed-clock progress assessment. This is a selected design direction; current runtime retention still uses position-only clock results. Both Real11 initial0/8 setup families remain explicitly protected by the user's preceding decision. The policy/solver has not been changed by this discussion.

## Spatial goal: compare elapsed time

Hold a physical target fixed: for example, land on the same block or complete the same passage. Determine the earliest validated achievement time found for each candidate. This comparison helps identify the fastest potential route.

Landing requires the actual declared finite contact; being airborne over the goal is insufficient. Preserve input/state indexing: an event produced by input t is observed at state t+1. Earlier unresolved times remain unknown. A found time is not automatically a proof of minimum time.

## Time goal: compare objective

Hold the absolute elapsed state tick h fixed across candidates. Optimize/measure the same horizontal objective S(h), usually progress/distance along the shared outgoing route direction. Compare after turnarounds where the direction is meaningful. Y is feasibility, not a height reward.

This is a proxy for which candidate goes farthest. It is not equivalent to the shortest time to a spatial goal. Do not compare progress sampled at each candidate's own arrival time and call it a same-time objective comparison. Record spatial-goal times and fixed-clock objectives separately.

### Position and velocity at the common clock

Keep the accepted witness's full horizontal state `(p_XZ(h), v_XZ(h))`. For the shared route direction d and anchor a, report forward position `s=d dot(p-a)` and forward velocity `u=d dot v`; retain perpendicular position/velocity too, since lateral alignment affects a turn or blockage. Use identical absolute h, direction, anchor and stored-velocity convention for every compared candidate. Position and velocity in one state label must come from the same physically accepted trajectory. Combining the best position of one solve with the best velocity of another would invent a state that was never solved.

Proposed initial retention treatment: keep position/velocity tradeoffs explicitly, including farther-but-slower versus nearer-but-faster states, rather than immediately choosing an arbitrary conversion weight. Continue comparing time to the shared spatial goal separately. No weight, velocity margin or rigorous dominance certificate is established by this discussion.

Movement/contact phase is also necessary comparison context. The Real11 state10 witnesses show jump0 has position2.392651 and forward velocity0.226887836, while jump8 has position2.721058 and forward velocity0.263399323. Both horizontal measurements favor8. Yet0 is descending with vy=-0.373904036 and8 is ascending with vy=+0.248135999; their next contact/jump opportunities differ. Protect their distinct setup families as instructed. A horizontal position/velocity frontier alone would still falsely classify0 as inferior if used across those phases. This uses vertical phase for future-event context; the progress objective remains horizontal.

Current IPOPT calls maximize position for a fixed discrete program. Reading velocity from that one returned witness does not explore the program's own continuous position/velocity tradeoff. A possible later experiment is a second fixed-input solve maximizing route-directed velocity while preserving a small allowed loss of position through an explicit row, with full physical validation. That is a proposal, not an implemented or newly selected numerical objective/weight. It would reuse the existing continuous solver and original-state/free-facing contract.

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
