# Linkcraft real/11 margin investigation handoff

Date: 2026-10-23  
Branch: `codex/first30-margin-trace-bookkeeping`  
Current HEAD: `f980e29` (`experiment(solver): record normal-margin 7-window trace`)

## Question under investigation

For the first 30 jumps of `~/Downloads/LinkcraftII_Competition_01/real/11.json`, compare LongRunSolver with its normal robust-margin ladder against a run fixed at margin `0.0003`. Both runs use `commit=1` and window ladder `7,5,3,2,1`. The practical question is why one run continues while the other fails, especially around zero-based start indices 11–13 (one-based jumps 12–14).

## Results established by the saved traces

- The normal-margin run solved all 30 jumps in about 7.1 seconds. Its report and objective-by-objective CSV are `linkcraft-real11-first30-normal7-commit1.md` and `linkcraft-real11-first30-normal7-commit1-objectives.csv` in this directory.
- The fixed-`0.0003` run committed through zero-based jump index 12, then failed at index 13 after the 7-, 5-, 3-, 2-, and 1-jump windows all missed. Its raw log is `core/build/reports/solver-trace-linkcraft-real11-first30-single-margin-7.txt` (an ignored build artifact, so preserve/copy it before cleaning `core/build`).
- My previous chat table incorrectly said the normal run solved at index 9 with `Z/MIN` at margin 0.0. The CSV corrects this: window `[9,16)` solved with `Z/MAX`, using inertia pattern `x@9-15+z@7`, at margin `0.0012`. At `0.0003`, that pattern had a replay violation of `0.01739`; the `0.0012` rung certified. Source: the normal objective CSV row for window `[9,16)`, objective `primary Z/MAX@42`.
- At zero-based index 11, the normal run solved window `[11,16)` (five jumps) with `Z/MIN`; inertia pattern `x@4-10+z@2` certified at `0.0012`. The fixed run attempted `[11,16)` at `0.0003`; `Z/MAX`, `Z/MIN`, `X/MAX`, and `X/MIN` all missed. It then solved the smaller `[11,13)` two-jump window with `Z/MIN` at `0.0003`.
- These were **not the same five-jump optimization problem**. The normal run entered index 11 at tick 80 with `(x,z;vx,vz)=(732.813498656565,-1088.466968184592; 0.102282870380,0.270883459243)`. The fixed run entered with `(732.819704258099,-1088.639926044153; 0.108648592033,0.270482712973)`. Earlier commits had already changed the state. Therefore the existing traces do not isolate a margin-only cause.
- At index 12, the normal run solved `[12,19)` via `X/MIN` at `0.0003`; fixed margin tried that window and the smaller 5-, 3-, and 2-jump windows unsuccessfully, then solved the 1-jump `[12,13)` window. At index 13, fixed margin failed every ladder window. Normal margin solved `[13,18)` through fallback SLP after all four closed-form objectives missed.

## Committed boundary states: normal vs fixed margin

Jump numbers below are one-based. Tick values use the solver's zero-based indexing. Each row is the last logged committed state for that jump; positions and velocities are `(x,z)` and `(vx,vz)`. The trace's path records are `LRS-COMMIT-PATH` rows.

