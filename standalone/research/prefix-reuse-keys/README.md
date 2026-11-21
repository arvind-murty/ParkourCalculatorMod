# Norm-check reuse and prepared analysis keys

2026-11-21, `perf/prefix-reuse-keys` off encounter groups `fb50d8f8`. Design:
[obstacle relevance](../../docs/design/OBSTACLE_RELEVANCE.md).

## Why

The Real11 extended task (`cases/real11/extended-anchors.json`, goal B22) is
generation-bound. On `fb50d8f8` a 300 s run spends 2.5 s in Clarabel requests and
most of the rest building and screening child programs
([profile](2026-11-21/extended300-fb50d8f8-profile.json); with durable saves,
[profile](2026-11-21/extended300-fb50d8f8-durable-profile.json)). Two costs there
grow with the program's length on every child and survive a port to C++:

- `reachable_envelopes` checks every pair of earlier states (O(ticks²)) for each
  child, although siblings share their prefix states. On 205 captured inputs
  (median 80 ticks) the pair loop was 70% of the function: 0.392 s per pass, 0.117 s
  without it.
- `InteractionGate.analyze_model` and `required` keyed each analysis and proof
  context by serializing the whole model or envelope (up to 101 KB of JSON per call)
  through `interaction_context_key`.

## Change

- `reachable_envelopes(..., verified=...)` starts the pair loop after the leading
  states that equal an earlier contradiction-free envelope's states and radii,
  capped at that envelope's last checked state. The gate passes its last 8.
- `analyze` names an analysis by its `prepare` key, open-terminal flag, scope and
  initial intervals; `required` uses that key as its proof context. `PROOF_POLICY` 4.

## Equal work, before and after

Fixed work: the extended case with `max_solver_calls` 100, `candidate_seconds` 20 and
`spatial_seconds` 50 ([input](2026-11-21/extended-fixedwork100.json)), so the run ends
at the call cap with the same work instead of at a deadline. Old (`fb50d8f8`) and new
runs alternated, `--profile-mode none`, Python bridge, single-threaded BLAS, load
average 3.2 to 4.4.

| Run | Search s | Expanded | Calls / IPOPT | Preparations | Analysis misses / hits | Sequence checks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| old 1 | 89.07 | 28 | 100 / 22 | 3,423 | 3,142 / 1,080 | 2,338 |
| new 1 | 75.25 | 28 | 100 / 22 | 3,423 | 3,433 / 789 | 2,338 |
| old 2 | 86.29 | 28 | 100 / 22 | 3,423 | 3,142 / 1,080 | 2,338 |
| new 2 | 74.48 | 28 | 100 / 22 | 3,423 | 3,433 / 789 | 2,338 |

Mean search time falls 14.6%. Programs whose models are equal no longer share an
analysis, hence the extra misses. The migration parity harness
(`tools/profiling/parity.py` at `364f5699` on `research/standalone-cpp-benchmark`)
finds identical reports between old and new: status, termination and all 19
transitions, digests included. The short case with bumped timeouts
(`cases/real11/physical-anchors.json`, `candidate_seconds` 20, `spatial_seconds` 50)
also matches: T64, objective -1093.2381026602418, `exhausted`, all 62 transitions,
5.88 s old and 5.82 s new ([old](2026-11-21/short-base-summary.json),
[new](2026-11-21/short-new-summary.json)).

After the run-tick rework (`8ccc9e49`, merged into main as `d9e1ec9c`) the same input
does less generation work. The same comparison against `8ccc9e49` alone, load average
3.2 to 4.5 ([summaries](2026-11-21-main/)):

| Run | Search s | Expanded | Calls / IPOPT | Preparations | Sequence checks |
| --- | ---: | ---: | ---: | ---: | ---: |
| old 1 | 45.17 | 28 | 100 / 22 | 1,412 | 1,015 |
| new 1 | 39.91 | 28 | 100 / 22 | 1,412 | 1,015 |
| old 2 | 45.32 | 28 | 100 / 22 | 1,412 | 1,015 |
| new 2 | 39.13 | 28 | 100 / 22 | 1,412 | 1,015 |

Mean search time falls 12.6%; both pairs and the short case (T64, 4.89 s old, 4.60 s
new) have identical transitions. The native gate on main plus this change passed 828
tests with no failures, errors or skips ([report](native-gate-main/report.json); run on
the pre-rebase commit `4745444e`, whose sources and tests equal this commit's).

Direct inclusive timers on `fb50d8f8` ([`stage_timers.py`](stage_timers.py), outermost call per name;
[old](2026-11-21/stages100-base.json), [new](2026-11-21/stages100-new.json)) on the
same input:

| Stage | Old s / calls | New s / calls |
| --- | ---: | ---: |
| `interaction_context_key` | 13.20 / 6,407 | 0.16 / 4,267 |
| `reachable_envelopes` | 12.21 / 3,445 | 7.13 / 3,736 |
| `identity.digest` | 10.97 / 551,067 | 4.82 / 548,429 |
| `InteractionGate.analyze_model` | 17.29 / 4,222 | 8.66 / 4,222 |
| `InteractionGate.required` | 35.10 / 2,961 | 18.83 / 2,961 |

```sh
"$PKC_IPOPT_PYTHON" -B -m tools.profiling.run --input research/prefix-reuse-keys/2026-11-21/extended-fixedwork100.json \
  --output runs/<new> --seconds 900 --profile-mode none
PYTHONPATH="$PWD" "$PKC_IPOPT_PYTHON" -B research/prefix-reuse-keys/stage_timers.py runs/<new>.json \
  --input research/prefix-reuse-keys/2026-11-21/extended-fixedwork100.json --output runs/<new> --seconds 900 --profile-mode none
```
