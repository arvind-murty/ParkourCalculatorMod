# Full-route forward endpoint adjustment experiments

## Implemented policy

Commit `824f962` adds opt-in `LongRunConfig.withWindowAdjustment`: `FORWARD_JUMP` or `FORWARD_TICK`, default `NONE`. On a fixed-start nonterminal window, detect a direct positional +Z objective cap. Prefer the requested forward extension; if its endpoint is also directly capped or unavailable, try the other forward size. No backward extension is attempted. The commit interval stays the original nominal interval; every original constraint through the actual new endpoint is sliced and checked. The actual end already participates in the window-cache key.

This is preflight selection, not an extra optimization stage: if the selected window misses, the existing alternate objectives, SLP and smaller-window ladder run. It does not additionally retry the unselected shift after a solve failure. If neither candidate removes the direct cap, retain the nominal end. The last route endpoint is reserved for the existing terminal solve with the original objective; free-start windows are outside this experiment.

After adjustment the trace's `jumps=[i,we)` remains the **nominal** jump range, while `ticks=[a,c)` gives the actual window. `LRS-ADJUST` records nominal and selected ends explicitly. Use actual tick endpoints when reading shifted runs.

## Full-route outcomes

All six runs use the complete 808-tick input (636 constraints), fixed closed-form margin0.0003, current EPS2=1e-24, no outer deadline, and commit ladder3,1. SLP settings remain unchanged. Commit1 starts again from the original state, not from the failed commit3 state. A test passing means the probe recorded its outcome; it does not assert that this route solved.

| Nominal ladder | Adjustment | Commit3 stuck before jump / tick | Commit1 stuck before jump / tick | Whole invocation ms |
|---|---|---|---|---:|
| 7,5,3,2,1 | None, fresh baseline | 14 / 90 | 14 / 90 | 1490.065 |
| 7,5,3,2,1 | Forward jump preferred | 14 / 90 | 14 / 90 | 1898.252 |
| 7,5,3,2,1 | Forward tick preferred | 14 / 90 | 14 / 90 | 1074.893 |
| 10,7,5,3,2,1 | None, fresh baseline | 13 / 88 | 14 / 90 | 3454.303 |
| 10,7,5,3,2,1 | Forward jump preferred | 14 / 90 | 14 / 90 | 3975.946 |
| 10,7,5,3,2,1 | Forward tick preferred | 14 / 90 | 14 / 90 | 3833.682 |

No complete solution was returned. All adjusted chains commit through tick90, then every remaining window rung misses from that entry state. These are single JVM measurements with tracing; they are not a controlled performance benchmark. The ten-ladder commit3 improves from tick88 to90 relative to its fresh baseline, but its final commit1 result does not improve.

## What changed, concretely

The seven-ladder initial nominal end68 now becomes71 under forward-jump preference or69 under forward-tick preference; Z/MAX is accepted in both cases, matching the isolated-window probe. For commit3, the next window30–77 uses centered SLP with the jump policy but accepts primary Z/MAX with the tick policy. Thus the adjusted routes genuinely differ; the identical final failure location does not imply identical intermediate states.

The ten-ladder's first nominal end77 is not directly capped, so neither policy moves it. Both accept alternate X/MAX there. Subsequent forward choices can coincide when one proposed endpoint remains capped. See every actual choice in the raw `LRS-ADJUST` lines and the window table.

At the failing entry state tick90, the actual window ends tried are:

| Ladder and policy | Actual ends tried, in order |
|---|---|
| Seven, forward jump | 155,130,128,113,99 |
| Seven, forward tick | 155,130,118,113,99 |
| Ten, forward jump | 196,155,130,128,113,99 |
| Ten, forward tick | 188,155,130,118,113,99 |

Both commit attempts try these ends in each policy; all miss even after their alternate objectives and SLP fallback. Moving the first objective off its wall helps that local solve, but it does not solve the onward continuation problem. These results do **not** establish infeasibility from tick90 or from the original start. Determining why that continuation fails needs its validation residuals and committed entry-state comparison; this experiment measures endpoint selection, not that diagnosis.

## Every accepted intermediate window