| Jump | Tick | Normal `(x,z)` | Normal `(vx,vz)` | Normal yaw | Fixed `(x,z)` | Fixed `(vx,vz)` | Fixed yaw |
|---:|---:|---|---|---:|---|---|---:|
| 1 | 12 | 743.707005, -1103.714624 | -0.223148, 0.067035 | 69.499695 | 743.704467, -1103.725681 | -0.223000, 0.067374 | 69.240623 |
| 2 | 21 | 740.782891, -1102.628110 | -0.259248, 0.110824 | 56.458252 | 740.785074, -1102.622263 | -0.261181, 0.107753 | 54.622684 |
| 3 | 30 | 737.886542, -1101.003673 | -0.254481, 0.149129 | 57.194775 | 737.879061, -1101.013137 | -0.255132, 0.148100 | 57.260780 |
| 4 | 40 | 734.738602, -1099.039450 | -0.249842, 0.158262 | 56.563908 | 734.751259, -1099.024040 | -0.247208, 0.161573 | 58.082737 |
| 5 | 55 | 730.338594, -1096.094697 | -0.226905, 0.168495 | 19.866928 | 730.330045, -1096.115837 | -0.227440, 0.167240 | 17.350090 |
| 6 | 64 | 729.470230, -1093.541305 | 0.019594, 0.216686 | -7.527575 | 729.422803, -1093.643065 | 0.018458, 0.206892 | -9.112720 |
| 7 | 68 | 729.888283, -1092.709983 | 0.096759, 0.166518 | -34.657738 | 729.796070, -1092.788378 | 0.087160, 0.172362 | -37.146427 |
| 8 | 71 | 730.517873, -1091.763474 | 0.156344, 0.232296 | -38.152817 | 730.421735, -1091.841418 | 0.156079, 0.231933 | -39.916409 |
| 9 | 75 | 731.509106, -1090.430953 | 0.190637, 0.249848 | -42.336174 | 731.437591, -1090.530906 | 0.196345, 0.244617 | -46.304005 |
| 10 | 77 | 732.162233, -1089.651388 | 0.219667, 0.262326 | 10.092477 | 732.139678, -1089.800145 | 0.236712, 0.245421 | 2.976783 |
| 11 | 80 | 732.813499, -1088.466968 | 0.102283, 0.270883 | 147.381760 | 732.819704, -1088.639926 | 0.108649, 0.270483 | 119.000107 |
| 12 | 88 | 732.697999, -1089.740940 | -0.000111, -0.231609 | 180.656433 | 732.699690, -1089.745818 | -0.000002, -0.226463 | 180.104996 |
| 13 | 90 | 732.700850, -1090.631120 | -0.000805, -0.301385 | 172.077408 | 732.700317, -1089.563769 | 0.000000, 0.073811 | 405.000000 |

The tick-90 states show the consequence of different window choices: normal margin's five-jump solution ended with `vz=-0.301385`; fixed margin's one-jump solution ended with `vz=+0.073811`. That is a downstream divergence, not yet an explanation of why the fixed-margin five-jump solve missed.

## Next experiment: isolate margin from entry state

1. Preserve the fixed-margin raw trace before any build cleanup. It is currently an ignored `core/build` artifact.
2. Reproduce the exact normal entry state at tick 80, including position, velocity, yaw, inputs, constraints, and all solver settings. Run the same `[11,16)` window twice from this identical state: once with the normal margin ladder and once with only `0.0003`.
3. Capture every objective, closed-form pattern, rung, replay violation, fallback SLP status, and candidate path. Confirm that changing margin is the only experimental variable.
4. If the two runs differ, inspect how the margin changes candidate generation/certification and whether a lower margin changes solver numerics or pattern discovery. Do not infer that from the current full-run comparison, because the start states differ.
5. Separately, run the reverse control from the fixed-margin tick-80 entry state under both margin settings. This tests whether the outcome is primarily entry-state-dependent.
6. Have the next investigator independently verify the trace-to-table extraction and the meaning of each reported tick before treating the table as “one tick before landing.” These rows are specifically the last logged committed states for each jump segment.

## Sources and bookkeeping

- Normal report and all-window attempt table: `docs/research/solver-traces/linkcraft-real11-first30-normal7-commit1.md`.
- Normal objective/rung data: `docs/research/solver-traces/linkcraft-real11-first30-normal7-commit1-objectives.csv`.
- Normal raw path/solver trace: `docs/research/solver-traces/linkcraft-real11-first30-normal7-commit1.txt`; key locations include tick-80 entry at line 647 and the `[11,16)` objective at lines 609 onward.
- Fixed raw trace: `core/build/reports/solver-trace-linkcraft-real11-first30-single-margin-7.txt`; key locations include `[11,16)` attempt at lines 1410–1600, `[11,13)` success at lines 1627–1662, `[12,19)` at lines 1672–1879, and terminal index-13 ladder at lines 2490–2908.
- Reproducer: `core/src/test/java/de/legoshi/parkourcalc/anglesolver/First30NormalMarginTraceTest.java`; its default TAS path is the Linkcraft real/11 JSON and it supports `PKC_TRACE_INPUT`.
- Branch commit `5b632de` contains the temporary fixed-`0.0003` change; `79bb653` reverts it. Current HEAD `f980e29` has the normal margin ladder and normal trace. Use `git show 5b632de:<path>` to inspect the temporary override without changing branches.
- Current branch: `codex/first30-margin-trace-bookkeeping`. Earlier solver logs, tests, and experiment changes were separated into commits; see `git log --oneline`.
