# Successful normal-margin route versus fixed-margin full route

This compares commit=1 in both runs. The fixed run is its restart after commit=3 failed. Both use shared actual-inertia continuous validation and prefix-only Minecraft replay checks. They differ in margins, window ladder, and endpoint: normal is the successful 250-tick/30-jump probe with windows 7,5,3,2,1; fixed is the 808-tick probe with windows 10,7,5,3,2,1. This is not a controlled margin-only comparison.

All rows are actual saved outcomes, not inferred from the earlier traces that used different validation. “Miss” means all four closed-form objectives plus centered SLP failed for that window. Listed sizes are attempted in order; the last succeeds unless every one is marked miss. Durations below sum every attempted window at that start, not just the successful one. These runs have different warmup/load, so do not infer relative solver performance from them.

| Starting jump | Normal window sizes/results | Normal accepted candidate | Normal total ms | Fixed window sizes/results | Fixed accepted candidate | Fixed total ms |
|---:|---|---|---:|---|---|---:|
| 1 | 7 pass | Z/MAX @ 0.05 | 1.9 | 10 pass | X/MAX @ 0.0003 | 7.4 |
| 2 | 7 pass | Z/MAX @ 0.05 | 2.1 | 10 pass | centered SLP | 29.1 |
| 3 | 7 pass | Z/MAX @ 0.02 | 2.5 | 10 pass | centered SLP | 85.4 |
| 4 | 7 pass | Z/MAX @ 0.02 | 1.6 | 10 pass | centered SLP | 84.8 |
| 5 | 7 pass | Z/MAX @ 0.02 | 2.0 | 10 miss → 7 pass | centered SLP | 122.0 |
| 6 | 7 pass | Z/MIN @ 0.0003 | 62.1 | 10 miss → 7 pass | centered SLP | 158.6 |
| 7 | 7 pass | Z/MIN @ 0.01 | 23.7 | 10 miss → 7 pass | Z/MIN @ 0.0003 | 115.2 |
| 8 | 7 pass | Z/MIN @ 0.0003 | 28.5 | 10 miss → 7 miss → 5 pass | Z/MIN @ 0.0003 | 133.9 |
| 9 | 7 pass | Z/MAX @ 0.0003 | 20.9 | 10 miss → 7 miss → 5 pass | Z/MIN @ 0.0003 | 206.2 |
| 10 | 7 pass | Z/MAX @ 0.0012 | 22.6 | 10 miss → 7 miss → 5 miss → 3 pass | Z/MIN @ 0.0003 | 205.2 |
| 11 | 7 pass | X/MAX @ 0.0012 | 133.0 | 10 miss → 7 miss → 5 miss → 3 pass | Z/MIN @ 0.0003 | 292.8 |
| 12 | 7 miss → 5 pass | Z/MIN @ 0.0012 | 392.6 | 10 miss → 7 miss → 5 miss → 3 miss → 2 pass | Z/MIN @ 0.0003 | 403.3 |
| 13 | 7 pass | X/MIN @ 0.0003 | 59.9 | 10 miss → 7 miss → 5 miss → 3 miss → 2 miss → 1 pass | Z/MAX @ 0.0003 | 253.1 |
| 14 | 7 miss → 5 pass | centered SLP | 563.0 | 10 miss → 7 miss → 5 miss → 3 miss → 2 miss → 1 miss | none | 343.1 |
| 15 | 7 miss → 5 pass | Z/MIN @ 0.01 | 670.8 | not reached | — | — |
| 16 | 7 pass | Z/MAX @ 0.05 | 33.2 | not reached | — | — |
| 17 | 7 miss → 5 pass | Z/MIN @ 0.01 | 585.9 | not reached | — | — |
| 18 | 7 miss → 5 pass | Z/MAX @ 0.02 | 545.2 | not reached | — | — |
| 19 | 7 miss → 5 miss → 3 pass | Z/MAX @ 0.05 | 1361.3 | not reached | — | — |
| 20 | 7 miss → 5 miss → 3 pass | Z/MAX @ 0.02 | 1396.4 | not reached | — | — |
| 21 | 7 miss → 5 miss → 3 miss → 2 pass | Z/MAX @ 0.05 | 531.4 | not reached | — | — |
| 22 | 7 miss → 5 miss → 3 pass | centered SLP | 393.8 | not reached | — | — |
| 23 | 7 miss → 5 pass | centered SLP | 517.2 | not reached | — | — |
| 24 | 7 pass | terminal recovery + polish | 369.2 | not reached | — | — |

The normal run's final window starts at jump 24 and commits jumps 24–30 together. Its later starting-jump rows therefore do not exist. The fixed run stops before jump 14 at tick 90.

The normal run first uses centered SLP at starting jump 14 (after its 7-jump window fails, its 5-jump window succeeds). The fixed run uses centered SLP as early as starting jump 2. Fixed closed-form margin therefore does not imply that every accepted prefix is objective optimized: centered SLP is still a feasibility/centering fallback.

Sources: [normal full trace](shared-inertia-validation-2026-10-23/normal7-commit1.txt), [fixed full trace](full-fixed0003-ten-ladder-2026-10-23/solver-trace.txt), [fixed every-objective table](linkcraft-real11-full-fixed0003-all-objectives.md).
