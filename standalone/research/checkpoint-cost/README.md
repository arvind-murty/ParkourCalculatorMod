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

## What a full save contains

Final state of the durable 300 s run above (32.61 MB compact JSON;
[generation breakdown](2026-11-21/final-state-generation.txt)):

| Part | MB |
| --- | ---: |
| top frame's generation `continuation` | 29.37 |
| - `nodes` (76 solved decision nodes: 61 feasible 3.12, 13 exhausted 5.23, 1 suspended 2.46, 1 oracle-rejected 0.11) | 10.93 |
| - `pending` (162 queued child programs) | 6.70 |
| - `seen` (238 serialized program search keys, about 25 KB each) | 6.12 |
| - `alternatives` (held branch programs) | 2.78 |
| - `nodeInFlight` (program under repair) | 2.46 |
| `searchState.domain` | 1.15 |
| lower stack frames | 0.87 |

## What the search reads back

From the code:

- `continuation['nodes']` is read only by `evaluate_node` (`src/parkour_solver/search/dfs.py` at `saved=nodes.get(identity)`):
  `status`, the stored program and alternatives of feasible nodes, and the suspended entry's `repairState`.
  Exhausted and oracle-rejected nodes' programs (5.34 MB) are never read back.
- The suspended node's `program` is a deep copy of `nodeInFlight` (2.46 MB).
- `attemptLog` and `contactLog` only carry into records' `numericalAttempts` and `contactAttempts`: report audit.
- `seen` is a set of full key strings; the backlog note in `dca14538` proposed digests (3.757 MB to about 17 KB in
  its sample) with a checkpoint format version.

Measured with [`copy_sites.py`](copy_sites.py) and a node-memo probe in a scratch copy of the source, on a durable
300 s run of `05a1f9e9` ([summary](2026-11-21/copies-ext300-summary.json), [sites](2026-11-21/copies-ext300.json);
the copy wrapper slows the run): `evaluate_node` read `nodes` 80 times and found nothing each time, so an
uninterrupted run never uses its node results. Checkpoint callbacks took 150.14 s of 299.34 s, and the deep copies
taken for repair progress snapshots (`search/repair.py` `snapshot` and `fallbacks`) 20.23 s.

## What repeats between saves

[`save_churn.py`](save_churn.py) classified every chunk-sized subtree at each of the 70 full saves of the durable
fixed-work run ([result](2026-11-21/churn-ext100.json)). Excluding audit windows, which the store archives once,
348.0 MB were re-encoded: 325.1 MB (93.4%) had been saved before and 22.9 MB (6.6%) were new; 282.0 MB (81.0%)
were the same object with the same content as at the previous save.

| Component | Re-encoded | Unchanged | Same object as previous save |
| --- | ---: | ---: | ---: |
| `domain` | 76.5 MB | 98.5% | 93.9% |
| `nodes` | 73.9 MB | 96.3% | 94.5% |
| lower stack frames and frame fields | 46.8 MB | 99.6% | 98.2% |
| `seen` | 31.2 MB | 95.6% | 51.9% |
| `pending` | 30.8 MB | 95.3% | 58.1% |
| `alternatives` | 22.2 MB | 99.7% | 75.9% |
| `nodeInFlight` | 18.9 MB | 31.2% | 1.3% |

Within the final 300 s state ([`final_state.py`](final_state.py), [output](2026-11-21/final-state-duplication.txt)),
22.32 MB (68%) are repeated copies of identical subtrees held by different objects, and 10.64 MB of the 10.95 MB
of per-tick `profile`/`trace` arrays in 362 stored programs (97%) repeat an earlier array whole or as its prefix.

## Progress saves behind a flag

The solve CLI (`python -m parkour_solver`) saves progress during a run only with `--save-progress` (user decision,
2026-11-21); the final report is always saved. Fixed-work input as above, CLI wall time, alternating, load average
2.1 to 3.0 ([times](2026-11-21/save-flag/wall-times.txt)): 39.78 s and 39.65 s without the flag, 66.28 s and
66.53 s with it. The final reports match exactly in both pairs (all 19 transitions), and resuming from a run
saved without the flag continues exactly (`resume.exact`, session 2). The native gate passed 829 tests with no
failures, errors or skips ([report](native-gate-save-flag/report.json); run on `05a1f9e9` with the flag uncommitted).
