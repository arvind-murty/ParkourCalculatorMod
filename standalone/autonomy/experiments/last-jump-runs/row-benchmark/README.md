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

## Can we simply enable bound extraction?

It is a promising optimization, but the one-line toggle is not compatible with the current monitor. [Probe data](extraction-probe.json) and `extraction_probe.py` test three cases on the saved request, without changing production:

| Probe | Backend | Original constraints at backend's final point | Our existing monitor |
| --- | --- | --- | --- |
| Extraction off | converges in17 iterations | feasible | accepts |
| Extraction on, stock monitor | converges in11 iterations | feasible | rejects |
| Extraction on, recompute original expressions for monitoring | converges in11 iterations | feasible | accepts |

The stock monitor interprets CasADi's returned `g` vector against the original row bounds. With extraction enabled in installed CasADi3.8.1, that interpretation fails: its largest apparent residual is about35.31, while independent scalar validation confirms the actual final point is feasible. The backend is not returning an invalid solution; the monitor rejects a valid one and the current phase fallback can return the old seed instead. The prototype checks the original `g(x)` expression directly before reporting incumbents, preserving full original-row validation.

Thus enabling `detect_simple_bounds=True` plus a small monitor compatibility change is a concrete next implementation. The flag alone is not a drop-in change in this worker. Production remains unchanged pending focused worker regressions, including incumbent/early-stop behavior and non-simple constraints. Extraction also leaves the Python row lists, inherited state and JSON payloads intact, so it cannot remove orchestration/reporting overhead by itself. The [CasADi option documentation](https://web.casadi.org/api/internal/d4/d89/group__nlpsol.html) describes extraction as moving single-variable constraint rows to variable bounds.

## What exactly is snapshot saving?

A snapshot is a checkpoint of search progress so a budget stop or restart can resume without losing candidates or replaying completed requests. It contains the DFS stack and siblings, discrete programs/constraints, current in-flight request and repair/generation cursors, seen/completed work, feasible incumbents and local-run representatives. Our current file also embeds full diagnostic/audit history: numerical requests, candidate trajectories, past windows, accepted variants and repeated copies of related records.

The current sequence is:

1. The worker emits an improved candidate; the parent validates it and updates the incumbent.
2. The parent builds an inner repair-state snapshot and attaches it to the outer DFS state.
3. The progress callback rewrites the **whole report**, using indented JSON and gzip, then atomically replaces the checkpoint file.
4. Only after this returns does the parent continue receiving worker messages and checking the search queue.

This is synchronous work in the orchestration process. The IPOPT subprocess can continue or finish meanwhile, but the parent cannot collect its updates until saving finishes. The same wall-time deadlines continue to advance. That is why the probe's otherwise0.235-second numerical call returns `time_limit` after4.169 seconds with one large save; its accepted incumbent is still preserved.

The measured109MB is one late **uncompressed progress payload**, not merely the278-row request (about44KB). It includes extensive repeated history. About4.04 seconds was JSON encoding plus compression alone, excluding disk writes. Compact formatting lowers the same payload to44.73MB and0.746 seconds, but several such saves can still exhaust a two-second call budget.

A better design keeps current mutable resume state small, records immutable audit/request/trajectory data once and refers to it by identity, and avoids rewriting the entire accumulated history for every incumbent. Small durable records for in-flight/completed work and incumbent updates can preserve resumability, with periodic/full checkpoints and a final report on exit. This is a recommended change, not an implemented redesign. It is separate from corridor work and does not require changing solver physics or the decisions being searched.

Reproduce only the extraction compatibility probe:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 PYTHONPATH="$PWD" .venv-guidance/bin/python \
  standalone/autonomy/experiments/last-jump-runs/row-benchmark/extraction_probe.py \
  --input standalone/autonomy/experiments/last-jump-runs/evidence/real11-retry-after-handoff.json.gz \
  --output /tmp/extraction-probe.json
```
