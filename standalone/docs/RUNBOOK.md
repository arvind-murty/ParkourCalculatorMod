# Standalone runbook

Use the session's physical checkout. From `standalone/`, `pwd -P` and
`git rev-parse --show-toplevel` identify it; do not change checkout based on an old
handoff path. Ordinary Python/documentation work requires standalone checks.
Java bridge build/export commands below explicitly run from the repository root.

## Install and environment

The existing repository-root `.venv-guidance` is the native environment on this
machine. From `standalone/`, install the package into the parent and worker Python:

```sh
../.venv-guidance/bin/python -m pip install --editable . --no-deps
export PKC_IPOPT_PYTHON="$PWD/../.venv-guidance/bin/python"
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
```

The movement bridge runs in process: coefficients come from tables generated from
the Java bridge, and scene and footprint checks are a Python port of its
finite-scene contract. Commands use the Java bridge only when given an explicit
`--compiler PATH` or `PKC_RUN_TICKS_COMPILER`; the flag wins. Both bridges return
identical coefficients, so request hashes match and checkpoints resume across them.
Scene checks differ only in tolerance: the legacy Java bridge compares at 1e-7, the
Python port at `REPLAY_TOLERANCE` (1e-4), so production runs use the Python bridge.
Executable Minecraft replay and export remain Java tools.

