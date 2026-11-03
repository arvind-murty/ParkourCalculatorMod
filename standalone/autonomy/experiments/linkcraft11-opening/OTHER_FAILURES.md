# Why the four Real11 searches returned unknown

2026-11-02 follow-up investigation. These four results do not indicate four failures of the same numerical problem. The first two never submit a candidate in the final64-input objective stage. The distant-B5 run does separately try two spatial B5-landing probes at64, totaling five numerical requests; both remain unknown. Those probe problems require a B5 landing at64 and are distinct from the final open64-input objective. The third and fourth reach the final stage but fail to discover an accepted solution within their chosen proposal/repair search. New controlled probes recover two missed valid continuations, including one under the original per-candidate budget. No production search/physics code is changed in this investigation.

The goal remains maximumZ at absolute64 with required B2/B3/B4/B5 support interactions and no required B7 landing. A valid64-input result need not get beyond B7. All probes preserve the original full state, seven declared physical blocks, normal momentum/zero thresholds and free earlier facings.

## Where each run failed

| Run | Main search progress | Terminal-stage64-input programs evaluated | Actual stopping mechanism |
| --- | --- | ---: | --- |
| Distant B5 goal | B2 first accepted at28; B3 at44; B4 at55 | 0 | retained histories are too late/unproductive; bounded final extension emits none; rescue fails to complete |
| Nearer spatial goals | two accepted28-input prefixes; zero accepted at44; only a falling unknown survives | 0 | contact-free continuation proposals dominate; frontier collapses before reaching B4/B5 |
| Near goals plus support-progress priority | more useful28-input prefixes, but necessary B3-wait variant not proposed inside20-candidate batch; later known prefixes are slow | 18 | early decision coverage failure plus final repair-queue starvation;300-second deadline |
| Resume retained55-input histories | starts with the same eight retained55-input histories | 20 | repeats18 previous final candidates, without their64-input repair progress;20-candidate limit after126.5s |

All old accepted prefix/probe witnesses remain valid on replay. `Unknown` means no accepted full64-input incumbent was found by that search. `Exhausted` in the top-level first/second reports does not certify that every spatial route or discrete history was considered: earlier windows had already stopped at candidate caps and discarded/parked histories.

## 1. Distant B5: poor next-event coverage, followed by dead-end prefixes

The initial10-input frontier has no useful accepted arrival-time measurement to the distant B5. Two of its spatial probes attempt B5 landing at64 and remain unknown after one and four numerical requests respectively; their failed full input programs were not preserved by the old probe summary. Immediate-jump0 is lost to the scored resource cap; the retained set mostly contains initial jumps3..9 and one unresolved run-off history. This early loss is already covered by the new protection plan, but is not the only failure.

At28, only one of20 programs passes the full checks. It has jumps7/19, returns toB1 at19 and reachesB2 at28. Faster useful contact schedules are not numerically represented early enough; air-gap extensions consume the evaluation batch. At44, the most advanced accepted support interaction isB3@44. At55, a late accepted chain first reachesB4@55, while other retained chains repeat earlier blocks or remain in air. The bounded64-input extension generates484 raw choices, screens60 prefixes and rejects266 by the support-order policy, but emits no complete allowed64-input program. IPOPT never receives a final objective-stage candidate in the main search.

The rescue does revisit immediate-jump0, but retaining the initial setup alone does not repair proposal generation. Its20 programs at28 produce only two accepted prefixes: jumps0/13 ending at feetY2.9305, and jumps0/14 ending atY3.5797. They have returned toB1 but then missed the useful higher landings. They are physically valid prefixes yet cannot complete the required remaining supports under the configured normal/stepping catalogue. Direct catalogue checks from both states produce zero finite-contact/stepping choices; only further free fall remains. All declared support tops areY5 or higher. The rescue then carries falling unknowns instead of completing the route.

Conclusion: this run fails before terminal solving. Besides early retention, it needs better proposal coverage of the next actual interaction and a generic check for already-dead discrete prefixes. More time spent continuing the same falling prefixes cannot help them. This is scoped to the existing declared geometry and supported movement catalogue, not an infeasibility claim for unmodeled movement/world data.

## 2. Near goals: better time labels, but no accepted44-input frontier

Using B2/B3/B4/B5 as nearer shared spatial goals correctly identifies the promising initial8/9 setups. It does not ensure the next numerical proposals contain the required contact.

At28 only two of20 fixed-clock programs pass. The useful8/17 jump history reachesB2@17, jumps again at17, but its chosen28-input continuation omits theB3 landing. It ends atY6.1213 after having fallen below B3'sY7 top. The other accepted program is the same late B2@28 chain seen in trial1. Accepted arrival probes are used for ranking; their successful whole future programs are not automatically adopted as the next frontier state.

At44 all20 evaluated programs are unknown. The single unknown-reserve survivor has jumps8/17 and ends atY=-8.6062, withB2@17 its last selected landing. A direct normal/stepping catalogue check from that boundary state finds no finite event. At55 the same unresolved fall is atY=-26.6653. The final64-input generator can produce only an unfinished fall, rejected by the required support order, so no final numerical call occurs.

