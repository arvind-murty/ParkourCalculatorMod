# Joint solver scaling campaign

Protocol fixed before the measured run: six synthetic finite-box extensions of existing fixtures, two repeats, alternating method order. Each method has a nominal 20-second budget including its search orchestration and final acceptance, with a small validation reserve. Each joint horizon receives at most six seconds. Timings include numerical process startup and model construction. Shared scene preparation/coefficient/footprint extraction is outside that per-method budget and recorded separately. Any actual budget overrun is retained in elapsed time.

Cases: flat routes of three and five jumps; a three-jump route beginning with the required-run fixture; a three-jump height-change route; the cross-neo fixture followed by a second finite wall and two additional landings; and the joint wide-neo fixture extended to five jumps. Plane-only fixture rectangles are explicitly extruded into unit-depth boxes. These are synthetic fixtures, not captures of real-world surroundings. Linkcraft fixtures include unsupported run-off/reduced-airtime cases and are not silently reinterpreted as ordinary jumps.

Both methods receive the same supplied support route, geometry, initial state, normal sprint-jump/sprint-45 policy, shared footprints and coefficients, run bounds, positional objective and per-physical-block passage caps. No recorded facings or run allocation is supplied. Original-state X/Z remains free. Each method generates its own seeds and branch choices. Production baseline parameters: two-second candidate budget, one retry, 64 contact branches, 128 retained passage candidates, no optional wall hits, and the existing equal-cost/objective optimization settings. Full velocity/ground scene acceptance is enabled for the baseline; its final witness is recompiled and checked through the same acceptance path as the joint witness.

The existing method uses prefix allocation search and bounded per-leg passage generation. The joint method uses increasing horizons, departure selectors and globally timed face slots. Their representations, truncation and unknown behavior differ despite shared scene/physics/caps. This experiment compares these implemented search policies; it does not establish equivalence of their search domains or a generic mixed-integer speedup.

The joint model captures up to 32 incumbent trajectories and their SCIP availability timestamps per horizon, plus root LP time, node/LP counts, primal/dual bounds and gap. Incumbents are independently validated after horizon optimization. Its first-valid-availability metric combines the first subsequently validated candidate timestamp, measured validation cost and estimated process overhead. Actual first validation completion time is reported separately. The baseline's first-valid time is measured at the actual full-scene check. These timing definitions are explicit; both total time and candidate quality are needed when comparing them.

Run sequentially, without parallel solver jobs:

```sh
PYTHONPATH="$PWD/standalone/autonomy/build/mip-site" \
.venv-ipopt/bin/python -m standalone.autonomy.mip_spike.scaling \
  --output standalone/autonomy/build/scaling-campaign \
  --seconds 20 --horizon-seconds 6 --repeats 2
```

Results retain every attempted case/method, including errors and unknowns. Compare accepted duration first, positional objective second; report time to first valid candidate, actual first validation, total time, domain size and search work. Timeouts are not infeasibility conclusions. After measurement, preserve the inputs, compact statistics and accepted original-model/scene witnesses under `experiments/` and document findings before proposing further formulation changes.
