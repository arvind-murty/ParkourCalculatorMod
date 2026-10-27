# Ladder and vine strategy search

2026-10-27. Planning only. This is the next orchestration family after [one-input wall/ceiling contacts](ONE_TICK_CONTACT_PLAN.md). No new climbing search is implemented by this document. The Real 11 numerical prototype and shared movement/collision helpers already provide the movement primitives.

## Goal and accepted approach

Choose a complete ladder interaction: approach and entry time, wall distance when entering, fall/sneak/climb inputs, consecutive climb contacts, last climb input and top/side exit. Generate many bounded discrete schedules cheaply, screen/rank them, then solve the promising ones with free facings and exact positional/contact/clamp conditions. Optimize total elapsed inputs to the actual landing or continuation goal, then the numerical objective among equal-time routes. Ladder-local climb time or exit height alone is not the objective.

Climbing is separate from the one-input reversal family. A climb interval requires actual horizontal clipping on consecutive inputs; each refreshes the upward carry. Do not apply the reversal family's next-input outward guard, same-axis-adjacency rejection or its two-hit route cap to climbing. Give ladder schedule, climb-duration, candidate and numerical-probe budgets their own explicit accounting. Per-block H2/V2/combined3 blockage caps retain their existing meaning and do not count the climb inputs.

## Verified movement order

Checked against cached Minecraft 1.8.9 `EntityLivingBase.isOnLadder` / `moveEntityWithHeading`, the mod's `PlayerSprintMachine`, the shared `ReplayInputState` / `StaticScene`, and `standalone/ipopt/ladder.py` / `worker.py`. The cached decompilation is under ignored `build/obstacle-research/decompiled`; [the reference record](experiments/blockage-order/README.md) describes its provenance.

With p[t] before input t, retain distinct flags for climbable-cell occupancy at t and t+1, chosen keys at t, horizontal clipping during t, and stored incoming Y velocity. The stock supported predicate samples the cell at floor(X), floor(feet Y), floor(Z), with Forge's climbable hook; it is not full-body overlap with the ladder's thin collision box. The supplied static scene must declare supported ladder/vine cells explicitly. Special custom climbable hooks remain outside this inert-cell profile.

| Stage | Effect |
| --- | --- |
| Input processing | Sneak scales horizontal inputs by .3 and affects sprint. Compile it through the shared input state; it does not directly multiply all stored horizontal carry by .3. |
| Before movement, if occupying a climbable cell | Independently cap X/Z intended movement at widened `0.15F`; limit descending Y movement to double `-.15`; if sneaking and Y movement is negative, replace that movement with zero. |
| Movement | Resolve Y-X-Z collisions, finite surfaces and step alternatives. |
| After movement | If horizontally clipped and the resulting state occupies a climbable cell, set Y to .2 before normal gravity/drag. The stored next-input carry is approximately .1176. |
| Following input | Move using that carry. Continued climb inputs refresh it; loss of wall contact or occupancy changes the next carry. |

Entry may therefore be uncapped on its movement but still establish climb carry for the next input. Earlier entry may instead cap horizontal movement before the player reaches the wall. Generate both entry timings and wall distances rather than deciding earlier entry must be better.

Sneak prevents downward movement while in the cell; it does not cancel positive upward carry. Sneak plus another wall hit can still refresh climbing. After the last hit, residual upward motion can continue even with sneak. A zero movement input also need not leave stored Y velocity exactly zero, because gravity/drag follows. Use the existing recurrence, not a synthetic instant-stop rule.

Ladders have actual thin collision solids as well as climbable cells. Vines declare the climbable cells without inventing solid vine boxes; their selected wall contacts use actual surrounding solids. Landing atop a ladder requires a real selected top support and descending contact, not merely reaching the top cell's height.

## 1. Represent a strategy as independent schedules

Each candidate carries relative anchors for:

- Exact chosen key rows, including Sneak on/off and the existing movement/strafe policy. Keep facings free. In 1.8.9, midair Jump is not a substitute for the required wall hits.
- Climbable-cell membership at every state, including cell transitions, first entry, last occupancy and any selected re-entry.
- Selected horizontal contact faces and hit flags per input, with consecutive runs for sustained climbing and no-hit intervals for falling/holding/exiting.
- Existing floor/ceiling/step event choices needed by that vertical skeleton.
- Intended destination: a physical top support or a side exit followed by a landing block. Include exit time, not just last climb time.

Do not make fall, sneak and climb three mutually exclusive keys. Sneak is a key, climbing is a wall-contact outcome, and falling is a movement outcome. In particular, retain sneak+hit and sneak+no-hit alternatives. An unrefreshed input may still move upward because of prior carry.

## 2. Generate many cheap vertical schedules

Use a bounded best-first/beam search over short input sequences. Start with compact phase patterns for ordering, but retain bounded alternatives with more switches:

```text
approach -> enter -> fall -> climb -> side exit -> land
approach -> enter+sneak -> climb -> hold -> top/side exit -> land
approach -> delay entry one input -> enter+hit -> consecutive climb -> exit
approach -> enter -> climb -> stop one input earlier -> coast/hold -> exit
```

These are examples, not an exclusive grammar. Generate lengths, switch positions, entry cell/time, first hit, consecutive-hit count, Sneak intervals and exit choices. Specifically keep n-1/n/n+1 climb alternatives where reachable. Sneak may begin before entry if that improves positioning; screen its horizontal cost rather than assuming it is useless outside the cell.

For each schedule, replay the existing scalar Y recurrence from its actual incoming Y/velocity. Apply occupancy, Sneak, selected hit flags and selected Y contacts. This derives Y states, stored Y velocities, clamp-active inputs, top/side exit windows and possible landing times without solving yaw. No free Y variable or nonsmooth Y clamp is needed inside IPOPT for a fixed schedule. A changed schedule gets a newly derived trace.

At unknown entry states from another route segment, derive the trace from that selected upstream skeleton. Do not replace it with rest velocity or a constant climb rate. When necessary, keep distinct possible entry states instead of collapsing them by height alone.

## 3. Screen and rank before numerical work

Reject cheap, genuine contradictions:

- Y/feet-cell membership inconsistent with the proposed cell sequence, missed selected Y contact, impossible descending top landing or an empty target-height window.
- No reachable signed wall face on the first climb input, including the .15 movement limit when already inside the cell. An entering input uses the uncapped regime if it started outside.
- Conservative X/Z envelopes cannot reach the entry cell, wall, required finite tangential interval, exit corridor or landing window in the available time.
- A prescribed run/floor/contact/climbable-cell condition cannot be met at its correct stage.
- A valid completion-time lower bound already exceeds the best accepted total.

Compute movement envelopes using compiled input/sprint state and the actual independent component caps. Retain both capped and free inertia possibilities as required. Do not apply the ordinary-jump height rejection to a route whose ladder segment can climb higher. Extend route/profile nodes and time bounds to include ladder interactions before enabling their global pruning.

Prioritize optimistic total arrival time, then reachable exit geometry and estimated horizontal difficulty. Those geometry/difficulty scores order work; they do not prove infeasibility. At equal time retain different exits, Y velocities, cell/contact histories and horizontal possibilities. A higher exit or fewer climb inputs does not dominate another schedule automatically. Any beam/duration/switch/candidate drop is a reported heuristic limit.

## 4. Solve the promising schedules exactly

For a selected candidate:

