# Standalone ownership and package layout

The reviewed folder refactor is implemented. This changes ownership and packaging,
while preserving solver policy, physical equations and saved request/checkpoint
schemas. [Implementation and validation](LAYOUT.md)
and the [original inventory/review](LAYOUT.md) record its scope.

| Root | Contents and dependency direction |
| --- | --- |
| `src/parkour_solver/` | Installed production code. Cannot import research, tools, tests or old checkout namespaces. |
| `tests/unit/`, `tests/integration/`, `tests/research/` | Domain checks, native/package boundaries and deliberate research/archive regressions. |
| `tests/support/`, `tests/fixtures/` | Shared builders/harnesses and small reusable test data. No test imports another test case. |
| `cases/` | Curated runnable user tasks. Real11 physical anchors are a maintained derived input; its README records domain/configuration changes. |
| `tools/` | Current native gate, scoped profiling, saved-report audits and optional plotting. Tools may import production APIs. |
| `native/java/`, `native/gradle/` | Four Java bridge sources and their standalone build/export/replay adapters. They reuse existing mod physics. The runtime needs none of them: they generate the coefficient tables, serve as the differential oracle and own executable replay/export. |
| `native/cpp/` | C++ core for the migration (CMake, pybind11 module `_pkc_native`): canonical identity, typed search programs (`program.hpp`, strict conversion that rejects unknown keys and keeps int/float kinds, `append_choice` and the physics key) vertical traces (`vertical.hpp`) the scene package (`scene/`: geometry, normalization, objectives, routes and schedules, scene checks, ladder columns and regions) with the route parts of `constraints/` (passages, wall hits, run-off faces) and `movement/` (contact events and merged top faces, units, ladder events and settings, `trace_ladder_schedule`, `normal.py`'s traces and choice builders) `numerics/` (request checks, wall reduction, `numerical_problem`, the disk relaxation's model and Clarabel solve over Clarabel.cpp's C API, the continuation schedule and the IPOPT worker executable `pkc_numerics_worker`, linked against the casadi wheel's C++ library; `third_party/clarabel/Cargo.lock` pins Clarabel's crates) with `validation/numerical` and `validation/vertical` the interaction gate's model preparation (`gate.hpp`) the coefficient compiler (`coefficients.hpp`, rows generated into `src/coefficient_tables.inc` with the Python tables) interval envelopes and interaction screens (`envelopes.hpp`) `spatial/` (surfaces' contact options, clusters, sides and free rectangles; the cell decomposition, support components and geometry graph of cells and regions) the CPython `math` functions results depend on (`src/py_math.hpp`; its `math.hypot` port carries `LICENSE.cpython`) the gate's model and required-contact screen (`screen.hpp`) and, for native callers, the whole interaction gate (`reachability/service.hpp`: one session cache, face tables, approach envelopes, box, walk-off and sequence queries, program screens with the disk relaxation and its decided-result cache) and `search/anchors/` (the anchor policy's program readers: merged-face names, visits, the visit state, audits and advantageous targets), `search/comparison` and `search/deferrals.assess` (labels, priority keys, the incumbent bound and hard-anchor deferrals), `search/bounds/remaining`, `search/feasibility` (node identity, meaningful choices), `search/goals` (landing prefixes) and `reachability/viability`; `program.hpp` also has the search key and `truncate_program`. Newly ported packages mirror their Python package under `src/` and `include/pkc/` (`scene/`). Built into ignored `build/native/cpp/` by `tools/native_cpp.py`. Production loads it only when `PKC_NATIVE_CORE` names a build (`src/parkour_solver/native_core.py`); it switches the interaction gate's preparation, analysis, required-contact and program screens (onto a native gate per search and scene) and the child programs its sequence screens build, `numerics/disk.solve` onto the native decided-result cache, `movement/generation` its open generators (normal, stepping, ceiling) onto that native gate, and the Python bridge's scene checks and footprints; `scene/` dispatches its scene, route and schedule functions to it, `numerics/`, `validation/numerical` and `validation/vertical` their request checks and replays, `spatial/surfaces.py` its scene geometry, `spatial/cells.py` and `spatial/regions.py` the decomposition and geometry graph, `movement/vertical_profiles.py` its Y traces, and `numerics/solver.py` launches the native worker. A Python function hands its callers to the native core only where the native call, conversion included, measures faster; the others keep their Python body for Python callers and their native port for native callers. Each ported unit is checked against its Python oracle with `tools/native_cpp.py` and `tools/profiling/parity.py`. |
| `research/` | Scientific prototype, maintained older experiment drivers and immutable dated evidence bundles. Excluded from the installed package. |
| `docs/design/` | Detailed implemented/deferred designs; use the decisions index for authority. |
| `docs/history/`, `docs/research/`, `docs/reference/` | Source-era records, literature investigations and detailed reusable contracts. |
| `build/native/` | Ignored compiled helpers and machine-specific compiler configuration. Regenerate when needed. |
| `runs/` | Ignored new local runs; prior ignored output was preserved under legacy subdirectories. |

Production groups follow responsibilities: `schema`, `physics`, `scene`,
`movement` (including ladders), `spatial`, `search`, `constraints`, `reachability`,
`numerics`, `validation`, `io`, `bridge`, `observability` and `cli`. Low-level
schema/physics/scene primitives do not import search. Rendering lives in
`tools/plotting`; compact turn records remain in production. Numerical replay,
vertical checks and full scene validation have explicit owners. Shared helpers
have one implementation; a few source-module re-exports preserve internal patch
and caller contracts during this change. [Code map](CODE_MAP.md) gives each file.

## Supported public entrypoints

```python
from parkour_solver.api import solve_task, find_checkpoint_route, solve, replay_witness

# solve_task(task_fixture, compiler, ...) returns (result, independent_validation).
# solve(fixed_problem, ...) performs fixed-program continuous optimization.
```

Console commands are `parkour-solve` (the anchor search; `python -m parkour_solver`
is the same CLI) and `parkour-ipopt` (one fixed numerical problem). See the [runbook](RUNBOOK.md) for native dependencies,
input files and resume. Neither package imports nor worker launch depend on the
checkout working directory. Runtime takes task/compiler paths explicitly.

The old **public** autonomy/ipopt facades remain for `standalone.autonomy` and
`standalone.ipopt`, the `ipopt` solve command and the cell_routes `automatic_experiment` CLI. They require an installed
`parkour_solver`; the `standalone` namespace itself requires the repository parent
on the import path. Old private module paths and direct execution of moved
implementation files are not supported. Maintained tests/tools use current names.
Use module or console entrypoints, rather than executing `src/.../*.py` by filename.

## Boundary rules

1. Only `search/`, `observability/`, `cli/` and `api.py` import `search/`
   (`tools.validate_layout` checks this). Search composes movement, spatial,
   constraint, reachability and numerical services, which C++ units replace while
   Python search orchestrates; the numerical worker does not import the DFS.
   Shared modules sit below every domain package: `schema.py` and `canonical.py` import
   nothing from the package, and `work.py` only `bridge.counting`.
2. Ordinary import stays light: CasADi belongs to the numerical worker and plotting
   is optional. Clarabel, NumPy and SciPy (`reachability` extra) load only when a
   disk-relaxation screen or solve runs; search requires them.
3. The generic CLI takes task and config paths; the solver never locates Real11 or a
   benchmark through `Path(__file__).parents[...]`. Install the package for both the
   parent and the worker interpreter.
4. Compatibility code is a supported older strategy or format, kept tested and using
   the shared physical, model and policy helpers.
5. Historical evidence stays immutable: keep manifest and SQLite pairs together and
   never rewrite recorded paths, hashes or results to look current.

## Evidence, relocation and reproduction

[tools/layout-migration.json](../tools/layout-migration.json) maps original paths
to destinations and stores original SHA256 values. Its902 protected files include
whole old experiment bundles and the61 original reusable fixtures. Their bytes,
embedded paths, saved source hashes and SQLite/report companions are unchanged.
The [extraction catalog](../tools/layout-extractions.json) records later symbol
owners, and [test support catalog](../tools/layout-test-support.json) records
shared builders. Source provenance recursively hashes installed production files;
research scripts, tests and plots no longer appear as runtime dependencies.

Frozen scripts/readmes describe their recorded revision. Reproduce them in a
separate checkout at that revision; their relative/source paths are historical.
The current [research guide](../research/README.md) and runbook provide maintained
replacements. A report manifest and its `.store.sqlite3` companion must move
together and be read with `parkour_solver.io.checkpoint_store.read_report`.
Do not rewrite old provenance to resemble the current checkout.

Regenerate navigation with `python -m tools.docs_index` from `standalone/`.
`python -m tools.validate_layout` checks production import boundaries and protected
file hashes. The native gate keeps its declared module scope plus package and profiler
regressions; it does not silently include or retire excluded capability suites
([current gate](TESTING.md#current-gate)). No physical/decision policy version was bumped for directory moves.
