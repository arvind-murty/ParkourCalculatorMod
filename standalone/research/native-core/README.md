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

## Scene geometry, 2026-11-22

Source: `3c31cb51` plus native `scene/geometry` (`normalize_geometry`, `route_footprints`, `support_center_interval`);
the Python bridge derives footprints natively under `PKC_NATIVE_CORE`. Every collision-box scene in the repository
(107) went through both `normalize_geometry`s with 8 perturbations each, compared as plain JSON so key order counts
too; their supports' footprints (standing and not), and 6,000 interval pairs of random, subnormal and near-edge
doubles: identical on release and UBSan ([release](2026-11-21-scene/geometry-release.json),
[UBSan](2026-11-21-scene/geometry-sanitize.json)). `verify-scene` compared
21 scene checks, 89 footprint derivations and 4 normalizations on the 120 s Real11 solve, and 135 / 1,122 / 3 on the
extended input for 90 s, with no difference ([short](2026-11-21-scene/live-geometry-short-summary.json),
[extended](2026-11-21-scene/live-geometry-extended-summary.json)).

## Scene package: routes, schedules and scenes, 2026-11-22

Source: `0b1c6442` plus native `scene/route` (`prepare_route`, `schedule`, `initial_guess`), `scene/normalize`
(`prepare_scene`) and `scene/objectives`, with the route parts of `constraints/` (passages, wall hits and ceilings) and
`movement/` (contact events, movement units, ladder-event validation). Under `PKC_NATIVE_CORE`, `scene/route.py` and
`scene/normalize.py` hand every caller to them; scenes with climbable cells and routes with ladder events stay in Python.
The native gate builds its routes through the same code.

- The 1,216 recorded gate preparations are unchanged.
- Live calls recorded on the 120 s Real11 and extended solves (4,339 `prepare_route`, 4,255 `schedule`, 1,252
  `initial_guess`) replay identically, key order included, on release and UBSan
  ([release](2026-11-22-scene-routes/live-calls-replay-release.json),
  [UBSan](2026-11-22-scene-routes/live-calls-replay-sanitize.json)).
- Synthetic multi-leg routes for what production never builds (run allocations, the run stage, run-off exits,
  contact entries, route constraints, passages, wall hits, ceilings, reversal contacts with avoidance rows, centre and
  standing bounds) and 21 invalid fields agree ([release](2026-11-22-scene-routes/synthetic-release.json),
  [UBSan](2026-11-22-scene-routes/synthetic-sanitize.json)).
- `prepare_scene` agrees on all 123 scene requests in the repository with 18 variants each: 565 normalized scenes, 1,669
  identical errors, 102 left to Python for climbable cells ([release](2026-11-22-scene-routes/scenes-release.json),
  [UBSan](2026-11-22-scene-routes/scenes-sanitize.json)).
- `verify-scene` on the extended input for 90 s compared every `scene/` call (7,495 routes, 4,444 schedules, 816 yaw
  guesses) with no difference on either build ([release](2026-11-22-scene-routes/live-scene-release-summary.json),
  [UBSan](2026-11-22-scene-routes/live-scene-sanitize-summary.json)).
- Fixed work (extended, 100 solver calls): the Python and native solves match exactly on all 19 transitions
  ([parity](2026-11-22-scene-routes/parity-python-native.json)).

## Numerics: request checks, validator and IPOPT worker, 2026-11-22

Source: `f9c2a436` plus native `numerics/model` (`prepare`, `fingerprint`), `numerics/constraints` (`reduce_walls`),
`numerics/builders.numerical_problem`, `numerics/continuation` and `numerics/worker` (the executable
`pkc_numerics_worker`, built against the casadi and IPOPT the worker Python ships), with `validation/numerical`
(`validate`, `validate_controls`), which numerics calls throughout. Under `PKC_NATIVE_CORE` the Python functions hand
every caller to them and `numerics/solver.py` launches the native worker.

