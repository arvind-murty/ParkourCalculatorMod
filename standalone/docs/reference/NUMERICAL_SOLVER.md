# Standalone continuous IPOPT solver

This package, anchor orchestration and local replay share one implementation; see [architecture](../ARCHITECTURE.md).

The installed `parkour_solver.numerics` package provides a Python API and command-line solver for fixed-input horizontal trajectories. It runs independently of Minecraft. An optional headless Java exporter reads ordinary captured TAS files using the repository's existing physics compiler. The mod's engine, UI and save format are unchanged.

Implementation findings, validation results and archived research provenance are summarized in [MOVEMENT_PORT.md](MOVEMENT_PORT.md).

## Setup and first solve

The worker dependencies are pinned and tested with Python 3.12:

```sh
python3.12 -m venv ../.venv-guidance
../.venv-guidance/bin/python -m pip install --editable '.[numerics]'
../.venv-guidance/bin/python -m parkour_solver.cli.numerical \
  --input tests/fixtures/numerics/real11-400.json.gz \
  --seconds 10 --output /tmp/real11-result.json
```

The copied real11 fixtures contain 200 and 400 ticks, every facing row in their selected spans, prescribed wall contacts, and per-axis inertia. Their acceleration coefficients use the captured Force 45 / Derive policies. The fixture source and SHA-256 are recorded inside each request.

The parent process uses only the Python standard library. To run it from a different interpreter, supply `--python /absolute/path/to/the/worker/python` or set `PKC_IPOPT_PYTHON`. The worker needs CasADi and NumPy. Nothing installs or downloads automatically.

## Export a new captured TAS

From the repository root, with the usual JDK 21 Gradle configuration:

```sh
./gradlew -I standalone/native/gradle/export.gradle :core:exportStandaloneIpopt \
  -PipoptCapture=/absolute/path/to/captured-tas.json \
  -PipoptTicks=200 -PipoptOutput=/tmp/problem.json

../.venv-guidance/bin/python -m parkour_solver.cli.numerical \
  --input /tmp/problem.json --seconds 5 --output /tmp/result.json
```

The exporter accepts JSON or gzipped JSON with an angle-solver seed. It compiles the core and test fixture reader without running tests or launching Minecraft. Optional properties are `ipoptStart` (zero-based), `ipoptInputs` (`KEEP` or `FORCE_45`), and `ipoptSprint` (`DERIVE`, `ALWAYS`, or `NEVER`). Input and sprint settings, including per-tick overrides, otherwise come from the captured TAS. A changed start requires a recorded state at that tick.

If the checkout pins another machine's Gradle Java home, add `-Dorg.gradle.java.home=/absolute/path/to/jdk21` to the Gradle command.

All positional rows in the span are hard constraints, including a legal-mode goal wall. Start position and stored velocity are fixed to the captured state. Start-position bounds absorbed by the native compiler are restored when they add a restriction. An initial no-turn row is restored around the fixed start facing. Facing equalities use the typed target's normal 1e-4 equality corridor, without sine-table-cell widening. Relative references outside the span and displacement constraints on its initial state are rejected explicitly. Facing on the final state belongs to the next input and lies outside this span's facing domain. Smooth lambda must be zero.

The exporter selects the game's per-axis inertia threshold when supported. For a game model with combined horizontal inertia, it exports `threshold: 0` and records `perAxisInertiaSupported: false`; the standalone model then carries all velocity normally. The solver does not implement combined inertia or game arithmetic.

## Command-line options and results

`--feasibility-only` stops at the first independently validated feasible candidate. Otherwise the worker optimizes the objective and the parent retains the best valid candidate it sees. `--seconds` bounds worker startup, model construction and optimization together. A successful local optimization can finish earlier. `--initial-solution previous-result.json` takes that result's facings as a warm start and preserves the request's original facings as a fallback for automatic inertia continuation. Both use the same total time limit. `--threshold 0` disables inertia zeroing; `--no-recorded-walls` removes prescribed contacts. These options change the requested model explicitly.

The parent validates the initial facings before launching the worker and retains them if feasible. Feasibility-only mode can therefore return an already valid seed without starting IPOPT. An invalid seed remains an initial guess. During optimization, the worker also keeps the best automatically replayed path, uses its actual inertia pattern for polishing, and retains the best nominally feasible phase iterate instead of a failed final iterate. With a distinct fallback, the warm attempt receives 40% of the remaining worker budget and the fallback can use the rest. A successfully polished incumbent is not polished again unless the best path changes.

