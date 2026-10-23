# Every objective attempt: full-route fixed-margin 10-ladder

All closed-form attempts use only margin **0.0003**. Validation uses full continuous simulation with actual inertia threshold checks and committed-prefix Minecraft replay. Jumps are one-based, inclusive; ticks are global half-open ranges. Cells show outcome and elapsed milliseconds. A dash means not attempted because an earlier objective succeeded. Each objective call may contain multiple inertia passes; their complete rung timings and continuous/replay violations remain in the raw trace. SLP retains its existing centering policy; it does not use the closed-form margin.

## Commit 3

| Jumps | Ticks | Z/MAX | Z/MIN | X/MAX | X/MIN | SLP | Total ms |
|---|---|---|---|---|---|---|---:|
| 1–10 | 0–77 | miss 63.0 | miss 14.5 | **pass** 8.4 | — | — | 86.3 |
| 4–13 | 30–90 | miss 31.9 | miss 69.3 | miss 8.1 | miss 10.4 | **pass** 182.0 | 301.8 |
| 7–16 | 64–117 | miss 9.9 | miss 6.1 | miss 6.9 | miss 20.4 | miss 116.6 | 160.0 |
| 7–13 | 64–90 | miss 1.6 | **pass** 0.9 | — | — | — | 2.6 |
| 10–19 | 75–132 | miss 15.1 | miss 6.0 | miss 19.0 | miss 13.0 | miss 97.6 | 150.8 |
| 10–16 | 75–117 | miss 4.2 | miss 2.0 | miss 6.2 | miss 8.4 | miss 46.4 | 67.3 |
| 10–14 | 75–99 | miss 0.4 | miss 0.3 | miss 0.3 | miss 0.3 | miss 0.4 | 1.9 |
| 10–12 | 75–88 | miss 0.4 | **pass** 0.3 | — | — | — | 0.7 |
| 13–22 | 88–176 | miss 47.2 | miss 34.3 | miss 17.2 | miss 17.4 | miss 93.5 | 209.6 |
| 13–19 | 88–132 | miss 9.8 | miss 4.3 | miss 8.8 | miss 4.0 | miss 16.1 | 43.0 |
| 13–17 | 88–128 | miss 8.6 | miss 4.8 | miss 1.9 | miss 3.8 | miss 15.7 | 34.8 |
| 13–15 | 88–113 | miss 0.8 | miss 0.4 | miss 1.5 | miss 1.0 | miss 6.1 | 9.9 |
| 13–14 | 88–99 | miss 0.4 | miss 1.6 | miss 0.2 | miss 0.2 | miss 3.4 | 5.8 |
| 13–13 | 88–90 | **pass** 0.1 | — | — | — | — | 0.1 |
| 14–23 | 90–187 | miss 8.3 | miss 42.2 | miss 36.0 | miss 32.6 | miss 56.3 | 175.5 |
| 14–20 | 90–155 | miss 11.1 | miss 11.4 | miss 20.4 | miss 11.9 | miss 24.5 | 79.3 |
| 14–18 | 90–130 | miss 3.0 | miss 5.8 | miss 6.0 | miss 6.3 | miss 18.2 | 39.4 |
| 14–16 | 90–117 | miss 2.3 | miss 0.5 | miss 0.8 | miss 0.3 | miss 0.6 | 4.6 |
| 14–15 | 90–113 | miss 0.1 | miss 0.2 | miss 0.1 | miss 0.1 | miss 0.1 | 0.7 |
| 14–14 | 90–99 | miss 0.1 | miss 0.1 | miss 0.1 | miss 0.1 | miss <0.1 | 0.3 |

## Commit 1

