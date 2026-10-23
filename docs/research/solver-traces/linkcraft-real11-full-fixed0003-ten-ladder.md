# Full Linkcraft real/11: standard 10-ladder, fixed margin, actual-inertia validation

Commit before experiment: `aac6527`. Input: `~/Downloads/LinkcraftII_Competition_01/real/11.json`. The original file is read only.

## Configuration

- Saved full endpoint: tick 808 (810 input rows), 636 compiled constraints, saved objective X/MAX at tick 808. No first-30-jump truncation.
- Window ladder: 10, 7, 5, 3, 2, 1 jumps. Commit attempts: 3, then restart at 1.
- Closed-form margin: only 0.0003, via `LongRunConfig.defaults().withFixedMargin(0.0003)`. This configures both closed-form ladders, including terminal recovery and alternate closed-form seeds; ordinary defaults outside this run remain unchanged. The existing SLP fallback and its centering policy are retained.
- Shared validation from `aac6527`: full continuous window with actual inertia threshold checks; Minecraft replay over the potentially committed prefix. Terminal/final validation remains full exact replay.
- No outer deadline (`deadlineNanos=0`). Internal iteration limits remain in force.
- Direct LongRunSolver probe, not a whole-graph search with other full-map fallback nodes.

## Outcome

**No solution returned.** Both commit attempts reach tick 90 after committing the first 13 jump segments, then fail on every window size starting at jump 14. The solver trace ends at 3829.48 ms. This is a solver miss, not a proof of infeasibility, and it was not caused by an outer timeout. No final yaw solution was generated.

The focused JUnit probe completed successfully because it records either solver outcome; a successful test invocation does not mean the route solved. No full suite was run. The source probe logs the imported input, every window/objective/margin attempt, validations, intermediate states/yaws, and timings.

## Every attempted window

Jump labels below are one-based and inclusive. Commit=1 restarts from the original entry state after commit=3 fails.

| Commit count | Jumps attempted | Global ticks | Replay inputs | Result | Window ms |
|---:|---|---|---:|---|---:|
| 3 | 1–10 | [0,77) | 30 | solved | 86.3 |
| 3 | 4–13 | [30,90) | 34 | solved | 301.8 |
| 3 | 7–16 | [64,117) | 11 | miss | 160.0 |
| 3 | 7–13 | [64,90) | 11 | solved | 2.6 |
| 3 | 10–19 | [75,132) | 13 | miss | 150.8 |
| 3 | 10–16 | [75,117) | 13 | miss | 67.3 |
| 3 | 10–14 | [75,99) | 13 | miss | 1.9 |
| 3 | 10–12 | [75,88) | 13 | solved | 0.7 |
| 3 | 13–22 | [88,176) | 25 | miss | 209.6 |
| 3 | 13–19 | [88,132) | 25 | miss | 43.0 |
| 3 | 13–17 | [88,128) | 25 | miss | 34.8 |
| 3 | 13–15 | [88,113) | 25 | miss | 9.9 |
| 3 | 13–14 | [88,99) | 11 | miss | 5.8 |
| 3 | 13–13 | [88,90) | 2 | solved | 0.1 |
| 3 | 14–23 | [90,187) | 27 | miss | 175.5 |
| 3 | 14–20 | [90,155) | 27 | miss | 79.3 |
| 3 | 14–18 | [90,130) | 27 | miss | 39.4 |
| 3 | 14–16 | [90,117) | 27 | miss | 4.6 |
| 3 | 14–15 | [90,113) | 23 | miss | 0.7 |
| 3 | 14–14 | [90,99) | 9 | miss | 0.3 |
| 1 | 1–10 | [0,77) | 12 | solved | 7.4 |
| 1 | 2–11 | [12,80) | 9 | solved | 29.1 |
| 1 | 3–12 | [21,88) | 9 | solved | 85.4 |
| 1 | 4–13 | [30,90) | 10 | solved | 84.8 |
| 1 | 5–14 | [40,99) | 15 | miss | 109.8 |
| 1 | 5–11 | [40,80) | 15 | solved | 12.2 |
| 1 | 6–15 | [55,113) | 9 | miss | 133.7 |
| 1 | 6–12 | [55,88) | 9 | solved | 24.9 |
| 1 | 7–16 | [64,117) | 4 | miss | 113.5 |
| 1 | 7–13 | [64,90) | 4 | solved | 1.7 |
| 1 | 8–17 | [68,128) | 3 | miss | 129.0 |
| 1 | 8–14 | [68,99) | 3 | miss | 4.1 |
| 1 | 8–12 | [68,88) | 3 | solved | 0.8 |
| 1 | 9–18 | [71,130) | 4 | miss | 162.4 |
| 1 | 9–15 | [71,113) | 4 | miss | 42.7 |
| 1 | 9–13 | [71,90) | 4 | solved | 1.1 |
| 1 | 10–19 | [75,132) | 2 | miss | 142.0 |
| 1 | 10–16 | [75,117) | 2 | miss | 61.4 |
| 1 | 10–14 | [75,99) | 2 | miss | 1.4 |
| 1 | 10–12 | [75,88) | 2 | solved | 0.4 |
| 1 | 11–20 | [77,155) | 3 | miss | 215.9 |
| 1 | 11–17 | [77,128) | 3 | miss | 55.5 |
| 1 | 11–15 | [77,113) | 3 | miss | 20.9 |
| 1 | 11–13 | [77,90) | 3 | solved | 0.5 |
| 1 | 12–21 | [80,167) | 8 | miss | 295.1 |
| 1 | 12–18 | [80,130) | 8 | miss | 70.9 |
| 1 | 12–16 | [80,117) | 8 | miss | 36.2 |
| 1 | 12–14 | [80,99) | 8 | miss | 0.6 |
| 1 | 12–13 | [80,90) | 8 | solved | 0.5 |
| 1 | 13–22 | [88,176) | 2 | miss | 181.3 |
| 1 | 13–19 | [88,132) | 2 | miss | 48.0 |
| 1 | 13–17 | [88,128) | 2 | miss | 18.2 |
| 1 | 13–15 | [88,113) | 2 | miss | 5.3 |
| 1 | 13–14 | [88,99) | 2 | miss | 0.2 |
| 1 | 13–13 | [88,90) | 2 | solved | 0.1 |
| 1 | 14–23 | [90,187) | 9 | miss | 219.7 |
| 1 | 14–20 | [90,155) | 9 | miss | 63.8 |
| 1 | 14–18 | [90,130) | 9 | miss | 48.7 |
| 1 | 14–16 | [90,117) | 9 | miss | 10.3 |
| 1 | 14–15 | [90,113) | 9 | miss | 0.5 |
| 1 | 14–14 | [90,99) | 9 | miss | 0.1 |

