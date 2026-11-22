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

## Gate screen kept native, 2026-11-21

Source: `51b689fb` plus the native screen: preparation, coefficients, model, envelopes and `required` stay in C++;
Python dicts are built only for readers that ask. Same fixed-work input and method as above, load average 4.9 to 5.4.

| Run | Search s | Expanded | Calls | Preparation misses / hits | Analysis misses / hits | Coefficient misses | Sequence checks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Python 1 | 32.60 | 28 | 100 | 1,412 / 876 | 1,426 / 633 | 991 | 1,015 |
| native 1 | 27.28 | 28 | 100 | 1,412 / 876 | 1,426 / 633 | 991 | 1,015 |
| Python 2 | 34.41 | 28 | 100 | 1,412 / 876 | 1,426 / 633 | 991 | 1,015 |
| native 2 | 27.78 | 28 | 100 | 1,412 / 876 | 1,426 / 633 | 991 | 1,015 |

Mean search time falls 17.8% (33.51 s to 27.53 s) with identical gate counters, and exact parity finds all 19
transitions identical ([1](2026-11-21-screen/parity-python-1-native-1.json),
[2](2026-11-21-screen/parity-python-2-native-2.json)). `tools.native_cpp verify-screen` on the extended input for
90 s repeated 3,486 `required` and 1,264 `check_program` calls (release) and 3,563 / 1,294 (UBSan) on a shadow Python
gate with no difference ([release](2026-11-21-screen/live-screen-release-summary.json),
[UBSan](2026-11-21-screen/live-screen-sanitize-summary.json)).

## Sequence screens built natively, 2026-11-21

Source: `08ef3aa1` (main's PR #8 merged, which roughly doubles the fixed-work case's sequence screens) plus native
sequence screens: `InteractionGate.sequence` appends the choice to a native copy of the context's program
(`append_choice`'s extend path, the physics key and the vertical trace in C++) and screens that child without building
it in Python. Same fixed-work input and method as above. Step 1 is a native build of `08ef3aa1`; runs alternated, load
average 4.4 to 5.4.

| Run | Search s | Expanded | Calls | Preparation misses / hits | Analysis misses / hits | Coefficient misses | Sequence checks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| step 1 native 1 | 35.10 | 41 | 100 | 3,269 / 2,119 | 3,284 / 1,554 | 2,069 | 2,020 |
| step 2 native 1 | 34.43 | 41 | 100 | 3,269 / 2,119 | 3,284 / 1,554 | 2,069 | 2,020 |
| step 1 native 2 | 33.81 | 41 | 100 | 3,269 / 2,119 | 3,284 / 1,554 | 2,069 | 2,020 |
| step 2 native 2 | 33.30 | 41 | 100 | 3,269 / 2,119 | 3,284 / 1,554 | 2,069 | 2,020 |
| Python 1 | 50.35 | 41 | 100 | 3,269 / 2,119 | 3,284 / 1,554 | 2,069 | 2,020 |
| step 2 native 3 | 33.21 | 41 | 100 | 3,269 / 2,119 | 3,284 / 1,554 | 2,069 | 2,020 |
| Python 2 | 50.30 | 41 | 100 | 3,269 / 2,119 | 3,284 / 1,554 | 2,069 | 2,020 |
| step 2 native 4 | 33.55 | 41 | 100 | 3,269 / 2,119 | 3,284 / 1,554 | 2,069 | 2,020 |

Native sequence screens cut mean search time 1.7% against step 1 (34.46 s to 33.87 s), about the size of the run
spread; against Python the native core now cuts it 33.7% (50.33 s to 33.38 s). Every pair has identical gate counters
and exact parity on all 19 transitions ([step 1 vs 2: 1](2026-11-21-sequence/parity-step1-1-step2-1.json),
[2](2026-11-21-sequence/parity-step1-2-step2-2.json); [Python vs native: 1](2026-11-21-sequence/parity-python-1-native-1.json),
[2](2026-11-21-sequence/parity-python-2-native-2.json)). `tools.native_cpp verify-screen` on the extended input for
90 s repeated 2,198 `sequence`, 1,566 `required` and 1,577 `check_program` calls (release) and 2,302 / 1,680 / 1,691
(UBSan) on a shadow Python gate with no difference ([release](2026-11-21-sequence/live-screen-release-summary.json),
[UBSan](2026-11-21-sequence/live-screen-sanitize-summary.json)).

## Scene checks, 2026-11-21

Source: `27091fb1` plus the native `scene/static_collision.check_scene`, which the Python bridge uses under
`PKC_NATIVE_CORE`. The 21 scene checks of a 120 s Real11 solve (4 anchors; feasible, T64) were replayed with 13
perturbations each (shifted positions, scaled movement, dropped contacts, a flipped ground flag, sneak, no step
height, ladders, a start inside a solid, shrunk bounds, an unknown field, changed friction), plus 102 synthetic
step, sneak-edge and out-of-bounds checks, 48 trajectories generated by Python's own `StaticScene.move` (planned
steps and wall hits, also relabelled as movement cases) and the budget: Python and native results and errors are
identical on release and UBSan ([release](2026-11-21-scene/replay-release.json),
[UBSan](2026-11-21-scene/replay-sanitize.json)). `tools.native_cpp verify-scene` on that solve compared all 21 checks
with no difference ([summary](2026-11-21-scene/live-scene-release-summary.json)).
