# Full route: 7-window ladder, fixed 0.0003 margin

This changes only the window ladder relative to the full-route 10-ladder probe in `ff44792`: windows are now 7,5,3,2,1. Both runs use commit 3 then 1, the saved 808-tick endpoint with 636 constraints, fixed closed-form margin 0.0003, no outer deadline, and the shared actual-inertia continuous/full-window plus Minecraft replay/committed-prefix validation. SLP fallback remains centered feasibility.

**Outcome: no solution.** Both commit passes again stall at tick 90 before jump 14, after committing the first 13 segments. Total trace elapsed time 1512.21 ms. The corresponding fixed 10-ladder run took 3829.48 ms and also stalled there. These separate instrumented runtimes are not a controlled performance benchmark. Equal failure locations do not establish equal committed states.

The one focused probe completed; no full suite was run. A solver miss is recorded as an experimental outcome and does not fail the probe. Original normal-margin traces remain untouched.

## Every window and objective attempt

Jump numbers are one-based and inclusive. Cells show outcome / milliseconds; dash means not attempted after an earlier objective succeeded. Every closed-form attempt uses only margin 0.0003. An objective may contain multiple inertia passes. All corresponding continuous/replay violations and rung timings remain in the raw trace.

### Commit 3

| Jumps | Ticks | Z/MAX | Z/MIN | X/MAX | X/MIN | SLP | Total ms |
|---|---|---|---|---|---|---|---:|
| 1–7 | 0–68 | miss / 60.714 | **pass** / 4.913 | — | — | — | 66.1 |
| 4–10 | 30–77 | miss / 6.332 | miss / 3.476 | miss / 5.665 | miss / 3.589 | **pass** / 26.487 | 45.7 |
| 7–13 | 64–90 | miss / 7.929 | **pass** / 5.554 | — | — | — | 13.5 |
| 10–16 | 75–117 | miss / 13.627 | miss / 7.447 | miss / 7.310 | miss / 4.334 | miss / 110.561 | 143.4 |
| 10–14 | 75–99 | miss / 0.451 | miss / 3.019 | miss / 1.104 | miss / 0.525 | miss / 0.505 | 5.7 |
| 10–12 | 75–88 | miss / 0.466 | **pass** / 1.511 | — | — | — | 2.0 |
| 13–19 | 88–132 | miss / 10.645 | miss / 11.883 | miss / 8.976 | miss / 4.036 | miss / 72.508 | 108.1 |
| 13–17 | 88–128 | miss / 2.806 | miss / 4.154 | miss / 3.853 | miss / 3.970 | miss / 19.683 | 34.5 |
| 13–15 | 88–113 | miss / 4.571 | miss / 1.822 | miss / 0.463 | miss / 0.506 | miss / 8.730 | 16.1 |
| 13–14 | 88–99 | miss / 0.397 | miss / 0.239 | miss / 0.317 | miss / 0.179 | miss / 3.873 | 5.0 |
| 13–13 | 88–90 | **pass** / 0.315 | — | — | — | — | 0.3 |
| 14–20 | 90–155 | miss / 25.684 | miss / 14.294 | miss / 13.687 | miss / 13.299 | miss / 41.785 | 108.8 |
| 14–18 | 90–130 | miss / 26.400 | miss / 13.425 | miss / 23.491 | miss / 6.331 | miss / 52.914 | 122.6 |
| 14–16 | 90–117 | miss / 3.232 | miss / 0.334 | miss / 0.627 | miss / 0.548 | miss / 0.497 | 5.3 |
| 14–15 | 90–113 | miss / 0.187 | miss / 0.156 | miss / 0.080 | miss / 0.248 | miss / 0.127 | 0.8 |
| 14–14 | 90–99 | miss / 0.046 | miss / 0.039 | miss / 0.037 | miss / 0.037 | miss / 0.013 | 0.2 |

### Commit 1