The immediate0 rescue again retains the two physically valid but unfinishable28-input falls from trial1. It does not find a replacement complete route. The whole run stops after233.9s with an exhausted restricted frontier, leaving time unused rather than timing out.

Conclusion: the first decisive collapse is **state44, zero accepted programs**, after missing useful contact-bearing continuations at28. Unknown retention blindly preserves a structurally dead history. Nearer goals improve information; they do not fix generation or viability by themselves.

## 3. Support-progress priority: the right earlier parent exists, but its useful continuation is starved

This ordering improves accepted28-input programs from1 in trial1 to9. In particular, the main frontier retains a physically accepted state28 program with initial jump9, B2@18, B3@27 and a ground input27 that keeps it onB3 through28. Its saved ID begins28223d4f73. This parent can produce a valid full route; losing immediate0 is not the only reason the main search fails.

The next proposal batch strongly favors plans claiming more completed support interactions, even when those proposed future contacts have not passed numerical validation. The first20 state44 programs emphasize immediate B3->B4 jumps, such as takeoff27/landing37, or one run input followed by takeoff28/landing38. Extending those proposed contacts produces many later variants before revisiting the earlier B3 wait decision. The previous constructive probes returned unknown for the9 setup's B4 arrivals37/38/39 and accepted a three-run variant arriving40. These numerical unknowns are not proofs that the faster fixed schedules are impossible, but the three-run variant is demonstrably feasible.

New generation-only reproduction:

- Restore all eight actual retained28-input parents and use the same generator/settings/support priority.
- The first20 emitted candidate IDs exactly match the original state44 batch.
- The first proposal with the corrected B3 takeoff30 appears at **proposal125**.
- The exact known useful continuation with jumps9/18/30/41 appears at **proposal135**. It has B4@40 and one run input onB4 before the next jump.
- The original search stops numerical proposal selection at20. This is a missed generated continuation, not pruning after evaluating it.

Directly constructing that same continuation from the actual retained state28 parent and solving from the original start succeeds in **two numerical calls**, with full scalar/scene/corridor replay passing. The warm guess is from our own previously accepted full9-setup solve; no recorded TAS facings or frozen intermediate endpoint are used. This proves feasibility of the missed schedule, not cold two-call discovery from an arbitrary seed.

By44, the main search retains two accepted slower5-setup variants and one unresolved fast9-setup variant that has already committed to takeoff27. Later decisions cannot insert the missing runs before that committed takeoff. At55 the known slower histories have B4@45 or later, while the fast B5@52 claim remains numerically unresolved. The full64 stage evaluates18 programs/328 numerical requests before the global300-second deadline.

Conclusion: a useful branch was available from a retained parent but lay far behind the20-proposal selection cap. The new velocity/schedule protections cannot rescue a candidate never evaluated. This needs proposal scheduling that revisits the relevant earlier input choice before elaborating many downstream variants of unvalidated contacts. Raising the cap to135 is not the proposed generic solution.

## 4. Why the final64-input calls fail: inherited repair work starves the base problem

Consider original final candidate2, with jumps5/17/26/35/46/61, B2@26, B3@35, B4@45, B5@61 and a final jump fromB5. It is a slower route, but its prescribed movement schedule is physically feasible within the original corridor and goal.

It inherits a repair pool from its55-input parent:

- 125 pending branches, one unresolved branch and an incumbent repair,127 restored branches after deduplication.
- The representative incumbent already contains eight corridor rows and a three-face B2 passage, with face-transition gates19/29. Much of the terminal search is still varying how an earlier block was passed, not only B6.
- The unconstrained-by-extra-repair base branch is present at priority score6, behind the inherited low-score branches. Additional repairs of those branches keep outranking it.
- The carried reference facing seed has length10 even though the current accepted prefix guess has length55. The search prefers that carried reference over the new guide/branch guess. This is a secondary reuse issue; its independent contribution is not isolated by these controls.

An exact budgeted re-evaluation restores the127 branches and returns unknown after24 numerical calls. Its24 scalar-feasible returns are rejected by corridor projection; another attempt is numerically unknown. The pending queue grows to174. Two genuine inner-resume passes,24 calls each, still return unknown; pending work grows to222 and270. Saving the queue alone therefore does not fix its ordering.

Controlled probes of this **same fixed movement schedule**:

| Intervention | Result |
| --- | --- |
| Original inherited queue,8-second/24-call limit | unknown,24 calls |
| Same queue, two further exact inner-resume passes | still unknown after another48 calls |
| Fresh solve enforcing the full physical scene first, with corridor filtering disabled during discovery | accepted64-input trajectory; independent replay passes |
| Check that accepted trajectory against the original corridor and terminal signature afterward | both pass |
| Fresh original-corridor solve seeded with that same accepted trajectory | accepted, preserving a valid incumbent |
| Move the existing base branch ahead of inherited repairs, original8-second/24-call limits | **accepted after21 calls total; first accepted around5.18s;Z=-1095.244174** |