| Run | Commit count | First jump | Window ticks | Nominal end | Commit through tick | Accepted solver | ms |
|---|---:|---:|---|---:|---:|---|---:|
| seven-forward-jump | 3 | 1 | 0–71 | 68 | 30 | primary Z/MAX@71 | 63.0 |
| seven-forward-jump | 3 | 4 | 30–77 | 77 | 64 | fallback SLP Z/MAX@47 | 59.7 |
| seven-forward-jump | 3 | 7 | 64–90 | 90 | 75 | alternate Z/MIN@26 | 16.8 |
| seven-forward-jump | 3 | 10 | 75–90 | 88 | 88 | fallback SLP Z/MAX@15 | 5.8 |
| seven-forward-jump | 3 | 13 | 88–90 | 90 | 90 | primary Z/MAX@2 | 0.1 |
| seven-forward-jump | 1 | 1 | 0–71 | 68 | 12 | primary Z/MAX@71 | 3.0 |
| seven-forward-jump | 1 | 2 | 12–71 | 71 | 21 | alternate Z/MIN@59 | 6.2 |
| seven-forward-jump | 1 | 3 | 21–75 | 75 | 30 | primary Z/MAX@54 | 3.0 |
| seven-forward-jump | 1 | 4 | 30–77 | 77 | 40 | fallback SLP Z/MAX@47 | 20.4 |
| seven-forward-jump | 1 | 5 | 40–80 | 80 | 55 | primary Z/MAX@40 | 3.2 |
| seven-forward-jump | 1 | 6 | 55–77 | 77 | 64 | primary Z/MAX@22 | 0.6 |
| seven-forward-jump | 1 | 7 | 64–90 | 90 | 68 | fallback SLP Z/MAX@26 | 21.5 |
| seven-forward-jump | 1 | 8 | 68–90 | 88 | 71 | alternate Z/MIN@22 | 3.0 |
| seven-forward-jump | 1 | 9 | 71–90 | 90 | 75 | alternate Z/MIN@19 | 2.5 |
| seven-forward-jump | 1 | 10 | 75–90 | 88 | 77 | fallback SLP Z/MAX@15 | 3.4 |
| seven-forward-jump | 1 | 11 | 77–90 | 90 | 80 | alternate Z/MIN@13 | 0.7 |
| seven-forward-jump | 1 | 12 | 80–90 | 90 | 88 | alternate Z/MIN@10 | 0.6 |
| seven-forward-jump | 1 | 13 | 88–90 | 90 | 90 | primary Z/MAX@2 | 0.1 |
| seven-forward-tick | 3 | 1 | 0–69 | 68 | 30 | primary Z/MAX@69 | 0.8 |
| seven-forward-tick | 3 | 4 | 30–77 | 77 | 64 | primary Z/MAX@47 | 1.4 |
| seven-forward-tick | 3 | 7 | 64–90 | 90 | 75 | fallback SLP Z/MAX@26 | 14.3 |
| seven-forward-tick | 3 | 10 | 75–90 | 88 | 88 | alternate Z/MIN@15 | 0.6 |
| seven-forward-tick | 3 | 13 | 88–90 | 90 | 90 | primary Z/MAX@2 | 0.1 |
| seven-forward-tick | 1 | 1 | 0–69 | 68 | 12 | primary Z/MAX@69 | 0.7 |
| seven-forward-tick | 1 | 2 | 12–71 | 71 | 21 | alternate Z/MIN@59 | 2.1 |
| seven-forward-tick | 1 | 3 | 21–75 | 75 | 30 | alternate Z/MIN@54 | 2.5 |
| seven-forward-tick | 1 | 4 | 30–77 | 77 | 40 | fallback SLP Z/MAX@47 | 10.2 |
| seven-forward-tick | 1 | 5 | 40–80 | 80 | 55 | fallback SLP Z/MAX@40 | 11.9 |
| seven-forward-tick | 1 | 6 | 55–77 | 77 | 64 | primary Z/MAX@22 | 0.2 |
| seven-forward-tick | 1 | 7 | 64–90 | 90 | 68 | alternate Z/MIN@26 | 2.8 |
| seven-forward-tick | 1 | 8 | 68–90 | 88 | 71 | fallback SLP Z/MAX@22 | 7.4 |
| seven-forward-tick | 1 | 9 | 71–90 | 90 | 75 | alternate Z/MIN@19 | 1.5 |
| seven-forward-tick | 1 | 10 | 75–90 | 88 | 77 | alternate Z/MIN@15 | 0.6 |
| seven-forward-tick | 1 | 11 | 77–90 | 90 | 80 | alternate Z/MIN@13 | 0.5 |
| seven-forward-tick | 1 | 12 | 80–90 | 90 | 88 | alternate Z/MIN@10 | 0.4 |
| seven-forward-tick | 1 | 13 | 88–90 | 90 | 90 | primary Z/MAX@2 | 0.1 |
| ten-forward-jump | 3 | 1 | 0–77 | 77 | 30 | alternate X/MAX@77 | 8.4 |
| ten-forward-jump | 3 | 4 | 30–90 | 90 | 64 | fallback SLP Z/MAX@60 | 85.8 |
| ten-forward-jump | 3 | 7 | 64–90 | 90 | 75 | alternate Z/MIN@26 | 1.3 |
| ten-forward-jump | 3 | 10 | 75–90 | 88 | 88 | alternate Z/MIN@15 | 0.8 |
| ten-forward-jump | 3 | 13 | 88–90 | 90 | 90 | primary Z/MAX@2 | 0.1 |
| ten-forward-jump | 1 | 1 | 0–77 | 77 | 12 | alternate X/MAX@77 | 6.5 |
| ten-forward-jump | 1 | 2 | 12–80 | 80 | 21 | fallback SLP Z/MAX@68 | 26.3 |
| ten-forward-jump | 1 | 3 | 21–90 | 88 | 30 | fallback SLP Z/MAX@69 | 63.4 |
| ten-forward-jump | 1 | 4 | 30–90 | 90 | 40 | fallback SLP Z/MAX@60 | 70.7 |
| ten-forward-jump | 1 | 5 | 40–80 | 80 | 55 | alternate Z/MIN@40 | 2.6 |
| ten-forward-jump | 1 | 6 | 55–77 | 77 | 64 | alternate X/MAX@22 | 1.6 |
| ten-forward-jump | 1 | 7 | 64–90 | 90 | 68 | alternate Z/MIN@26 | 4.7 |
| ten-forward-jump | 1 | 8 | 68–90 | 88 | 71 | alternate Z/MIN@22 | 2.2 |
| ten-forward-jump | 1 | 9 | 71–90 | 90 | 75 | fallback SLP Z/MAX@19 | 10.9 |
| ten-forward-jump | 1 | 10 | 75–90 | 88 | 77 | alternate Z/MIN@15 | 0.5 |
| ten-forward-jump | 1 | 11 | 77–90 | 90 | 80 | alternate Z/MIN@13 | 0.5 |
| ten-forward-jump | 1 | 12 | 80–90 | 90 | 88 | alternate Z/MIN@10 | 0.5 |
| ten-forward-jump | 1 | 13 | 88–90 | 90 | 90 | primary Z/MAX@2 | 0.1 |
| ten-forward-tick | 3 | 1 | 0–77 | 77 | 30 | alternate X/MAX@77 | 7.8 |
| ten-forward-tick | 3 | 4 | 30–90 | 90 | 64 | fallback SLP Z/MAX@60 | 152.3 |
| ten-forward-tick | 3 | 7 | 64–90 | 90 | 75 | alternate Z/MIN@26 | 1.5 |
| ten-forward-tick | 3 | 10 | 75–90 | 88 | 88 | alternate Z/MIN@15 | 0.6 |
| ten-forward-tick | 3 | 13 | 88–90 | 90 | 90 | primary Z/MAX@2 | 0.1 |
| ten-forward-tick | 1 | 1 | 0–77 | 77 | 12 | alternate X/MAX@77 | 6.5 |
| ten-forward-tick | 1 | 2 | 12–80 | 80 | 21 | fallback SLP Z/MAX@68 | 26.6 |
| ten-forward-tick | 1 | 3 | 21–90 | 88 | 30 | fallback SLP Z/MAX@69 | 63.1 |
| ten-forward-tick | 1 | 4 | 30–90 | 90 | 40 | fallback SLP Z/MAX@60 | 68.3 |
| ten-forward-tick | 1 | 5 | 40–80 | 80 | 55 | alternate Z/MIN@40 | 2.7 |
| ten-forward-tick | 1 | 6 | 55–77 | 77 | 64 | alternate X/MAX@22 | 1.4 |
| ten-forward-tick | 1 | 7 | 64–90 | 90 | 68 | alternate Z/MIN@26 | 4.9 |
| ten-forward-tick | 1 | 8 | 68–90 | 88 | 71 | alternate Z/MIN@22 | 2.1 |
| ten-forward-tick | 1 | 9 | 71–90 | 90 | 75 | fallback SLP Z/MAX@19 | 10.8 |
| ten-forward-tick | 1 | 10 | 75–90 | 88 | 77 | alternate Z/MIN@15 | 0.4 |
| ten-forward-tick | 1 | 11 | 77–90 | 90 | 80 | alternate Z/MIN@13 | 0.5 |
| ten-forward-tick | 1 | 12 | 80–90 | 90 | 88 | alternate Z/MIN@10 | 0.4 |
| ten-forward-tick | 1 | 13 | 88–90 | 90 | 90 | primary Z/MAX@2 | 0.1 |

