# Full fixed seven-ladder: per-objective extension and10000 dual iterations

## Run settings

Full real/11 route:808 ticks,636 constraints, original X/MAX@808. Fixed start, nominal windows7,5,3,2,1, commits3 then1, fixed CF margin0.0003, EPS2=1e-24, no outer deadline. Default direct/CF dual cap10000 with existing early stops. FORWARD_JUMP preference is evaluated independently for every CF objective; centered SLP uses the nominal window. Its phase1/total budgets40/60 and center-clearance target0.02 remain as before. Only the existing focused `FullRouteFixedMarginTraceTest.traceSevenForwardJump` probe ran, in the background with output redirected.

Relative to the previous fixed1000 trace, both the cap and endpoint policy changed. It is not a controlled iteration-only comparison. The successful normal reference is the saved250-tick/30-jump, commit1 run with normal margins and EPS2=1e-14. Its nonterminal prefix windows in this comparison are within both routes' endpoints. Different choices produce different entry states.

## Outcome

No full solution returned. Commit3 stops before jump14 at tick90 at12015.44ms of trace time. Commit1 restarts from the original state and also stops before jump14 at tick90. Total solve invocation26005.618ms (26.006s).

The previous same fixed ladder with1000 iterations and a shared extension took4678.576ms (4.679s), and likewise stopped at tick90. These single instrumented runs are not a controlled performance benchmark; neither elapsed times nor changed candidates can be attributed solely to one of the two changes.

## Solve-type comparison, commit1

Within each window, CF attempts Z/MAX→Z/MIN→X/MAX→X/MIN, then centered SLP, stopping on success. A named method means earlier methods failed and later ones were not called. `7×` means all four CF objectives plus SLP failed on that nominal size. Numbers are nominal jump counts; individual objectives can use different actual endpoints in the new run. All fixed CF margins are0.0003.

| Starting jump | Successful normal margins | Fixed1000, shared extension | Fixed10000, per-objective extension |
|---:|---|---|---|
| 1 | 7: Z/MAX @0.05 | 7: Z/MAX | 7: Z/MAX |
| 2 | 7: Z/MAX @0.05 | 7: Z/MAX | 7: Z/MAX |
| 3 | 7: Z/MAX @0.02 | 7: Z/MAX | 7: Z/MAX |
| 4 | 7: Z/MAX @0.02 | 7: Z/MAX | 7: Z/MAX |
| 5 | 7: Z/MAX @0.02 | 7: Z/MAX | 7: Z/MAX |
| 6 | 7: Z/MIN @0.0003 | 7×<br>5: X/MAX | 7×<br>5: Z/MAX |
| 7 | 7: Z/MIN @0.01 | 7: SLP | 7×<br>5: Z/MAX |
| 8 | 7: Z/MIN @0.0003 | 7×<br>5: Z/MIN | 7×<br>5: SLP |
| 9 | 7: Z/MAX @0.0003 | 7×<br>5: Z/MIN | 7×<br>5: Z/MIN |
| 10 | 7: Z/MAX @0.0012 | 7×<br>5×<br>3: Z/MIN | 7×<br>5×<br>3: Z/MIN |
| 11 | 7: X/MAX @0.0012 | 7×<br>5×<br>3: Z/MIN | 7×<br>5×<br>3: Z/MIN |
| 12 | 7×<br>5: Z/MIN @0.0012 | 7×<br>5×<br>3×<br>2: Z/MIN | 7×<br>5×<br>3×<br>2: Z/MIN |
| 13 | 7: X/MIN @0.0003 | 7×<br>5×<br>3×<br>2×<br>1: Z/MAX | 7×<br>5×<br>3×<br>2×<br>1: Z/MAX |
| 14 | 7×<br>5: SLP | 7×<br>5×<br>3×<br>2×<br>1× | 7×<br>5×<br>3×<br>2×<br>1× |


### Changes from fixed1000

