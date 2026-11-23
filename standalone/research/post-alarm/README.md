# Post-alarm answers

Evidence for the post-alarm changes to the relaxed backend
([solver](../../docs/reference/NUMERICAL_SOLVER.md#disk-relaxation)). The alarmed-request populations come from the
post-alarm study (research branch `research/ipopt-seeds` at `1a5dcb53`); copy `population.json`,
`population-fresh.json` and `population-heldout.json` into the ignored `runs/post-alarm/` to rerun.
The sections from "Real11 A/B" on ran against dfc38a56, whose pipeline ended in two IPOPT calls; their scripts
subclass that pipeline and run at that revision. The pipeline that replaced it is their `lean-decayprobe`
variant: on all 482 Real11 and 370 study requests it returns the same method and objective
(`implemented-vs-benchmark.log`).

## Kept-alive worker

The kept-alive worker was removed with the pipeline's IPOPT calls (no other caller); the script runs at `725648e9`.

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

## Real11 A/B

`run_real11_ab.sh` (2026-11-23 01:50-03:06 PST, `dfc38a56`, load average 1.9 to 7.5): `post_alarm` `ipopt` (A) against
`pipeline` (B) on the same source; comparison `compare_real11_ab.py` with the migration parity harness
(`tools/profiling/parity.py` at `364f5699`) ([comparison](real11-ab-compare.log), [verdicts on common
requests](real11-ab-verdicts.log)). Every run passed every replay; neither arm finds a route in its budget (as on main).
Every pair's 19 transitions are identical, exactly, so no search decision changed.

| runs | search s (A / B) | confirmations | IPOPT calls for them | Clarabel solves for them | worker launches | seconds answering alarms (A / B) |
|---|---|---|---|---|---|---|
| fixed work, 100 calls, 3 pairs | 38.60 / 37.95 mean | 5 / 5 | 5 / 1 | 0 / 145 | 5 / 1 | not recorded (`--profile-mode none`) |
| extended, 300 s | deadline both | 41 / 41 | 41 / 26 | 0 / 1023 | 41 / 1 | 11.96 / 5.59 (median 231.6 / 68.2 ms) |
| extended, 4096 calls, 1800 s | deadline both | 198 / 206 | 198 / 68 | 0 / 3299 | 198 / 1 | 60.22 / 15.96 (median 234.0 / 31.1 ms) |

On the 179 alarmed requests both 1800 s runs confirmed: 136 answered by both, 24 by neither; the pipeline answers 14 the
IPOPT confirmation called infeasible (`Infeasible_Problem_Detected`; 11 of them at full strength, 3 relaxed), and leaves 5
unanswered that it answered. Of the 136, 45 pipeline answers are more than 1e-3 below IPOPT's, 2 by more than 1e-2
(reversal-race answers: 0.0239 and 0.4767 below), and 3 above.

The 5 misses ([capture](capture_misses.py), [check](check_misses.py), [log](real11-ab-misses.log)): a 300 s rerun of
arm B saved its 12 unanswered problems. Today's confirmation answers 3 of them, each only through its `fallbackYaw`
phase; from the production seed alone IPOPT ends `Infeasible_Problem_Detected` at 0.2 s and at 2 s. The other 9 are
unanswered by today's confirmation too.

## Seed bench

Which IPOPT starting points, built only from what the pipeline already has when it reaches IPOPT, answer alarmed
requests in one start each. Source dfc38a56 plus this directory's uncommitted scripts; `.venv-guidance`,
`OPENBLAS_NUM_THREADS=1`; machine shared with other runs (load average 3.7-4.0 on 8 cores).

- `seed_bench.py` runs the production push from the disk point (recording iterates), builds each seed (its docstring
  lists them), validates it, and runs one kept-alive IPOPT start without `fallbackYaw` at 0.2 s, rerun at 2 s only when
  0.2 s gave no answer. Labels are read only by the scorers.
- Sets: the 12 unanswered Real11 requests of the A/B (`capture_misses.py`); the 371 study requests (labels as
  `pipeline_grid_score.py`); 482 fresh alarmed confirmations from an 1800 s pipeline run of the A/B's long-B fixture
  (`capture_alarmed.py`), 78 of which reached IPOPT (production seed 34, polish 8, none 36). Real11 references are the
  best answer of any seed or of the pipeline itself.
- Scores: `seed_bench_score.py` (per seed; `seed-bench-misses-score.log`, `seed-bench-population-score.log`),
  `seed_bench_alarmed.py` (`seed-bench-alarmed-pairs.log`), `seed_bench_policies.py` (two IPOPT calls at the
  production 0.2 s cap, the second only when the first gives no answer; `seed-bench-policies.log`).

Two-call policies, requests answered within 1e-2 of the reference (gained/lost against today's production seed then
polish), and mean IPOPT seconds per request:

| policy | 12 misses | study requests reaching IPOPT | Real11 requests reaching IPOPT |
|---|---|---|---|
| production seed, then polish (today) | 0/5, 0.273 s | 18/18, 0.189 s | 42/55, 0.217 s |
| polish, then flip1 | 5/5 (+5), 0.258 s | 17/18 (-1), 0.156 s | 52/55 (+12/-2), 0.175 s |
| flip1, then polish | 5/5 (+5), 0.221 s | 16/18 (-2), 0.195 s | 53/55 (+12/-1), 0.198 s |
| last push iterate, then flip1 | 5/5 (+5), 0.243 s | 18/18, 0.163 s | 48/55 (+12/-6), 0.194 s |

- `flip1` is the relaxed directions with the single most short tick reversed at full strength. Every one of its 12
  Real11 gains is one route family: the most short tick is tick 69, a non-jump tick 52-66% short, which the race never
  considers (it only reverses jump ticks near an alarm), on 82-86 tick routes with objective about 754.6. Alone it is
  weaker than the production seed on the study set (11 vs 16 of 18 reaching IPOPT).
- Each loss of the polish-first policies is an answer, not a miss: polish lands 0.03-0.08 below the reference where the
  production seed found the better one.
- No seed answered any of the 27 known-infeasible study requests or the 15 Real11 requests the sector check certified
  infeasible.
- The scalar validator's start violation does not pick seeds: on the misses the least-violation seed answers 0 of 5
  (the push iterates have the least violation and fail every time); on the study set 18 of 18.
- Tracking, sigma-delta, central-point, relaxed-states and warm-prefix seeds answer at most 2 of the 5 misses and never
  more than polish on the study or Real11 requests reaching IPOPT (per-seed tables in the score logs).

## Push stall and pipeline variants

Why the push leaves requests to IPOPT, and whole-pipeline variants against today's. Source dfc38a56 plus this
directory's uncommitted scripts; same environment as the seed bench. The variants are subclasses in
`pipeline_variants.py`; production code is unchanged.

- The push stalls. On the 78 Real11 requests that reached IPOPT, the push hits its 10-step cap every time without a
  full-strength iterate; 100 steps answer 5 (`push_why.py`, `push-why.log`). Its objective weight changes only after a
  full-strength iterate, so it stays at W0 and the objective keeps ticks short. Halving it after every step that is not
  full strength answers 38 of the 55 with a known answer, 32 within 1e-2, in 0.080 s mean, Clarabel only
  (`push_decay.py`, `push-decay.log`). It answers 2 of the 17 requests of the tick-69 route family, whose answer turns
  that tick about 100 degrees from the push's direction.
- Variants (`pipeline_variants.py` docstring), scored by `pipeline_variants_score.py` with the seed bench's references
  (Real11: best of any variant, seed or the captured pipeline; the 15 sector-certified requests stand in for
  known-infeasible). `pipeline-variants-score.log` holds the decay variants and `pipeline-variants-score-nodecay.log` the
  rest; each has its own `today` arm run alongside.

| variant | study: within 1e-2 (+/- vs today), IPOPT calls, mean ms | Real11 482: within 1e-2 (+/-), IPOPT calls, mean ms | Real11 within 1e-3 |
|---|---|---|---|
| today (no-decay run; the decay run's today arm: 46 / 123 IPOPT calls, 37 / 73 ms) | 340, 46, 37 | 418, 122, 71 | 270 |
| race also reverses the most short tick, probes after a race answer; IPOPT as today | 340 (+0/-0), 46, 40 | 438 (+20/-0), 93, 68 | 273 |
| same, IPOPT flip1 then production seed | 337 (+0/-3), 51, 42 | 432 (+20/-6), 105, 73 | 271 |
| decayed push (cap 30), extended race, probes after race, IPOPT flip1 then production seed | 337 (+0/-3), 26, 37 | 433 (+23/-8), 46, 63 | 254 |
| decayed push (cap 30), IPOPT as today | 337 (+0/-3), 26, 35 | 415 (+6/-9), 73, 65 | 252 |

- The extended race is the gain: 19 of the 20 Real11 gains of the first variant reverse the most short tick (12
  requests today leaves unanswered, all the tick-69 family; 7 better answers); answers on infeasible are 0 everywhere.
- The decayed push trades quality for IPOPT calls: every loss (`pipeline-variants-decay-losses.log`) is a push answer
  0.011-0.23 below the reference where today's IPOPT or race answer is better, and the probes do not recover them,
  also with only the plain push decaying (1 of 11 recovered).
- Once the race covers the reversed-tick case, flip1 as the first IPOPT seed loses to the production seed (-6 on
  Real11).
- Without IPOPT (`pipeline-variants-score-noipopt.log`): the extended race with probes, then, where today calls IPOPT,
  the decayed push from the central point (cap 30) and probes. Study 340 within 1e-2 (+0/-0), 0 IPOPT calls, 32 ms
  mean (today 40); Real11 445 answered (today 431), 433 within 1e-2 (+25/-8), 0 IPOPT calls (today 124), 54 ms mean
  (today 76), p90 171 ms (269). Of the 8 losses (`pipeline-variants-noipopt-losses.log`) 5 sit on the 1e-2 boundary
  (both 0.0100) and 3 are 0.012, 0.013 and 0.044 below. No answers on infeasible. Today's own counts move between runs
  (418 and 416 within 1e-2) because its 0.2 s IPOPT cap is timing-sensitive on a loaded machine.
- Winning reversal ticks (`pipeline-variants-reversal-ranks.log`): on Real11, 48 of 59 are the disk point's most short
  tick, 8 the 2nd or 3rd, 3 jump ticks in today's window with little shortfall; the study's 4 are all window jump
  ticks. Today's window tries 2.98 ticks per request on average (max 6), adding the most short tick 3.36 (max 7).
- Keep going (`pipeline-variants-score-keep.log`): no IPOPT, and the race and then the decayed push also run while the
  best accepted answer is more than G below the bound (each stage with its own probes; the best answer is kept).
  Against the no-IPOPT variant in the same run: Real11 434 vs 432 within 1e-2, 276 vs 264 within 1e-3, mean 64 vs 47 ms
  (today 67), p90 187 vs 146 ms; study 340 vs 340, 125 vs 123 within 1e-3, mean 40 vs 28 ms (today 35), p90 150 vs 50 ms.
  It improves 45 Real11 and 7 study answers, never worsens one, by 0.0145 and 0.0789 at most and under 0.01 otherwise
  (`pipeline-variants-keep-improvements.log`; mostly the decayed push after a push answer). The 8 Real11 losses
  against today's IPOPT remain. Even the best known answer is more than 1e-2 below the relaxed bound on 113 of 445
  Real11 and 72 of 340 study requests (`pipeline-variants-bound-gap.log`), so those always run every stage.

## Solves in the no-IPOPT pipeline

Where the proposed no-IPOPT pipeline spends Clarabel solves, and leaner versions (`flow_stages.py` instruments it;
`flow-stages.log`, `flow-stages-payoff.log`; variants in `lean_variants.py`, scored in `lean-variants-score.log` and
`lean-variants-guard-score.log`).

- Real11, 8802 solves on 482 requests: race 2994 (125 requests), first push 2354, decayed push 1032 (62), sector check
  966 (140; 15 certified infeasible), probes after the push 488 (69 requests, 5 gained), after the race 241 (34, 5
  gained), after the decayed push 245 (35, 0 gained), node setup 482. Study, 4157 solves: first push 1482, race 868 (35
  requests, 4 won), decayed push 563, probes after the push 384 (55, 3 gained), after the decayed push 112 (16, 0 gained).
- The race's finishing pushes (1771 Real11 solves) restart each finished candidate from its central point, repeating
  537 screening steps, and improve on the screening answer by more than 1e-2 on 2 of 86 candidates (0.0295 at most);
  61 of 63 winners already had an answer after screening, and the winner was the best-bound candidate in 55 of 63.
  40 Real11 requests spend 1176 race solves and are then answered by the decayed push.
- Probe success is not predicted by the gap to the bound (gained 0.012-0.557, not gained 0.01-0.145).

| variant | Real11 within 1e-2 (+/- vs proposed) | Real11 solves mean | Real11 mean / p90 ms | study within 1e-2 | study solves mean |
|---|---|---|---|---|---|
| proposed | 432 | 18.3 | 48 / 147 | 340 | 11.2 |
| lean: race screens candidates in bound order, stops once no other candidate's bound can beat its answer, no finishing unless nothing answered; no probes after the decayed push | 432 (+0/-0) | 15.6 | 41 / 121 | 340 | 10.3 |
| lean with decaying probe pushes | 433 (+1/-0) | 15.6 | 41 / 121 | 340 | 10.3 |
| decayed push before the race | 430 (+1/-3) | 13.4 | 36 / 75 | 338 | 9.5 |
| decayed push first, race also when its answer is more than 0.1 below the bound | 430 (+0/-3 vs lean with decaying probe pushes, same run) | 14.4 | 39 / 101 | 339 | 10.0 |
| decayed push continuing the first push's state | 423 (+7/-16) | 16.7 | 45 / 158 | 339 | 9.5 |

- Decayed-push-first answers differ from race-first ones by under 0.007 on most requests either way, but are 0.22 lower
  on 2 requests (study 8f2c03b2463d, Real11 d7dea8787150) and 0.010-0.018 lower on 3 (`lean-variants-decayfirst-diff.log`);
  the 0.1 guard recovers the 0.22 ones.

## IPOPT on pipeline answers

Does IPOPT find a full-strength route at each answer of the Clarabel-only pipeline (4a2f6315)? `ipopt_check.py`: one
IPOPT start (2 s, no `fallbackYaw`) from each answer's facings turned by the production seed's 1e-4 rad, on 445 Real11
and 340 study answers (`ipopt-check-summary.log`); `ipopt_check_failures.py` reruns the unconfirmed ones from the exact
facings (`ipopt-check-failures.log`).

- Full-strength answers (408 Real11, 320 study): IPOPT from the turned facings ends `Infeasible_Problem_Detected` on 22
  and 11; from the exact facings it accepts all 33 (the start itself is feasible: validator violations 8e-10 to 1e-7),
  though its later phases still end infeasible on most. The 1e-4 rad turn alone moves these validated points out of
  IPOPT's 1e-9 tolerance.
- Answers with ticks up to 1% short (37 Real11, 20 study): IPOPT confirms 35 and 19 from their facings. Of the other
  three, the study request is feasible by the study's labels (754.591); on the two Real11 requests (fab671bfb906,
  a6a2637448b7) no IPOPT start of the seed bench found a route either, so these answers are unconfirmed.
