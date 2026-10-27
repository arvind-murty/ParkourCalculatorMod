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

## 2. Generate a broad, structured schedule family

The generation order is now: solve entry feasibility, enumerate timing/contact/Sneak schedules cheaply, derive Y, group comparable timing outcomes, rank straight-line horizontal potential, then solve the best representatives. A few simple phase templates are not enough. No climbing implementation is added by this plan.

### 2.1 Two entry families and solved timing

For each usable climbable region and selected incoming route skeleton, generate both **front entry** and **side entry**. Store the actual signed cell boundary and finite approach corridor. A side-entry family can have either signed side when geometry permits; do not discard the other side merely from a local direction score. These remain the two entry families, with signed geometric alternatives inside them.

Reach envelopes supply only an optimistic first tick to attempt. Probe entry ticks in increasing order with the continuous solver and actual approach/cell constraints; do not choose the entry tick from a distance estimate alone. Find the earliest validated entry for each family/channel and allowed approach-key policy, then generate entry at that tick plus delays of 1 through 5 inputs by default. A reduced-budget setting may use 3 delayed inputs; expose the limit explicitly. Do not assume entry feasibility is monotone in time or use binary search.

Earlier numerical failures/timeouts are unknown, not proof that the first found entry is the true minimum. Record the first validated tick and earlier unresolved ticks separately, retain retries for them, and describe the delayed window as relative to the earliest entry found. A sound reach contradiction may exclude a tick. A locally failed probe may not. Cache probes only for identical incoming state/skeleton, geometry, approach-key policy and constraints.

These are approach-prefix feasibility probes, not full climb solves. They establish that the selected entry boundary can be reached at that tick; they do not certify the downstream climb/landing. If a retried earlier probe succeeds, regenerate its delayed window and deduplicate the candidate queue rather than freezing the first window forever.

Let E be the first sampled state inside the selected climbable-cell union. Input E-1 produces entry, and input E is the first that starts inside. Keep these labels distinct: E-1 can be uncapped but establish climb carry if it also hits a wall and ends inside; E is ordinarily the first ladder-clamped input. For an initially occupied cell, the first input is already eligible for the clamp. A solver-produced entry position or facing is a warm witness, not a pin on subsequent full solves.

For every entry time, vary first wall-hit time independently. Include entry+hit on the entering movement where feasible, entry followed by a capped approach before the first hit, and deliberately delayed entry closer to the wall. Later entry can permit an earlier first climb than an early entry whose horizontal movement is capped far from the wall. Do not tie climbing start to E or assume earliest entry wins the full route.

### 2.2 Consecutive climbing with up to two isolated stops

Enumerate a potential climbing interval and its duration, retaining nearby shorter/longer durations compatible with the target. Its hit mask normally requires a wall hit on every input. Remove zero, one or two selected **one-input** hits to generate stops:

- One optional stop at the beginning to adjust height before sustained climbing.
- One optional stop at the end to stop refreshing upward carry earlier and potentially land sooner.
- Generate exactly four choices: neither stop, beginning only, end only, or both. If both are selected, retain at least one climbing input between them.
- Do not enumerate internal stops. User ruling: represent an internal stop by the beginning stop, which provides the most horizontal momentum. This is the canonical schedule reduction for this family.

The stop is a no-wall-hit input, not a command to set Y velocity to zero. Recompute its actual movement and later carry. Keep no-hit falling/holding periods before and after the climbing interval with variable lengths; their inputs are not limited by the two interruptions. This allows a substantial non-climbing interval with arbitrary Sneak choices.

Canonicalize schedules with identical actual contact-face/timing, key and cell-membership masks. For example, omitting the final potential hit can duplicate choosing a shorter climb interval. Do not merge schedules merely because their climb counts or final heights match. The two-stop limit concerns interruptions of climbing, not Sneak switches.

### 2.3 Arbitrary Sneak masks on non-climbing inputs

For M eligible non-climbing inputs, generate the binary Sneak mask rather than just one Sneak interval or a few phase templates. For eight inputs, the complete family has 256 masks; the subset with four Sneak inputs has 70 arrangements. In particular, retain interspersed patterns such as:

```text
S . S . S . S .
. S . S . S . S
. . . . S S S S
S S S S . . . .
```

