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

## Python dispatch only where it measures faster, 2026-11-22

Source: `21cea955`. Per call on recorded calls of a 90 s extended solve, Python against native with conversion:
`disk.spec` 154 against 286 us (750 before precomputing its row keys and set), `fingerprint` 409 against 510,
`deficits` 16 against 82, `alarm_ticks` 3 against 71: their Python callers keep the Python body. Kept native:
`prepare` 385 against 122, `validate` 169 against 79, `validate_controls` 297 against 192, `numerical_problem` 1,163
against 582, the Clarabel solve 5,585 against 4,279, `validate_vertical_schedule` 480 against 404, `ipopt_seed` 3.5
against 2.2. Fixed work: exact on all 19 transitions; search 42.9 s Python, 26.2 s native
([parity](2026-11-22-dispatch/parity-python-native.json)).

## Milestone on 6a34e295, 2026-11-22

- The corpus recaptured at `299b72a7` replays identically on release and UBSan
  ([release](2026-11-22-overnight/corpus-replay-release.json), [UBSan](2026-11-22-overnight/corpus-replay-sanitize.json)).
- The gate modules plus the spatial, contact and ladder-schedule modules pass without and with `PKC_NATIVE_CORE`: 802
  tests, 10 Java-bridge skips ([Python](2026-11-22-overnight/suite-python-summary.txt),
  [native](2026-11-22-overnight/suite-native-summary.txt)).
- Live `verify-scene` and `verify-screen` on the extended input for 90 s: no difference on either build
  ([scene release](2026-11-22-overnight/live-scene-release-summary.json),
  [scene UBSan](2026-11-22-overnight/live-scene-sanitize-summary.json),
  [screen release](2026-11-22-overnight/live-screen-release-summary.json),
  [screen UBSan](2026-11-22-overnight/live-screen-sanitize-summary.json)).
- Fixed work: exact on all 19 transitions; search 42.0 s Python, 25.5 s native ([parity](2026-11-22-overnight/parity.json)).

## Ladder routes and climbable-cell scenes, 2026-11-22

Source: `86a8636c` plus native `scene/ladders`, the ladder parts of `movement/units` (`ladder_units`,
`movement_program` with events, `exit_matches`), `movement/ladder/events` (full validation and
`compile_ladder_events`), `movement/ladder/settings`, ladder passages in `constraints/avoidance` and ladder events in
`scene/route.schedule`. Routes with ladder events and scenes with climbable cells no longer fall back to Python; only
ladder appends still do.

- Recorded suite calls replay identically on release and UBSan: 1,354 `scene/ladders` calls (including 41
  `schedule_constraints`), 2,183 `prepare_route` (33 with ladder events), 1,676 `schedule` (21 with ladder events),
  4,000 `ladder_units`, 772 `movement_program` and 4,000 `exit_matches`
  ([release ladders](2026-11-22-ladders/ladders-replay-release.json),
  [release routes](2026-11-22-ladders/routes-replay-release.json), [UBSan](2026-11-22-ladders/replay-sanitize.json)).
- `prepare_scene` on every scene request in the repository with its variants: 657 normalized scenes and 1,641
  identical errors, none left to Python ([release](2026-11-22-ladders/scenes-release.json),
  [UBSan](2026-11-22-ladders/scenes-sanitize.json)).
- The gate modules plus the ladder modules pass in both modes: 844 tests, 10 Java-bridge skips
  ([Python](2026-11-22-ladders/suite-python-summary.txt), [native](2026-11-22-ladders/suite-native-summary.txt));
  live `verify-scene` finds no difference ([summary](2026-11-22-ladders/live-scene-release-summary.json)); fixed work
  matches on all 19 transitions, search 42.0 s Python and 25.8 s native
  ([parity](2026-11-22-ladders/parity-python-native.json)).

## Ladder appends built natively, 2026-11-22

Source: `28e9460b` plus native ladder appends (Python `_rebuild`'s ladder path: the whole program traced again,
ladder events shifted and re-checked, units rebuilt around them and ceilings owned again by
`movement/ladder/ceiling.own_ceiling_contacts`), so `append_choice` builds every child natively. Native
`merge_contacts` now keys on any axis value, as the Python does: contact events name their axis.

- 3,084 recorded suite appends (21 with ladder events or ticks) give the same child digest and physics key, or the
  same None or error, on release and UBSan ([release](2026-11-22-ladder-appends/appends-release.json),
  [UBSan](2026-11-22-ladder-appends/appends-sanitize.json)); `test_native_core` now compares a ladder program with
  ceiling effects append by append.
- The gate and ladder modules pass in both modes: 844 tests, 10 Java-bridge skips
  ([Python](2026-11-22-ladder-appends/suite-python-summary.txt),
  [native](2026-11-22-ladder-appends/suite-native-summary.txt)); live `verify-scene` finds no difference
  ([summary](2026-11-22-ladder-appends/live-scene-release-summary.json)); fixed work matches on all 19 transitions,
  search 43.8 s Python and 26.7 s native ([parity](2026-11-22-ladder-appends/parity-python-native.json)).

## Interaction gate and open generators for native callers, 2026-11-22

Source: `8a51f1a3` (main with PR #9 merged in) plus a native `InteractionGate` (`reachability/service.hpp`: one
session cache of preparations, coefficients, analyses and program screens; face tables; approach envelopes; box,
walk-off and sequence queries; `check_program` with the disk relaxation and a native decided-result cache) and the
three open generators (`normal`, `stepping`, `ceiling`) with `movement/cases` and `normal.py`'s traces. Nothing in
Python dispatches to them yet: they are the first layer of the native DFS node unit.

Both were checked by shadowing the fixed-work solve (`research/prefix-reuse-keys/2026-11-21/extended-fixedwork100.json`,
Python mode): every outer Python gate call, and every open `generate_choices` call, was repeated natively and compared
exactly, including the exclusion entries each adds and the gate's approach and face-table metrics.

- Gate ([shadow](2026-11-22-gate-generators/gate-shadow-release.json)): 6,366 face tables, 7,631 box queries, 134
  approach envelopes, 93 walk-off checks, 1,694 sequence screens and 1,152 `check_program` verdicts with their walk-off
  face selections, all equal. Two native bugs found on the way: the analysis key lacked `interaction_context_key`'s
  policy wrapper, and range-for loops over `items()` of a temporary `Value` read freed memory.
- Generators ([shadow](2026-11-22-gate-generators/generators-shadow-release.json)): 165 calls of each type, 740 normal,
  3,698 stepping and 119 ceiling choices, equal in order. The solve's other 2,904 normal-generator calls are route
  preparation's filler units, not open generation.
- Both shadows also ran against an AddressSanitizer build of the module (an ad-hoc-signed copy of the interpreter, so
  macOS lets the ASan runtime load) with no sanitizer report and no difference
  ([gate](2026-11-22-gate-generators/gate-shadow-asan.json), [generators](2026-11-22-gate-generators/generators-shadow-asan.json));
  the repository's `--variant sanitize` build is UBSan only.

## The Python gate on its native core, 2026-11-22

Source: `91fd8942` plus: under `PKC_NATIVE_CORE` the Python `InteractionGate` keeps a native gate per search and scene
(`cores`, shared through the search adapter like its cache and metrics) for preparation, analysis, required-contact
proofs and `check_program`; `numerics/disk.solve` and the gate's relaxation counters use the native decided-result
cache, which the native screens share. Generator queries (approach, box, walk-off and sequence) stay in Python.

- Fixed work matches Python on all transitions ([parity](2026-11-22-native-gate-dispatch/parity-python-native.json),
  [milestone](2026-11-22-native-gate-dispatch/parity-milestone.json)); alternating native runs with and without the core
  took 26.3/26.1 s against 29.4/29.9 s ([timing](2026-11-22-native-gate-dispatch/alternating-native-timing.json)).
- Suites: 844 tests pass in Python mode; in native mode 3 errors came from gates built on scenes without a
  `sceneModel`, fixed before commit ([Python](2026-11-22-native-gate-dispatch/suite-python-summary.txt),
  [native](2026-11-22-native-gate-dispatch/suite-native-summary.txt)); live `verify-scene` finds no difference
  ([summary](2026-11-22-native-gate-dispatch/live-scene-release-summary.json)).
- Two tests that stub Python's `disk._solve`/`disk.solve` run in Python mode only; the native cache's retry and caching
  are covered in native mode by `test_disk`'s IPOPT-oracle test.

## Open generators on the native gate, 2026-11-22

