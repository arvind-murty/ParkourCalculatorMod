# Standalone continuous IPOPT solver

This directory provides a Python API and command-line solver for fixed-input horizontal trajectories. It runs independently of Minecraft. An optional headless Java exporter reads ordinary captured TAS files using the repository's existing physics compiler. The mod's engine, UI and save format are unchanged.

## Setup and first solve

The worker dependencies are pinned and tested with Python 3.12:

```sh
python3.12 -m venv .venv-ipopt
.venv-ipopt/bin/python -m pip install -r standalone/ipopt/requirements.txt
.venv-ipopt/bin/python standalone/ipopt/solve.py \
  --input standalone/ipopt/fixtures/real11-400.json.gz \
  --seconds 10 --output /tmp/real11-result.json
```

The copied real11 fixtures contain 200 and 400 ticks, every facing row in their selected spans, prescribed wall contacts, and per-axis inertia. Their acceleration coefficients use the captured Force 45 / Derive policies. The fixture source and SHA-256 are recorded inside each request.

The parent process uses only the Python standard library. To run it from a different interpreter, supply `--python /absolute/path/to/the/worker/python` or set `PKC_IPOPT_PYTHON`. The worker needs CasADi and NumPy. Nothing installs or downloads automatically.

## Export a new captured TAS

From the repository root, with the usual JDK 21 Gradle configuration:

```sh
./gradlew -I standalone/ipopt/export.gradle :core:exportStandaloneIpopt \
  -PipoptCapture=/absolute/path/to/captured-tas.json \
  -PipoptTicks=200 -PipoptOutput=/tmp/problem.json

.venv-ipopt/bin/python standalone/ipopt/solve.py \
  --input /tmp/problem.json --seconds 5 --output /tmp/result.json
```

The exporter accepts JSON or gzipped JSON with an angle-solver seed. It compiles the core and test fixture reader without running tests or launching Minecraft. Optional properties are `ipoptStart` (zero-based), `ipoptInputs` (`KEEP` or `FORCE_45`), and `ipoptSprint` (`DERIVE`, `ALWAYS`, or `NEVER`). Input and sprint settings, including per-tick overrides, otherwise come from the captured TAS. A changed start requires a recorded state at that tick.

If the checkout pins another machine's Gradle Java home, add `-Dorg.gradle.java.home=/absolute/path/to/jdk21` to the Gradle command.

All positional rows in the span are hard constraints, including a legal-mode goal wall. Start position and stored velocity are fixed to the captured state. Start-position bounds absorbed by the native compiler are restored when they add a restriction. An initial no-turn row is restored around the fixed start facing. Facing equalities use the typed target's normal 1e-4 equality corridor, without sine-table-cell widening. Relative references outside the span and displacement constraints on its initial state are rejected explicitly. Facing on the final state belongs to the next input and lies outside this span's facing domain. Smooth lambda must be zero.

The exporter selects the game's per-axis inertia threshold when supported. For a game model with combined horizontal inertia, it exports `threshold: 0` and records `perAxisInertiaSupported: false`; the standalone model then carries all velocity normally. The solver does not implement combined inertia or game arithmetic.

## Command-line options and results

`--feasibility-only` stops at the first independently validated feasible candidate. Otherwise the worker optimizes the objective and the parent retains the best valid candidate it sees. `--seconds` bounds worker startup, model construction and optimization together. A successful local optimization can finish earlier. `--initial-solution previous-result.json` takes that result's facings as a warm start and preserves the request's original facings as a fallback for automatic inertia continuation. Both use the same total time limit. `--threshold 0` disables inertia zeroing; `--no-recorded-walls` removes prescribed contacts. These options change the requested model explicitly.

The parent validates the initial facings before launching the worker and retains them if feasible. Feasibility-only mode can therefore return an already valid seed without starting IPOPT. An invalid seed remains an initial guess. During optimization, the worker also keeps the best automatically replayed path, uses its actual inertia pattern for polishing, and retains the best nominally feasible phase iterate instead of a failed final iterate. With a distinct fallback, the warm attempt receives 40% of the remaining worker budget and the fallback can use the rest. A successfully polished incumbent is not polished again unless the best path changes.

The result file is written atomically. Standard output contains one short JSON status. Exit codes are:

| Code | Status | Meaning |
| --- | --- | --- |
| 0 | `feasible` | The independent replay validated a candidate. |
| 2 | `unknown` | No validated feasible candidate was found. This is not an infeasibility proof. |
| 3 | `error` | Invalid input, missing runtime, worker failure or invalid protocol. |
| 130 | `cancelled` | Cancellation discarded the in-flight result. |

The file includes `candidate.yaw`, world-coordinate `candidate.positions`, stored `candidate.velocity`, objective, violation, first-feasible and total timings, termination reason, worker status, phase iterations and rejected-candidate diagnostics. Each phase identifies its `kind` (`carry`, `zero_events`, `fixed_pattern` or `incumbent_polish`) and attempt `seed` (`initial` or `fallback`). A timeout retains a validated incumbent. IPOPT success without a valid candidate yields `unknown`. If a worker errors, an already validated seed or candidate can still yield `feasible`, with the error retained in diagnostics. `workerStatus` describes the last completed phase; `feasible` does not certify objective optimality. SIGINT/SIGTERM cancel the command. A failed fixed-pattern solve is also only a failed local search for the broader problem.