The last intervention retains the same unique branch pool and physical movement problem. It promotes the inherited pool to the current64-input context, clears prior-context dedup state as normal extension does, and promotes the already-existing empty-repair branch to the front. The extracted assertions verify that no new unique row/face/hit choice or different movement schedule was inserted. This is an isolated research state/priority intervention, not a production fix.

The scene-first control's candidate scoresZ=-1095.920360 and also satisfies the original corridor and terminal signature. Thus the original corridor does **not** make this schedule infeasible. It is the discovery/repair/seed ordering that fails to reach a valid trajectory. Purely restarting without the inherited state also remains unknown at24 calls, so blindly deleting saved work is not a robust solution either. A fresh base needs guaranteed useful work alongside warm branches, with accepted incumbents preserved.

The priority-control result is worse than our best0-setupZ=-1093.267553, but it is a valid full64-input solution from a parent the original search actually retained. This demonstrates why reporting the final stage as merely too difficult or underfunded would be misleading: the same per-candidate limits can succeed with a different repair ordering.

## 5. Retained55 resume: repeated candidates, without their terminal repair progress

The resume CLI restores the last retained comparison window, not the unfinished state64 searches. It regenerates their final candidates in the same order.

Of20 final candidates in the resumed run:

- 18 have exactly the same IDs as the earlier run's18 final candidates, in exactly the same order.
- 335 of383 numerical calls are spent on those repeats.
- Only two new final variants receive the remaining48 calls.
- The per-program24-call limit remains, and the same55-input repair state is reused. The previously accumulated64-input repair frontier is not restored because final records did not retain it.

The result is unknown after126.5s because of the20-candidate limit, not a300-second timeout. It does not test the missed B3-wait branch or reopen the earlier setup choices. This is a real persistence/continuation gap in what the outer resume operation currently provides. It is separate from the warm-repair scheduling problem: the two exact inner-resume controls above do restore terminal work, yet still need an effective base-branch opportunity.

## Generic conclusions and fixes suggested by this evidence

1. **Generation coverage:** preserve/revisit alternate inputs at the decision that needs changing. Do not spend the first proposal batch elaborating later events of unvalidated earlier contact schedules. Measure the user's new(n,-J,R) ordering on the exact saved-parent/proposal fixture; do not assume velocity guards alone fix it.
2. **Future-event viability:** reject or deprioritize discrete prefixes with no supported remaining event path. Physically accepted free fall below every usable support should not win retention for a route that still requires higher landings. Reuse existing movement builders/reach screens and keep the finite-catalogue scope explicit.
3. **Repair scheduling:** give the current program's base problem and its latest accepted seed useful work alongside inherited repairs; do not exhaust hundreds of old passage variants first. Preserve alternate fixed constraints and accepted incumbents rather than changing IPOPT or moving discrete choices into it.
4. **Real continuation:** persist terminal per-program repair states and enumeration progress. Resume them instead of regenerating/restarting the same first18 candidates. Maintaining progress and scheduling useful work are both necessary.
5. **Keep failure categories separate:** numerical unknown, scalar-feasible but route/scene-rejected, physically valid but unfinishable prefix, never-proposed useful choice and a resource stop have different remedies.

These are additional failure mechanisms beyond the early time/progress exclusion. The [velocity/schedule protection plan](../../STATE_PROTECTION_IMPLEMENTATION_PLAN.md) remains useful, but cannot honestly be presented as fixing all four runs on its own.

## Evidence and reproduction

Machine summary: [other-failures-summary.json](audit/other-failures-summary.json). Original reports remain under `evidence/real11-clock64-*.json.gz`. New controlled reports are `real11-failure-generation`, `real11-failure-numeric`, `real11-failure-fresh` and `real11-failure-priority`, all `.json.gz` under [evidence](evidence/). They retain fixed programs, numerical/repair attempts, resume state, accepted candidates and replay verdicts. Original main-run subattempt logging gaps still apply; new probes are controlled reruns, not invented reconstructions of missing old numerical calls.

With the existing standalone compiler and numerical environment, run from the repository root:

```sh
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 PYTHONPATH="$PWD"
.venv-guidance/bin/python standalone/autonomy/experiments/linkcraft11-opening/probe_other_failures.py generation
.venv-guidance/bin/python standalone/autonomy/experiments/linkcraft11-opening/probe_other_failures.py numeric
.venv-guidance/bin/python standalone/autonomy/experiments/linkcraft11-opening/probe_other_failures.py fresh
.venv-guidance/bin/python standalone/autonomy/experiments/linkcraft11-opening/probe_other_failures.py priority
.venv-guidance/bin/python standalone/autonomy/experiments/linkcraft11-opening/summarize_other_failures.py
```

Numeric mode must precede fresh/priority because they consume its saved frontier/witness controls. Generation/summary require no IPOPT calls. The numerical follow-up uses182 recorded solver requests across nine controlled evaluations; all five accepted witnesses independently replay. These are search diagnosis, not an optimality proof. No production source or solver model changes, no mod Gradle gate, no game launch and no other-agent communication.
