# Checkpoint cost and alias paths from the save walk

2026-11-21, `perf/prefix-reuse-keys` at `fb055163` (main `d9e1ec9c` plus prepared analysis
keys and the parent-window travel check). Storage design: [checkpoint storage](../../docs/CHECKPOINT_STORAGE.md).

## Where a durable Real11 extended run goes

`cases/real11/extended-anchors.json` (goal B22), 300 s deadline, `--durable --profile-mode none`, Python
bridge, single-threaded BLAS, load average 3.3 to 4.5. Timers: [`checkpoint_timers.py`](checkpoint_timers.py),
outermost call per name ([timers](2026-11-21/extended300-durable-timers.json),
[summary](2026-11-21/extended300-durable-summary.json)). The run ended at the deadline after 224 solver
calls and 71 expanded prefixes.

| Item | Calls | Seconds | Share of 299.77 s search |
| --- | ---: | ---: | ---: |
| All checkpoint callbacks | 922 | 183.27 | 61.1% |
| Full saves | 174 | 159.96 | 53.4% |
| of which `identity_paths`, a second walk finding the active program's aliases | 161 | 54.39 | 18.1% |
| of which encoding, hashing and commit | 174 | 105.57 | 35.2% |
| Repair patches | 748 | 23.30 | 7.8% |

Each full save re-encodes the whole state: the final state is 32.6 MB of compact JSON (30.3 MB of it the
DFS stack), while all 926 archives together stored 93.5 MB of new content.

[`dup_saves.py`](dup_saves.py) classified the full saves of the fixed-work input below by caller
([summary](2026-11-21/dup-ext100-summary.json)): 4 of 70 store the same content as the save before them
(0.73 s of 18.69 s), 3 of them `score`'s `begin_repair` right after its dispatch transition. Most full-save
time comes in pairs around internal-node evaluations (`evaluate_node`: 24 saves before, 7.03 s; 24 after,
8.16 s), whose state does change.

## Change

`CheckpointStore.archive(report, targets)` also returns every key path at which each target occurs,
collected in the walk that encodes the report; `SearchCheckpoint` takes its alias paths from there and no
longer walks the saved state again.

[`paths_parity.py`](paths_parity.py) compared those paths with the old walk at every full save: no
mismatch over 75 saves of the short case with bumped timeouts and 70 of the fixed-work input
([short](2026-11-21/paths-parity-short.json), [extended](2026-11-21/paths-parity-ext100.json)).

## Equal work, before and after

The extended case with `max_solver_calls` 100, `candidate_seconds` 20 and `spatial_seconds` 50
([input](../prefix-reuse-keys/2026-11-21/extended-fixedwork100.json)), `--durable --profile-mode none`, old
(`fb055163`) and new runs alternating, load average 4.0 to 6.3 ([summaries](2026-11-21/)):

| Run | Search s | Checkpoint callbacks s | Dispatch saves s | Expanded | Calls / IPOPT | Preparations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| old 1 | 78.55 | 35.75 | 13.60 | 28 | 100 / 22 | 1,412 |
| new 1 | 73.63 | 29.45 | 7.31 | 28 | 100 / 22 | 1,412 |
| old 2 | 73.62 | 32.84 | 12.19 | 28 | 100 / 22 | 1,412 |
| new 2 | 68.82 | 28.57 | 7.12 | 28 | 100 / 22 | 1,412 |

Mean search time falls 6.4% and checkpoint callback time 15.4%. The migration parity harness
(`tools/profiling/parity.py` at `364f5699`) finds identical reports for both pairs, all 19 transitions.
The native gate passed 829 tests with no failures, errors or skips ([report](native-gate/report.json);
run on `fb055163` with this change uncommitted).

```sh
PYTHONPATH="$PWD" "$PKC_IPOPT_PYTHON" -B research/checkpoint-cost/checkpoint_timers.py runs/<new>-timers.json \
  --input cases/real11/extended-anchors.json --output runs/<new> --seconds 300 --profile-mode none --durable
```