`--ladder-capture /path/to/captured-tas.json` attaches prescribed ladder ticks and vertical references from a Minecraft 1.8.x debug capture. It identifies climb states by stored upward velocity and horizontal collision, then includes other recorded states in those climb-proven cells and authored LADDER overrides. `--capture-start` overrides the zero-based capture start; otherwise `source.startTick` is used. Initial horizontal position and velocity must match the capture. Jump inputs are treated as taps; held-jump cooldown is outside this interface.

```sh
../.venv-guidance/bin/python -m parkour_solver.cli.numerical \
  --input tests/fixtures/numerics/real11-400.json.gz \
  --ladder-capture /path/to/real/11.json \
  --seconds 10 --output /tmp/real11-ladders-result.json
```

The copied `real11-200-ladders.json.gz` and `real11-400-ladders.json.gz` fixtures already contain this schedule and can be passed directly to `--input`. The original fixtures retain their original physics.

The result file is written atomically. Standard output contains one short JSON status. Exit codes are:

| Code | Status | Meaning |
| --- | --- | --- |
| 0 | `feasible` | The independent replay validated a candidate. |
| 2 | `unknown` | No validated feasible candidate was found. This is not an infeasibility proof. |
| 3 | `error` | Invalid input, missing runtime, worker failure or invalid protocol. |
| 130 | `cancelled` | Cancellation discarded the in-flight result. |

The file includes `candidate.yaw`, world-coordinate `candidate.positions`, stored `candidate.velocity`, objective, violation, first-feasible and total timings, termination reason, worker status, phase iterations and rejected-candidate diagnostics. Each phase identifies its `kind` (`carry`, `zero_events`, `fixed_pattern` or `incumbent_polish`) and attempt `seed` (`initial` or `fallback`). A timeout retains a validated incumbent. IPOPT success without a valid candidate yields `unknown`. If a worker errors, an already validated seed or candidate can still yield `feasible`, with the error retained in diagnostics. `workerStatus` describes the last completed phase; `feasible` does not certify objective optimality. SIGINT/SIGTERM cancel the command. A failed fixed-pattern solve is also only a failed local search for the broader problem.

With ladder/vertical fields, the candidate also includes inferred `ladderPattern`, `horizontalCollision`, `y`, `vy`, `onGround`, `verticalFailures` and the final vertical `resume` state. Phases record `capAttempt` and `ladderSaturated`. Initial vertical velocity is already stored carry. A completed horizontal collision, together with ladder occupancy at the next state, establishes that next state's climb velocity; the upward movement happens on the following tick. Preserve the stored velocities and previous collision flag when splicing.

## Python API

After installing `parkour_solver`, imports work from any cwd. This example uses a fixture path relative to the repository root:

```python
import gzip
import json
from parkour_solver.numerics import prepare, solve, validate

with gzip.open("tests/fixtures/numerics/real11-200.json.gz", "rt") as stream:
    problem = json.load(stream)

result = solve(problem, seconds=5, python_executable="/absolute/path/to/python")
if result["feasible"]:
    candidate = result["candidate"]
    checked = validate(prepare(problem), candidate["yaw"])
    exit_state = candidate["positions"][-1], candidate["velocity"][-1]
```

`solve` also accepts `stop_on_feasible`, a `threading.Event` as `cancel_event`, and an `on_incumbent(candidate)` callback. The callback receives a copy; keep it short. It is called for valid objective improvements, allowing a caller to save incumbents while a solve continues. Invalid API requests raise `ValueError` before starting a process. Caller orchestration chooses input patterns, stores multiple candidates and splices selected spans.

`from parkour_solver.io.capture import attach_capture` (also `parkour_solver.api.attach_capture`) provides the same capture attachment through `attach_capture(request, capture, start=None)`. It returns a prepared copy without changing either input.

## Problem format, version 1

Ticks are zero-based. Positions and stored velocities have `n+1` states, and yaw/input arrays have `n` entries. Position at state `t` precedes input `t`; that input produces state `t+1`. `base` is in radians; `yaw` and all facing constraints are in absolute degrees.