S is Sneak; a dot is no Sneak. Falling/holding inputs, both one-input climbing stops, and entry/exit inputs can participate. Counts and positions are variable. Do not require one contiguous Sneak run, alternate at most once, or silently limit the number of Sneak switches.

Also branch Sneak on the first input that will actually be clamped, even if that input is a selected climbing hit. Prioritize that variant when the reduced horizontal input still reaches the cap, because the immediate movement loss can then be hidden by clamping. It is a heuristic, not a rule that Sneak is free: it changes sprint/input state, may stop an axis saturating, and can prevent reaching the wall. Compile both alternatives through the shared state. Keep further Sneak+hit alternatives under explicit candidate budgets when their cap/geometry behavior warrants them.

Try masks with Sneak moved later and the first-clamped-input exception early in enumeration, then include interspersed and complementary masks. For manageable M enumerate all masks. If a larger family requires beam/lazy enumeration or caps, report omitted masks and preserve diversity by entry family, delay, interruption placement, Sneak count and exit choice. A candidate cap must not quietly turn the family back into a few contiguous intervals.

### 2.4 Derive complete Y and exit schedules

For each entry, hit and key mask, replay the existing scalar Y recurrence from actual incoming Y/velocity, applying occupancy, Sneak, selected hit flags and selected Y contacts. Determine possible side/top departure times, target-height windows and descending landing times. Generate alternative exit faces and timings rather than deriving every exit from a fixed maximum climb height. Explicitly include one fewer climb input when a lower side exit can reach its block sooner.

An exit is leaving the actual climbable-cell union: upward across its top integer Y boundary or horizontally across a side integer X/Z boundary, using the sampled feet position. Crossing an integer into another climbable cell is not an exit. This is separate from body clearance of the finite collision solids and from the final descending support contact. Preserve the actual exit axis/side, position corridor, Y velocity and remaining contact/sprint state.

Record airtime T from the common departure state to the selected landing, and inputs A after leaving the climbable region until that landing. The departure input counts in T, as with existing jump profiles; grounded run-up inputs are accounted for separately in global elapsed time. Compare T only within the same incoming route context/run allocation. In a simple single-entry/single-exit candidate, A is the landing state minus the first outside state after the ladder interaction. If re-entry is introduced later, use the final exit of that interaction and expose the extra membership episodes. Include these anchors in relative retiming data so run insertion changes every related constraint consistently.

No free Y variable or nonsmooth Y clamp is needed inside IPOPT for a fixed complete schedule. At unknown entry states from another segment, derive Y from the selected upstream skeleton rather than rest velocity or a universal climb rate. Retain distinct possible incoming states where needed.

### 2.5 Group equal timings and rank horizontal potential

Group schedules for the same destination and incoming route context by **(T, A)**: the same total airtime and the same time after leaving the climbable region. Both conditions matter. One fewer in-cell tick can be beneficial even if the eventual total time is unchanged; total time alone does not describe the horizontal opportunity after exit.

Within each timing bucket, run a cheap geometry-free straight-line benchmark from zero initial X/Z momentum, including the approach and the full candidate key/occupancy/contact timing. Compile actual Sneak and sprint/air-factor histories with the shared compiler; use existing inertia, acceleration, component-cap and drag primitives. Measure unobstructed travel along a fixed horizontal axis, without a turning penalty or a real prefix's momentum advantage. Wall-hit flags still determine the compiled sprint history; the benchmark's travel axis is a neutral free direction, so it does not forcibly zero horizontal progress on every synthetic hit. This is a ranking proxy, not a physical wall-contact solve or acceptance test.

Record distance by landing, speed at exit and speed at landing. Rank primarily by average straight-line speed over T, equivalent to distance by landing because T is equal within the bucket; use exit/final speed as secondary information. Maximizing final stored speed alone can favor a different Sneak order and must not silently replace progress over the full equal-duration schedule. This scoring often favors pushing Sneak later, with the cap-aware early-Sneak variant considered explicitly. Do not implement a positional preference instead of actually evaluating the movement benchmark.

