# C++ pruning pilot

The first experiment measures a C++17 port of the production
`reachable_envelopes` and `interaction_reachability` functions in
[`reachability/intervals.py`](../../src/parkour_solver/reachability/intervals.py).
They build conservative bounds and reject unreachable movement/contact/avoidance
choices inside the real candidate-generation and constraint-enumeration paths.
The pilot can replace both functions in the existing search, including imported
aliases, without changing production source or its default backend.
The batched experiment below adds Python batch entry points and an experimental
native owner while preserving scalar decisions and the default Python backend.

This is an experimental mixed Python/C++ backend, not a full C++ search engine.
Movement generation, traversal, remaining model preparation and checkpoint storage
remain Python. Clarabel/IPOPT and optional refinement retain their existing
implementations and settings. The fixed baseline contains the encounter-groups
lazy frontier identity tiebreak; removing that known bug is not counted as a C++
benefit.

## Build

Use the worktree's own installed numerical environment from the
[runbook](../../docs/RUNBOOK.md). The only additional build dependency is pinned
in [requirements.txt](requirements.txt). From the worktree's standalone directory:

```sh
../.venv-guidance/bin/python -m pip install -r research/cpp-pruning-pilot/requirements.txt
../.venv-guidance/bin/python -B -m tools.profiling.cpp_pruning build --output /private/var/tmp/cpp-pruning-build-new
```

The command builds the extension into ignored `build/cpp-pruning-pilot/`, records
compiler/version/flags and binary/source hashes, and checks the two-coordinate
norm against the running CPython on 10,003 cases. Build with the Python interpreter
used for execution. The norm's compensated implementation is adapted from
CPython 3.12.2; [LICENSE.cpython](LICENSE.cpython) supplies its license. It preserves
the reference rounding rather than substituting a sometimes one-ULP-different
libm `hypot`. Optimizations use `-O3` with floating-point contraction and fast-math
disabled.

## Capture and exact validation

Each output directory must be new. Capture real inputs by deterministic call
ordinal, before inspecting outcomes:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 ../.venv-guidance/bin/python -B -m tools.profiling.cpp_pruning run \
  --input cases/real11/extended-anchors.json --seconds 90 --capture \
  --output /private/var/tmp/cpp-pruning-capture-new
../.venv-guidance/bin/python -B -m tools.profiling.cpp_pruning validate \
  --corpus /private/var/tmp/cpp-pruning-capture-new/corpus.pkl.gz \
  --output /private/var/tmp/cpp-pruning-parity-new
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 ../.venv-guidance/bin/python -B -m tools.profiling.cpp_pruning tests \
  --output /private/var/tmp/cpp-pruning-tests-new
```

The corpus is a local gzip/pickle artifact because interval bounds include
infinities and preserved object aliases. It contains exact inputs and reference
outputs; validation reruns an isolated copy of the real Python source, then checks
the native results without rounding or tolerances. Model identity and signed zero
are checked. Equality is by value: the pilot stores bounds as doubles, so integer
coordinates that Python keeps as `int` (for example envelope `states[0]`) return as
equal floats. Canonical digests encode `1` and `1.0` differently, and envelope
contents feed `interaction_context_key`, so native and Python digests of the same
bounds differ. The shared comparator in
[`tools/profiling/parity.py`](../../tools/profiling/parity.py) rejects that by
default; the pilot opts out explicitly (65 of the 87 focused tests hit it).
The comparison oracle does not recursively call the C++ replacement.
Existing reachability, encounter, contact and policy tests run through checked
native functions. Effectful numerical refinement is invoked once, preserving mock
and production call counts.

Capture retains the first 128 calls and every 64th later call, with at most 1,024
samples per function. It is a bounded performance sample, not a completeness
claim. Capture and per-call verification overhead are excluded from speed claims.
The sampler includes rejected cases and does not use eventual search outcomes to
select inputs.

## Timing

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 ../.venv-guidance/bin/python -B -m tools.profiling.cpp_pruning bench \
  --corpus /private/var/tmp/cpp-pruning-capture-new/corpus.pkl.gz --repetitions 10 \
  --output /private/var/tmp/cpp-pruning-kernels-new
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 ../.venv-guidance/bin/python -B -m tools.profiling.cpp_pruning matrix \
  --output /private/var/tmp/cpp-pruning-live-new
```

Component timing includes both public function calls and complete output
materialization, including Python/C++ container overhead. It excludes corpus
loading and numerical-refinement calls. Both implementations warm up and alternate
order; fresh-process repeats distinguish within-process repetition from independent
processes. The interaction check retains its Python dictionary interface, so this
experiment measures that boundary cost rather than a fully typed native query API.

The live matrix uses five short search pairs without durable checkpoints, three
short pairs with normal durable callbacks, and two 60-second extended pairs. It
runs serially with alternating arm order. Timings cover the application search
and independent final replay after backend import; post-run report rendering is
excluded. Source hashes must stay unchanged. Completed short pairs must match
the entire search-transition sequence after removing only elapsed timestamps,
as well as work counters, ticks, objective and validation. Extended deadline runs
measure progress rather than time to a solution.

