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

## Why jump6 Z/MIN differs despite an accepted margin of0.0003

The endpoint is identical (states55–88), but the optimization problems and execution paths differ. Normal has53 rows; fixed drops `Z@88lo >= -1090.300000011921`, leaving52. Thus normal minimizes toward a retained lower wall, whereas fixed can continue below it. Entry state normal: X730.338594178782,Z-1096.094697424725,VX-0.226905272350,VZ0.168494581577,yaw86.486671448. Fixed: X730.296913235570,Z-1096.123854818772,VX-0.233829877256,VZ0.159910783797,yaw93.870376587.

Normal's unpatterned Z/MIN ladder reaches0.0003 with replay0 but continuous violation0.540808411714; it does NOT pass there. The next0-margin rung has violation0.0421825973708 and supplies the best candidate for inertia discovery. Pass1 fixes local X8,X26 (global X63,X81), restarts the margin ladder, and finally passes0.0003 in71 iterations with replay0 and continuous0. Each rung warm-starts the next rung's multipliers (`ClosedFormSolve.runLadder`), so its accepted0.0003 is not a cold standalone0.0003 solve.

Fixed has one cold0.0003 rung. It stops at1264 iterations with `RECOVERED_INPUT_STALL`, projected residual0.0394, replay-prefix violation0.0113403853090, full continuous violation1.50530499057. Its candidate produces an empty effective inertia pattern (`pattern free ineffective`), so no inertia-pattern retry occurs. Input-stall means the recovered dual controls stopped changing; it is not a feasibility certificate. This is not a max10000-iteration failure. Normal also used cap100/EPS2=1e-14; fixed uses10000/1e-24.

Sources: normal trace lines102–181 (Z/MIN begins149); fixed trace lines1907–1934 (Z/MIN begins1928). These establish the different trajectories and actual stop, but do not isolate which change causes it; no new control run was made.
