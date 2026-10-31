# Joint run-tick, passage and angle model

Implemented on `spike/ipopt-mixed-integer`, extending the fixed-schedule spike. The production planner is unchanged. The joint backend receives a finite scene, a supplied sequence of supports and run-count bounds. It jointly chooses departure times, run allocations, obstacle passages, blockage gate times, inertia regimes and continuous angles. It does not generate complete run allocations or passage-chain/timing combinations before solving.

## Supported simulation

The subset is ordinary sprint jumps, sprint-45 run/air inputs, normal block friction, free facing and finite-box avoidance. Routes can revisit a support and can change height. The initial state must be grounded at the first support height with normal resting Y carry; original X/Z position and velocity are retained exactly. Maximum modeled duration is 200 inputs.

Wall hits, ceiling hits, stepping, ladders, sneaking, run-offs, water, status effects and facing constraints are not choices in this model. Every non-prescribed collision must be avoided, including unintended early landings. Supplied scene geometry may include overhead or lower boxes that must be cleared. No geometry is removed merely because a feature is unsupported. The shared scene checker rejects any unmodeled contact or step.

For this subset, relative Y/VY and ground histories for each ordinary jump are derived using the existing Y simulation. A departure-time decision selects when its air phases occur. Running retains the grounded height and resting carry. Thus Y and ground are functions of the selected phases inside the model; no completed global Y schedule is supplied. A future ceiling/contact extension must add its conditional vertical transitions rather than reuse these uninterrupted-jump profiles.

The shared Java `FixedMovementCompiler` supplies local run/jump/air magnitude, friction and angle-offset definitions. A seven-input probe checks repeated instances of those modes. With held sprint, no sneak and no horizontal contacts, sprint is uninterrupted after the grounded initial input; air inputs therefore use the sprint air factor. Every selected schedule is freshly recompiled and every mode coefficient compared before acceptance. Jump inputs use W; other inputs use WA, as in the existing force-45 policy.

## Timing and trajectory variables

For each leg, a one-hot departure selector ranges over possible absolute input ticks. Differences between departures and preceding arrivals are the run counts, constrained by the supplied per-leg and total bounds. The final arrival equals the tested horizon. Running and jump/air phase indicators are derived from these departure selectors. Complete allocation products are never formed in model construction.

Every input has a unit control circle and a selected-mode acceleration vector. Linear indicator equations select its magnitude and the velocity recurrence's friction. Shared original-state X/Z position and velocity variables span the entire route. The inertia dead zone uses the same guarded three-branch encoding as the fixed-schedule spike. No intermediate endpoint is frozen.

Running requires support at the correct pre-input state. Landing requires the target footprint on the final air position tick, before the following downward Y clipping. The last grounded movement can leave the support before the subsequent jump input; an extra support condition is deliberately not imposed on that jump's current X/Z. This preserves the existing normal movement behavior. An additional run input instead requires support and cannot continue on a falsely assumed floor.

## Obstacle and blockage variables

Each relevant collision box has up to four selected face slots and up to three optional gate slots. Gate activation is a prefix. Each active gate chooses a directed pair of faces on different axes and two position ticks. They coincide for same-tick blockages, or differ by one for consecutive-tick blockages, using `blockages.consecutive` and Y-X-Z order.

Prefix sums of gate selectors determine which face slots apply at each position state. Conditional half-space constraints enforce the selected face throughout its interval. These intervals use the same semantics as `blockages.intervals`; constraints are not restricted to the gate points alone. Gate ordering permits multiple gates on one position tick where the existing rules allow it.

Each box has independent gates against the shared trajectory. Events for different boxes can interleave. Horizontal, vertical and total transition counts aggregate by physical block provenance, preserving caps of two horizontal, two vertical and three combined. A cross neo is one horizontal transition; a wide neo is two. The represented family is capped and is not a physical completeness claim for arbitrary routes.

Outside-face intervals also enforce swept collision safety: retaining a face clears the movement, while a transition that lacks same-state overlap is permitted only in the collision-order direction that can use the pre-state face followed by the post-state face. Tests compare this encoding to the independent stagewise clearance formulation on 729 single-input endpoint pairs, including varying Y.