## Evidence and recovery

- [All 220 window attempts](full-forward-shifts-2026-10-23/windows.csv), including misses, actual/nominal ends, committed endpoint, solver and total window timing.
- [All 930 objective/SLP calls](full-forward-shifts-2026-10-23/objectives.csv), with individual outcomes and timings.
- [Route stop summaries](full-forward-shifts-2026-10-23/summary.csv), [settings](full-forward-shifts-2026-10-23/metadata.txt).
- [Seven forward jump trace](full-forward-shifts-2026-10-23/seven-forward-jump.txt), [seven forward tick](full-forward-shifts-2026-10-23/seven-forward-tick.txt), [ten forward jump](full-forward-shifts-2026-10-23/ten-forward-jump.txt), [ten forward tick](full-forward-shifts-2026-10-23/ten-forward-tick.txt). These contain every committed tick's position, velocity and yaw.
- [Fresh seven baseline](full-forward-shifts-2026-10-23/seven-baseline.txt), [fresh ten baseline](full-forward-shifts-2026-10-23/ten-baseline.txt).
- [Four focused full-route probes](full-forward-shifts-2026-10-23/full-probes.log) passed as diagnostic tests. [Five endpoint-policy units, five existing config units, six detector units and two fresh baseline probes](full-forward-shifts-2026-10-23/policy-unit-and-baseline-tests.log) passed (18 tests). No full suite ran.
- [Implementation](../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/LongRunSolver.java), [full-route probe](../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/FullRouteFixedMarginTraceTest.java), [policy unit checks](../../../core/src/test/java/de/legoshi/parkourcalc/core/anglesolver/solver/LongRunWindowAdjustmentTest.java).
