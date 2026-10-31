# Mixed-integer local-solve spike

Experimental package on `spike/ipopt-mixed-integer`, forked from the consolidated standalone solver at `99ecfe9`. Production route search is unchanged. The research agent owns the broad spatial-route/cell design; this spike evaluates a possible local backend.

The subsequent [joint schedule prototype](JOINT_SCHEDULE.md) puts run-tick allocation, departure timing, blockage selection/gate timing and angles in one model. Its complete supported simulation subset is ordinary sprint jumps plus run ticks and avoidance on a supplied support route. The fixed-schedule experiment below remains a separate historical baseline.

## Scope

The route, tick horizon, vertical schedule, contact history and therefore input/sprint coefficient history are supplied. A fresh shared Java compilation supplies the coefficients for each case. No saved witness yaw is used as the initial guess.

SCIP jointly selects finite-box clearance faces at each Y/X/Z collision stage, automatic inertia regimes and ladder clamp regimes. It optimizes normalized 2D control vectors with either a unit-circle equality or a unit-disk inequality. The latter is a relaxation, not a legal variable-strength movement input.

Selected face rows and a recovered facing seed are passed to the existing full-original-state IPOPT API. Independent scalar replay and the shared finite-scene checker decide acceptance. Intermediate positions and momentum are never frozen. A raw mixed-integer point or successful solver status is insufficient. The refinement API can accept an already valid seed without launching a worker, just like the existing baseline.

Geometry constraints cover resolved Y sweeps, X movement with pre-Z, then Z movement with post-X. X/Z swept segments must remain outside a finite box on a common separating face; endpoint-only collision tests are insufficient. Unselected-clearance rows use a 2e-7 guard. Declared contact planes and fixed initial tangency retain their exact boundary. Inertia regimes use the existing numerical worker's 1e-8 branch guard. These are numerical search guards, not a complete encoding for global infeasibility certification.

This is not automatic route, jump-time, wall-hit-time or ladder-schedule discovery. The fixed-contact wall/ladder examples validate those dynamics while the backend chooses the remaining clearance/inertia/clamp alternatives. It does not yet encode step branch mechanics or Sneak edge protection. Facing constraints fail explicitly. A full-world mode/timing formulation remains future work.

## Files

- `geometry.py`: stage-aware finite-box disjunctions and selected-row materialization.
- `backend.py`: isolated PySCIPOpt model, indicators, quadratic control sets and result extraction.
- `cases.py`: scene-backed fixed-schedule cases, fresh coefficient compilation and provenance.
- `run.py`: existing branch/solve baseline, circle+IPOPT and disk+IPOPT comparison with common acceptance.
- `tests.py`: model equivalence, inertia/caps/contacts, collision-order and relaxation rejection controls.
- `requirements.txt`: optional spike dependency only. Existing solver requirements are unchanged.

## Reproduce

Prepare the worker runtime using `standalone/ipopt/requirements.txt`, then install the optional dependency in that environment or in a dedicated site directory. The latter does not modify the reused worker environment:

```sh
.venv-ipopt/bin/python -m pip install --only-binary=:all: --no-deps \
  --target standalone/autonomy/build/mip-site \
  -r standalone/autonomy/mip_spike/requirements.txt

./gradlew --configure-on-demand \
  -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home \
  -I standalone/autonomy/compiler.gradle :core:prepareRunTicksCompiler

PYTHONPATH="$PWD/standalone/autonomy/build/mip-site" \
.venv-ipopt/bin/python -m unittest standalone.autonomy.mip_spike.tests -v

PYTHONPATH="$PWD/standalone/autonomy/build/mip-site" \
.venv-ipopt/bin/python -m standalone.autonomy.mip_spike.run \
  --seconds 12 --output standalone/autonomy/build/mip-results
```

The runner emits per-case inputs/results and a compact summary. Model construction and backend process startup are included in each mixed method's time. Shared scene/schedule preparation is measured separately. `existing` uses the current branch/solve machinery in feasibility-first mode. `production` enables the generic search caller's objective/equal-cost settings and separately records the first accepted scene-check time and the complete optimization time. The mixed methods are feasibility-first at fixed durations; their objective values are not certified equal-time optima and their total times must not be compared directly with a production method's full objective-improvement budget. SCIP stops at its first integer solution and IPOPT can refine the recovered controls.

The existing bounded passage-chain baseline and stagewise mixed-integer clearance model have different candidate representations and work caps. They share scene, movement schedule, initial state and final acceptance, but this is not a controlled production-pruning speedup claim. A failed refinement can exclude a selected face assignment only as a reported heuristic shortlist decision. Unknowns are never turned into physical infeasibility or global optimality certificates.

The [experiment report](../experiments/mixed-integer-spike/README.md) records 80 measured method/case runs, 16 independently replayable witnesses and the passing 14-test formulation/evidence gate. Both mixed variants accepted all eight fixed-schedule cases in three repeats. They are not uniformly faster or better on objective than production settings. No mod Java test gate or game replay is needed for this spike.