Source: `7b1ca207` plus: `movement/generation.generate_choices` runs the open normal, stepping and ceiling generators on
the context gate's native core when the context scene shares the gate's geometry; the Python gate then reads face
tables, its query log and checked count from the core. The first milestone found two inputs the fixed-work solve never
produced (a `cases` list, and the face table of a program on no face, which the anchor policy asks for); both were
fixed before commit.

- Fixed work matches Python on all transitions ([parity](2026-11-22-generator-dispatch/parity-python-native.json));
  alternating native runs with and without the dispatch took 24.5/24.8 s against 25.5/25.5 s
  ([timing](2026-11-22-generator-dispatch/alternating-native-timing.json)).
- The generator shadow over `test_stepping_cases`, `test_anchor_search`, `test_anchor_dfs` and `test_block_contacts`
  compares 2,537 more calls and 8,201 choices, all equal
  ([shadow](2026-11-22-generator-dispatch/generators-shadow-tests.json)).
- Suites pass in both modes, 844 tests with 10 Java-bridge skips
  ([Python](2026-11-22-generator-dispatch/suite-python-summary.txt),
  [native](2026-11-22-generator-dispatch/suite-native-summary.txt)); live `verify-scene` finds no difference
  ([summary](2026-11-22-generator-dispatch/live-scene-release-summary.json)).

## Anchor policy readers for native callers, 2026-11-22

Source: `a83f4c06` plus a native `AnchorScene` (`search/anchors/policy.hpp`): `face_namer`, `support_face`, `visits`,
`launch`, `feet_touches`, `visit_state`, `audit`, `next_anchor` and `advantageous_targets` on typed programs, with the
scene's face and owner tables built once.

- Shadowing every Python call in the fixed-work solve: 10,737 `visits`, 4,063 `feet_touches` and `visit_state`, 3,830
  `audit`, 1,803 `next_anchor`, 1,752 `advantageous_targets` and 1,642 `launch` results, all equal
  ([shadow](2026-11-22-anchor-policy/policy-shadow-release.json)); the same under AddressSanitizer
  ([shadow](2026-11-22-anchor-policy/policy-shadow-asan.json)).
- Dispatching them from Python was slower (alternating native runs 26.2/26.7 s against 23.8/25.9 s,
  [timing](2026-11-22-anchor-policy/dispatch-timing.json)): each call converts a whole program, which costs more than the
  Python reader. Python keeps them; the native DFS node, whose children stay native, calls `AnchorScene`.

## Comparison, deferrals and the remaining-time bound for native callers, 2026-11-22

Source: `7513d7d3` plus native `comparison_label`, `priority_key`, `compare_candidates` (the incumbent bound and the
hard-anchor deferral; the clock comparison has no production caller and is not ported), `deferrals.assess` and
`bounds/remaining.remaining_bound` (`search/comparison.hpp`, `search/bounds/remaining.hpp`). Nothing dispatches to
them yet.

- The fixed-work solve has no incumbent, so it exercises only labels (4,120 equal) and six comparisons
  ([shadow](2026-11-22-comparison-bounds/fixedwork-shadow-release.json)).
- Shadowing the comparison, transfer-bound, time-bound, anchor-search and minimum-run tests: 4,019 labels, 756
  comparisons, 279 remaining bounds and 41 assessments equal; 13 calls on the tests' partial programs, which the typed
  conversion rejects, were skipped ([release](2026-11-22-comparison-bounds/tests-shadow-release.json),
  [AddressSanitizer](2026-11-22-comparison-bounds/tests-shadow-asan.json)).

## Node helpers for native callers, 2026-11-22

Source: `5aae935c` plus native `program_search_key`, `truncate_program`, `feasibility.node_identity` and
`meaningful_choice`, `goals._landing_prefixes` (with whether it handed back the program itself, which the Python does by
identity) and `viability.prefix_viability`. Nothing dispatches to them yet.

- Fixed-work shadow: 5,705 landing-prefix sets, 1,152 viability verdicts, 1,141 search keys, 789 choice
  classifications and 238 node identities equal ([shadow](2026-11-22-node-helpers/fixedwork-shadow-release.json)).
- Test shadow (comparison, bounds, anchor-search, stepping-case and minimum-run tests): 4,220 search keys, 2,921
  landing-prefix sets, 1,455 choice classifications, 673 node identities, 245 viability verdicts and 2 direct
  truncations equal ([shadow](2026-11-22-node-helpers/tests-shadow-release.json)).
- `AnchorScene` gains `transfer_structure` (the hard-jump latch's per-pair check): 430 results over the anchor DFS, policy
  and search tests equal, with the other policy readers ([shadow](2026-11-22-node-helpers/policy-tests-shadow-release.json));
  46 calls on the tests' partial programs were skipped. The policy and fixed-work shadows are also clean under
  AddressSanitizer ([policy](2026-11-22-node-helpers/policy-tests-shadow-asan.json),
  [fixed work](2026-11-22-node-helpers/fixedwork-shadow-asan.json)).
- `AnchorScene` also gains `within_caps`, the adapter's `decision_point` and `program_objective`, with
  `anchor_guides.classify` and `decision_objective`: 1,196 decision points and objectives and 429 cap checks in the
  fixed-work solve, and 283 objectives and 470 cap checks over the anchor tests, all equal
  ([solve](2026-11-22-node-helpers/objectives-fixedwork-shadow-release.json),
  [tests](2026-11-22-node-helpers/objectives-tests-shadow-release.json)).

## The expansion batch for decisions that are not ordinary, 2026-11-22

Source: `6efd46d6` plus `search/node.hpp`: the DFS frame's screen and eligibility (`time_bound`, `hard_bound`,
`run_bound` with its comparison cache tokens, `eligible` with the caps and `InitialJumpDomain`, `screen`), configure,
and the frontier's per-type loop (propose, inherit decision fields, project owned endpoints, assess, accept, link) as
one native call per movement type. The DFS builds the frame snapshot and applies the batch's state changes in order;
the frontier applies queued children and exclusions. Ordinary decisions (minimum-run bookkeeping) stay in Python.
Two exactness findings: CPython 3.12's `sum()` over floats is Neumaier-compensated (`python_sum`, used by
`remaining_bound`), and `run_bound`'s context reads the frame's `comparisonContextId` before `tick_label` first sets
it, so a frame's first decision carries none.

- Fixed work: 57 batches over 246 proposals (4,311 proposals of ordinary decisions stay in Python); the report equals
  the unbatched native run's except provenance ([diff](2026-11-22-expansion-batch/fixedwork-report-vs-unbatched.json),
  [counts](2026-11-22-expansion-batch/fixedwork-batch-counts.json)); all 19 transitions match Python
  ([parity](2026-11-22-expansion-batch/parity-python-native.json)).
- The native screen and eligibility shadow every Python call exactly: fixed work 906 and 364
  ([shadow](2026-11-22-expansion-batch/screen-shadow-fixedwork.json)), the short Real11 control (finds T64; 56 and 63,
  seven remaining-time proofs) ([shadow](2026-11-22-expansion-batch/screen-shadow-short-control.json)), and the anchor
  and stepping tests (307 and 246, 82 incumbent proofs) on release and under AddressSanitizer
  ([release](2026-11-22-expansion-batch/screen-shadow-tests.json), [ASan](2026-11-22-expansion-batch/screen-shadow-tests-asan.json)).
- Suites pass in both modes (844 tests, 10 Java-bridge skips) and live `verify-scene` finds no difference
  ([Python](2026-11-22-expansion-batch/suite-python-summary.txt), [native](2026-11-22-expansion-batch/suite-native-summary.txt),
  [live](2026-11-22-expansion-batch/live-scene-release-summary.json)).

## The expansion batch for ordinary decisions, 2026-11-22

Source: `91e6d939` plus ordinary decisions in the batch: each accepted child's landings (its own face, or for an
airborne advantageous jump the earliest screened landing per face among its child's proposals, whose screens change
DFS state but no frontier state), the same-face fit of a jump back onto the launch face, and its advantageous target
keys, returned for the frontier's held, reached and landings records. The minimum-run search stays in Python. The
gate binding's `analysis` counted a second preparation hit per Python `analyze`; it now counts one, as the batch does.

- Fixed work: 102 batches over 739 proposals (3,423 left in Python, the minimum-run search); the short Real11 control:
  30 batches. Both reports equal the unbatched native runs' except that preparation-hit count and provenance
  ([fixed work](2026-11-22-expansion-batch-ordinary/fixedwork-report-vs-unbatched.json),
  [short control](2026-11-22-expansion-batch-ordinary/short-control-report-vs-unbatched.json),
  [counts](2026-11-22-expansion-batch-ordinary/fixedwork-batch-counts.json)); all 19 fixed-work transitions match
  Python ([parity](2026-11-22-expansion-batch-ordinary/parity-python-native.json)).