```json
{
  "version": 1,
  "ticks": 2,
  "position": [0.0, 0.0],
  "velocity": [0.0, 0.0],
  "magnitude": [0.1, 0.1],
  "friction": [0.91, 0.91],
  "base": [1.5707963267948966, 1.5707963267948966],
  "yaw": [-90.0, -90.0],
  "threshold": 0.005,
  "walls": [{"mode": "X", "t1": 2, "cmp": "GE", "rhs": 0.28}],
  "contacts": [],
  "objective": {"axis": "X", "sense": "MAX", "tick": 2},
  "seconds": 5,
  "stopOnFeasible": false
}
```

For each axis, the model zeroes an incoming component only if its absolute value is below `threshold`. It adds the fixed-magnitude control vector `magnitude[t] * (cos(base[t] + radians(yaw[t])), sin(base[t] + radians(yaw[t])))`, applies an optional ladder component cap, clips prescribed wall contacts and advances position. Normal collision velocity is reset; surviving components are multiplied by `friction[t]` to store the next velocity. IPOPT optimizes X/Z states and facings. Optional scalar Y replay validates the prescribed vertical schedule.

The historical `walls` field contains all scalar constraints. Modes `X`, `Z` and `F` evaluate a single state/input or combine `t1` and optional `t2` with `op: PLUS` or `MINUS`. Comparisons are `GE`, `LE` or `EQ`. Facing residuals wrap to (-180,180]. `DXZ` evaluates `abs(dx) - abs(dz)` and `DZX` reverses the axes, using the two referenced position states. Bounds/equality corridors from a TAS are compiled before export; a direct `EQ` row is checked as an equality at the solver tolerance.

The objective selects X or Z with `MAX`/`MIN`. Optional `customYaw` projects onto `(-sin(yaw), cos(yaw))`. `type: MOTION` requires a custom yaw and a positive tick; it projects that tick's displacement instead of position. `smoothLambda` must be zero. Optional `maxIterations` defaults to 10000. Optional `fallbackYaw` contains `n` facings for an alternative automatic-inertia initialization. Like `yaw`, it is excluded from the physical problem hash. Explicit inertia patterns and threshold-zero requests retain their single-phase solve.

A contact is `{"state": 356, "axis": 0, "plane": 700.4250000119209, "direction": -1}`. Axes 0/1 mean X/Z; direction +1 approaches an upper plane, -1 a lower plane. The proposed move must reach the plane from the allowed side, after which normal position is anchored and stored normal velocity is zero. Tangential motion continues. The NLP requires the move to cross the plane by `SOLVER_MARGIN` (1e-4, `schema.py`), so rounding never leaves a planned contact short. Independent replay refuses to reset a component for any missed plane, including a shortfall below the positional tolerance. The optional exporter infers this schedule from recorded collisions and zero normal velocities, including history before the selected start. An unresolvable direction causes an export error.

Optional `inertiaPattern` has two entries per input tick: `0` requires zeroing, `1` positive survival, `-1` negative survival, and `2` leaves the branch unrestricted. It requires a positive threshold. With no explicit pattern, the worker solves unrestricted carry, then inferred zero events, then complete fixed patterns, with bounded refresh attempts. Independent replay always applies the automatic threshold and checks any explicitly requested branches. Positional feasibility tolerance is `REPLAY_TOLERANCE` (1e-4, `schema.py`); branch mismatches and missed wall hits are rejected separately.

## Prescribed ladders and vertical replay

Optional `ladderTicks` contains `n+1` booleans, one for each state. On input tick `t`, each horizontal component after inertia and acceleration is clamped independently to `[-(double) 0.15F, (double) 0.15F]` before wall reach and friction. The final flag is needed to construct final climb carry.

Optional `ladderPattern` contains `n` pairs: `-1` selects negative saturation, `0` the free interval and `1` positive saturation. Non-ladder entries must be zero. Explicit branches remain fixed. Otherwise each phase infers branches from its seed and tries bounded neighboring cap regimes, including adjacent branches at an active boundary. The scalar validator always applies the actual clamp.

Optional `vertical` uses the Minecraft 1.8.9 profile:

```json
{
  "profile": "minecraft-1.8.9",
  "position": 5.0,
  "velocity": -0.0784000015258789,
  "onGround": true,
  "previousHorizontalCollision": false,
  "threshold": 0.005,
  "jumpVelocity": [null, 0.41999998688697815],
  "sneak": [false, false],
  "contacts": [{"state": 1, "plane": 5.0, "direction": -1}]
}
```

