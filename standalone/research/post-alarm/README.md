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