1. Realize its exact keys with `KEEP` and the corrected shared compiler. Include Sneak, ground, occupancy and every horizontal hit flag; repeated hits and sneak changes recompile sprint/air-factor state. Do not use the old constant-effective-sprint adapter.
2. Emit cell-membership guards at each sampled state. Inside a selected cell uses its half-open coordinate intervals; outside a union of cells uses selected outside-face branches. Preserve unions/disjoint cells instead of replacing them with one bounding rectangle. Before/after membership enforces the proposed entry/exit tick.
3. Emit finite approach/reach/overlap guards for each selected wall hit. Climb inputs remain at the selected wall and actually press into it; no-hit intervals forbid unselected collisions. Use the existing contact equations and correct Y-X-Z tangential stages. Do not add the reversal family's outward condition inside a climb run.
4. Reuse the prototype's free/positive/negative X/Z clamp regimes. Each fixed regime is smooth with exact branch inequalities. Refresh/selectively branch over regimes if needed; use the actual clamp in independent validation. The current four-attempt-per-phase heuristic is not an exhaustive strategy search or certificate.
5. Solve and reoptimize the relevant full prefix, including entry and exit. Do not freeze a locally convenient exit velocity or force recorded angles. Continue with other schedules when one numerical attempt is unknown.

The descent clamp and Sneak rule are already evaluated in the scalar schedule generator. Their switches are discrete schedule choices outside the continuous solve. The X/Z regime search stays with the numerical solver, avoiding the Cartesian product of every cap regime with every generated input schedule.

## 5. Validate the complete movement contract

Extend the orchestration bridge rather than writing a second movement engine:

- Pass chosen per-input Sneak to the compiler and `StaticScene.move`; the current scene bridge hardcodes false. Grounded sneak edge handling must remain shared too.
- Validate observed climbable-cell membership before and after every movement. `StaticScene.ladder` already supplies the declared-cell predicate; add diagnostics/contract comparison in the shared bridge as needed.
- Keep three distinct X/Z quantities: raw movement after inertia/control, ladder-clamped movement sent to collision resolution, and final resolved displacement. The current no-ladder adapter sends raw movement directly to collision resolution; that must change for ladder inputs.
- Use actual occupancy in `vertical_move` and the shared post-contact climb carry. The current adapter passes false for occupancy. Compare generated Y/velocity/ground and selected hit masks with the independently checked trajectory.
- Require actual clipping on each consecutive climb input and none in declared no-hit intervals. Validate wall changes, gaps, ladder/vine boundaries and the side/top exit against supplied finite geometry.
- Preserve resumed Y velocity, sprint/air factor, previous wall flag and relevant key/cooldown state at any composition seam. Climbing reduces neither a segment to a support-top state nor an exit to zero velocity.

An accepted result is still an independently validated continuous solve. Game replay, angle realization and TAS export retain their separate ownership.

## Implementation slices and acceptance

1. **Schedule/bridge foundation.** Add chosen Sneak rows, declared cells and consecutive climbing contacts without reversal guards; wire caps/occupancy into independent geometry checks. Use small prescribed schedules first, reusing Real 11 primitives and movement regressions.
2. **Vertical candidate generator.** Enumerate entry/fall/sneak/climb/exit schedules, derive Y cheaply, report counts/pruning and rank a bounded pool. No recorded Y/facing schedules as production inputs.
3. **Numerical scheduling loop.** Compile/solve the promising pool with finite contact and membership guards, existing cap patterns and explicit unknown handling. Validate a small bounded family against exhaustive schedule enumeration.
4. **Route composition and evidence.** Add ladder interaction profiles to the route search, retain alternatives and update admissible bounds. Test real geometry when available, keeping recorded traces only as regression diagnostics.

Required examples: early entry versus one-input-delayed entry; uncapped entry versus capped in-cell approach; fall capped at -.15; sneak arresting descent; positive carry persisting after stopping climb; sneak with and without continued hits; consecutive-hit climb and interrupted climb; one fewer climb input giving a faster side landing; top landing on actual ladder geometry; vines without invented solids; both X/Z clamp signs and boundary regimes; run insertion and segment seams preserving every schedule; a locally shorter climb losing to a faster overall continuation. Verify no early/late/missing/extra hit or occupancy state is accepted.

Record generated/screened/dropped schedules, numerical calls/timeouts, inferred clamp patterns, selected cell/contact/Sneak timelines, Y traces, total time and objective. Return best found with explicit limits; bounded schedule generation and local solves do not prove global optimality or infeasibility.