`jumpVelocity` and `sneak` have one entry per input. A non-null jump impulse requires the prior grounded state. Vertical inertia has its own threshold, independent of horizontal `--threshold 0`. Replay applies the downward ladder cap, sneak holding, gravity 0.08 and float 0.98 drag. Climb carry is `(0.2 - 0.08) * (double) 0.98F`.

Vertical contacts specify the player's feet Y at the resulting state. Direction `-1` is a floor and `1` a ceiling. A floor with `step: true` declares a step up to float 0.6 blocks, including a step during a grounded jump or an airborne landing. These are prescribed events, without supporting geometry checks. Optional `referencePosition` and `referenceVelocity` have `n+1` entries and become hard acceptance rows at the replay tolerance. Capture attachment supplies both.

Ladder ticks stay fixed when the path changes. Climb-proven cells infer a schedule for a captured route; they are not a complete world map. General geometry discovery and validation against ladder hitboxes remain future orchestration work.

## Validation and limits

Optional local Java replay supports exact movement arithmetic with declared contacts or discovery against a supplied static collision scene, neighboring-angle repair and `SaveIO` TAS export. It requires no running Minecraft client. See [REPLAY.md](REPLAY.md) for scene format, commands, regression evidence and supported mechanics. Continuous Python acceptance and local replay acceptance remain separate results.

See [MOVEMENT_PORT.md](MOVEMENT_PORT.md) for the current limitations and coefficient compilation workflow. The supplied legacy numerical fixtures retain their original sprint assumptions for benchmark reproduction; recompile them with the mod input state before using them for Minecraft replay. Local collision discovery covers the supported 1.8.9 static-block/ladder profile, not every Minecraft mechanic or a complete real11 world snapshot.

The parent validator uses standard-library scalar arithmetic in world coordinates. It reconstructs the path from returned facings, including actual automatic inertia and conditional wall clipping, and checks every requested row. The CasADi worker uses separate sparse state variables and relative positions. Returned positions or success flags are not trusted for acceptance.

These are local optimizations and prescribed-contact continuous trajectories. They are not global optimality or infeasibility certificates, general collision detection, or byte-exact Minecraft validation. Small authored constraints can disagree with the continuous coefficients: the original full real11 route has a recorded mismatch near states 651/652. The solver keeps its tolerances and reports the failed search. Prefer the intended shorter spans and validate realized game behavior separately.

## Disk relaxation

`numerics.disk` solves the convex relaxation of a threshold-0 request with
Clarabel (`reachability` extra): each tick's acceleration `|a[t]| == magnitude[t]`
becomes `|a[t]| <= magnitude[t] + 1e-9`. Every other row is the worker's, with
retained position/velocity/acceleration states: exact dynamics, fixed or interval
starts, contact resets with the `SOLVER_MARGIN` reach row, and X/Z walls. Equalities go in
Clarabel's zero cone and each tick's disk in a 3-dimensional second-order cone.
Settings are QDLDL, one thread, 200 iterations, a 0.1 s cap, `1e-9`
feasibility/gap tolerances (`1e-8` infeasibility) and iterative refinement off
(same verdicts, faster solves: [evidence](../../research/clarabel-refinement/README.md)); a run that ends `MaxIterations`
or `MaxTime` is solved once more with 500 iterations and a 0.5 s cap
(`retried: true`). Requests with an inertia
threshold, ladder ticks, an inertia pattern or non-X/Z rows have no spec.

`Solved`/`AlmostSolved` with a finite point give a relaxed point;
`PrimalInfeasible`/`AlmostPrimalInfeasible` exclude the full request (the disk
contains the circle). Every other status (a non-finite point, `NumericalError`,
`InsufficientProgress`, an exception, a limit hit on the retry too) is also
infeasible, so Clarabel never leaves a request undecided (user, 2026-11-22).
Results are cached process-wide by the canonical spec (1024 entries). `validation.numerical.validate_controls`
replays the returned acceleration vectors through the same scalar checks as
`validate` (which converts facings to full-strength vectors first) and also
rejects a vector outside its disk by more than `REPLAY_TOLERANCE`. `deficits` reports each
tick's relative acceleration shortfall.