- Suites pass in both modes (844 tests, 10 Java-bridge skips), live `verify-scene` finds no difference, and the anchor
  and stepping tests pass under AddressSanitizer with the batch active
  ([Python](2026-11-22-expansion-batch-ordinary/suite-python-summary.txt),
  [native](2026-11-22-expansion-batch-ordinary/suite-native-summary.txt),
  [live](2026-11-22-expansion-batch-ordinary/live-scene-release-summary.json),
  [ASan](2026-11-22-expansion-batch-ordinary/screen-shadow-tests-asan.json)).
- `search/node.hpp` also has `advantageous_candidates` (the minimum-run search's table of advantageous moves, with
  `case_profile`'s grind duration), for the native minimum-run search to come: 8 calls in the fixed-work solve and
  100 over the anchor tests match Python ([fixed work](2026-11-22-expansion-batch-ordinary/candidates-shadow-fixedwork.json),
  [tests](2026-11-22-expansion-batch-ordinary/candidates-shadow-tests.json)).

## The minimum-run search, 2026-11-22

Source: `b2160611` plus `frontier.minimum_runs` natively (`search/node.hpp` `minimum_runs`: lazy run-tick levels,
memoized checks and landing peeks, realize/take-entry, beating, reserves, the scan without an N, and the held
next/dominated/scan/widen restarts). Reserves hold the search's kept and landed lists by reference, as the Python's
dicts do, so later additions reach them. The frontier shares the Python tail that logs candidates and holds reserves.
With it, every proposal of a node expansion is native.

- Fixed work: 102 batches and 21 native minimum-run searches, no Python proposals; the short Real11 control: 30 and 10.
  Both reports equal the native runs with the Python minimum-run search except provenance
  ([fixed work](2026-11-22-minimum-runs/fixedwork-report-vs-python-minimum-runs.json),
  [short control](2026-11-22-minimum-runs/short-control-report-vs-python-minimum-runs.json),
  [counts](2026-11-22-minimum-runs/fixedwork-counts.json)); fixed work exercised every outcome (kept, unreached,
  dominated, held) and reserve kind (next, dominated, widen, scan). Transitions match Python on both
  ([fixed work](2026-11-22-minimum-runs/parity-python-native.json), [short control](2026-11-22-minimum-runs/short-control-parity.json)).
- Suites pass in both modes (844 tests, 10 Java-bridge skips), live `verify-scene` finds no difference, and the anchor,
  stepping and minimum-run tests pass under AddressSanitizer ([Python](2026-11-22-minimum-runs/suite-python-summary.txt),
  [native](2026-11-22-minimum-runs/suite-native-summary.txt), [live](2026-11-22-minimum-runs/live-scene-release-summary.json),
  [ASan](2026-11-22-minimum-runs/tests-asan-summary.txt)).


## Constraint variants, 2026-11-22

Source: `d24a47c2` plus `constraints/variants.hpp` (the encounter covers of `constraints/encounters.py` and the
resumable variant frontier of `constraints/variants.py`, with a typed state that converts to and from the Python state
dict), `reachability/screen.hpp` (`condition_envelopes`) and refined interaction screens through the native disk screen.
It is the first piece of the node solve, for the native repair loop to come; nothing dispatches to it yet. Route guards
and gate signatures, which have no producer, are left out; their context digest entries stay.

- A native twin beside every Python generator compares each variant and the state after it (timings aside):
  fixed work 65 generators, 103 variants, 18 obstacle restarts; the short Real11 control 15 and 46; the test modules
  that build variants 376 generators (73 resumed from a saved state), 2,175 variants. No difference
  ([fixed work](2026-11-22-constraint-variants/fixedwork-shadow.json),
  [short control](2026-11-22-constraint-variants/short-control-shadow.json),
  [tests](2026-11-22-constraint-variants/tests-shadow.json)). In the tests the twin restarts from the Python state after a
  Python deadline (70) or a test's own edit (6), and skips 2 instances whose `option` a test patches, one built with a
  gate signature and one whose test-made pending node lacks `choices`.
- Replaying the 65 fixed-work generators to exhaustion matches, on release and under AddressSanitizer, and the test
  shadow runs clean under AddressSanitizer with the same counts; its one failure is the known ladder baseline
  ([release](2026-11-22-constraint-variants/fixedwork-replay.txt), [ASan](2026-11-22-constraint-variants/fixedwork-replay-asan.txt),
  [tests ASan](2026-11-22-constraint-variants/tests-shadow-asan.json), [summary](2026-11-22-constraint-variants/tests-asan-summary.txt)).

## The repair loop, 2026-11-22

Source: `ad6ab930` plus `search/repair.hpp` (`solve_contact_branches`: the branch queue in Python's heap order, family
switch climbs, conflict-driven obstacle growth, repair sources over the full-enumeration generator, the confirmation
walk, the resume state and its compatibility check, audits with `check_geometry` and the contact conflicts) and
`search/ordered_route.hpp` (`find_route` and its run-allocation search, with the contradiction screen through the
native disk screen). The compiler, numerical solves, candidate filter, work budget and progress callback stay Python
hooks, with Python's TimeoutError, ValueError, RuntimeError and OSError mapped onto the native exceptions the loop
catches. Routes with passages, wall hits or ladder events stay in Python (production repairs have none). Nothing
dispatches to it yet.

- A recorder logs every repair call of a solve: its inputs, the generator's state, every hook call in order (arguments
  fingerprinted without budgets, answers kept, a numerical solve's incumbents with it) and the result. The native loop
  replays each against that log and must make the same hook calls in the same order and return the same result. Resume
  twins rerun each recorded call from a mid-run and its final progress snapshot (and, after a solid hit, from the
  state with only its repair source left), as a restarted session would.
- Fixed work: 141 calls (47 plus 94 exact resumes) and 2,411 hook events match; the short Real11 control, with its
  IPOPT finalization: 35 and 736; the test modules: 199 of 213 replayable calls with 2,429 events, the other 14 being
  ladder routes left to Python, and 62 calls skipped because their tests mock the solver or the generator
  ([fixed work](2026-11-22-repair-loop/replay-fixed-twins.txt), [short control](2026-11-22-repair-loop/replay-short-twins.txt),
  [tests](2026-11-22-repair-loop/replay-tests-twins.txt), [coverage](2026-11-22-repair-loop/coverage.json)).
- The fixed-work and test replays run clean under AddressSanitizer
  ([fixed work](2026-11-22-repair-loop/replay-fixed-twins-asan.txt), [tests](2026-11-22-repair-loop/replay-tests-twins-asan.txt)).

## Repair dispatch, 2026-11-22

Source: `b988b7c7` plus `search/engine.evaluate_program` building a native generator and calling the native repair
loop under `PKC_NATIVE_CORE` (routes without route guards, passages, wall hits or ladder events; production routes
have none). `constraints/variants.check_generation` is the one generator interruption both sides use.

- Fixed work and the short Real11 control: reports equal the runs with the Python repair loop except provenance
  ([fixed work](2026-11-22-repair-dispatch/fixed-report-vs-python-repair.json),
  [short control](2026-11-22-repair-dispatch/short-report-vs-python-repair.json)); transitions match Python
  ([fixed work](2026-11-22-repair-dispatch/fixed-parity-python-native.json),
  [short control](2026-11-22-repair-dispatch/short-parity-python-native.json)).
- Search seconds, one run each on an otherwise idle machine: fixed work 19.5 native against 23.3 with the Python
  loop (Python mode 46.1); the short control 1.6 against 2.9 (Python mode 3.9)
  ([times](2026-11-22-repair-dispatch/search-seconds.txt)).
- Suites pass in both modes (843 tests, 10 Java-bridge skips; the generator-recording test runs in Python mode), live
  `verify-scene` finds no difference, and the milestone's fixed-work rerun matches Python at 19.0 against 45.5 seconds
  ([Python](2026-11-22-repair-dispatch/suite-python-summary.txt), [native](2026-11-22-repair-dispatch/suite-native-summary.txt),
  [live](2026-11-22-repair-dispatch/live-scene-release-summary.json),
  [parity](2026-11-22-repair-dispatch/milestone-parity-python-native.json)).

## The node solve, 2026-11-22

Source: `992dbfef` plus `search/engine.hpp` (`evaluate_prepared`: `evaluate_program` after the gate's preparation,
with `search/identity`'s solve scope, solve identity and validation prefix, `constraints/contacts`' floor checks,
`segment_solids`, the native generator and repair loop, `numerical_counts` and `feasibility.outcome`). The gate's
`analyze_model` and face tables, the adapter's candidate check, the relaxed dispatch, the compiler, the work budget
and the progress callback stay Python hooks. Routes with passages, wall hits or ladder events stay in Python.
Nothing dispatches to it: a trial dispatch from `evaluate_program` measured no faster than the previous dispatch
(generator and repair loop only), so production keeps that one and the port waits for a native `evaluate_node`.

- With the trial dispatch, a recorder logs every `evaluate_program` result in call order. Against the previous native
  dispatch every result matches, key order included: the short Real11 control's 12 calls (its IPOPT finalization
  included), fixed work's 47 and the milestone test list's 72 (13 finalizations)
  ([short](2026-11-22-node-solve/short-base-vs-native.txt), [fixed work](2026-11-22-node-solve/fixed-base-vs-native.txt),
  [tests](2026-11-22-node-solve/tests-base-vs-native.txt)). Against Python mode the values match except one fixed-work
  call, in the same way as the previous dispatch and the code before it (`b988b7c7`); key order differs inside the
  native preparation's routes ([short](2026-11-22-node-solve/short-python-vs-native.txt),
  [fixed work](2026-11-22-node-solve/fixed-python-vs-native.txt), [tests](2026-11-22-node-solve/tests-python-vs-native.txt)).
