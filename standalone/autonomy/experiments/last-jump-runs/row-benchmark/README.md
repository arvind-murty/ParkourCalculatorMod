# Do duplicate rows cost time?

2026-11-03. Measurement-only experiments on one saved fixed continuous request from source61355976, run against production code at56806581 (CasADi3.8.1). No production code changed and no full route search was run. [A/B data](results.json), [run log](run.log), [callback probe](callback-results.json), [probe log](callback.log).

## Solver behavior and source references

The current worker appends every wall row as a general constraint. It does not enable CasADi's `detect_simple_bounds`, which is false by default according to the [CasADi API](https://web.casadi.org/api/internal/d4/d89/group__nlpsol.html). Consequently the simple bounds are not automatically moved to `lbx/ubx` by our setup. IPOPT's optional [dependency detector](https://www.coin-or.org/Ipopt/OPTIONS.html#OPT_dependency_detector) is disabled by default and concerns dependent equalities, not this list of duplicate positional inequalities. We should not assume a generic presolver will clean up this row list.

The benchmark keeps the same67 inputs, coefficients, momentum, all335 decision variables, initial facing seeds and objective. It changes only the explicit simple positional bounds: original278 rows, exact114, or strongest91. Total CasADi constraints including motion equations remain546,382,359 respectively. Every benchmark incumbent is rechecked against the original278 rows.

## Controlled A/B results

Five repetitions per formulation, rotating order; plugin load is warmed outside the first-phase timer. The first-phase200-iteration cap and5-second safety limit never bind. Fresh-worker tests use the existing solver wrapper, original initial/fallback seeds, a2-second budget and the same200-iteration cap, which is also inactive (all phases converge in≤25 iterations). OPENBLAS/OMP threads are1.

| Formulation | Explicit rows | First-phase iterations | IPOPT call median | Graph + solver setup + first solve median | Fresh worker/two-seed median |
| --- | --- | --- | --- | --- | --- |
| Original | 278 | 17 | 13.31ms | 46.32ms | 198.55ms |
| Exact duplicates removed | 114 | 12 | 7.16ms | 34.11ms | 177.09ms |
| Strongest bounds retained | 91 | 11 | 6.01ms | 32.61ms | 182.79ms |

The fresh-worker column has three repetitions per formulation; startup and other fixed costs make the difference smaller and noisier than the isolated IPOPT change. All24 fixed-model runs produce candidates feasible against the original rows, and their best objectives agree within about1.1e−9. This is one saved request, not a suite-wide speed guarantee. Exact-row and strongest-bound transformations are mathematically equivalent here; no decision or geometry is relaxed.

Removing duplicates helps, but the absolute savings on this case are milliseconds. It does not explain the original317.53-second search. The saved retry's12 numerical-call diagnostics sum to21.312s; those durations include parent-side incumbent callbacks and are not pure IPOPT CPU time. Compiler timings total about4.02s. Most wall time is outside those recorded call spans and has not been completely attributed.

## Orchestration overhead from rows

Per-operation means over100 calls, with a common feasible yaw and the LP-result cache warmed:

| Operation | 278 rows | 91 rows |
| --- | --- | --- |
| Copy/validate request (`prepare`) | 1.394ms | 0.778ms |
| Fingerprint | 0.460ms | 0.250ms |
| Request JSON encoding | 0.399ms | 0.254ms |
| Scalar candidate validation | 0.258ms | 0.196ms |
| Reach envelope | 2.591ms | 1.998ms |
| Cached linear-screen preprocessing | 0.265ms | 0.186ms |
| Compact request size | 43,772 bytes | 23,763 bytes |

These are isolated timings, not an exclusive accounting of a whole solve. Effective-row/branch logic and `linear_exclusion` already merge/deduplicate rows for their own keys/LPs; duplicate final rows do not automatically multiply the number of discrete obstacle choices. The row count still enlarges copying, hashing, validation, request payloads and retained state. Prefix keys can also distinguish lists with different duplicate multiplicity; proving additional model equivalence remains separate from mere solver-row cleanup.

## A much larger measured cost: synchronous progress snapshots

Reconstructing the actual final progress-publication payload (source, fixture, windows, searchState) gives the following one-snapshot measurements. This excludes disk writes and the additional copying done when building inner repair snapshots:

| Encoding of the same data | Uncompressed JSON | Compressed JSON | Encode + compress |
| --- | --- | --- | --- |
| Current indented JSON | 109.30MB | 4.32MB | **4.039s** |
| Compact JSON | 44.73MB | 2.30MB | **0.746s** |

There are71 recorded DFS transitions, and additional inner progress publications. Earlier snapshots are smaller, so multiplying4s by71 would be invalid. This nevertheless identifies a substantial late-stage cost that dwarfs the per-request row-processing savings.

The callback chain is synchronous: numerical incumbent → `search.publish` → `block_contacts.retain/publish` → DFS progress → `fixed_clock_experiment.checkpoint_progress` → whole-report `save`. The parent solver's deadline continues advancing during this work.

A separate probe replays the same fixed278-row request with a2-second budget and a callback that encodes/compresses the late snapshot, without writing it to disk:

| Incumbent callback | Solver wrapper elapsed | Outcome |
| --- | --- | --- |
| None | 0.235s | both phases converge; worker_finished |
| Current-format snapshot | 4.169s | time_limit after one4.046s callback; valid incumbent preserved |
| Compact snapshot | 2.284s | time_limit after three snapshots totaling2.162s; valid incumbent preserved |

This is a causal callback probe, not a full live-search profile. It demonstrates that reporting alone can consume a solve budget and prevent the parent from collecting later phase messages. Compact formatting helps but does not solve excessive publication frequency or repeated full-state copying. It does not establish how much of all317.53s is attributable to reporting.

## Recommendation/status

Final-row canonicalization is a justified small cleanup. Larger priority: preserve exact resumability while reducing redundant full-state encoding/copying and controlling synchronous publication cost. Keep in-flight requests, repair cursors and incumbents durable; do not simply remove logging or discard state. Stage-level timing would close the remaining attribution gap. None of these runtime fixes was implemented by this measurement turn; corridor/cell changes remain separate.

Reproduce from the physical repo root:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 PYTHONPATH="$PWD" .venv-guidance/bin/python \
  standalone/autonomy/experiments/last-jump-runs/row-benchmark/benchmark.py \
  --input standalone/autonomy/experiments/last-jump-runs/evidence/real11-retry-after-handoff.json.gz \
  --output /tmp/row-benchmark.json --repeats 5 --iterations 200 --external-repeats 3

OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 PYTHONPATH="$PWD" .venv-guidance/bin/python \
  standalone/autonomy/experiments/last-jump-runs/row-benchmark/callback_probe.py \
  --input standalone/autonomy/experiments/last-jump-runs/evidence/real11-retry-after-handoff.json.gz \
  --output /tmp/row-callback-probe.json
```
