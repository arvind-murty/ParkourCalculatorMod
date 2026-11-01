# Joint decision windows and objective bands

This implements the user's 2026-11-01 policy in the isolated `research/ipopt-cell-routes` worktree. Nearby choices are enumerated jointly, comparisons extend to the next separate opportunity's earliest input, and an objective-gap band selects similarly scoring histories. The score remains horizontal position at one shared absolute state. Velocity and earlier states are recorded for diagnosis only.

## Opportunity and cluster model

`cell_routes.decision_windows.plan_windows` consumes explicit opportunity IDs, inclusive absolute input ranges, optional kinds (`jump`, `stepping`, `ceiling`, `landing`, `passage`), optional `dependsOn` IDs, and optional `readyState`/outgoing `segment`/comparison `region` metadata. It merges overlapping/nearby ranges, dependencies and intervening opportunities, and a following opportunity whose onset is too early for the current cluster's ready state. The final cluster continues to the real goal.

For each earlier cluster, the checkpoint is the earliest input of the next cluster. If that is input q, compare p[q], the state after input q-1 and before input q. Every sibling has the same tick, horizontal objective and route stage. The existing physical program generator recursively combines compatible methods through that horizon, with actual scalar replay, finite contacts and shared collision acceptance. It does not optimize each decision independently before combining them.

The opportunity timing ranges are supplied experimental domains, not discovered from geometry or certified reachable-time bounds. `DecisionPolicy` checks selected jumps, stepping/grind cases, ceiling contacts and optional air landings against those domains. Ordinary same-height ground maintenance remains available, and reaching the actual goal is allowed to finish early. Passage opportunities contribute clustering/stage metadata; lazy horizontal blockage timing remains handled by the existing corridor/physical repair solver and is not comprehensively audited against opportunity time ranges. Automatic detection of all unresolved interactions entering a nominally clear interval remains unfinished. These limits are explicit in the API/report.

All numerical solves retain the original full initial position/velocity and reoptimize every earlier facing. Retention stores symbolic decisions/constraints and a yaw seed, not a numerical intermediate state. Normal velocity carry, acceleration, friction and contact resets remain. Horizontal and vertical velocity thresholds are zero. Y is absent from the score but remains in real physical feasibility.

## Retention and work

Use `objective_gap=epsilon` in `find_checkpoint_route` with `opportunities=...`. Keep valid candidates with `bestScore-score <= epsilon`; the normalized direction makes epsilon a gap in blocks. No default tolerance is claimed optimal. A hard `beam_width` cap separately limits work. The report distinguishes outside-band candidates, eligible candidates lost to the hard cap, and numerical unknowns lost to their reserve cap. In band mode, `unknown_slots` is a maximum reserve; unknowns do not fill every unused scored slot.

`fair_parents=True` splits the remaining prefix expansion budget into explicit per-parent quotas and round-robins yielded candidates, with provenance and generation work per parent. Per-candidate numerical caps remain the same for every parent. This prevents one productive parent from monopolizing all first proposals. It does not dynamically recycle unused prefix quotas, guarantee monotone quality with wider pools, or override a global deadline/call limit. The legacy explicit-checkpoint/fixed-width API remains available.

Each accepted prefix records its last clear selected-event states: horizontal progress, forward/lateral stored velocity, Y, VY and ground state. This is diagnostic data, not a new score. Absence of a selected event is not a certificate that every possible unresolved geometric decision has been detected.

## Experiments

Six fixtures reuse the earlier open, ceiling, stepping, grind, multiple-support and blockage-turn geometry. The input domains, caps and planned boundaries are stored with every run. These are supplied-route tests; comparison with the older checkpoint report is descriptive because horizons, opportunity domains, pool caps and some work settings differ.

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 .venv-guidance/bin/python \
  -m standalone.autonomy.cell_routes.window_experiment \
  --input standalone/autonomy/experiments/decision-windows/fixtures \
  --output standalone/autonomy/build/decision-windows.json.gz \
  --repeats 3 --gaps fixed 0.25 0.5
```

`fixed` and numeric bands use the same new windows, opportunity domains, hard pool cap and budgets. Their difference is retention. All saved accepted prefix and final witnesses are independently replayed.

The diagnostic separately uses known accepted early/late jump, normal-step/slab-boost and grind-chain histories from the prior source `5e36f490` evidence. It truncates them at several shared horizons, reoptimizes their prefixes from the original state, and independently searches their continuations. It also reoptimizes each known full symbolic schedule with the newly retained prefix guards, providing a compatible feasible completion control when accepted. An unconditioned historical completion is not silently counted as feasible for a new retained branch. This is warm controlled ranking evidence, not cold discovery or a global optimum.

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 .venv-guidance/bin/python \
  -m standalone.autonomy.cell_routes.window_diagnostic \
  --output standalone/autonomy/build/decision-window-diagnostic.json.gz --repeats 3
```

Use the common available clear ticks to inspect progress/velocity differences when a useful history loses. First measure which histories an objective band omits. A multi-tick/velocity score is explicitly deferred until those failures identify the missing information. Lower bounds, mixed initializers, ladders, intentional wall hits and actual-world threshold repair remain outside this experiment.

Validation at the implementation milestone: 37 compiler/IPOPT-enabled focused tests pass, including the prior cell, stepping and composition integration controls. The broader autonomy suite completes 370 tests with 82 environment skips and no failures after excluding the previously reproduced baseline ladder failure. IPOPT completes 51 tests with eight skips and no failures.
