# Native core in production solves

Evidence for units the opt-in C++ core (`PKC_NATIVE_CORE`, [runbook](../../docs/RUNBOOK.md)) switches.

## Interaction gate preparation, 2026-11-21

Source: `5a8f43f7` plus the switch (`InteractionGate.prepare` builds natively for collision-box routes without
ladder events). Fixed work: the extended case capped at 100 solver calls
([input](../prefix-reuse-keys/2026-11-21/extended-fixedwork100.json)), `tools.profiling.run --profile-mode none`,
Python bridge, single-threaded BLAS, release build, Python and native runs alternated, load average 5.6 to 6.8.

| Run | Search s | Expanded | Calls | Preparation misses / hits | Analysis misses | Sequence checks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Python 1 | 33.63 | 28 | 100 | 1,412 / 876 | 1,426 | 1,015 |
| native 1 | 31.59 | 28 | 100 | 1,412 / 876 | 1,426 | 1,015 |
| Python 2 | 34.00 | 28 | 100 | 1,412 / 876 | 1,426 | 1,015 |
| native 2 | 32.19 | 28 | 100 | 1,412 / 876 | 1,426 | 1,015 |

Mean search time falls 5.7% (33.82 s to 31.89 s). `tools.profiling.parity` finds identical reports in exact mode:
status, termination and all 19 transitions ([1](2026-11-21/parity-python-1-native-1.json),
[2](2026-11-21/parity-python-2-native-2.json)). `tools.native_cpp verify-gate` on the extended input for 90 s
compared 5,794 (release) and 5,722 (UBSan) preparations with no difference
([release](2026-11-21/live-gate-release-summary.json), [UBSan](2026-11-21/live-gate-sanitize-summary.json)).