`RelaxedDispatch(gate=...).solve` has `solve`'s signature and result
shape and is what automatic discovery hands `find_route`. A feasible relaxed point
that passes `validate_controls` returns `status: feasible` with the candidate
(`model: relaxed_disk`, `controls`, derived facings as a seed), `backend:
clarabel`, `termination: disk_finished` and `workerLaunched: false`; an infeasible
request returns `unknown` with the Clarabel status in `workerStatus`, and so does
a relaxed point the validator rejects (`termination: disk_rejected`; the replay
only fails a solved point by numerical error, user 2026-11-23). IPOPT answers, with
`backend: ipopt` and `fallback: unsupported`, only requests with no spec. Every relaxed result lists its
alarm ticks in `disk.alarmTicks`: jump ticks (`vertical.jumpVelocity`;
`alarm_ticks`; a request without that schedule never alarms) with a relative
acceleration shortfall above 10%. `solve` never confirms; the repair loop's
confirmation walk decides which alarmed candidates to confirm. `confirmed`
confirms a relaxed optimum of an optimization request (`stop_on_feasible=False`)
that alarms. A jump input always
applies its whole sprint-jump push, so a relaxed optimum that brakes it can be
impossible at full strength.
IPOPT re-solves the same request once and its result replaces the relaxed one.
Every tick is seeded with its disk direction turned by `SEED_TILT` (`1e-4` rad,
`ipopt_seed`), as the finalization is; the request's fallback facing stays. The
turn breaks mirror symmetry: on a symmetric request Clarabel returns the
symmetric optimum, whose short-tick direction lies exactly between the two
full-strength completions, and IPOPT started on that line stops at a local
infeasibility. The seed is an assumption resting on a measurement
([seed benchmark](../../research/disk-relaxation/README.md#confirmation-seed)):
of the single-attempt seeds tried it had no false infeasibilities and the fewest
worse optima at the same cost; revisit it if confirmations start rejecting
feasible requests. The result carries `confirmation`: the shortfall peak and
tick, both objectives, `gap = sense*(disk - ipopt)` and `material` (`gap > 1e-4`).
A gap describes only its own request. Decided
confirmations are cached per run in the interaction gate's cache by problem
fingerprint. Search's use of it is in the
[execution reference](../ANCHOR_SEARCH_EXECUTION_REFERENCE.md#numerical-backend-and-finalization).
`RelaxedDispatch(gate=...)` answers the alarm with the post-alarm pipeline below;
`post_alarm='ipopt'` makes that IPOPT call instead.

### Post-alarm pipeline

`numerics.post_alarm.run` answers an alarmed request with Clarabel alone; it never
calls IPOPT. Every constant was measured on 371 study and 482 Real11 alarmed
requests ([evidence](../../research/post-alarm/README.md)). The first acceptable
answer ends the search; later steps run only without one.

1. Setup: the request's own disk solve gives the bound (its objective) and the disk
   point; one more solve gives the central point (no objective). Push problems use
   the exact disk `|a| <= magnitude`. Setup solves follow `disk.solve`'s limits
   (`decide`); a limit the deadline cut short is no verdict.
2. Push: up to `PUSH_CAP` (10) solves of `w*cost - sum_t y_t.a_t` updated through
   `solver.update(q=...)`, from the central point (a child's warm prefix,
   `warmYaw`, starts at full strength along the parent's facings). `w` starts at
   0.3; after a full-strength step that follows another, `w` grows by 1.2 (to
   1e3) and `y` moves on with momentum 0.5; a short step after one halves `w` and
   returns `y` to the last full-strength point. A point is accepted only at full
   strength: every tick's shortfall below 1e-6 and `validate` passing at the
   point's facings, so every answer is a physical candidate (user, 2026-11-23).
   The push stops within `G` (1e-2) of its bound, after a gain below `G/10` over
   3 steps, or at the cap.
3. An accepted answer more than `G` below the bound gets up to 3 objective-cut
   probes: the row objective >= answer + 1e-2 (MAX; <= answer - 1e-2 for MIN)
   on a fresh problem, a 5-step decaying push (step 6) from its central point,
   and the next probe only after a better answer.
4. Without an answer: the sector check (7 solves) branches on the worst tick's
   direction, 4 quadrants and then halves, each sector relaxed to its chord; all
   sectors infeasible is a certificate (`quadrant_infeasible`).
5. Reversal race: the candidates are the jump ticks within 6 of an alarm tick and
   the disk point's most short tick of any kind (at least 1e-3 short). Each gets
   the cut `n.a_t >= 0.5 m_t` against the disk point's direction; feasible cut
   problems are screened with 5 push steps in order of their bound, stopping once
   the answer is at least the next candidate's bound. Only when no screen answers
   does the best-bound candidate's push go on, to 30 steps in all. A race answer
   gets the probes of step 3, which are not held by the cut.
6. Decaying push: the plain push again from the central point, up to `DECAY_CAP`
   (30) steps, with `w` halved after every step short of full strength until the
   first full-strength one (at a fixed 0.3 the objective can hold ticks short
   indefinitely). Its answer gets no probes.

The result has `solve`'s shape with `backend` `clarabel`: `feasible` with the
accepted candidate, `unknown` (`quadrant_infeasible`, `post_alarm_none`,
`time_limit`) or `cancelled`; `termination` names the step that answered
(`post_alarm_push`, `_probe`, `_race`, `_decay`). `confirmation` keeps the IPOPT
confirmation's fields (`ipoptObjective` is the answer's objective, `ipoptCalls`
is 0) and adds `method`, `pushSteps`, `probes`,
`reversalTick`, `raced`, `quadrant` and `clarabelSolves`. Answers, certificates
and `post_alarm_none` are cached like confirmations; a cache hit reports 0 solves.
The problem field `warmYaw` is a hint: validated by `prepare`, left out of
`fingerprint`, and read only by the pipeline.

## Tests

Standard-library tests need no CasADi installation:

```sh
python3 -m unittest discover -s standalone/tests/unit/numerics -t standalone -v
```

Enable the actual worker tests explicitly:

```sh
PKC_IPOPT_PYTHON="$PWD/../.venv-guidance/bin/python" \
  python3 -m unittest discover -s standalone/tests/unit/numerics -t standalone -v
```

The integration tests cover a known optimum, contradictory constraints reported as unknown, both real11 spans with all facing rows and contacts, warm-start objective recovery, a cap-regime optimization with an analytic optimum, and ladder/Y capture attachment through the CLI. Ladder fixtures must match every recorded Y position and stored vertical velocity. The reduced `real11-ladder-evidence.json.gz` reproduces the inferred first-400-tick schedule without depending on an external capture file.

Other tests cover independent replay, missed and wrong-side contacts, fixed patterns, relative/angular rows, malformed candidates, cancellation, timeout, seed retention, recovery after a failed phase, fallback exploration, rejected fake success, atomic CLI output, ladder timing, vertical contacts/steps and state handoff. The warm-facing fixture contains copied facings from the separately archived 400-tick diagnostic results.


## Numerical row reduction and bound extraction

The worker removes exact duplicate wall expressions and retains the strongest lower/upper absolute X/Z bounds for a state. Original request rows, names/ownership and problem hashes remain intact; the parent validator checks every original row. Distinct facing bounds, relative expressions and equalities are preserved. CasADi `detect_simple_bounds` is enabled; the progress monitor evaluates unextracted constraint expressions at each iterate/final point rather than interpreting the transformed returned `g` vector. Solver tolerances and motion/contact equations are unchanged.

Phase diagnostics include `wallReduction` (original/retained counts and original-index source groups), `simpleBoundExtraction`, and `constraintRowsBeforeExtraction` (after reduction, before extraction; not the final backend general-row count). [Detailed contracts, measurements and test results](../IPOPT_CONSTRAINT_REDUCTION.md).


## Pure feasibility (2026-11-04)

Early stopping and objective choice are independent after the 2026-11-10 integration. `--feasibility-only`, `stop_on_feasible=True` and request `stopOnFeasible: true` retain the configured directional objective by default. Explicit request `objectiveMode: constant_zero` selects pure feasibility and requires feasible stopping. Default `objectiveMode: configured` does not imply optimization continued past the first feasible point. Every original constraint/contact is still checked; `candidate.objective` remains its measured value. An already valid seed can return before worker startup. [Integration and compatibility](../ARCHITECTURE.md), [earlier mode benchmarks](../IPOPT_FEASIBILITY_MODE.md).