- That call (fixed work's 19th) receives the same program in both modes, but Python mode's preparation carries the
  warm yaw of an earlier, physically identical program: the gate caches preparations by physics key, which leaves out
  `guideSeedYaw`. The warm yaw, the generator's reference trajectory and the request yaws differ
  ([paths](2026-11-22-node-solve/fixed-python-vs-native-values.txt)); the next entry fixes it.
- Reports equal the previous dispatch's except provenance, and transitions match Python
  ([fixed work](2026-11-22-node-solve/fixed-report-vs-previous-dispatch.json),
  [short](2026-11-22-node-solve/short-report-vs-previous-dispatch.json),
  [fixed parity](2026-11-22-node-solve/fixed-parity-python-native.json),
  [short parity](2026-11-22-node-solve/short-parity-python-native.json)).
- Search seconds, alternating one at a time: fixed work 20.0 and 20.0 with the previous dispatch against 21.6 and 20.1;
  the short control 1.69 and 1.69 against 1.68 and 1.64 ([times](2026-11-22-node-solve/search-seconds.txt)).
- With the trial dispatch, the search and repair test modules run clean under AddressSanitizer, 63 of their 70 node
  solves natively ([summary](2026-11-22-node-solve/asan-tests-summary.txt)).

## Preparations with the program's own warm yaw, 2026-11-22

Source: `9158d69b` plus `InteractionGate.prepare` returning a cached preparation with the asking program's own
`guideSeedYaw` as its route's `warmYaw`. The cache key is physical, so a cached route carried the seed of the program
that built it; both modes have that cache, and on fixed work Python mode's 19th node solve hit it.

- Recording every `evaluate_program` result in both modes, values now match in every call: fixed work 47 of 47, the
  short Real11 control 12 of 12, the milestone test list 72 of 72; key order still differs inside the native
  preparation's routes ([fixed work](2026-11-22-seed-yaw/fixed-python-vs-native.txt),
  [short](2026-11-22-seed-yaw/short-python-vs-native.txt), [tests](2026-11-22-seed-yaw/tests-python-vs-native.txt)).
- In Python mode the fix changes only that call ([fixed work](2026-11-22-seed-yaw/fixed-python-before-after.txt),
  [short](2026-11-22-seed-yaw/short-python-before-after.txt), [tests](2026-11-22-seed-yaw/tests-python-before-after.txt)).
  A sequential native fixed-work report equals the one before the fix except provenance, and transitions match Python
  ([report](2026-11-22-seed-yaw/fixed-native-report-vs-before.json),
  [fixed parity](2026-11-22-seed-yaw/fixed-parity-python-native.json),
  [short parity](2026-11-22-seed-yaw/short-parity-python-native.json)).
- The relaxation screens' undecided counts depend on load (Clarabel's time cap): the fixed-work recordings before the
  fix, run three at a time, counted 5 (Python mode) and 7 (native) undecided screens where every sequential run,
  before and after the fix, counts 3, so reports are compared from sequential runs.
- The prepared route's profile copy still carries the building program's `constraintOwners`; nothing reads them
  there (programs own their constraint owners).

## The decision check, 2026-11-22

Source: `24385d46` plus `search/dfs.hpp` (`evaluate_node`: the solve-due and saved-node checks, the gate preparation,
the native node solve, the interrupted-climb rule, the committed branch program with `commit_arrival` and
`retain_prefix`, `node_alternatives` and `candidate_key`, the node record) and `dfs.evaluate_node` dispatching to it
under `PKC_NATIVE_CORE` once the gate has prepared a route without passages, wall hits or ladder events. The check
records into the node table, checkpoint progress and observations through Python callbacks; anchor-leg and finish
solves keep `evaluate_program`. The `evaluate_prepared` binding goes: the check calls it natively.

- Reports equal the previous code's except provenance, and transitions match Python
  ([fixed work](2026-11-22-decision-check/fixed-report-vs-previous.json),
  [short](2026-11-22-decision-check/short-report-vs-previous.json),
  [fixed parity](2026-11-22-decision-check/fixed-parity-python-native.json),
  [short parity](2026-11-22-decision-check/short-parity-python-native.json)). Fixed work's node table holds every
  outcome (feasible, exhausted, oracle-rejected, suspended) and 10 nodes with held alternatives.
- Search seconds, alternating one at a time: fixed work 18.1 and 18.4 against 19.8 and 21.4 before; the short control
  1.71 and 1.63 against 1.73 and 1.72 ([times](2026-11-22-decision-check/search-seconds.txt)).
- The search and repair test modules run clean under AddressSanitizer with 30 native checks, and fixed work runs clean
  with a report equal to the release build's except provenance ([summary](2026-11-22-decision-check/asan-summary.txt),
  [report](2026-11-22-decision-check/fixed-asan-report-vs-release.json)).
- The milestone test list passes in both modes; the three DFS tests that stub `evaluate_program` run in Python mode
  ([suites](2026-11-22-decision-check/suites-summary.txt)).

## The DFS loop, 2026-11-22

Source: `d60bb7f3` plus `search/dfs_search.hpp` (`DfsSearch`: `search_goals` from its first publish through the main
loop and finalization, owning the typed state of `search/dfs_state.hpp`; frames, discovery, scoring, selection,
refinement, recovery, windows, latches, peers and observations, with NodeSearch's bounds and eligibility). The
adapter's stage views, discovery and advance, anchor-leg and finish solves, the movement frontier (one generation at a
time, its callbacks reaching the state through the search) and progress saving stay Python hooks. Nothing dispatches
to it: the trial dispatch in `search_goals` (with its Python refactors, kept outside the repository) measured slower,
because every publish converts the whole state to a new Python dict and the frontier's generations go through Python.
The frontier and checkpoint ports remove both.

- With the trial dispatch, every published DFS state and every result state equals the previous native code's by
  value, in call order: the short Real11 control's 191, fixed work's 438 and the milestone test list's 328. The trial's
  Python refactors leave Python mode's states unchanged (fixed work 438, tests 328)
  ([states](2026-11-22-native-loop/state-parity.txt)).
- Reports equal the previous code's except provenance, and transitions match Python
  ([fixed work](2026-11-22-native-loop/fixed-report-vs-previous.json),
  [short](2026-11-22-native-loop/short-report-vs-previous.json),
  [fixed parity](2026-11-22-native-loop/fixed-parity-python-native.json),
  [short parity](2026-11-22-native-loop/short-parity-python-native.json)).
- Search seconds, alternating one at a time: fixed work 60.2 and 60.3 against 17.4 and 17.4 before; the short control
  5.24 and 5.24 against 1.60 and 1.60 ([times](2026-11-22-native-loop/search-seconds.txt)).
- A checkpoint saver finds the repair's program and node entry in the published state by identity; the trial publishes
  the open generation's live Python report inside each state so the saver's incremental repair checkpoints still apply
  (`test_checkpoint_store`'s native persistence check).
- The search, checkpoint and native test modules run clean under AddressSanitizer, and fixed work runs clean with a
  report equal to the release build's except provenance ([summary](2026-11-22-native-loop/asan-summary.txt),
  [report](2026-11-22-native-loop/fixed-asan-report-vs-release.json)). The milestone test list passes in both modes
  ([suites](2026-11-22-native-loop/suites-summary.txt)).

## The movement frontier, 2026-11-22