| Jumps | Ticks | Z/MAX | Z/MIN | X/MAX | X/MIN | SLP | Total ms |
|---|---|---|---|---|---|---|---:|
| 1–10 | 0–77 | miss 1.6 | miss 4.4 | **pass** 1.4 | — | — | 7.4 |
| 2–11 | 12–80 | miss 4.6 | miss 9.4 | miss 3.6 | miss 3.9 | **pass** 7.5 | 29.1 |
| 3–12 | 21–88 | miss 17.7 | miss 6.2 | miss 3.9 | miss 10.4 | **pass** 47.0 | 85.4 |
| 4–13 | 30–90 | miss 8.9 | miss 21.4 | miss 15.7 | miss 10.8 | **pass** 28.0 | 84.8 |
| 5–14 | 40–99 | miss 18.9 | miss 11.2 | miss 12.0 | miss 9.6 | miss 58.0 | 109.8 |
| 5–11 | 40–80 | miss 2.1 | miss 2.8 | miss 1.9 | miss 1.1 | **pass** 4.2 | 12.2 |
| 6–15 | 55–113 | miss 9.4 | miss 8.8 | miss 7.2 | miss 14.4 | miss 93.9 | 133.7 |
| 6–12 | 55–88 | miss 3.0 | miss 3.8 | miss 4.4 | miss 3.0 | **pass** 10.7 | 24.9 |
| 7–16 | 64–117 | miss 14.1 | miss 9.5 | miss 3.8 | miss 9.3 | miss 76.7 | 113.5 |
| 7–13 | 64–90 | miss 0.9 | **pass** 0.8 | — | — | — | 1.7 |
| 8–17 | 68–128 | miss 3.9 | miss 13.0 | miss 3.9 | miss 18.1 | miss 90.0 | 129.0 |
| 8–14 | 68–99 | miss 0.6 | miss 1.6 | miss 0.7 | miss 0.4 | miss 0.7 | 4.1 |
| 8–12 | 68–88 | miss 0.6 | **pass** 0.1 | — | — | — | 0.8 |
| 9–18 | 71–130 | miss 16.4 | miss 13.8 | miss 16.5 | miss 21.4 | miss 94.4 | 162.4 |
| 9–15 | 71–113 | miss 3.5 | miss 2.5 | miss 2.7 | miss 6.3 | miss 27.8 | 42.7 |
| 9–13 | 71–90 | miss 0.6 | **pass** 0.5 | — | — | — | 1.1 |
| 10–19 | 75–132 | miss 30.4 | miss 1.5 | miss 16.4 | miss 12.9 | miss 80.8 | 142.0 |
| 10–16 | 75–117 | miss 4.7 | miss 1.8 | miss 6.1 | miss 8.8 | miss 39.9 | 61.4 |
| 10–14 | 75–99 | miss 0.3 | miss 0.2 | miss 0.3 | miss 0.3 | miss 0.3 | 1.4 |
| 10–12 | 75–88 | miss 0.2 | **pass** 0.2 | — | — | — | 0.4 |
| 11–20 | 77–155 | miss 18.1 | miss 19.2 | miss 31.9 | miss 14.4 | miss 132.2 | 215.9 |
| 11–17 | 77–128 | miss 2.7 | miss 9.0 | miss 3.0 | miss 8.6 | miss 32.1 | 55.5 |
| 11–15 | 77–113 | miss 1.7 | miss 1.4 | miss 1.7 | miss 2.4 | miss 13.6 | 20.9 |
| 11–13 | 77–90 | miss 0.4 | **pass** 0.1 | — | — | — | 0.5 |
| 12–21 | 80–167 | miss 30.1 | miss 44.4 | miss 13.7 | miss 24.8 | miss 182.0 | 295.1 |
| 12–18 | 80–130 | miss 9.8 | miss 10.6 | miss 8.2 | miss 3.6 | miss 38.6 | 70.9 |
| 12–16 | 80–117 | miss 2.0 | miss 1.2 | miss 3.0 | miss 2.3 | miss 27.8 | 36.2 |
| 12–14 | 80–99 | miss 0.1 | miss 0.1 | miss 0.2 | miss 0.1 | miss 0.1 | 0.6 |
| 12–13 | 80–90 | miss 0.5 | **pass** <0.1 | — | — | — | 0.5 |
| 13–22 | 88–176 | miss 18.3 | miss 10.5 | miss 25.0 | miss 25.2 | miss 102.1 | 181.3 |
| 13–19 | 88–132 | miss 6.3 | miss 10.7 | miss 6.0 | miss 6.3 | miss 18.6 | 48.0 |
| 13–17 | 88–128 | miss 1.6 | miss 4.2 | miss 1.8 | miss 2.8 | miss 7.8 | 18.2 |
| 13–15 | 88–113 | miss 0.3 | miss 0.4 | miss 0.6 | miss 0.3 | miss 3.7 | 5.3 |
| 13–14 | 88–99 | miss <0.1 | miss <0.1 | miss <0.1 | miss <0.1 | miss <0.1 | 0.2 |
| 13–13 | 88–90 | **pass** 0.1 | — | — | — | — | 0.1 |
| 14–23 | 90–187 | miss 21.3 | miss 42.8 | miss 57.8 | miss 16.2 | miss 81.5 | 219.7 |
| 14–20 | 90–155 | miss 6.2 | miss 14.1 | miss 7.7 | miss 15.7 | miss 20.1 | 63.8 |
| 14–18 | 90–130 | miss 13.0 | miss 6.8 | miss 6.5 | miss 10.5 | miss 11.9 | 48.7 |
| 14–16 | 90–117 | miss 1.7 | miss 1.9 | miss 0.4 | miss 0.2 | miss 6.0 | 10.3 |
| 14–15 | 90–113 | miss 0.1 | miss 0.1 | miss 0.1 | miss 0.2 | miss 0.1 | 0.5 |
| 14–14 | 90–99 | miss <0.1 | miss <0.1 | miss <0.1 | miss <0.1 | miss <0.1 | 0.1 |

Source: [complete raw trace](full-fixed0003-ten-ladder-2026-10-23/solver-trace.txt). Both passes stop before jump 14 at tick 90; total elapsed solver trace time is 3829.48 ms. No outer deadline.

The user explicitly requires keeping fixed margin for subsequent probes to avoid committing early candidates from the robust margin ladder. Note a remaining distinction: LongRun fallback calls `SlpSolve.optimizeCentered`, which runs centering/feasibility rather than its phase-2 objective improvement (`hugObjective=false`). The SLP-pass cells therefore do not certify objective-optimized prefixes. This behavior has not been changed.