- Live calls recorded on a 90 s extended solve (2,580 `prepare`, 197 `fingerprint`, 1,632 `validate`, 132
  `validate_controls`) and on the unit/integration suite (4,000, 2,028, 4,000, 97 and 34 `reduce_walls`) replay
  identically, key order included ([solve](2026-11-22-numerics/live-calls-replay-release.json),
  [suite](2026-11-22-numerics/suite-calls-replay-release.json)); so do 1,152 recorded `numerical_problem` calls
  ([replay](2026-11-22-numerics/numerical-problem-replay-release.json)).
- Synthetic variants of 40 recorded requests (bad and edge values for every field, objectives, contacts, every wall
  mode, inertia and ladder patterns, vertical and movement schedules) agree on 32,000 `prepare` calls (25,540 identical
  errors), 32,300 `validate` and 38,760 `validate_controls` replays
  ([synthetic](2026-11-22-numerics/synthetic-release.json)).
- The 21 IPOPT requests of a 90 s extended solve, each as recorded, from zero and random facings, and with a 0.005
  threshold (zero-event, fixed-pattern and polish phases), give identical worker message streams in all 84 runs (275
  phases, 164 candidates; elapsed times aside) with a 60 s budget that no phase reached. Total worker time 19.6 s native
  against 35.9 s Python ([parity](2026-11-22-numerics/worker-parity-release.json)).
- The UBSan build gives the same results on all of the above, with no trap
  ([calls](2026-11-22-numerics/calls-replay-sanitize.json),
  [numerical_problem](2026-11-22-numerics/numerical-problem-replay-sanitize.json),
  [synthetic](2026-11-22-numerics/synthetic-sanitize.json), [worker](2026-11-22-numerics/worker-parity-sanitize.json)).

## Numerics: the disk relaxation over Clarabel.cpp, 2026-11-22

Source: `081f4f30` plus native `numerics/disk` (`spec`, `_solve` with its cost, `deficits`, `ipopt_seed`,
`alarm_ticks`) over Clarabel.cpp v0.11.1's C API, the Python package's Clarabel version; the decided-result cache,
`STATS` and `RelaxedDispatch` stay in Python. `_solve` and `alarm_ticks` take `MAX_ITER` and `ALARM_FRACTION` from the
Python module, so its settings stay the ones in force.

- Every `disk.py` call of a 90 s extended solve replays identically through the Python functions on release and UBSan:
  1,279 `spec`, 1,053 Clarabel solves (740 solved, 305 infeasible, 8 undecided; statuses, controls, objectives and
  iteration counts), 142 `deficits` and `alarm_ticks`, 20 `ipopt_seed`
  ([release](2026-11-22-numerics/disk-replay-release.json), [UBSan](2026-11-22-numerics/disk-replay-sanitize.json)).
- Synthetic variants of 12 recorded requests (strict and screen specs with offset, facing, relative, equality and
  duplicate rows, unsupported classes, constant-zero and supplied objectives, custom and motion objectives, interval
  starts) agree on 10,296 specs and 998 solves except the score of 30 motion-objective solves, which differs by at most
  3.6e-15: numpy's `cost @ x` groups the four terms of a motion score differently from the native sum
  ([release](2026-11-22-numerics/disk-synthetic-release.json), [UBSan](2026-11-22-numerics/disk-synthetic-sanitize.json)).

## Worker cap guess follows the phase pattern, 2026-11-22

`numerics/worker.phase` inferred each phase's starting ladder-cap pattern from `forward` with the run's inertia pattern
still a Python list, where numpy compares a row whole (`[0, 2] == 0` is False), so the guess carried every velocity; the
IPOPT model itself always applied the pattern. Both workers now pass an array. Only threshold > 0 requests with ladder
ticks change (production requests have threshold 0 and no ladder ticks): from zero facings, 8 of 39 such suite
requests take a different path, two of them ending `Solve_Succeeded` instead of `Infeasible_Problem_Detected`. After the
fix the Python and native workers agree on 121 runs of 40 suite ladder requests (664 phases)
([parity](2026-11-22-numerics/worker-ladder-parity-release.json)).