Source: `94f29ba1` plus `search/frontier.hpp` (`Frontier`: `persistent_programs` on the path native expansion takes,
over a typed continuation that frames and windows now hold) run by `DfsSearch` with its decision checks
(`search/dfs.hpp`'s `evaluate_node`, whose new `solve_route` hook sends a route the native node solve leaves to Python
to the Python node solve). Native mode never takes the frontier's Python proposal path, leaves no decision check to
Python and resumes no Python proposal in flight (short control, fixed work, milestone tests); that path stays
Python's. Nothing dispatches to it: with the trial dispatch, every publish still converts the whole state to a new
Python dict, which the checkpoint port removes.

- With the trial dispatch, every published and result state equals the previous native code's by value in call order
  (short control 191, fixed work 438) and in 44 of the milestone list's 46 searching tests; the other two stub
  `iter_movement_programs`, which the native frontier does not call. With every decision check's route forced onto
  the Python node solve, the short control's 191 states and the same 44 tests' states equal the Python fallback's
  ([states](2026-11-22-native-frontier/state-parity.txt)).
- Reports equal the previous code's except provenance, and transitions match Python
  ([fixed work](2026-11-22-native-frontier/fixed-report-vs-previous.json),
  [short](2026-11-22-native-frontier/short-report-vs-previous.json),
  [fixed parity](2026-11-22-native-frontier/fixed-parity-python-native.json),
  [short parity](2026-11-22-native-frontier/short-parity-python-native.json)).
- Search seconds, alternating one at a time: fixed work 63.6 and 67.4 against 24.3 and 18.5 before; the short control
  5.29 and 5.42 against 1.69 and 1.69 ([times](2026-11-22-native-frontier/search-seconds.txt)).
- The search, checkpoint and native test modules run clean under AddressSanitizer with the trial dispatch, and fixed
  work runs clean with a report equal to the release build's except provenance
  ([summary](2026-11-22-native-frontier/asan-summary.txt),
  [report](2026-11-22-native-frontier/fixed-asan-report-vs-release.json)).
- The milestone test list passes in both modes ([suites](2026-11-22-native-frontier/suites-summary.txt)). The native
  core's published-state round trip now takes its states from a search with the real frontier: the stubbed one it
  used published generations `persistent_programs` never writes, which the typed generation rejects.

## Native checkpoints, 2026-11-22

Source: `ebde9131` plus `io/checkpoint_store.hpp` (`CheckpointEncoder`: the store's object encoding, tagged objects
and content keys) and `DfsSearch` publishing frozen snapshots: the state as one immutable value, with the paths of the
repair program and targets a checkpoint saver patches (by shared program, as the saver finds them by identity). The
store itself (SQLite, manifest, patches, reading) stays Python.

- The native encoding equals `CheckpointStore.archive`'s byte for byte: every recorded published state gives the same
  root key, a hash over every chunk below it (short control 191, fixed work 438, milestone tests 325)
  ([parity](2026-11-22-native-checkpoint/encode-parity.txt)).

## The native DFS loop in production, 2026-11-22

Source: `e8010c83` plus the dispatch in `search_goals`: under `PKC_NATIVE_CORE`, an anchor search whose gate has a
native core and whose program filter is at most an initial jump domain runs `DfsSearch` (loop, frontier, decision
checks) and publishes its snapshots; `io/search_checkpoint` saves them through the native encoding and the paths they
name. Measured faster, so it dispatches.

- Every published and result state equals the previous production code's by value in call order (short control 191,
  fixed work 438) and in 45 of the milestone list's 46 searching tests; the other's search changed in `ebde9131`
  ([states](2026-11-22-native-dispatch/state-parity.txt)).
- Reports equal the previous code's except provenance, with and without durable checkpoints, and transitions match
  Python ([fixed work](2026-11-22-native-dispatch/fixed-report-vs-previous.json),
  [short](2026-11-22-native-dispatch/short-report-vs-previous.json),
  [fixed durable](2026-11-22-native-dispatch/fixed-durable-report-vs-previous.json),
  [short durable](2026-11-22-native-dispatch/short-durable-report-vs-previous.json),
  [fixed parity](2026-11-22-native-dispatch/fixed-parity-python-native.json),
  [short parity](2026-11-22-native-dispatch/short-parity-python-native.json)).
- Search seconds, alternating one at a time: fixed work 17.45 and 17.47 against 17.89 and 17.86 before, the short
  control 1.632 and 1.627 against 1.634 and 1.621; with durable checkpoints fixed work 58.3 and 59.1 against 123.8 and
  125.8, the short control 9.40 and 9.21 against 15.88 and 15.77 ([times](2026-11-22-native-dispatch/search-seconds.txt)).
- `test_checkpoint_store`'s native check compares the store with the live state after every repair patch and passes.
  The search, checkpoint and native test modules run clean under AddressSanitizer, and durable fixed work runs clean
  with a report equal to the release build's except provenance
  ([summary](2026-11-22-native-dispatch/asan-summary.txt), [report](2026-11-22-native-dispatch/fixed-asan-report-vs-release.json)).
  The milestone test list passes in both modes; two DFS tests that read the Python frontier's arguments run in Python
  mode ([suites](2026-11-22-native-dispatch/suites-summary.txt)).

## The node solve's services, 2026-11-22
Source: `41163279` plus the node solve's gate analysis and face tables on the gate's native core
(`InteractionGate::analyze_model`: the coefficient and analysis caches under Python's keys), the relaxed dispatch
(`numerics/disk.hpp` `relaxed_dispatch`, its confirmations in the core's cache as Python keeps them in `gate.cache`;
the Python `RelaxedDispatch` hands a gate with a core to it) and the IPOPT driver (`numerics/solver.hpp`
`solve_numerical`: worker lifecycle, deadline, cancellation, protocol and validation). A native node solve now calls
Python only for the adapter's candidate check, the compiler and the work budget.

- The IPOPT driver equals `numerics.solver.solve` on the Python suite's scripted workers (timeouts, cancellation,
  malformed output, a missing worker, exit codes, stderr, first-feasible stops, seeds) and 21 captured production
  requests: 38 of 38 ([cases](2026-11-22-node-services/solver-parity.json)). The native JSON parser's error message now
  has Python's `line L column C (char N)` form.
- The relaxed dispatch equals Python's on the 144 captured candidate requests, each as `solve` and as `confirmed`,
  each side in its own process: 288 of 288, with 61 confirmations (20 cached) and 4 IPOPT fallbacks
  ([cases](2026-11-22-node-services/dispatch-parity.json)).
- Every published and result state equals the previous production code's by value in call order (short control 191,
  fixed work 438) and in all 46 of the milestone list's searching tests ([states](2026-11-22-node-services/state-parity.txt)).
- Reports equal the previous code's except provenance and two counters, with and without durable checkpoints, and
  transitions match Python: node-solve coefficient compiles now go through the native gate's cache, so the Python
  compiler's `compile` count falls (52 to 49 on the short control) and some coefficient misses become hits, as in
  Python mode's single gate cache ([fixed work](2026-11-22-node-services/fixed-report-vs-previous.json),
  [short](2026-11-22-node-services/short-report-vs-previous.json),
  [fixed durable](2026-11-22-node-services/fixed-durable-report-vs-previous.json),
  [short durable](2026-11-22-node-services/short-durable-report-vs-previous.json),
  [fixed parity](2026-11-22-node-services/fixed-parity-python-native.json),
  [short parity](2026-11-22-node-services/short-parity-python-native.json)).
- Search seconds, alternating one at a time against `41163279` with its own native build: fixed work 17.03 and 16.52
  against 18.33 and 18.09, the short control 1.677 and 1.593 against 1.668 and 1.602; with durable checkpoints fixed
  work 58.1 and 57.7 against 57.7 and 58.3, the short control 9.29 and 9.05 against 9.40 and 9.15
  ([times](2026-11-22-node-services/search-seconds.txt)).
- The milestone test list passes in both modes ([suites](2026-11-22-node-services/suites-summary.txt)). Under
  AddressSanitizer the search, checkpoint, native, disk, solver and proposal-analysis modules, durable fixed work and the
  driver cases run with no reports, and durable fixed work's report equals the release build's except provenance
  ([summary](2026-11-22-node-services/asan-summary.txt), [report](2026-11-22-node-services/fixed-asan-report-vs-release.json)).

## The leg solves, 2026-11-23
Source: `cd3100ce` plus `DfsSearch::evaluate_leg`: the native search's anchor-leg and finish solves run as its decision
checks' node solves do, after the gate's preparation of the leg's scene (`prepare_leg`), through the stage view's
solve services (`begin_leg`) and `search/engine.hpp`'s `evaluate_prepared`. A route with passages, wall hits or ladder
events still goes to the Python `evaluate` hook.