- Jumps1–5 remain seven-jump CF Z/MAX successes, matching the normal reference's method choices there.
- Jump6 still misses the seven-jump window, then the five-jump fallback now accepts CF Z/MAX instead of X/MAX.
- Jump7 now misses seven jumps and accepts five-jump Z/MAX, rather than seven-jump centered SLP.
- Jump8 now accepts centered SLP on five jumps, rather than CF Z/MIN on five jumps.
- Later nominal sizes/accepted methods remain as shown, ultimately shrinking to one-jump Z/MAX at jump13 and missing every window at jump14.

The first remaining method/window-size mismatch with the successful reference is still jump6. The reference accepts seven-jump Z/MIN@0.0003; the new run needs five-jump Z/MAX. No additional entry-state, margin or endpoint controls were run after this route probe.

## Per-start timing comparison

Each entry sums all attempted windows at that starting jump under commit1, including failed larger windows. These times come from LRS window timing lines and exclude a little outer slicing/trace overhead. They are observations from three separate runs, not paired benchmarks.

| Starting jump | Normal total window-attempt ms | Previous fixed1000 ms | New fixed10000 ms |
|---:|---:|---:|---:|
| 1 | 1.9 | 1.2 | 1.3 |
| 2 | 2.1 | 0.8 | 0.9 |
| 3 | 2.5 | 1.9 | 1.9 |
| 4 | 1.6 | 1.8 | 1.8 |
| 5 | 2.0 | 1.6 | 1.6 |
| 6 | 62.1 | 151.7 | 472.1 |
| 7 | 23.7 | 56.3 | 488.1 |
| 8 | 28.5 | 28.2 | 525.8 |
| 9 | 20.9 | 106.7 | 353.2 |
| 10 | 22.6 | 243.2 | 281.9 |
| 11 | 133.0 | 286.9 | 509.3 |
| 12 | 392.6 | 377.8 | 1644.3 |
| 13 | 59.9 | 364.7 | 1445.2 |
| 14 | 563.0 | 1216.3 | 8260.6 |

## Endpoints and validation

The nominal window is logged by `LRS-WIN`; each CF attempt's own end appears in `LRS-ADJUST`/its local objective tick; `LRS-SELECT` records the accepted end used for exact commit replay. The extraction checks every accepted new-window endpoint against LRS-SELECT. Centered SLP is listed as a feasibility/centering method, not as a Z/MAX optimization.

Shared validation still applies when the committed prefix is shorter than the chosen scenario: full continuous constraints with actual per-axis inertia, plus committed Minecraft replay constraints. Full-commit windows retain full Minecraft replay validation. This run changed neither validator semantics nor their tolerances.

## Evidence and recovery

- [Full new trace](fixed-seven-objective-windows-cap10000-2026-10-23/solver-trace.txt), including committed positions, velocities and yaws.
- [Every window attempt](fixed-seven-objective-windows-cap10000-2026-10-23/windows.csv), [every objective/SLP call](fixed-seven-objective-windows-cap10000-2026-10-23/objectives.csv), [every parsed CF rung with actual iteration count and timing](fixed-seven-objective-windows-cap10000-2026-10-23/cf-rungs.csv), [stop records](fixed-seven-objective-windows-cap10000-2026-10-23/stops.csv). Comparison rows retain source trace line numbers; objective rows include actual selected ends.
- [Settings](fixed-seven-objective-windows-cap10000-2026-10-23/metadata.txt), [focused test log](fixed-seven-objective-windows-cap10000-2026-10-23/focused-test.log), [reproducible extraction script](fixed-seven-objective-windows-cap10000-2026-10-23/compare_solve_types.py).
- Implementation: `0edbfbf` per-objective endpoint selection; `94d7a80`10000 iteration cap. No production edits were made during this rerun.
- [Saved successful normal reference](shared-inertia-validation-2026-10-23/normal7-commit1.txt), [previous fixed1000 trace](fixed-seven-forward-jump-cap1000-2026-10-23/solver-trace.txt).