## Reproduction and sources

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.fullRouteProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.FullRouteFixedMarginTraceTest
```

Run in the background with output redirected to a file. `PKC_TRACE_INPUT` overrides the capture path. The test is opt-in and tagged SlowSolverTests.

- [Full raw trace](full-fixed0003-ten-ladder-2026-10-23/solver-trace.txt)
- [Window table CSV](full-fixed0003-ten-ladder-2026-10-23/window-attempts.csv)
- [Objective calls and timings](full-fixed0003-ten-ladder-2026-10-23/objective-attempts.csv)
- [Probe command log](full-fixed0003-ten-ladder-2026-10-23/probe.log)
- [JUnit result](full-fixed0003-ten-ladder-2026-10-23/probe-junit.xml)

Next investigation: compare the accepted tick-88/tick-90 states and which objectives produced them against the successful normal-margin 7-ladder run, then inspect the first failed jump-14 window's continuous and prefix-replay violations separately.

## Comparison with the immediately preceding 30-jump run

The run immediately before this probe **succeeded** on 250 ticks. It used the normal robust margin ladder and windows 7,5,3,2,1 with commit=1. It did not use fixed margin 0.0003. Both use the same shared actual-inertia validation. The earlier 30-jump failure at index 13 used the superseded fixed-pattern continuous check; do not confuse that with the successful shared-helper run.

| Setting | Successful 30-jump probe | Full-route probe |
|---|---|---|
| Endpoint | 250 | 808 |
| Windows | 7,5,3,2,1 | 10,7,5,3,2,1 |
| Commit attempts | 1 | 3 then 1 |
| Closed-form margins | Normal ladder | Only 0.0003 |
| First accepted objective | Z/MAX at local tick 68, margin 0.05 | X/MAX at local tick 77, margin 0.0003 |

Even the full-route commit=1 retry diverges on its first window: Z/MAX and Z/MIN miss, then X/MAX succeeds. Its tick-12 state is X=743.716245683098, Z=-1103.682285411922, VX=-0.222463944016, VZ=0.069311167258, carried yaw=115.161872864. The successful 30-jump run's tick-12 state is X=743.707005403898, Z=-1103.714623692396, VX=-0.223148328561, VZ=0.067035152688, carried yaw=115.534652710. Thus these runs do not share a solved prefix before jump 14.

Sources: the first window and following `LRS-WIN` in [the full trace](full-fixed0003-ten-ladder-2026-10-23/solver-trace.txt), and the first 22 lines of [the successful 30-jump trace](shared-inertia-validation-2026-10-23/normal7-commit1.txt). Changing endpoint alone has not been isolated; this comparison cannot attribute the failure to route length, window size, or margin individually. Next controls would hold windows and commits fixed while changing only margins, then only endpoint.


[Expanded table: every objective result and timing](linkcraft-real11-full-fixed0003-all-objectives.md). Fixed margin remains a user requirement for subsequent probes. The existing SLP fallback is centered feasibility, not phase-2 objective improvement; fixed closed-form margin alone does not make every committed prefix objective optimized.


[Side-by-side comparison with the successful normal-margin run](linkcraft-real11-normal-success-vs-fixed-full.md) now lists all attempted window sizes, selected methods/margins, and summed attempt times for every starting jump.