- Every published and result state equals the previous production code's by value in call order (short control 191,
  fixed work 438) and in all 46 of the milestone list's searching tests, also with every leg solve sent to the Python
  hook as an uncovered route (9, 3 and 38 solves diverted) ([states](2026-11-23-leg-solves/state-parity.txt)).
- Reports equal `cd3100ce`'s except provenance and the same two counters as before: leg-solve coefficient compiles now
  share the native gate's cache, so the Python compiler's `compile` count falls (49 to 41 on the short control, 114 to
  111 on fixed work); transitions match Python ([fixed work](2026-11-23-leg-solves/fixed-report-vs-previous.json),
  [short](2026-11-23-leg-solves/short-report-vs-previous.json),
  [fixed durable](2026-11-23-leg-solves/fixed-durable-report-vs-previous.json),
  [short durable](2026-11-23-leg-solves/short-durable-report-vs-previous.json),
  [fixed parity](2026-11-23-leg-solves/fixed-parity-python-native.json),
  [short parity](2026-11-23-leg-solves/short-parity-python-native.json)).
- Search seconds, alternating one at a time against `cd3100ce` with its own native build: fixed work 16.40 and 16.75
  against 16.59 and 16.28, the short control 1.258 and 1.312 against 1.643 and 1.613; with durable checkpoints fixed
  work 54.98 and 55.03 against 56.10 and 55.99, the short control 7.81 and 7.74 against 8.98 and 8.96
  ([times](2026-11-23-leg-solves/search-seconds.txt)).
- One test records `engine.solve_contact_branches`, which a native leg solve never calls; it runs in Python mode. The
  milestone test list passes in both modes ([suites](2026-11-23-leg-solves/suites-summary.txt)). Under AddressSanitizer
  the search, checkpoint, native, disk, solver and proposal-analysis modules and durable fixed work run with no reports,
  and durable fixed work's report equals the release build's except provenance
  ([summary](2026-11-23-leg-solves/asan-summary.txt), [report](2026-11-23-leg-solves/fixed-asan-report-vs-release.json)).