## Python API

Run from the repository root or put the repository root on `PYTHONPATH`:

```python
import gzip
import json
from standalone.ipopt import prepare, solve, validate

with gzip.open("standalone/ipopt/fixtures/real11-200.json.gz", "rt") as stream:
    problem = json.load(stream)

result = solve(problem, seconds=5, python_executable="/absolute/path/to/python")
if result["feasible"]:
    candidate = result["candidate"]
    checked = validate(prepare(problem), candidate["yaw"])
    exit_state = candidate["positions"][-1], candidate["velocity"][-1]
```

`solve` also accepts `stop_on_feasible`, a `threading.Event` as `cancel_event`, and an `on_incumbent(candidate)` callback. The callback receives a copy; keep it short. It is called for valid objective improvements, allowing a caller to save incumbents while a solve continues. Invalid API requests raise `ValueError` before starting a process. Caller orchestration chooses input patterns, stores multiple candidates and splices selected spans.

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

For each axis, the model zeroes an incoming component only if its absolute value is below `threshold`. It adds the fixed-magnitude control vector `magnitude[t] * (cos(base[t] + radians(yaw[t])), sin(base[t] + radians(yaw[t])))`, advances position, and multiplies the displacement by `friction[t]` to store the next velocity. Y is outside this horizontal problem.

The historical `walls` field contains all scalar constraints. Modes `X`, `Z` and `F` evaluate a single state/input or combine `t1` and optional `t2` with `op: PLUS` or `MINUS`. Comparisons are `GE`, `LE` or `EQ`. Facing residuals wrap to (-180,180]. `DXZ` evaluates `abs(dx) - abs(dz)` and `DZX` reverses the axes, using the two referenced position states. Bounds/equality corridors from a TAS are compiled before export; a direct `EQ` row is checked as an equality at the solver tolerance.

The objective selects X or Z with `MAX`/`MIN`. Optional `customYaw` projects onto `(-sin(yaw), cos(yaw))`. `type: MOTION` requires a custom yaw and a positive tick; it projects that tick's displacement instead of position. `smoothLambda` must be zero. Optional `maxIterations` defaults to 10000. Optional `fallbackYaw` contains `n` facings for an alternative automatic-inertia initialization. Like `yaw`, it is excluded from the physical problem hash. Explicit inertia patterns and threshold-zero requests retain their single-phase solve.

A contact is `{"state": 356, "axis": 0, "plane": 700.4250000119209, "direction": -1}`. Axes 0/1 mean X/Z; direction +1 approaches an upper plane, -1 a lower plane. The proposed move must reach the plane from the allowed side, after which normal position is anchored and stored normal velocity is zero. Tangential motion continues. The NLP uses a 1e-7 reach guard. Independent replay refuses to reset a component for any missed plane, including a shortfall below the positional tolerance. The optional exporter infers this schedule from recorded collisions and zero normal velocities, including history before the selected start. An unresolvable direction causes an export error.

Optional `inertiaPattern` has two entries per input tick: `0` requires zeroing, `1` positive survival, `-1` negative survival, and `2` leaves the branch unrestricted. It requires a positive threshold. With no explicit pattern, the worker solves unrestricted carry, then inferred zero events, then complete fixed patterns, with bounded refresh attempts. Independent replay always applies the automatic threshold and checks any explicitly requested branches. Positional feasibility tolerance is 1e-7; branch mismatches and missed wall hits are rejected separately.

## Validation and limits

The parent validator uses standard-library scalar arithmetic in world coordinates. It reconstructs the path from returned facings, including actual automatic inertia and conditional wall clipping, and checks every requested row. The CasADi worker uses separate sparse state variables and relative positions. Returned positions or success flags are not trusted for acceptance.

These are local optimizations and prescribed-contact continuous trajectories. They are not global optimality or infeasibility certificates, general collision detection, ladder speed-cap simulation, or byte-exact Minecraft validation. Small authored constraints can disagree with the continuous coefficients: the original full real11 route has a recorded mismatch near states 651/652. The solver keeps its tolerances and reports the failed search. Prefer the intended shorter spans and validate realized game behavior separately.

## Tests

Standard-library tests need no CasADi installation:

```sh
python3 -m unittest discover -s standalone/ipopt/tests -v
```

Enable the actual worker tests explicitly:

```sh
PKC_IPOPT_PYTHON="$PWD/.venv-ipopt/bin/python" \
  python3 -m unittest discover -s standalone/ipopt/tests -v
```

The integration tests cover a known optimum, contradictory constraints reported as unknown, both real11 fixtures with all facing rows and contacts, and warm-start objective recovery from saved no-inertia and inertial paths. Other tests cover independent replay, small missed contacts, wrong-side hits, fixed patterns, relative/angular rows, malformed candidates, cancellation, timeout, seed retention, recovery after a failed phase, fallback exploration, rejected fake success and atomic CLI output. The warm-facing fixture contains copied facings from the separately archived 400-tick diagnostic results.
