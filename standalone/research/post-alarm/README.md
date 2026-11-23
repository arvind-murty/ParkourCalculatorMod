# Post-alarm answers

Evidence for the post-alarm changes to the relaxed backend
([solver](../../docs/reference/NUMERICAL_SOLVER.md#disk-relaxation)). The alarmed-request populations come from the
post-alarm study (research branch `research/ipopt-seeds` at `1a5dcb53`); copy `population.json`,
`population-fresh.json` and `population-heldout.json` into the ignored `runs/post-alarm/` to rerun.

## Kept-alive worker

`keep_alive_bench.py` (2026-11-23, `2a59b06e` plus the kept-alive worker, load average 2.7 to 3.8): the production
confirmation seed on the first 60 requests of `population.json`, 20 s budget, a worker per call and a kept-alive worker
alternated per request ([log](keep-alive-bench.log)). Same candidate on 60 of 60; median 0.172 s per call against
0.057 s kept alive (mean 0.204 / 0.090 s). The study's prototype measured 0.170 / 0.058 s, and 0.055 s for the same
solve in process.

```sh
OPENBLAS_NUM_THREADS=1 ../.venv-guidance/bin/python -B research/post-alarm/keep_alive_bench.py runs/post-alarm/population.json 60
```

## Population evaluation

`population_eval.py` (2026-11-23 01:37 PST, `725648e9` plus the pipeline, load average 4.0 to 5.4): every request of the
three populations, this source's disk solve of its spec, then `post_alarm.run` with a 20 s budget and the kept-alive
worker ([log](population-eval.log)). Scored by the study's `research/ipopt-seeds/pipeline_grid_score.py` against its best
known objectives ([score](population-eval-score.log)):

| set | feasible answered | within 1e-2 of best known | answers on infeasible | mean / median / p90 ms | under 50 ms | IPOPT calls (requests) |
|---|---|---|---|---|---|---|
| development (184 feasible, 17 infeasible) | 184 | 184 | 0 | 35 / 9 / 54 | 90% | 28 (19) |
| held-out (157 feasible, 10 infeasible) | 157 | 157 | 0 | 35 / 9 / 49 | 91% | 18 (12) |

The study's in-process prototype of the same configuration measured 33 / 31 ms mean and 28 / 19 IPOPT calls with a 0.1 s
IPOPT cap; this run includes the worker round trips. Two requests differ in kind: 3.637's disk point is no longer alarmed
on this source (its contact margin moved the point), so production answers it relaxed without the pipeline; 3.746 is
answered by the production seed (754.54857) instead of the polish (754.55003), because the 0.2 s cap gives the seed's
IPOPT call the time the study's 0.1 s did not.

```sh
OPENBLAS_NUM_THREADS=1 ../.venv-guidance/bin/python -B research/post-alarm/population_eval.py runs/post-alarm/eval.json \
  runs/post-alarm/population.json runs/post-alarm/population-fresh.json runs/post-alarm/population-heldout.json
```