The `run --backend native --verify` command additionally compares every native
pruning output with Python during an actual search. It is a correctness run,
not a timing result. Solver processes remain the same in both arms.

## Results and evidence

The first run's source baseline is `e9ca522c063b79d5300d31b7a51e50f00d49de62`
plus its captured one-line test assertion change. The other agent's encounter-group
checkout is read-only. The preserved run directory is
`/private/var/tmp/standalone-cpp-pilot-20261120T130422`; its `REPORT.md` and
`comparison.json` contain the measured component and integrated results. The small
portable [results.json](results.json) is the deliberate summary of those raw runs.
The initial Python baseline's separate backend-selection test failure remains
recorded; the pilot does not modify or suppress it, and does not claim the full
Java differential native gate.

| Measurement | Python/C++ time ratio |
| --- | ---: |
| Envelope corpus, three fresh processes | 7.141982x |
| Interaction corpus, three fresh processes | 0.924265x |
| Completed short search, five pairs | 1.066065x |
| Completed short search with checkpoints, three pairs | 1.030490x |

Ratios above one favor C++. Extended deadline pairs expanded Python `[21, 21]`
and native `[22, 21]`, with no finished route in either arm. These two pairs do
not establish consistent throughput improvement. The native checks passed 82
existing tests with 84,694 exact comparisons, corpus validation and a live
per-call comparison run. A separate undefined-behavior sanitizer build in trap
mode passed the same corpus and tests; sanitized runs are excluded from timing.
Keep this implementation experimental and measure envelope construction plus
an ordered batch of interaction queries within one native boundary before
expanding the migration.

To regenerate the report, invoke [summarize.py](summarize.py) with the evidence
directory and the explicit list of component `benchmark.json` files. It checks
paired live results and search-transition hashes before writing `REPORT.md` and
`comparison.json`. Keep full corpora, logs and build metadata with the evidence;
do not infer a full-migration speedup from a component multiplier.

## Batched experiment

The next experiment adds ordered independent-query batches to encounter phase
members/faces and finite contact regions. The Python batch calls the existing
scalar predicate for each request. The native implementation compiles whole query
trees into C++ row/region/alternative structures and evaluates them against native
bounds. `Envelope(problem, y).check_many(queries)` retains its bounds in C++;
`screen_batch(problem, y, queries)` constructs bounds and checks the batch inside
one call, returning only the complete ordered proofs. They use the real interval
and travel rules; scalar checks retain the existing optional Clarabel refinement.

The existing search also reads and can mutate Python envelope dictionaries. Its
public batch bridge imports referenced bounds only for that call. It never reuses
a hidden native copy after Python mutations. The attempted retained dictionary
adapter failed the existing probe-uncertainty mutation test and was removed;
that failure is retained in the experiment evidence. The direct opaque owner
and the public bridge have separate measurements, because the current Python
callers still require materialized snapshots.

Capture uses the first 32 batch calls and every 64th later call, up to 128 batches;
scalar/envelope capture retains its original sampling rule. Existing scalar-gate
source is preserved as an oracle for query proofs, cache/fallback counts and
exclusion order. The focused native checks include an explicit between-batch
mutation regression; additional movement tests exercise contacts, stepping,
ceiling, composition and existing interruption/resume controls.

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 ../.venv-guidance/bin/python -B -m tools.profiling.cpp_pruning batch_bench \
  --corpus /private/var/tmp/cpp-batch-capture-new/corpus.pkl.gz --repetitions 10 \
  --output /private/var/tmp/cpp-batch-kernels-new
```

Run the command in three fresh processes, then run the existing serial `matrix`
command with the newly built extension. `build_and_batch` includes construction,
query conversion and full decision outputs; `retained_batch` excludes envelope
construction but includes query conversion; `public_batch` consumes the current
Python snapshot. Capturing, validation and sanitizer execution remain separate
from timing. The batched evidence is preserved under
`/private/var/tmp/standalone-cpp-batch-20261120T134000`; its `REPORT.md` is generated
by `summarize.py --batch --build <build.json>` with the three explicit kernel
result files.

The [portable batched results](batch-results.json) record 17.999197x for direct
construction plus checks, 1.045212x for checks with retained native bounds, and
0.780464x for the public snapshot bridge. These use a different corpus and return
boundary from the first experiment; their ratio is not an isolated batching gain.
Across 20 live runs, completed-search speedup is 1.096448x without checkpoints and
1.029087x with them. The two extended pairs expanded native `[24, 24]` versus
Python `[21, 21]`, with no finish. All eight completed pairs match each other and
the original scalar search-transition hashes. Focused and additional movement
scope passed 83 and 53 tests, respectively, with no failures, errors or skips.
Keep the experimental backend separate from the default Python execution; a
further port must own candidate construction and its private bounds together.