## Main's PR #10 in the native core, 2026-11-23
Source: the merge of main at PR #10 (`a6d67405`) plus the native port of its behaviour: O10 for every move (the
direction class `leg_class`, `behind_decision` per region with the need in a disallowed move, `entry_backward` on the
allowed moves, the checks in steps, slab boosts, corner cases and blip-ups), one solver margin and one replay tolerance
(`schema.hpp`'s `kSolverMargin`/`kReplayTolerance`; scenes and routes no longer carry `supportClearance`), Clarabel
verdicts (a limit retried once at `RETRY_CAP`/`RETRY_MAX_ITER`, every other unsolved status infeasible, no undecided
screen, dispatch or route pre-solve check), settled-stretch obstacle repairs, the stepping cases (corner blip,
take-off ceilings, corner ledges and the corner jump cancel's airtime) and the held side variants (O12: the
generator's tags, the frontier's side reserves and held side entries, the decision check's `sideOutcome`).

- Python mode is the oracle on the merged tree: every published and result state equals it in call order (short
  control 191, fixed work 454) ([states](2026-11-23-main-pr10/state-parity.txt)), transitions match
  ([fixed](2026-11-23-main-pr10/fixed-parity-python-native.json), [short](2026-11-23-main-pr10/short-parity-python-native.json)),
  and the reports differ in cache and compiler counters and in the order of the gate's exclusion log, which held the
  same entries before the merge too ([order](2026-11-23-main-pr10/exclusion-order.txt)).
- Every open generator call of a Python-loop search, run both ways, gives the same choices and exclusions: 141 calls on
  the short control and 543 on fixed work, with 638 corner blips, 168 side-variant tags and 671 O10 exclusions
  ([short](2026-11-23-main-pr10/g1-short-generators.json), [fixed](2026-11-23-main-pr10/g2-fixed-generators.json)).
- The milestone list passes in both modes; two side-variant tests that stub Python seams run in Python mode
  ([suites](2026-11-23-main-pr10/suites-summary.txt)). AddressSanitizer runs clean
  ([summary](2026-11-23-main-pr10/asan-summary.txt), [report](2026-11-23-main-pr10/fixed-asan-report-vs-release.json)).
- Search seconds, alternating Python mode and native mode: fixed work 40.28 and 39.46 against 14.17 and 13.83, the
  short control 3.946 and 3.835 against 1.288 and 1.257; with durable checkpoints fixed work 125.6 and 129.9 against
  55.97 and 55.31, the short control 17.03 and 16.88 against 8.55 and 8.45 ([times](2026-11-23-main-pr10/search-seconds.txt)).

## The anchor adapter, 2026-11-23
Source: `5f917520` plus the native anchor adapter (`search/anchors/adapter.hpp` on `spatial/event_routes.hpp`):
initial cursors, goal discovery, stage facts (the guide slice, `onward_objective`, the score context and corridor),
advance with `observed_route`, and the candidate check (the anchor audit, then the event check with its support
episodes; its `eventAudits` and `requiredEventRejections` counts join the Python adapter's summary). The native search
no longer calls Python for them; its node and leg solves take the stage's objective, identity and candidate check
natively, and only the Python solves of routes with passages, wall hits or ladder events build a Python stage view.

- Every adapter call of a Python-loop search, run both ways, gives the same result: on the short control 1 initial
  cursor, 7 discoveries, 8 stage views, 19 candidate checks and 6 advances, on fixed work 1, 4, 4, 76 and 3, with equal
  counts ([short](2026-11-23-anchor-adapter/short-adapter-parity.json),
  [fixed](2026-11-23-anchor-adapter/fixed-adapter-parity.json)). Across the milestone list: 57, 118, 148, 127 and 62
  calls, plus 2,292 perturbed candidate checks (other events' goals, shifted cursors, both node modes) reaching 454
  `missing_prior_anchor` and 492 `required_event_not_observed` rejections, all equal, with equal counts
  ([tests](2026-11-23-anchor-adapter/tests-adapter-parity.json)).
- Python mode is the oracle: every published and result state equals it in call order (short control 191, fixed work
  454, and the milestone list's searching tests except two `disk.cached` flags that earlier tests' process-wide cache
  sets) ([states](2026-11-23-anchor-adapter/state-parity.txt)); transitions match
  ([fixed](2026-11-23-anchor-adapter/fixed-parity-python-native.json),
  [short](2026-11-23-anchor-adapter/short-parity-python-native.json)) and the reports differ as before
  ([fixed](2026-11-23-anchor-adapter/fixed-report-python-vs-native.json),
  [short](2026-11-23-anchor-adapter/short-report-python-vs-native.json)).
- Reports equal the previous code's except provenance, plain and durable
  ([fixed](2026-11-23-anchor-adapter/fixed-report-vs-previous.json),
  [short](2026-11-23-anchor-adapter/short-report-vs-previous.json),
  [fixed durable](2026-11-23-anchor-adapter/fixed-durable-report-vs-previous.json),
  [short durable](2026-11-23-anchor-adapter/short-durable-report-vs-previous.json)).
- The milestone list passes in both modes ([suites](2026-11-23-anchor-adapter/suites-summary.txt)). AddressSanitizer
  runs clean ([summary](2026-11-23-anchor-adapter/asan-summary.txt),
  [report](2026-11-23-anchor-adapter/fixed-asan-report-vs-release.json)).
- Search seconds, alternating against `5f917520` with its own native build: fixed work 14.18 and 14.38 against 14.80
  and 14.31, the short control 1.297 mean of five against 1.365; durable fixed work 56.68 and 54.67 against 56.50 and
  56.66 ([times](2026-11-23-anchor-adapter/search-seconds.txt)).

## The setup builders, 2026-11-23
Source: `2060b2ee` plus native ports of the anchor search's setup: `movement/settings.hpp` (`movement_settings`),
`search/goals.hpp` (`goal_spec`), `spatial/event_routes.hpp` (`surface_groups`, `build_route_domain`,
`build_event_graph`) and `search/anchors/policy.hpp` (the anchor policy's `settings` and `semantic_settings`,
`build_guides`, `encounter_groups` on a native gate's face tables); `py_math.hpp`'s `python_norm` is CPython's
`vector_norm` for `math.dist`. Only `build_route_domain` (handing back its canonical payload) and `encounter_groups`
dispatch: on the fixed-work input the others cost more as native calls than in Python, their conversions included.

- Every builder call, run both ways, gives the same result or the same error message: across the milestone list 66
  goal specs, 94 anchor policies, 322 movement settings, 605 semantic settings, 84 route domains, 76 event graphs, 79
  guide plans and 80 encounter-group partitions, and on the short control and fixed work, except one semantic settings
  call in a test that patches `HARD_JUMP_POLICY_VERSION` ([tests](2026-11-23-setup/tests-setup-parity.json),
  [short](2026-11-23-setup/short-setup-parity.json), [fixed](2026-11-23-setup/fixed-setup-parity.json)).
- Builder medians on the fixed-work input: the route domain 84.1 ms dispatched against 91.9 ms, encounter groups 2.3 ms
  against 6.7 ms; the event graph, guides and anchor policy take 3.4, 3.3 and 0.66 ms natively against 0.18, 0.14 and
  0.08 ms in Python ([times](2026-11-23-setup/builder-times.txt)).
- Python mode is the oracle: every published and result state equals it in call order (short control 191, fixed work
  454, the milestone list's searching tests except the two `disk.cached` flags) ([states](2026-11-23-setup/state-parity.txt));
  transitions match ([fixed](2026-11-23-setup/fixed-parity-python-native.json),
  [short](2026-11-23-setup/short-parity-python-native.json)), and reports equal the previous code's except provenance,
  plain and durable ([fixed](2026-11-23-setup/fixed-report-vs-previous.json),
  [short](2026-11-23-setup/short-report-vs-previous.json),
  [fixed durable](2026-11-23-setup/fixed-durable-report-vs-previous.json),
  [short durable](2026-11-23-setup/short-durable-report-vs-previous.json)).
- The milestone list passes in both modes ([suites](2026-11-23-setup/suites-summary.txt)). AddressSanitizer runs clean
  ([summary](2026-11-23-setup/asan-summary.txt), [report](2026-11-23-setup/fixed-asan-report-vs-release.json)).
- Search seconds, alternating against `2060b2ee` with its own native build: fixed work 14.38 and 14.04 against 15.05 and
  14.47, the short control 1.356 and 1.316 against 1.289 and 1.306; durable fixed work 54.14 and 54.05 against 54.59
  and 53.80 ([times](2026-11-23-setup/search-seconds.txt)).

## Native report encoding, 2026-11-23
Source: `d9519289` plus `CheckpointStore.archive` handing each plain JSON container to the native encoder
(`io/checkpoint_store.hpp`, already used for native snapshots) when it has no targets, walking in Python only the
levels that hold stored references or native values. The conversion keeps a container the tree holds in several
places as one value, so the encoder's identity memo reuses its encoding as the Python walk's does.

- Python mode is the oracle: every published and result state equals it in call order (short control 191, fixed work
  454, the milestone list's searching tests except the two `disk.cached` flags) ([states](2026-11-23-report-encode/state-parity.txt));
  transitions match ([fixed](2026-11-23-report-encode/fixed-parity-python-native.json),
  [short](2026-11-23-report-encode/short-parity-python-native.json)), and reports equal the previous code's except
  provenance, plain and durable ([fixed](2026-11-23-report-encode/fixed-report-vs-previous.json),
  [short](2026-11-23-report-encode/short-report-vs-previous.json),
  [fixed durable](2026-11-23-report-encode/fixed-durable-report-vs-previous.json),
  [short durable](2026-11-23-report-encode/short-durable-report-vs-previous.json)).
- The milestone list passes in both modes ([suites](2026-11-23-report-encode/suites-summary.txt)). AddressSanitizer runs
  clean ([summary](2026-11-23-report-encode/asan-summary.txt),
  [report](2026-11-23-report-encode/fixed-asan-report-vs-release.json)).
- Alternating against `d9519289` with its own native build: durable fixed work searches in 42.56 and 42.50 s against
  53.40 and 53.03 s, the durable short control in 5.68 and 5.93 s against 8.12 and 8.14 s; without durable saves the
  whole run (its final save included) takes 15.00 and 15.13 s against 16.15 and 16.08 s on fixed work and 1.79 and
  1.81 s against 2.23 and 2.28 s on the short control. Fixed work's final save alone falls from 2.48 s to 1.36 s
  ([times](2026-11-23-report-encode/search-seconds.txt)).

## The checkpoint encode cache, 2026-11-23
Source: `b502cf66` plus a native encode cache per checkpoint store (`io/checkpoint_store.hpp` `EncodeCache`): the
reference of every immutable native container the store stored as its own object, watched without keeping it alive,
so an unchanged program, attempt log or frozen part costs a lookup in the next save. `SearchCheckpoint` hands the store
native values: the snapshot (or a dict state converted once when no alias needs finding), one frozen copy of the event
search's domain and graph, and a snapshot's repair parts unconverted. Before it, a durable save re-encoded the whole
state: on fixed work the saver took 28.6 of the native loop's 42.4 s. The store's Python walk also no longer treats a
native value inside a small dict as a JSON scalar.

- Python mode is the oracle: every published and result state equals it in call order (short control 191, fixed work
  454, the milestone list's searching tests except the two `disk.cached` flags) ([states](2026-11-23-encode-cache/state-parity.txt));
  transitions match ([fixed](2026-11-23-encode-cache/fixed-parity-python-native.json),
  [short](2026-11-23-encode-cache/short-parity-python-native.json)), and reports equal the previous code's except
  provenance, plain and durable ([fixed](2026-11-23-encode-cache/fixed-report-vs-previous.json),
  [short](2026-11-23-encode-cache/short-report-vs-previous.json),
  [fixed durable](2026-11-23-encode-cache/fixed-durable-report-vs-previous.json),
  [short durable](2026-11-23-encode-cache/short-durable-report-vs-previous.json)). The frozen domain and graph kept
  their digests across every save.
- The milestone list passes in both modes ([suites](2026-11-23-encode-cache/suites-summary.txt)). AddressSanitizer runs
  clean ([summary](2026-11-23-encode-cache/asan-summary.txt),
  [report](2026-11-23-encode-cache/fixed-asan-report-vs-release.json)).
- Alternating against `b502cf66` with its own native build: durable fixed work searches in 16.93 and 17.88 s against
  43.81 and 44.95 s (whole run 20.06 and 21.21 s against 48.99 and 50.46 s), the durable short control in 2.46 and
  2.48 s against 5.83 and 6.73 s; peak memory 963 and 916 MiB against 865 and 804 MiB. Plain runs are unchanged
  ([times](2026-11-23-encode-cache/search-seconds.txt)). A cache that held its containers ran as fast at 1346 and
  1384 MiB; without reuse across saves a durable fixed run took 37.31 s.

## The result state keeps its shared containers, 2026-11-23
Source: `2b2ba7d6` plus `native.state()` converting each container the native state holds in several places to one
Python object, as the Python search's state shares its dicts; it used to copy every occurrence, so the final report
save converted and encoded each copy.

- States equal Python mode (short control 191, fixed work 454, the milestone list's searching tests except the two
  `disk.cached` flags); reports equal the previous code's except provenance, plain and durable; the milestone list
  passes in both modes and AddressSanitizer runs clean ([checks](2026-11-23-shared-state/checks.txt),
  [fixed](2026-11-23-shared-state/fixed-report-vs-previous.json), [short](2026-11-23-shared-state/short-report-vs-previous.json)).
- Fixed work, alternating against `2b2ba7d6`: whole run 14.92 and 14.95 s against 15.62 and 15.41 s, peak memory 750
  and 756 MiB against 895 and 877 MiB. Real11 extended at 300 s (one native run each): final report save 18.44 s
  against 29.61 s, peak memory 2,842 MiB against 3,639 MiB ([times](2026-11-23-shared-state/times.txt)).

## Main's PR #12 in the native core, 2026-11-23
Source: the merge of main at PR #12 (`5cb0d44f`) plus the native port of its behaviour: Clarabel refinement off,
validator-rejected relaxed points infeasible (`disk_rejected`), the disk problem built once and re-solved with cut and
level rows and new linear costs (`DiskProblem`), the Clarabel-only post-alarm pipeline (`numerics/post_alarm.hpp`),
`RelaxedDispatch.full_strength` with its frame-end cache, `warmYaw`, the search's frame-end re-solves in place of the
finalization, frames finishing their queue before choosing, unresolved branches infeasible and the confirmation
counters. Native leg solves report their requests to the profiler, and a use-after-free in the decision check's
minimum-run deferral (a pointer to an annotation read after the annotations moved) is fixed; the full queue made it
reachable.

- The pipeline in each mode, each side in its own process: the 7 fixture requests and the 482 alarmed Real11 requests
  give the same method, steps, solves, reversal tick and objective, to the bit
  ([pipeline](2026-11-23-pr12/pipeline-parity.txt)). Frame-end answers with real IPOPT polishes on the fixtures and
  the 401 unalarmed and 482 alarmed Real11 requests: same records, facings and objectives
  ([frame ends](2026-11-23-pr12/full-strength-parity.txt)).
- Every published and result state equals Python mode in call order (short control 186, fixed work 468), transitions
  match ([states](2026-11-23-pr12/state-parity.txt), [fixed](2026-11-23-pr12/fixed-parity-python-native.json),
  [short](2026-11-23-pr12/short-parity-python-native.json)). `requests.json` rows are equal in both modes on the short
  control and on `cases/real11/physical-anchors.json`, which finds the same T64 route (objective
  -1093.2383028783543, 47 requests, 8 frame-end solves, 8/8 replays) in 1.48 s against 5.11 s
  ([requests and physical anchors](2026-11-23-pr12/requests-and-physical-anchors.txt)).
- The gate and the milestone list pass in both modes; the cancelled frame-end test, which cancels through
  `RelaxedDispatch`'s Python seams, runs in Python mode ([suites](2026-11-23-pr12/suites-summary.txt)).
  AddressSanitizer runs clean ([summary](2026-11-23-pr12/asan-summary.txt),
  [report](2026-11-23-pr12/fixed-asan-report-vs-release.json)).
- Real11 extended at 300 s, one run each: native mode makes 1,844 solver calls and expands 554 prefixes against 776 and
  243; neither finds a finish ([run](2026-11-23-pr12/real11-extended-300s.txt)).
- Search seconds, alternating Python mode and native mode: fixed work 37.32 and 38.99 against 12.23 and 12.42, the
  short control 4.88 and 5.46 against 1.55 and 1.53; with durable checkpoints fixed work 132.69 and 125.38 against
  16.55 and 16.15, the short control 15.65 and 15.97 against 2.48 and 2.54 ([times](2026-11-23-pr12/search-seconds.txt)).

## Native coverage of the resume tests, 2026-11-23
The request observer now hears each natively dispatched request before and after it is answered, with its entry point
and seconds, so the two resume tests that record or cancel at `RelaxedDispatch`'s Python seams run in both modes: the
cancelled frame-end test and the bridge's saved-search test. In native mode they record the same requests and
frame-end requests as Python mode, fingerprint for fingerprint, and the profiler's `requests.json` rows are unchanged
([evidence](2026-11-23-observer/resume-tests.txt)).

## The native checkpoint saver, 2026-11-23
Source: `ef1f5919` plus a native checkpoint store (`io/store.hpp`: SQLite, zlib, the manifest, patches and compaction
on the Python store's files and objects) and a native search saver (`io/search_checkpoint.hpp`): under the native
core `CheckpointStore` writes through the native store, and a `SearchCheckpoint` on it hands the native anchor search
a saver, so the search's full saves and repair patches are written with no Python call. The search's final snapshot
goes to the checkpoint too, so the final save encodes the result's state from it. The native encoder's per-archive
memo now holds each container it names (it keyed raw addresses, so an encoded temporary's address could return a
later temporary's stale encoding), and a failed native file write raises the `OSError` Python raises.

- Same objects: in one run, every snapshot saved by the Python saver is also saved by a native saver on a second
  store, and after every save and patch both manifests name the same root: 185 of 185 on the short control and 467 of
  467 on fixed work ([short](2026-11-23-saver/store-parity-short.json), [fixed](2026-11-23-saver/store-parity-fixed.json));
  `tests/unit/io/test_checkpoint_store.py` checks the same in the suite. Two runs cannot be compared store to store:
  their timing fields differ.
- The suite passes in both modes (873 tests, 10 skipped as before), the gate in both modes (621, no failures, errors
  or skips), AddressSanitizer runs clean on the search, checkpoint and numerics tests (the known
  `test_cli_writes_structured_error_atomically` harness failure aside) and on durable fixed work, whose report equals
  the release build's.
- Durable, alternating with `ef1f5919`: reports equal; fixed work 15.00 s and 14.97 s against 15.79 s and 15.84 s,
  the short control 2.21 s and 2.16 s against 2.47 s and 2.43 s; on fixed work the 469 Python save callbacks (3.71 s)
  become 467 native saves and patches (2.08 s) and the final save takes 1.05 s instead of 1.84 s
  ([runs](2026-11-23-saver/durable-ab.txt)). Real11 was not rerun.

## The native solve entry, 2026-11-23
Source: `69dee392` plus `search/application.hpp` (`solve_case`: `search/application.run` through
`find_checkpoint_route`, the event search's sessions and the native anchor search, with no Python) and `work.hpp`
(the work budget), behind the `pkc_solve` executable and a `native.solve_case` binding. The services a Python-hosted
native search gets from Python run natively: the compiler (coefficient tables, scene checks and footprints, counted
as `CountingCompiler` counts), the interaction gate's preparation cache and its interval analysis, the node solves'
services, the transfer-time bounds (with their ceiling proofs), `motion_bound` and the final witness replay. Routes
with wall hits or ladder events, which the native solves leave to Python, are rejected; the search forces
`maxWallHits=0` and runs without ladders. `pkc_solve` mirrors `cli/solve.py` (checkpoint report, `--save-progress`,
SIGINT/SIGTERM cancel with exit 130) without `--resume`; CMake's `PKC_PYTHON_MODULE=OFF` builds it, the core and the
worker with no Python at configure time.

- Results equal the Python-hosted native run's, each side in its own process and without wall-clock fields, on the
  eight straight calibration courses, physical-anchors with and without anchors, fixed work and the short control:
  no differing path in any of the 12 ([parity](2026-11-23-entry/parity.txt)). The native transfer-time bounds equal
  Python's on the ceiling scenes of `test_transfer_time_bounds` and the Real11 anchor contexts.
- `pkc_solve` links no Python and runs with an empty environment; its report equals the Python CLI's, a cancelled
  durable run resumes in the Python CLI to the uninterrupted route, and a Python-free CMake build solves
  ([executable](2026-11-23-entry/executable.txt)). `tests/integration/test_native_core.py` `SolveEntryTests` checks
  the report and the transfer bounds in the suite. AddressSanitizer runs clean on `pkc_solve` (durable physical-anchors
  and fixed work) and on the native-core and checkpoint-store tests. The suite passes in both modes (875 tests, 10
  skipped as before) and the gate in both modes (621, no failures, errors or skips; [Python mode](2026-11-23-entry/gate-python-mode-report.json),
  [native mode](2026-11-23-entry/gate-native-mode-report.json)).

## Work-only mode, 2026-11-24
Source: `4df8943c` plus the case setting `work_only` ([runbook](../../docs/RUNBOOK.md#current-physical-anchor-solve-profile-and-resume)):
the run sets no deadline and lifts every time limit under it (per-request budgets, the disk relaxation's Clarabel
caps, the IPOPT wall limit, sent as `"seconds": null`, the post-alarm deadline, the frame-end re-solve's 600 s), and
keeps every work limit, so the same case gives the same outcome on any machine load. Python and native
(`work.work_only`, `pkc/work.hpp`) set it process-wide for a run, since the disk screens and their decided cache are
process-wide.

- On an idle machine at `4df8943c`, no time limit decided anything on the two controls (Python mode, counting
  wrappers around the disk attempts, IPOPT requests and the post-alarm pipeline): short control 89 Clarabel solves
  and fixed work 640, none `MaxTime` or `MaxIterations`, the slowest at 2.9 % and 5.7 % of the 0.1 s cap and at most
  21 % of `MAX_ITER`; 8 and 3 IPOPT requests, all `worker_finished` within 0.03 % of their budgets
  ([short](2026-11-24-work-only/caps-short.json), [fixed](2026-11-24-work-only/caps-fixed.json)). Lifting the time
  limits therefore keeps what a timed run finds there, rather than simulating a work clock.
- Short control and fixed work under `work_only`, each run in its own process without wall-clock fields
  ([log](2026-11-24-work-only/evidence.txt)): Python mode, Python-hosted native and the native entry each give the
  same result idle and under eight busy loops (load average 28.8), Python mode under a second `PYTHONHASHSEED` too;
  the native entry equals the hosted run; and every work-only result equals the timed run's in its mode. Python
  against native mode differs only as the timed runs already do (compiler and gate counters on the short control,
  gate exclusion records on fixed work: 4538 differing values in timed and work-only runs alike).
- `pkc_solve` reports what the Python CLI reports under `work_only` too (`SolveEntryTests`). The suite passes in both
  modes (880 tests, 11 skipped: the new Java-bridge test, like the others, needs the bridge) and the gate in both
  modes (626, no failures, errors or skips; [Python mode](2026-11-24-work-only/gate-python-mode-report.json),
  [native mode](2026-11-24-work-only/gate-native-mode-report.json)). AddressSanitizer runs clean on the durable fixed
  work run and the native-core, checkpoint-store, search and numerics tests; under it the Java bridge's infinite
  `queue.get` timeout was found and fixed (it now waits without one).
