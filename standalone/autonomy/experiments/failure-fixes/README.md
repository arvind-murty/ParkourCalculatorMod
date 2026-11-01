# Generic scheduling, incumbent and initialization fixes

The user authorized fixing the three mechanisms isolated in [the root-cause research](../failure-rootcause/RESULTS.md). This changes the standalone runtime on isolated branch `research/ipopt-cell-routes`; older research-only statements describe historical source revisions.

- Zero-threshold solves now try the supplied fallback within the same total deadline. Normal momentum carry is unchanged; no threshold/pattern inference is introduced. Custom-yaw objectives also orient the fallback correctly.
- Complete-plan feasible candidates enter a separate fully validated incumbent channel. Initial seeds and improvements must pass support/exit, Y, optional-floor, passage, corridor, checkpoint readiness and finite-scene checks. Invalid optimizer proposals still drive repair. Errors, cancellation or later failed repair cannot erase an accepted witness. Acceptance results are cached within one symbolic branch.
- Complete movement proposals have their own bounded queue, ordered by elapsed ticks. Partial prefixes cannot evict them. The first completion is exposed immediately after its generating expansion; eight partial expansions are reserved between subsequent proposals. Partial discovery alternates shortest-first and elapsed-bucket round robin, with fair bucket retention. Ready candidates drain at the partial-expansion cap while other budgets remain. Each queue uses `maxQueuedPrefixes`; combined capacity is at most twice this setting. `movementSearch.expansionsPerProposal` controls the discovery reservation. `queue_policy='legacy'` exists for diagnostic reproduction.

These changes do not implement the event-ready time/progress frontier, prove infeasibility, or improve the horizontal score. Earlier facings stay free; all solves use the original full X/Z position and velocity. Current cell-route experiments keep velocity thresholds zero. Ladders and intentional wall hits remain outside this benchmark.

| Check | Previous result | Fixed implementation |
| --- | --- | --- |
| Fully valid warm seed/objective cases | 25/28 retained | 28/28 retained |
| Deliberately invalid perturbed starts | 6/8 solved | 6/8 solved |
| Successful warm objective regressions | Reference | None observed |
| Declared fallback, seven scalar probes | Three X controls and captured turn fail | All seven feasible |
| Dense grind, actual window search | Unknown, zero solver calls | Valid 11 ticks, three calls |
| Multiple supports, actual window search | 14 ticks | 14 ticks |
| Cold turn, actual window search | Unknown | Unknown |

The actual six window searches keep gap0.25 and the original budgets: open12, ceiling13, multi-support14, step14, grind11, turn unknown. All 47 returned partial/final witnesses independently replay. The 36 warm conditions produce 34 accepted/replayed witnesses. Ten generation controls expose completions; matched offline physical verification obtains grind11, multiple-support14 and extra-grind5, while cold turn and the additional repeated-ceiling pool remain unknown. Offline pool validation sorts a bounded generated pool; it is separate from the actual online window searches.

These are finite-corpus results with one new run per condition. Timings are not generalized speedup claims. Ceiling/step score-versus-time errors remain; the deferred frontier is still needed. The cold turn's valid saved prefix is now retained and replays, but cold discovery under current guards/budgets remains unresolved.

Reproduction uses the existing compiler/IPOPT environment with numerical threads pinned:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 .venv-guidance/bin/python \
  -m standalone.autonomy.cell_routes.fix_validation --part warm \
  --output standalone/autonomy/build/fix-warm.json.gz
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 .venv-guidance/bin/python \
  -m standalone.autonomy.cell_routes.fix_validation --part generation \
  --output standalone/autonomy/build/fix-generation.json.gz
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 .venv-guidance/bin/python \
  -m standalone.autonomy.cell_routes.fix_validation --part verify \
  --generation-input standalone/autonomy/build/fix-generation.json.gz \
  --output standalone/autonomy/build/fix-verify.json.gz
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 .venv-guidance/bin/python \
  -m standalone.autonomy.cell_routes.window_experiment \
  --input standalone/autonomy/experiments/decision-windows/fixtures --gaps 0.25 \
  --output standalone/autonomy/build/fix-windows.json.gz
```

The scalar reproduction command remains in the [root-cause README](../failure-rootcause/README.md). New low-level and outer-search regression tests cover fallback budgets without inference, preserved incumbents, invalid seeds, continued repair, cancellation/errors, complete-plan publication, queue-cap starvation and the captured known post-turn prefix. Standalone numerical and wider test results plus reproducible inputs/outputs are archived in `evidence/`.

Validation at implementation source `bc7659b8`: 59 focused compiler/IPOPT tests pass; all 54 IPOPT tests pass with numerical integration enabled; wider autonomy runs 384 tests with 83 environment skips and no failures, excluding the already documented baseline repeated-ladder-ceiling failure. No mod Gradle test suite was run.

All 98 returned physical witnesses in the warm, pool-validation and window reruns pass independent replay (34 + 17 + 47). Scalar-only probes are counted separately. In the inherited warm diagnostic schema, `lostValidSeed` flags an invalid final inner proposal after a valid seed; final `result.best` and replay describe whether the fixed outer search retained a valid incumbent. It is not a claim that the fixed search still loses that seed.