## Vertical schedule checks and interaction screens, 2026-11-22

Source: `ea92580c` plus native `validation/vertical.validate_vertical_schedule` (the general Python form, through
native `prepare` and `validate`; the gate now calls it instead of its own one-profile copy) and
`reachability/intervals.interaction_reachability`/`_many` (the gate's required-contact screen now calls it too; a
refined screen of envelopes with a model raises `NativeUnsupported`, since `disk.screen`'s cache stays in Python). The
CPython `math.hypot` port now has one copy, `src/py_math.hpp`.

- 4,000 recorded `validate_vertical_schedule` calls of a 90 s extended solve replay identically on release and UBSan
  ([release](2026-11-22-validation/replay-release.json), [UBSan](2026-11-22-validation/replay-sanitize.json)).
- 3,000 `interaction_reachability` and 884 `interaction_reachability_many` calls of a 90 s extended solve replay
  identically through the Python functions, 2 of them refined screens that fall back
  ([release](2026-11-22-validation/replay-intervals-release.json),
  [UBSan](2026-11-22-validation/replay-intervals-sanitize.json)); synthetic rows (relative, equality, out-of-range),
  regions (every phase, interior, distance gaps) and alternatives on 30 recorded envelopes, with and without
  contradictions, origins and radii, agree on 2,400 queries
  ([release](2026-11-22-validation/synthetic-intervals-release.json),
  [UBSan](2026-11-22-validation/synthetic-intervals-sanitize.json)).
- The recorded gate corpus (1,216 preparations, envelopes, programs and appends) is unchanged on both builds
  ([release](2026-11-22-validation/gate-corpus-release.json), [UBSan](2026-11-22-validation/gate-corpus-sanitize.json)),
  and live `verify-screen` on the extended input compared 2,292 sequence screens, 1,680 required-contact proofs and
  1,691 program checks with no difference ([summary](2026-11-22-validation/live-screen-release-summary.json)).

## Milestone after merging PR #9, 2026-11-22

Source: `299b72a7` (PR #9 at `73d4489a` merged, with the native follow-ups for merged faces and the new
`compile_program`), before `e7cd2bc6`.

- A gate corpus recaptured on this source (1,410 preparations, 3,229 coefficient requests, 6,206 programs, 6,197
  appends, 6,194 envelope builds) replays identically on release and UBSan
  ([release](2026-11-22-merge9/corpus-replay-release.json), [UBSan](2026-11-22-merge9/corpus-replay-sanitize.json)).
- The gate modules pass without and with `PKC_NATIVE_CORE`: 734 tests, 10 skips (the Java bridge tests)
  ([Python](2026-11-22-merge9/suite-python-summary.txt), [native](2026-11-22-merge9/suite-native-summary.txt)).
- Live `verify-scene` and `verify-screen` on the extended input for 90 s compared every call with no difference on
  either build ([scene release](2026-11-22-merge9/live-scene-release-summary.json),
  [scene UBSan](2026-11-22-merge9/live-scene-sanitize-summary.json),
  [screen release](2026-11-22-merge9/live-screen-release-summary.json),
  [screen UBSan](2026-11-22-merge9/live-screen-sanitize-summary.json)).
- Fixed work (extended, 100 solver calls): exact on all 19 transitions ([parity](2026-11-22-merge9/parity.json)).

## First appends built natively, 2026-11-22

Source: `e7cd2bc6` plus native `append_choice` for a program's first append (Python `_rebuild` without ladder events:
the tail traced whole from the initial state), so only ladder appends still raise `NativeUnsupported`.