| Jumps | Ticks | Z/MAX | Z/MIN | X/MAX | X/MIN | SLP | Total ms |
|---|---|---|---|---|---|---|---:|
| 1–7 | 0–68 | miss / 1.586 | **pass** / 0.724 | — | — | — | 2.3 |
| 2–8 | 12–71 | **pass** / 1.344 | — | — | — | — | 1.4 |
| 3–9 | 21–75 | **pass** / 1.701 | — | — | — | — | 1.7 |
| 4–10 | 30–77 | miss / 1.871 | **pass** / 6.046 | — | — | — | 7.9 |
| 5–11 | 40–80 | miss / 1.989 | miss / 2.871 | miss / 1.736 | miss / 1.124 | **pass** / 13.309 | 21.3 |
| 6–12 | 55–88 | miss / 2.012 | miss / 2.389 | miss / 4.015 | miss / 4.980 | **pass** / 9.518 | 23.0 |
| 7–13 | 64–90 | miss / 0.993 | **pass** / 0.741 | — | — | — | 1.8 |
| 8–14 | 68–99 | miss / 1.556 | miss / 2.799 | miss / 1.683 | miss / 2.050 | miss / 52.259 | 60.4 |
| 8–12 | 68–88 | miss / 0.644 | **pass** / 0.301 | — | — | — | 1.0 |
| 9–15 | 71–113 | miss / 5.120 | miss / 1.200 | miss / 4.229 | miss / 4.167 | miss / 39.169 | 54.0 |
| 9–13 | 71–90 | miss / 3.756 | **pass** / 0.439 | — | — | — | 4.2 |
| 10–16 | 75–117 | miss / 4.004 | miss / 1.612 | miss / 4.193 | miss / 6.593 | miss / 51.920 | 68.4 |
| 10–14 | 75–99 | miss / 0.288 | miss / 0.205 | miss / 1.280 | miss / 0.232 | miss / 11.589 | 13.6 |
| 10–12 | 75–88 | miss / 0.208 | **pass** / 0.230 | — | — | — | 0.5 |
| 11–17 | 77–128 | miss / 2.824 | miss / 8.093 | miss / 6.367 | miss / 4.236 | miss / 71.218 | 92.8 |
| 11–15 | 77–113 | miss / 4.188 | miss / 1.938 | miss / 3.514 | miss / 2.339 | miss / 24.435 | 36.5 |
| 11–13 | 77–90 | miss / 0.769 | **pass** / 0.223 | — | — | — | 1.0 |
| 12–18 | 80–130 | miss / 8.872 | miss / 7.017 | miss / 11.463 | miss / 6.784 | miss / 124.875 | 159.1 |
| 12–16 | 80–117 | miss / 2.760 | miss / 4.032 | miss / 8.559 | miss / 4.117 | miss / 24.469 | 44.0 |
| 12–14 | 80–99 | miss / 0.137 | miss / 0.069 | miss / 0.197 | miss / 0.131 | miss / 0.076 | 0.6 |
| 12–13 | 80–90 | miss / 0.638 | **pass** / 0.232 | — | — | — | 0.9 |
| 13–19 | 88–132 | miss / 4.755 | miss / 12.089 | miss / 12.179 | miss / 5.802 | miss / 24.348 | 59.2 |
| 13–17 | 88–128 | miss / 1.555 | miss / 2.905 | miss / 2.033 | miss / 3.139 | miss / 12.998 | 22.7 |
| 13–15 | 88–113 | miss / 0.334 | miss / 0.541 | miss / 0.646 | miss / 0.505 | miss / 6.118 | 8.2 |
| 13–14 | 88–99 | miss / 0.066 | miss / 0.038 | miss / 0.046 | miss / 0.037 | miss / 0.018 | 0.2 |
| 13–13 | 88–90 | **pass** / 0.071 | — | — | — | — | 0.1 |
| 14–20 | 90–155 | miss / 18.073 | miss / 11.270 | miss / 14.145 | miss / 13.111 | miss / 22.972 | 79.6 |
| 14–18 | 90–130 | miss / 7.044 | miss / 5.884 | miss / 14.649 | miss / 6.481 | miss / 13.201 | 47.3 |
| 14–16 | 90–117 | miss / 0.622 | miss / 1.194 | miss / 0.655 | miss / 0.184 | miss / 0.466 | 3.1 |
| 14–15 | 90–113 | miss / 0.091 | miss / 0.205 | miss / 0.071 | miss / 0.077 | miss / 0.078 | 0.6 |
| 14–14 | 90–99 | miss / 0.035 | miss / 0.030 | miss / 0.030 | miss / 0.030 | miss / 0.010 | 0.2 |

## Reproduce

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest.traceFullRouteFixedMarginSevenLadder
```

Run backgrounded with output redirected to a file. Sources: [raw trace](full-fixed0003-seven-ladder-2026-10-23/solver-trace.txt), [window/objective CSV](full-fixed0003-seven-ladder-2026-10-23/window-attempts.csv), [command log](full-fixed0003-seven-ladder-2026-10-23/probe.log), [JUnit result](full-fixed0003-seven-ladder-2026-10-23/probe-junit.xml).
