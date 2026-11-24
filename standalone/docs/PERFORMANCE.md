# Performance: measurements, hotspots and invariants

Where solver time goes, what has already been optimized, and the contracts any
speedup must preserve. Measurements are timed observations of recorded controls,
not optimality certificates or guaranteed recoverable speedups.

## Measuring

Use the cold profiler from `standalone/` with the runbook environment, a fresh
output directory and single-threaded BLAS:

```sh
"$PKC_IPOPT_PYTHON" -B -m tools.profiling.run \
  --input cases/real11/physical-anchors.json --output /new/path/cold \
  --seconds 300 --profile-mode bookkeeping
```

`--profile-mode enumeration` attributes constraint-enumeration work instead.
Reports record source revision and hashes, fixture, interpreter, native
descriptor, the request ledger and independent retained-witness replays; the
per-route report and the other outputs are described in
[solve reports](RESULTS_AND_DEBUGGING.md#solve-reports).
Self times subtract instrumented children; inclusive categories overlap; hook
overhead is included. Without `--durable` the profiler still passes a progress callback, so
repair progress snapshots run there but not in the solve CLI without `--save-progress`; time
production behavior through the CLI. Keep profiler hooks in `tools/profiling/`, never in
production code.

## Python/C++ pruning pilot

On the frozen encounter-groups baseline `e9ca522c`, a C++17 port of interval
envelopes measured 7.141982x faster across three fresh-process corpus runs; the
individual interaction checks measured 0.924265x. Replacing both inside the real
Python search measured 1.066065x on five completed short pairs without durable
checkpoints and 1.030490x on three pairs with them. Every short pair matched
ticks, objective, work, replay validation and ordered search transitions.
Two extended deadline pairs expanded Python `[21, 21]` versus native `[22, 21]`;
this is exploratory progress evidence, not a time-to-solution speedup.
The [pilot and portable results](../research/cpp-pruning-pilot/README.md) record
the exact corpus, build, timing boundary, validation scope and raw evidence paths.
This measures two pruning functions, not a full C++ migration; the tiny-query
regression motivated the shared native batch experiment below.

The [batched experiment](../research/cpp-pruning-pilot/README.md#batched-experiment)
measured 17.999197x for native envelope construction plus an ordered query batch,
returning only decisions. Checks against retained native bounds measured
1.045212x; checks against mutable Python snapshots measured 0.780464x. These
boundaries use the same sampled batches within this experiment; they differ from
the first experiment's corpus and full-envelope return contract.
The integrated public bridge measured 1.096448x on five completed short pairs
and 1.029087x on three durable pairs, with identical search-transition hashes
also matching the scalar baseline. Extended pairs expanded native `[24, 24]`
versus Python `[21, 21]`, without a finish. This remains a small mixed backend;
the direct component multiplier does not estimate a complete migration's speed.

## Real11 cold 300 s controls

| Source | Change | Best T | Best Z | Requests / workers | Expanded | Replays | Evidence |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `bbb375cf` | unrestricted initial domain | 64 | -1093.2381026602416 | 61 / 55 | 39 | 55/55 | [bookkeeping baseline](../research/bookkeeping-baseline/README.md) |
| `86c06e39` | skip already-visited constraint children | 64 | -1093.4443574703134 | 72 / 66 | 39 | 61/61 | [enumeration reduction](../research/enumeration-reduction/README.md) |
| `641c68b5` | anchor search under global run budgets | 67 | -1093.823129063534 | 49 / 47 | 21 | 41/41 | [run budgets](../research/anchor-run-budget/README.md) |
| `e2b5e02c` | screen passage prefixes before extension, ceiling moves on | 67 | -1093.823129063534 | 59 / 57 | 21 | 50/50 | [early passage screen](../research/early-passage-screen/README.md) |
| `e266785c` | geometry-driven avoidance passages | 64 | -1093.2381026602418 | 53 / 49 | 37 | 47/47 | [geometry passages](../research/geometry-passages/README.md) |
| `b2977e0f` + step 2 | avoidance on obstacle clusters | 64 | -1093.2381026602416 | 25 / 21 | 37 | 20/20 | [obstacle clusters](../research/obstacle-clusters/README.md) |
| `6b964cb0` + step 4 | legs optimize, switch-tick climb, close fallbacks | 64 | -1093.2381026602416 | 18 / 17 | 37 | 10/10 | [switch climb](../research/switch-climb/README.md) |
| `4375cd96` | disk relaxation; one IPOPT finalization (stock `c9deef42` paired: 18 / 17, 7.54 s) | 64 | -1093.2381026602418 | 14 / 0 + 1 finalization | 37 | 10/10 | [disk relaxation](../research/disk-relaxation/README.md) |
| `db7f8f61` + avoidance table | block table for generators and avoidance | 64 | -1093.2381026602416 | 18 / 17 | 37 | 10/10 | [block table](../research/block-table/README.md) |
| `b4180095` + group frames | B4 → B7 searched as one encounter group | 64 | -1093.2381026602416 | 25 / 23 | 75 | 8/8 | [encounter groups](../research/encounter-groups/README.md) |
| `533e3831` + side families | decision checks commit side families, siblings queued | 64 | -1093.2381026602416 | 26 / 24 | 75 | 8/8 | [encounter groups](../research/encounter-groups/README.md) |
| `c2b2a3d2` (on PR 5) | confirmation walk, decision objectives, conflict-driven avoidance; 6.2 s | 64 | -1093.2381026602418 | 19 / 1 + 1 finalization | 11 | 8/8 | [encounter groups](../research/encounter-groups/README.md#phase-4-controls-2026-11-20) |
| `fb50d8f8` + run-tick revision | time-bounded minimum run ticks; 5.8 s | 64 | -1093.2381026602418 | 19 / 1 + 1 finalization | 11 | 8/8 | [encounter groups](../research/encounter-groups/README.md#time-bounded-minimum-run-ticks-2026-11-20) |
| `fb50d8f8` + run-tick rework, walk-off faces | run-offs vs the jump onto their block, walk-off faces; 6.1 s | 64 | -1093.2381026602418 | 19 / 1 + 1 finalization | 11 | 8/8 | [encounter groups](../research/encounter-groups/README.md#run-tick-rework-and-walk-off-faces-2026-11-20) |
| `7ac9eed6` | frame-end re-solves in place of the finalization (`25f7fc62` paired: 39 / 0 + 1 finalization, 4.10 s); 5.14 s | 64 | -1093.2383028783543 | 39 / 0 + 8 frame-end | 10 | 8/8 | [post-alarm](../research/post-alarm/README.md#frame-end-re-solves-on-real11) |

On the extended task (measurement only) side-family siblings multiply decision
checks inside the B4 → B20 group: 234 checks in 300 s versus the base's 140, and
the search does not leave that group ([evidence](../research/encounter-groups/README.md)).
At `c2b2a3d2` it still ends in the B4 → B22 frame at the deadline. It makes 172
requests, with 35 IPOPT confirmations instead of 85 before the confirmation walk,
and expands 60 prefixes instead of 44. Time-bounded minimum run ticks cut the
frontier's minimum-run step from 238.4 s to 157.7 s of the 295.0 s search; the freed
time goes to decision checks (293 requests, 65 confirmations, 93 expanded), still in
that frame. With run-offs compared with the jump onto their block and walk-off faces
the run was measured only under other applications' load (199 requests,
[evidence](../research/encounter-groups/README.md#run-tick-rework-and-walk-off-faces-2026-11-20)).

The switch-climb run finds T64 at 6.1 s and stops at 10.0 s with
`numerical_unknown`: the DFS ran out of runnable work while the B7 frame stayed
suspended on one avoidance alternative that the linear control relaxation
blocked before any IPOPT solve. Such a blocked alternative now keeps its proof and counts
as infeasible, so the current control (`1ea2bb1f` plus that change) ends `exhausted` with the
same T64, objective and 18 requests. Four IPOPT solves ended
`Infeasible_Problem_Detected`; a local IPOPT infeasibility is not a certificate,
so the run claims no exhaustive coverage. The B7 frames solve 3 alternatives each
instead of 7 (21 before obstacle clusters): the fourth alternative of the first B7
frame is now ruled out before solving, because the linear relaxation LP that HiGHS
used to leave `Unknown` is resolved by the interior-point retry (132 LPs solved in
that run, 15 proved infeasible, none undecided, one retry; requests unchanged); every anchor leg now optimizes
instead of stopping at its first feasible point, so feasibility-only solves remain
only in decision checks. Fewer replays reflect the dropped sibling routes, not
lost coverage of the kept routes.

Reading every generator reachability query from one approach per prefix and start
(instead of one per queried tick) cut the Real11 control's coefficient compiles from
213 to 156 and `analyze_model` self time from 0.82 s to 0.71 s, with an identical
report. An approach as long as the scene's 200-input limit cost more than the short
ones it replaced (2.14 s), so the shared approach stops at the last tick a generator
can query.

The block table removes generator questions about blocks out of one flight's reach.
On the Real11 control (`1df37d63` plus the table, load average 6 to 7) it built 5 owner
entries in 0.0008 s of CPU and cut route-based generator questions from 445 to 366, with the
same 148 approach builds and an identical report apart from timings; the removed questions
are the far B6 flight-case rejections. Applied to avoidance, the table cut Real11's
obstacle checks from 600 to 380 with the same touchable checks and an identical report,
and keeps a synthetic route's avoidance items at 10 from 7 to 1,000 far solids (356.7 s
without it at 100 solids, 0.012 s with it at 1,000; [evidence](../research/block-table/README.md)).

The interval engine checks travel pairs only up to the last constrained state, and the gate
shares one approach per prefix and start across a node's generators. On 54 Real11 approach
programs of 50 to 199 ticks an envelope build fell from 24.7 ms to 4.3 ms with identical
results (interleaved CPU timing, load average about 10). Real11 itself is unchanged: paired
runs of `3f869392` and the change, with `candidate_seconds` 20 and `spatial_seconds` 50 to
keep wall-clock budgets out of a loaded machine, give identical reports, 74 approach builds
instead of 142 analysis-cache lookups, and 5.8 s of CPU on both sides, because its
approaches are 10 to 79 ticks long.

## Measured hotspots

Unrestricted 300 s profile at `bbb375cf`
([raw profile](../research/bookkeeping-baseline/evidence/2026-11-15/unrestricted300-profile.json)):

| Instrumentation bucket | Calls | Self seconds |
| --- | ---: | ---: |
| `numerical.evaluate` | 53 | 145.6983 |
| `identity.digest` | 30,804,036 | 67.1996 |
| `copy:spatial.checkpoints:retain_prefix` | 449 | 29.8097 |
| `copy:search.repair:snapshot` (three sites) | 699 each | 33.6407 |
| `solver.invoke` | 61 | 9.9012 |

`numerical.evaluate` includes unwrapped preparation, refinement and validation
work. Verify digest call counts and call sites before attributing cost; the cold
profiler writes its shared store only after search, so durable checkpoint IO
needs a separate control.

The Real11 extended control (300 s, deadline-bound) is generation-bound. At `ea33c191`
`program.program_physics_key` took 81.3 s over 111,967 calls
([comparison with stock and encounter groups](../research/disk-relaxation/2026-11-19/extended-profile-compare.txt));
encounter groups cut it to 11.6 s over 18,134 at `fb50d8f8`, which leaves 137.4 s of
uninstrumented generation and DFS self time, `preparation.analyze_model` 56.1 s,
`preparation.prepare` 43.1 s, `numerical.evaluate` 25.0 s and `solver.invoke` 2.5 s, with
57.9 s of GC overlapping them; durable saves add 71.2 s of checkpoint callbacks
([profiles](../research/prefix-reuse-keys/README.md)). Reusing norm-check pairs across
children and keying analyses by their `prepare` key then cut search time 14.6% on equal
work (call cap 100) over `fb50d8f8` and 12.6% over the run-tick rework (`8ccc9e49`, which
itself halves that run), with identical transitions. Replacing that reuse with a travel check
of new states against new and the last 10 parent states cut a further 1.9% on that input,
again with identical transitions.

With durable saves the extended run is checkpoint-bound: at `fb055163` checkpoint callbacks took
183.27 s of 299.77 s, full saves 159.96 s of it, because each full save re-encodes the whole state
(32.6 MB at the end, mostly the DFS stack) although little of it changes
([checkpoint cost](../research/checkpoint-cost/README.md)). Full saves now find the active program's
aliases in their own encoding walk instead of a second one (54.39 s in that run), which cut search
time 6.4% on the durable call-capped input with identical reports. The solve CLI now saves progress
during a run only with `--save-progress`; on the call-capped input its wall time is 39.78 s and 39.65 s
without the flag against 66.28 s and 66.53 s with it, with identical final reports. Without a
saver the repair loop also takes no progress snapshots (it used to deep-copy its state on every
publication): 39.84 s to 34.37 s mean CLI wall time on that input, identical results.

Children extend their parent instead of rebuilding it ([parent-linked programs](../research/parent-linked-programs/README.md)):
an append without ladder involvement traces only its new ticks and shares the parent's content.
`append_choice` fell from 6.276 s to 0.408 s on the call-capped input and search time 16.2%, with
identical reports. Program identity is a per-state chain that a child resumes from its parent,
so `program_physics_key` fell from 2.043 s to 0.775 s there (search -2.1%). Gate preparation and
envelopes still cost time proportional to the whole program.

Constraint enumeration after `86c06e39` runs 2.93x to 3.56x faster on captured
B6/B7 catalogues, with 94.1% to 94.7% fewer iteration checks and identical
ordered options. That is targeted enumeration work, not a whole-solver speedup.

## Cost levers already in place

- In-process movement bridge: on the extended B22 task at an equal 300 s budget,
  coefficient, scene and footprint calls took 0.72 s against 6.84 s through the Java
  process, with identical requests ([evidence](../research/jvm-free-bridge/README.md)).
- Native gate screen (opt-in, `PKC_NATIVE_CORE`): preparation, analysis, the
  required-contact screen and the child programs of sequence screens stay in C++. At
  equal work on the extended case capped at 100 solver calls, search time is 33.7% lower
  than Python's with identical transitions; building sequence children natively added
  1.7% of that ([evidence](../research/native-core/README.md)).
- Native anchor DFS loop (`PKC_NATIVE_CORE`): the loop, its frontier and decision checks run in C++ and publish
  frozen snapshots the checkpoint store encodes natively. Fixed work takes 17.45 s against 17.89 s for the Python loop
  over the same native expansion, and 58.3 s against 123.8 s with durable checkpoints
  ([evidence](../research/native-core/README.md)).
- Native node-solve services (`PKC_NATIVE_CORE`): the gate's analysis and face tables, the relaxed dispatch and its
  IPOPT driver run on the gate's native core. Fixed work takes 17.03 s and 16.52 s against 18.33 s and 18.09 s with
  them as Python callbacks; the short control and durable runs are unchanged. Running the anchor-leg and finish solves
  the same way then takes the short control to 1.258 s and 1.312 s against 1.643 s and 1.613 s, and durable fixed work
  to 54.98 s and 55.03 s against 56.10 s and 55.99 s, with fixed work unchanged
  ([evidence](../research/native-core/README.md)).
- Native mode after main's PR #10 (merged search, native port): fixed work 14.17 s and 13.83 s against Python mode's
  40.28 s and 39.46 s, and 55.97 s and 55.31 s against 125.6 s and 129.9 s with durable checkpoints
  ([evidence](../research/native-core/README.md)). With the anchor adapter native too, search time is unchanged:
  fixed work 14.18 s and 14.38 s against 14.80 s and 14.31 s, the short control 1.297 s against 1.365 s (means of five)
  ([evidence](../research/native-core/README.md)). Of the setup builders only the event domain (84.1 ms against
  91.9 ms on fixed work) and the encounter groups (2.3 ms against 6.7 ms) run faster natively, so only they dispatch
  ([evidence](../research/native-core/README.md)).
- Native report encoding (`PKC_NATIVE_CORE`): checkpoint saves hand plain JSON containers to the native encoder.
  Durable fixed work searches in 42.56 s and 42.50 s against 53.40 s and 53.03 s, the durable short control in 5.68 s
  and 5.93 s against 8.12 s and 8.14 s, and fixed work's final report save falls from 2.48 s to 1.36 s
  ([evidence](../research/native-core/README.md)). With the store's encode cache an unchanged part costs a lookup
  in the next save: durable fixed work searches in 16.93 s and 17.88 s against 43.81 s and 44.95 s (plain 13.76 s and
  13.79 s), the durable short control in 2.46 s and 2.48 s against 5.83 s and 6.73 s, at about 100 MiB more peak
  memory ([evidence](../research/native-core/README.md)).
- Clarabel iterative refinement off in every disk solve: on the extended fixed-work control
  (100 solver calls) search time fell from 39.36 s to 37.50 s with identical transitions and
  verdicts, and a 300 s extended run spent 7.92 s instead of 11.43 s in Clarabel requests
  ([evidence](../research/clarabel-refinement/README.md)).
- IPOPT row reduction: final rows fall from 278 to 91 on the benchmark
  ([constraint reduction](IPOPT_CONSTRAINT_REDUCTION.md)).
- Content-addressed manifest plus SQLite checkpoints
  ([checkpoint storage](CHECKPOINT_STORAGE.md)).
- Shared interaction screens before any solve: interval and disk-relaxation reachability
  ([obstacle relevance](design/OBSTACLE_RELEVANCE.md)), and accumulated passage
  rows screened before each extension ([avoidance passages](design/AVOIDANCE_PASSAGES.md)).
- Frontier selection (`search/frontier.py` `take_first`) pops the smallest priority key
  and breaks ties by list order, so it hashes no program identity (user, 2026-11-21:
  every tied program is expanded eventually). On a saved 762-program B4 -> B20 queue a
  full-key take had cost 0.478 s against 0.004 s without identities
  ([encounter-groups evidence](../research/encounter-groups/README.md)).
- Bounded accelerator caches: interaction gate 512 entries
  (`reachability/service.py`), variant preparation 256 and option keys 1,024
  (`constraints/variants.py`).

Open measurement: solve cadence 1 versus 2 or 4 decision levels
([anchor policy O4](design/ANCHOR_POLICY_DECISIONS.md)).

## Owners to inspect

| File | What to look at without changing behavior |
| --- | --- |
| `canonical.py`, `search/identity.py` | `digest` (owned by `canonical.py`; a replacement must keep its exact bytes, since search order breaks ties on it), `motion_record`, `solve_scope`, `solve_identity`: repeated canonical serialization. |
| `movement/program.py` | `program_search_key` (owners and repair hash), `compile_program`: repeated serialization of immutable content. `program_physics_key` resumes the parent's per-state chain. |
| `reachability/service.py` | `InteractionGate.prepare` key construction (scene and program physics key), which costs even on a cache hit. |
| `search/engine.py` | `evaluate_program`: scope, coefficients, replay, solve identity, variants, validation. |
| `constraints/variants.py` | Repeated canonical row sets and cursor records. |
| `search/repair.py`, `search/repair_state.py` | `snapshot`, `publish`, `save_branch`: repeated copying of unchanged results. |
| `spatial/checkpoints.py` | `retain_prefix` copies; sharing must not let siblings mutate each other. |
| `io/search_checkpoint.py`, `io/checkpoint_store.py` | Full saves re-encode and hash the whole state, including unchanged frames; repair patches; content addressing. |

## Cache and identity invariants

1. Canonical identity outputs stay identical for unchanged semantics: no rounding,
   coordinate normalization, dropped ownership fields or constant-ID shortcuts. A
   pure cache or copy optimization needs no physical or search policy bump.
2. Physical keys exclude descriptive metadata; search keys keep constraint owners,
   committed horizon, interactions, scope and sometimes repair state.
   `dfs.candidate_key` excludes repair state. Keep the contracts distinct.
3. Solve identity covers initial state, modeled geometry, input policy, program,
   objective, validation scope and coefficients, plus the caller's solve intent,
   node query and assembly context. Different `stopOnFeasible`, oracle behavior or
   validation horizon never share an exact repair result.
4. Reachability proofs are scoped to role, windows, phases, rows, contact context
   and named-probe uncertainty. A conditioned row set cannot poison the
   unconditioned analysis.
5. Never cache mutable objects by `id()` across mutations or saves, unless the object is behind
   a freeze boundary: a built program's content (profile, trace, movement units, ladder events
   and their elements) never changes, so children share it. A timeout during construction
   commits no partial result and consumes no raw ordinal.
6. Caller mutations must not alter cached canonical values. Performance caches are
   disposable: rebuild them on external resume and never serialize them per node.
7. Ordering and deferral verdicts are not physical facts; incumbent, latch and peer
   revisions change independently of the model.

## Snapshot, resume and ownership invariants

- A published snapshot keeps its value at publication; returning the live repair
  dictionary or a shallow copy is not sufficient.
- Live aliases of the active program and its node entry receive the same repair
  publication; external decode yields independent mutable copies.
- Queue order, cursors, in-flight work, pending and unknown work, accepted
  variants, logs and numerical accounting are retained. Never improve timing by
  deleting evidence or work.
- Completed exact requests never rerun after resume; interrupted work stays
  recoverable; timeout, error and unknown are not infeasibility.
- A solve cut off by cancellation resumes before any other work in its frame
  (`dfs.py` `refine`), so a cancelled and continued search sends the same
  requests as an uninterrupted one even when sibling branches are runnable.
- Durable repair updates patch the frozen dispatch root, every active alias and
  cumulative calls. Store contents commit before root publication; manifest and
  SQLite companions stay paired.
- Stable ordinals and ownership survive JSON serialization.

## Related reading

- [Branch scheduling](design/BRANCH_SCHEDULING_INVESTIGATION.md): where earlier T64
  discovery is lost to DFS order, and the overhead and risk list for a scheduler.
- [Anchor execution reference](ANCHOR_SEARCH_EXECUTION_REFERENCE.md): which work
  reaches IPOPT and where pruning happens.
- [Pruning research](research/PRUNING_RESEARCH.md): root causes of earlier
  starvation and the anatomy of numerical requests.
- [Stratfinder investigation](research/STRATFINDER_INVESTIGATION.md): coefficient
  caching and filters that pruned nothing.