- Every recorded movement choice of the recaptured corpus (on the PR #9 merge), appended to an empty program at its own
  initial state with thresholds 0, .005 and .003, gives the same child digest and physics key, or the same `None` or
  error, on 1,974 appends (1,723 children) on release and UBSan
  ([release](2026-11-22-first-appends/first-appends-release.json),
  [UBSan](2026-11-22-first-appends/first-appends-sanitize.json)).
- The corpus replay now compares all 6,205 appends (its 8 first appends were skipped before) with no difference
  ([release](2026-11-22-first-appends/corpus-replay-release.json),
  [UBSan](2026-11-22-first-appends/corpus-replay-sanitize.json)).

## Scene geometry (spatial/surfaces), 2026-11-22

Source: `39ff210e` plus native `spatial/surfaces` (`free_rectangles` and the `SceneGeometry` computations: contact
options, cluster groups, sides) with `cells.subtract_open` and `regions.canonical_cover`/`geometry_key`. The Python
`SceneGeometry` keeps its caches and dataclasses and asks the native geometry on a miss. Coordinates keep their
int/float kinds, since a contact option's `surface` name is a key over them.

- On every collision scene in the repository (46 distinct solid sets) and an integer-coordinate copy of each, 18,144
  `contact_options` (every axis and sign, four clearances, all, single and random member sets), 368 cluster groupings,
  4,326 `sides` and 460 `free_rectangles` agree on release and UBSan
  ([release](2026-11-22-surfaces/surfaces-release.json), [UBSan](2026-11-22-surfaces/surfaces-sanitize.json)).
- Fixed work (extended, 100 solver calls) on this source with the Python interaction screen kept for Python callers
  (`e7cd2bc6`) and native first appends: Python and native match exactly on all 19 transitions
  ([parity](2026-11-22-surfaces/parity-python-native.json)); search 43.1 s Python, 28.0 s native. The same run before
  `e7cd2bc6` took 42.2 s Python and 45.9 s native.

## Y traces (trace_ladder_schedule), 2026-11-22

Source: `d04ee030` plus native `movement/vertical_profiles.trace_ladder_schedule` in full (target tops, minimum landing
states, occupied Y bounds, `stop_on_landing`, itemized failures), dispatched for every caller; `vertical_step` now
takes per-tick jump velocities and itemizes failures for it and for `validation/numerical`.

- Recorded calls (410 kept of 26,907 on a 90 s extended solve, 5,000 kept of 878,539 on the unit/integration suite)
  replay identically through the Python function on release and UBSan
  ([release](2026-11-22-trace/replay-release.json), [UBSan](2026-11-22-trace/replay-sanitize.json)); synthetic
  schedules with ladders, sneak, jumps, cases, steps, target tops and occupied bounds agree on 30,051 cases (15,165
  invalid with itemized failures, 12,077 identical errors) on release and 7,501 on UBSan
  ([release](2026-11-22-trace/synthetic-release.json), [UBSan](2026-11-22-trace/synthetic-sanitize.json)).
- 13.3 us per call native against 28.4 us Python on a 10 to 25 tick jump schedule, conversion included.
- The gate modules pass in both modes (777 tests, 10 Java-bridge skips;
  [Python](2026-11-22-trace/suite-python-summary.txt), [native](2026-11-22-trace/suite-native-summary.txt)); fixed
  work matches exactly on all 19 transitions, search 43.0 s Python and 28.1 s native
  ([parity](2026-11-22-trace/parity.json)).

## Cell decomposition and geometry graph, 2026-11-22

Source: `a77d39a5` plus native `spatial/cells.decompose` (with `merge_boxes`, `union_subtract`, contact patches,
support refinement and portals), `spatial/regions.support_components` and `build_geometry_graph`; Python builds its
dataclasses from the native rows. The last sampled profile spent 1.4 s of a 34.8 s native run in the graph's Python
`cells.intersection` loops.

- On every collision scene in the repository (47 distinct), 94 decompositions (with and without support refinement),
  47 support component sets and 94 graphs (no task goal and a landing goal) agree on release and UBSan
  ([release](2026-11-22-cells/cells-release.json), [UBSan](2026-11-22-cells/cells-sanitize.json)); over those scenes
  Python took 0.74 s to decompose and 7.84 s to build graphs, native 0.04 s and 0.46 s.
- Fixed work: exact on all 19 transitions; search 43.1 s Python, 26.7 s native
  ([parity](2026-11-22-cells/parity-python-native.json)).