`PKC_NATIVE_CORE` opts a solve into the C++ core: set it to a built module
directory (`build/native/cpp`, built as in
[run a solve and report it](#run-a-solve-and-report-it)). The interaction gate then
keeps a native gate per search and scene (with the table compiler on a collision-box
scene) that prepares and analyzes its programs, answers their required-contact screens
and screens whole programs (`check_program`, walk-off faces and the disk relaxation
included) in its own cache, building Python dicts only for readers that ask; the open
normal, stepping and ceiling generators run on that gate, with its face tables and query log. The anchor DFS
loop runs natively with its frontier (each decision expanded a movement type at a time, with its minimum-run
searches) and decision checks, publishing snapshots the checkpoint store encodes natively; decision checks of other
searches run natively after their gate preparation, and anchor-leg and finish solves run their constraint generator
and repair loop natively (see the
[execution reference](ANCHOR_SEARCH_EXECUTION_REFERENCE.md#implementation-ownership-and-persistence)).
Its sequence screens append the choice to a native copy of the context's program and
screen that child without building it in Python, ladder appends included. The Python bridge replays scene checks and derives footprints natively, and `scene/` normalizes scenes and builds routes and schedules natively for every caller, including scenes with climbable cells and routes with ladder events. The numerical request checks, `validation/numerical` replays and `validation/vertical` schedule checks, `numerical_problem` and the disk relaxation's Clarabel solve with its decided-result cache and counters (`numerics/disk.py`; its spec and small helpers stay in Python) run natively for every caller, `spatial/surfaces.py`'s scene geometry (contact options, clusters, sides, free rectangles) computes natively behind its Python caches, `movement/vertical_profiles.trace_ladder_schedule` traces natively, `spatial/cells.decompose`, `spatial/regions.support_components` and `build_geometry_graph` build the cell decomposition and its geometry graph natively, `spatial/event_routes.build_route_domain` builds the whole event domain natively, `search/anchors/policy.encounter_groups` groups anchors on a native gate's face tables, `io/checkpoint_store` encodes plain JSON containers natively (saves with targets excepted), and `numerics/solver.py` launches the native IPOPT worker (`pkc_numerics_worker`, built beside the module against the casadi of the build's Python) instead of `python -m parkour_solver.numerics.worker`. It compiles their coefficients from the native tables, so the compiler's call counts
omit them; only bridges whose replies equal those tables allow that
(`table_coefficients`), and any other compiler gets the Python analysis. Everything else, and every run without the
variable, uses the Python implementation, which stays the parity oracle.

For a fresh environment, use Python3.12+ and the pinned extras:

```sh
python3.12 -m venv ../.venv-guidance
../.venv-guidance/bin/python -m pip install --editable '.[numerics,reachability]'
# Optional scientific prototype and rendering dependencies:
../.venv-guidance/bin/python -m pip install --editable '.[research,plot]'
```

The parent API uses the standard library. CasADi/NumPy run in the numerical
worker. The `reachability` extra supplies Clarabel, NumPy and SciPy (sparse
matrices) for the disk relaxation in `numerics.disk`. Plotting is an optional
tool dependency. Nothing installs at runtime. Keep the venv
executable path rather than resolving its symlink to a system interpreter.
Install `parkour_solver` into each interpreter when parent and worker differ.
Installed API and CLI/worker launch work outside the repository cwd; task,
compiler and output paths must then be absolute. Developer `tools`/`tests` run
from `standalone/`, without a repository-root `PYTHONPATH` requirement.

## Java bridge oracle, only for oracle tests, table regeneration and replay/export

From the repository root, build the standalone bridge adapters using current
checked-in core/Forge sources. This does not launch Minecraft or run mod tests:

```sh
./gradlew --configure-on-demand \
  -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home \
  -I standalone/native/gradle/compiler.gradle -I standalone/native/gradle/export.gradle \
  :core:prepareRunTicksCompiler :core:compileStandaloneIpoptExport
```

The JDK path is machine-specific. Outputs live under `standalone/build/native/`;
`compiler.json` embeds a machine-specific classpath and is ignored. Do not rebuild
while another numerical job uses that classpath. Four bridge sources live under
`native/java/`; [mod references](MOD_REFERENCES.md) identifies reused APIs.

After changing a Java source listed in `SOURCES` of
[the generated coefficient tables](../src/parkour_solver/physics/_coefficient_tables.py),
regenerate them from `standalone/`. The generator writes the Python module and its C++ twin
`native/cpp/src/coefficient_tables.inc` from the same rows (rebuild the native core
afterwards); `--check` fails when either checked-in file differs:

```sh
"$PKC_IPOPT_PYTHON" -B -m tools.generate_coefficient_tables --compiler build/native/compiler.json
"$PKC_IPOPT_PYTHON" -B -m tools.generate_coefficient_tables --compiler build/native/compiler.json --check
```

## Tests and layout checks

From `standalone/` with the environment above:

```sh
"$PKC_IPOPT_PYTHON" -B -m unittest tests.unit.search.test_candidate_comparison tests.unit.reachability.test_proposal_analysis tests.unit.search.test_transfer_time_bounds
PKC_RUN_TICKS_COMPILER="$PWD/build/native/compiler.json" "$PKC_IPOPT_PYTHON" -B tools/native_gate.py --output /tmp/parkour-native-new
"$PKC_IPOPT_PYTHON" -B -m tools.validate_layout
"$PKC_IPOPT_PYTHON" -B -m unittest discover -s tests -t .
```

The explicit gate maintains the selected native module scope plus package,
profiling and avoidance-generation/continuation controls; added regressions
increase the total. Full discovery additionally includes dedicated ladder/climbable/vine
capabilities and their recorded baseline; it is a different scope. Ordinary tests
use the Python bridge; only the Java differential tests in
`tests.unit.bridge.test_python_compiler` read `PKC_RUN_TICKS_COMPILER`, which is why
the gate command sets it. Native variables absent means skips, not native acceptance.
See [testing](TESTING.md).
Use a new gate directory; its report records source hashes, source stability,
counts, skips, exclusions and evidence-integrity results.

## Static player bounds without a solve

The [geometry bounds tool](../tools/geometry_bounds.py) accepts a collision scene
or a task fixture's `request`, uses existing spatial geometry, and reports exact
expanded obstacles plus exposed landing covers. No native bridge or numerical
dependencies are needed; install the package first. Use a fresh output path:

```sh
../.venv-guidance/bin/python -B -m tools.geometry_bounds \
  --input cases/real11/extended-geometry.json \
  --output runs/real11-bounds-new/player-bounds.json \
  --markdown runs/real11-bounds-new/player-bounds.md
```

The [extended Real11 report](REAL11_EXTENDED_GEOMETRY.md) explains coordinate
conventions, body-overlap endpoints, neighbor cuts and standing headroom.
Static geometry is not a jump/replay check. That input is geometry-only; the
existing runnable benchmark below remains a distinct task.


## Run a solve and report it

Set the environment above, then run the profiled solve into a new directory. It
writes `report.md` (the readable per-route report) next to its JSON outputs:

```sh
"$PKC_IPOPT_PYTHON" -B -m tools.profiling.run \
  --input cases/real11/physical-anchors.json --output runs/<new-name> \
  --seconds 300 --profile-mode bookkeeping
```

Regenerate `report.md`, `routes.json` and `nodes.json` for a finished profiled run, or write
them for a saved report from `python -m parkour_solver`:

```sh
"$PKC_IPOPT_PYTHON" -B -m tools.profiling.trace runs/<run-name>
"$PKC_IPOPT_PYTHON" -B -m tools.profiling.trace /path/to/report.json.gz --output /path/to/dir
```

Compare two saved solves for C++ migration parity: outcome, ticks, objective and
the transition sequence without timestamps. Exact by default. `--tolerant` accepts
objective differences within the search's own objective tolerance (`parity.SOLVE`)
and drops digest strings from transitions; both routes must still have passed their
own validation. Exit status 1 means a difference:

```sh
"$PKC_IPOPT_PYTHON" -B -m tools.profiling.parity /path/to/python.json.gz /path/to/native.json.gz [--tolerant]
```

Build the native C++ core (`native/cpp/`, needs CMake, the pinned pybind11 from
`research/cpp-pruning-pilot/requirements.txt`, the `numerics` extra's casadi and cargo; the first configure
fetches Clarabel.cpp v0.11.1 and the crates pinned by `native/cpp/third_party/clarabel/Cargo.lock`) into `build/native/cpp/`, or the
undefined-behavior-sanitizer variant into `build/native/cpp-sanitize/`. Then check
every `canonical.digest` call of a real solve against the native encoder, round-trip
every program and movement choice that `movement/program.py` builds through the typed
native model, or repeat every `InteractionGate.prepare` call, `PythonCompiler.compile`
coefficient request or `reachable_envelopes` build natively. Each stops at the first
difference and saves it as `mismatch.json`:

```sh
"$PKC_IPOPT_PYTHON" -B -m tools.native_cpp build [--variant sanitize] [--output /path/to/provenance-dir]
"$PKC_IPOPT_PYTHON" -B -m tools.native_cpp verify-digest --input cases/real11/physical-anchors.json \
  --seconds 60 --output /path/to/new-dir [--variant sanitize]
"$PKC_IPOPT_PYTHON" -B -m tools.native_cpp verify-programs --input cases/real11/physical-anchors.json \
  --seconds 60 --output /path/to/new-dir [--variant sanitize]
"$PKC_IPOPT_PYTHON" -B -m tools.native_cpp verify-gate --input cases/real11/physical-anchors.json \
  --seconds 60 --output /path/to/new-dir [--variant sanitize]
"$PKC_IPOPT_PYTHON" -B -m tools.native_cpp verify-coefficients --input cases/real11/physical-anchors.json \
  --seconds 60 --output /path/to/new-dir [--variant sanitize]
"$PKC_IPOPT_PYTHON" -B -m tools.native_cpp verify-envelopes --input cases/real11/physical-anchors.json \
  --seconds 60 --output /path/to/new-dir [--variant sanitize]
"$PKC_IPOPT_PYTHON" -B -m tools.native_cpp verify-screen --input cases/real11/physical-anchors.json \
  --seconds 60 --output /path/to/new-dir [--variant sanitize]
"$PKC_IPOPT_PYTHON" -B -m tools.native_cpp verify-scene --input cases/real11/physical-anchors.json \
  --seconds 120 --output /path/to/new-dir [--variant sanitize]
```

`verify-screen` solves on the native gate and repeats every `sequence`, `required` and
`check_program` call on a shadow Python gate. `verify-scene` repeats every `scene/` call
natively: scene checks, footprints, geometry and scene normalization, routes, schedules
and initial yaw guesses.

A new program field fails the typed conversion by name, so add it to
`native/cpp/include/pkc/program.hpp` in the same change.

A report written from a bare solve report has no request numbers or IPOPT
statuses, which only the profiler records. [Results and debugging](RESULTS_AND_DEBUGGING.md#solve-reports)
explains every output. `runs/` is ignored by git; copy evidence worth keeping
into `research/` with its provenance.

## Current physical-anchor solve, profile and resume

[Execution reference](ANCHOR_SEARCH_EXECUTION_REFERENCE.md) describes current
combinations, pruning, bounds and dispatch. The curated Real11 case declares
finite physical B7 and anchors, with initial preparation selected by the orchestrator
without a first-jump whitelist; its origins and domain change are in
[cases/real11](../cases/real11/README.md). Run from `standalone/`:

```sh
"$PKC_IPOPT_PYTHON" -B -m parkour_solver \
  --input cases/real11/physical-anchors.json --output /tmp/real11-new.json.gz
"$PKC_IPOPT_PYTHON" -B -m parkour_solver \
  --input cases/real11/physical-anchors.json --resume /tmp/real11-new.json.gz \
  --output /tmp/real11-resumed-new.json.gz
"$PKC_IPOPT_PYTHON" -B -m tools.profiling.run \
  --input cases/real11/physical-anchors.json --output /tmp/real11-profile-new --seconds 300
"$PKC_IPOPT_PYTHON" -B -m tools.profiling.run \
  --input research/hard-jump-comparisons/2026-11-11/initial0.json \
  --output /tmp/real11-initial0-new --seconds 90
```

Console `parkour-solve` is equivalent to `python -m parkour_solver`. The CLI accepts
explicit `--compiler` (Java bridge) and `--python` paths as well. It saves the final
report, which `--resume` continues; add `--save-progress` to also save resumable
progress during the run, at the cost of about half of a long solve's time. Reports record the
selected bridge under `bridge`. Profile runs preserve cold
settings, instrumentation/source/configuration hashes, work counters and
independent retained-witness replay; inclusive timings overlap. Profiling overhead
and local solver behavior affect the work fitting a time budget. No archived
facings or trajectories are seeded into a cold profile.

Physical anchor mode uses `seconds` and `max_solver_calls` for the whole session,
and `candidate_seconds` for each numerical request. Legacy `score_seconds`,
`score_call_limit` and `final_call_limit` settings remain accepted for older input
files but do not limit anchor search. Non-anchor modes retain their existing
service budgets. Changing these budgets does not change physical model identities
or invalidate compatible checkpoints; it can change work reached before timeout.

Avoidance uses global budgets as well: obsolete local repair/passage fields are
removed during normalization. Constraint policy23/state11 and repair assembly14
change the continuation context; old face-generator queues require their original
source checkout. Current generated descriptions use phase spans and actual rows,
as described in the [avoidance contract](design/AVOIDANCE_PASSAGES.md).

`--resume` restores saved search state only when its exact context matches.
Compatible JSON/shared-store identities are preserved by this folder move; solver
policy changes still require their usual compatibility checks. Each re-solve
starts at the original state with earlier facings free. Inspect `resume` metadata
before claiming exact continuation. Cancellation preserves partial work; timeouts
and caps do not certify infeasibility or optimality.

Reports can be shared-store manifests with paired `.store.sqlite3` files. Keep
both together and read through:

```python
from parkour_solver.io.checkpoint_store import read_report
report = read_report('/tmp/real11-new.json.gz')
```

Do not treat raw gzip JSON as a fully expanded shared-store report. Write new
experiments to fresh output paths; preserve dated evidence and source hashes.

## Fixed movement prefix with free continuous facings

The existing `find_checkpoint_route(initial_programs=[program])` starts movement
generation after a supplied original-state program. Fixtures may pass that
argument as `settings.initial_programs`. The [longer Real11 experiment](../research/real11-long-solve/README.md)
uses a validated initial-0 root through B5@55. The next unmet anchors are B7/B20,
then terminal B22. Its first55 input/contact decisions are fixed; all facings
remain variables in each original-state continuous solve. Warm yaw values are
initial guesses, not equalities. Preserve actual physical rows and phase/clock
metadata, strip stale node/repair/comparison state, and validate the root against
the new full scene. This is a seeded new task, not an exact old-task resume.

The profiler or ordinary CLI commands above can consume that experiment fixture.
The ordinary CLI saves its final report (and in-run progress with `--save-progress`)
and supports its usual matching-context resume. A different geometry/task/root requires a new search context.

## Fixed numerical problem and replay/export

This optimizes facings for a captured fixed-input problem:

```sh
"$PKC_IPOPT_PYTHON" -m parkour_solver.cli.numerical \
  --input tests/fixtures/numerics/real11-200.json.gz \
  --seconds 10 --output /tmp/fixed-ipopt-new.json
```

`parkour-ipopt` is equivalent. See the [numerical reference](reference/NUMERICAL_SOLVER.md)
for formats, worker options, capture export and exit codes. Captured fixtures keep
their original inertia/contact settings; decision search explicitly uses threshold0.

The [replay contract](reference/REPLAY.md) supplies the full Gradle command and
TAS/export validation. The adapter is now `standalone/native/gradle/export.gradle`;
its tasks/properties are unchanged. Use the validated problem/result, effective
input template and supplied scene. A current validated replay report establishes
local replay acceptance. Scene/numerical validity does not establish undeclared
actual-world geometry validity.

## Saved-data audits and older strategies

```sh
"$PKC_IPOPT_PYTHON" -m tools.witness_bound_audit \
  --reports /absolute/path/to/accepted/report.json.gz --output /tmp/bound-audit-new.json
```

Bounds auditing checks retained valid witnesses and is not a cold discovery seed. The
candidate audit of the old orchestrator's saved evidence was removed with its searches. Read the [Real11 guide](REAL11_GUIDE.md) before interpreting counts.

The fixed-clock, virtual-goal, supplied-corridor and blocks searches were removed
(2026-11-21); their evidence records `8266a38d` or earlier as its source revision.

[research/README.md](../research/README.md) explains preserved bundles. Historical
scripts and READMEs retain their original commands/imports; reproduce those against
their recorded revision. Use current tools above for current code. No permanent
facades were created for every old private module.