Horizontal avoidance uses 2e-7 clearance, preserving exact initial tangency. Y tangency retains its exact plane. Selected X/Z interval rows accompany the reconstructed schedule into the existing IPOPT API. Y intervals remain satisfied because refinement keeps the selected timing/vertical phases and revalidates the complete scene.

## Bounds and solve policy

A dynamic program propagates conservative scalar speed and distance bounds through local departure possibilities. It stores maxima at timing states, not a list of completed histories. The bound follows `movementNorm <= speedNorm + magnitude` and the selected friction; inertia removal can only reduce the incoming norm. The model uses these bounds for variables, conditional phase constraints and redundant norm inequalities.

A collision box is omitted only if one separating face clears it throughout every state bound. Faces impossible at all states are disabled. A box with no possible vertical separating face needs only the horizontal transition budget. These are bound-based formulation reductions; a currently solved trajectory is not used to exclude alternate passages or timings.

The runner searches increasing horizons. At each horizon, positional objective optimization is enabled by default, bounded by `--horizon-seconds`. `--feasibility-only` stops SCIP at its first integer solution. A timeout remains unknown, even when shorter horizons were attempted. Output does not claim physical infeasibility, minimum-tick certification or a globally optimal positional objective. Numerical guards and the explicit movement/passage scope apply to all results.

## Acceptance and tests

The selected allocation is materialized using the existing scheduler, vertically simulated and freshly compiled by the shared Java helper. Recovered facings are replayed through the original scalar model. Position, X/Z velocity, VY and ground state must agree with the joint model. The raw candidate is independently scene-checked. The original-state IPOPT API can refine the full trajectory with the selected interval constraints; the final candidate must pass scalar and full scene acceptance again. All evidence records whether the raw integer trajectory already passed.

Focused tests cover every allocation in a small run-count domain, a joint solve where the one available run tick must precede the first jump, random-trajectory bound checks, different heights, repeated supports, normal departure after leaving the edge, prevention of extra unsupported run inputs, unintended floor/ceiling collisions, X/Z collision order, thin-box swept clearance, blockage caps across collision sub-boxes and interleaved obstacles. Small fixed-allocation checks are reference tests only, not the joint search algorithm.

## Files and reproduction

- `joint_contract.py`: supported request validation, shared local coefficients and materialization of selected schedules.
- `joint_bounds.py`: conservative speed/distance propagation over local timing states.
- `joint_model.py`: departure, phase, trajectory and control variables; CLI for one horizon.
- `joint_passages.py`: optional face/gate variables, interval coverage, per-block caps and extraction.
- `joint_run.py`: increasing-horizon subprocess runner, original-model acceptance and evidence.
- `joint_tests.py`: formulation, dynamics, geometry and shared-acceptance tests.
- `algebra.py`: inertia encoding shared with the original fixed-schedule spike.
- `fixtures/two-jump-wide-neo.json`: coupled two-jump finite scene; no saved angles or authored run allocation.

Use the optional dependency and helper setup in [README.md](README.md), then:

```sh
PYTHONPATH="$PWD/standalone/autonomy/build/mip-site" \
PKC_RUN_TICKS_COMPILER="$PWD/standalone/autonomy/build/compiler.json" \
.venv-ipopt/bin/python -m unittest \
  standalone.autonomy.mip_spike.joint_tests standalone.autonomy.mip_spike.tests -v

PYTHONPATH="$PWD/standalone/autonomy/build/mip-site" \
.venv-ipopt/bin/python -m standalone.autonomy.mip_spike.joint_run \
  --input standalone/autonomy/mip_spike/fixtures/two-jump-wide-neo.json \
  --output standalone/autonomy/build/joint-results \
  --seconds 30 --horizon-seconds 8
```

## Remaining work

This is an isolated prototype, not a production backend integration. Measure longer routes and more obstacle arrangements before judging scaling. Add conditional contact/input-state transitions for wall hits before claiming joint contact scheduling. Later add vertical alternatives, stepping and ladder phase rules. Those extensions must share state transitions and keep all intermediate X/Z state free. The fixed-route restriction, movement scope, guarded boundaries and per-block passage caps must remain explicit. No communication or edits to the other research agent's branch are needed.
