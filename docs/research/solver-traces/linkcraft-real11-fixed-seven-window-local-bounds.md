# Fixed seven-window rerun: temporary endpoint-bound removal

Code: `ae541ac`. Full real/11 route (808 ticks), fixed margin0.0003, windows7,5,3,2,1, commits3 then1, dual limit10000, EPS2=1e-24, no outer deadline. No window extension. Each coordinate objective removes matching endpoint caps from its temporary solve/validation spec; every new objective/window starts from original rows. Final full-route replay retains all original constraints. Only the focused `traceFullRouteFixedMarginSevenLadder` probe ran.

## Result

No complete route returned. Commit3 stops before jump14 at tick90 (trace10606.38ms). Commit1 reaches jump25 at tick196, then all7,5,3,2,1 windows fail. Total solve52691.357ms. Neither pass reaches final full-route validation, so this is a window-solving failure, not a final replay rejection. The previous per-objective-extension run stopped before jump14 for both commit counts and took26005.618ms. These are single instrumented runs and include different amounts of route solved.

## Commit1 comparison against the saved successful normal run

A number is the attempted window size in jumps. × means all four coordinate CF objectives and centered SLP failed. A named coordinate means CF; SLP means centered feasibility recovery. All new CF attempts use0.0003.

| Starting jump | Successful normal margins | Fixed0.0003, window-local bound removal |
|---:|---|---|
| 1 | 7: Z/MAX @0.05 | 7: Z/MAX |
| 2 | 7: Z/MAX @0.05 | 7: Z/MAX |
| 3 | 7: Z/MAX @0.02 | 7: Z/MAX |
| 4 | 7: Z/MAX @0.02 | 7: Z/MAX |
| 5 | 7: Z/MAX @0.02 | 7: Z/MAX |
| 6 | 7: Z/MIN @0.0003 | 7×<br>5: X/MAX |
| 7 | 7: Z/MIN @0.01 | 7: Z/MIN |
| 8 | 7: Z/MIN @0.0003 | 7×<br>5: Z/MIN |
| 9 | 7: Z/MAX @0.0003 | 7×<br>5×<br>3: Z/MAX |
| 10 | 7: Z/MAX @0.0012 | 7: X/MAX |
| 11 | 7: X/MAX @0.0012 | 7: Z/MAX |
| 12 | 7×<br>5: Z/MIN @0.0012 | 7: Z/MAX |
| 13 | 7: X/MIN @0.0003 | 7: Z/MIN |
| 14 | 7×<br>5: SLP | 7×<br>5: Z/MAX |
| 15 | 7×<br>5: Z/MIN @0.01 | 7: SLP |
| 16 | 7: Z/MAX @0.05 | 7×<br>5: Z/MAX |
| 17 | 7×<br>5: Z/MIN @0.01 | 7×<br>5×<br>3: Z/MIN |
| 18 | 7×<br>5: Z/MAX @0.02 | 7×<br>5: SLP |
| 19 | 7×<br>5×<br>3: Z/MAX @0.05 | 7×<br>5×<br>3×<br>2: SLP |
| 20 | 7×<br>5×<br>3: Z/MAX @0.02 | 7: SLP |
| 21 | 7×<br>5×<br>3×<br>2: Z/MAX @0.05 | 7×<br>5: SLP |
| 22 | 7×<br>5×<br>3: SLP | 7: SLP |
| 23 | 7×<br>5: SLP | 7: SLP |
| 24 | 7: terminal recovery | 7: SLP |
| 25 |  | 7×<br>5×<br>3×<br>2×<br>1× |

The normal reference is a 30-jump/250-tick route, whereas the new probe uses the full808 ticks. Normal's window starting at jump24 is terminal and solves jumps24–30 together; the new jump24 window is nonterminal. There are no separate normal solves starting at25–30. Earlier entry states also diverge, so this compares solve types rather than holding trajectories fixed. The first solve-type divergence remains jump6.

## Evidence

- [Raw new trace](fixed-seven-window-local-bounds-2026-10-23/solver-trace.txt), including all objective attempts, CF iterations, shared validation diagnostics, per-window timings and committed position/velocity/yaw.
- [Every window](fixed-seven-window-local-bounds-2026-10-23/windows.csv), [every objective attempt](fixed-seven-window-local-bounds-2026-10-23/objectives.csv), [CF rungs](fixed-seven-window-local-bounds-2026-10-23/cf-rungs.csv), [accepted-window timings](fixed-seven-window-local-bounds-2026-10-23/per-step-timing.md).
- [Saved normal reference](shared-inertia-validation-2026-10-23/normal7-commit1.txt).
- [Probe command/settings](fixed-seven-window-local-bounds-2026-10-23/metadata.txt), [background Gradle log](fixed-seven-window-local-bounds-2026-10-23/focused-test.log). Gradle succeeded; the probe records a solver miss without treating it as an assertion failure.