Use this score aggressively to choose which schedules receive expensive solves. Keep the top representative plus a small configurable set of backups per timing bucket, preserving distinct front/side entry, exit corridors and materially different clamp/contact histories. Hold lower-ranked representatives for recovery if the leaders fail geometry. It is acceptable to drop them under an explicit heuristic budget; equal (T,A) and a worse straight-line score are not a proof of dominance with actual momentum, turning and obstacles.

For scale, one signed front channel and one signed side channel with five delayed inputs produce 12 entry cases. Each climbing window has at most four beginning/end stop choices, independent of its length. Eight freely chosen Sneak inputs contribute 256 masks per compatible skeleton. This is thousands of cheap scalar candidates before numerical solving, not thousands of full IPOPT calls. Reuse shared prefixes and canonical masks to amortize simulation, but merge only identical full vertical/input/contact state or label any broader reduction heuristic.

## 3. Screen and rank before numerical work

Reject cheap, genuine contradictions:

- Y/feet-cell membership inconsistent with the proposed cell sequence, missed selected Y contact, impossible descending top landing or an empty target-height window.
- No reachable signed wall face on the first climb input, including the .15 movement limit when already inside the cell. An entering input uses the uncapped regime if it started outside.
- Conservative X/Z envelopes cannot reach the entry cell, wall, required finite tangential interval, exit corridor or landing window in the available time.
- A prescribed run/floor/contact/climbable-cell condition cannot be met at its correct stage.
- A valid completion-time lower bound already exceeds the best accepted total.

Compute movement envelopes using compiled input/sprint state and the actual independent component caps. Retain both capped and free inertia possibilities as required. Do not apply the ordinary-jump height rejection to a route whose ladder segment can climb higher. Extend route/profile nodes and time bounds to include ladder interactions before enabling their global pruning.

Across timing buckets prioritize optimistic total arrival time. Within equal (T,A), use the straight-line horizontal benchmark and geometry difficulty to select representatives and backups. These scores order expensive work; they do not prove infeasibility. A higher exit or fewer climb inputs does not dominate another schedule automatically. Keep distinct useful exits and input/contact histories under explicit representative budgets, reporting heuristic drops.

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
2. **Entry and vertical candidate generator.** Solve increasing front/side entry ticks, then generate the first found entry through five delayed inputs. Enumerate the four optional beginning/end one-input stop choices, arbitrary non-climb Sneak masks and alternative exits. Derive Y cheaply, canonicalize identical schedules and report generation/pruning. No recorded Y/facing schedules as production inputs.
3. **Ranking and numerical loop.** Group by total/post-exit airtime, benchmark straight-line horizontal progress from zero momentum, and compile/solve the best representatives and diverse backups. Enforce finite contact/membership guards and existing cap regimes with explicit unknown handling. Compare small generated families with exhaustive masks/stops and verify unresolved earlier entry ticks remain recorded.
4. **Route composition and evidence.** Add ladder interaction profiles to the route search, retain alternatives and update admissible bounds. Test real geometry when available, keeping recorded traces only as regression diagnostics.

Required examples: front versus side entry; solved earliest entry plus delays through five inputs; earlier unresolved entry probes retained; all four beginning/end one-input stop choices, with internal stops canonicalized to the beginning; all 256 eight-input Sneak masks, including four interspersed Sneak inputs; cap-aware first-input Sneak versus later Sneak; timing buckets distinguishing equal total airtime but different post-exit airtime; straight-line progress versus final-speed ranking; a heuristic leader failing geometry while its backup succeeds; early entry versus one-input-delayed entry; uncapped entry versus capped in-cell approach; fall capped at -.15; sneak arresting descent; positive carry persisting after stopping climb; sneak with and without continued hits; consecutive-hit climb and interrupted climb; one fewer climb input giving a faster side landing; top landing on actual ladder geometry; vines without invented solids; both X/Z clamp signs and boundary regimes; run insertion and segment seams preserving every schedule; a locally shorter climb losing to a faster overall continuation. Verify no early/late/missing/extra hit or occupancy state is accepted.

Record generated/screened/dropped schedules, numerical calls/timeouts, inferred clamp patterns, selected cell/contact/Sneak timelines, Y traces, total time and objective. Return best found with explicit limits; bounded schedule generation and local solves do not prove global optimality or infeasibility.
