# Generic mixed-integer local-solve spike

2026-10-31. Implemented in the isolated `spike/ipopt-mixed-integer` branch, forked from `feature/ipopt-consolidated` at `99ecfe9`. [Code and reproduction commands](../../mip_spike/README.md). The other agent retains spatial-route/cell-decomposition research. No production search, physics, compiler or replay source was changed.

## Outcome

Generic mixed-integer optimization is a viable local candidate generator for the tested scope. Both a unit-circle MIQCP and a unit-disk relaxation followed by the existing IPOPT API produced scalar/shared-scene-accepted witnesses for all eight fixed-schedule cases in all three repeats. The circle model's raw reconstructed facings also passed scene acceptance in these guarded runs.

This supports further local-backend work, not replacing the entire planner. Existing production settings are much faster on some cases and achieve better positional objectives. Route order, jump/contact timing and Y schedules are still supplied. These timings do not measure the schedule-generation bottleneck or broad spatial-route discovery.

## What the backend chooses

The fresh shared Java compiler supplies each schedule's magnitude, friction, base angle and input/sprint history. SCIP chooses a normalized 2D control per tick plus binary inertia, ladder clamp and finite-box clearance alternatives. The circle variant enforces `qx^2+qz^2=1`; the disk variant enforces `<=1` and therefore permits unrealizable reduced acceleration in its relaxation. The returned facings are always replayed through the original scalar model, and the disk point is never accepted merely because its relaxed constraints hold.

Finite-box clearance is represented at Y, then X with pre-Z, then Z with post-X. Swept horizontal segments must clear a common face. This prevents an endpoint-only diagonal shortcut through an obstacle. Selected face rows accompany the facing seed into whole-history IPOPT refinement; neither intermediate positions nor velocities are fixed. The final shared scene check includes the supplied contact history, ladder occupancy, Y, ground and velocity assumptions.

The model can combine many local clearance alternatives without listing every complete passage schedule first. For example, the cross-neo case has 36 clearance disjunctions and 210 explicit binary variables including inertia; the interleaved case has 49 disjunctions and 262 binaries. The repeated-support case has no obstacle-clearance disjunctions, but still has 138 inertia binaries and 24 nonconvex control circles.

## Measurement protocol

- Eight scene-backed local contracts: cross neo, wide neo, interleaved obstacles, required wall reset, wall plus ceiling, two fixed ladder-ceiling endpoint variants, and two jumps with an intermediate landing on the original support.
- Three repeats for the existing feasibility-first branch/solve wrapper and each mixed-integer method. Method order is rotated. One additional run per case uses the production caller's `rank_equal_cost=True, optimize_goal=True` settings. There are 80 measured method/case runs, with 48 mixed-integer acceptances.
- Per-method budget is 12 seconds. Model construction, backend process startup, numerical refinement and scene acceptance are included. Scene/schedule preparation and coefficient compilation are shared per case and recorded separately. First-case preparation was about .15 seconds; subsequent preparation was typically .003-.013 seconds.
- The comparison fixes the route, horizon, vertical history and required contacts. It uses no saved witness yaw. Wall/ladder examples import their contract from saved evidence but recompile the movement coefficients.
- The existing feasibility-first wrapper uses current production primitives with objective polish disabled. The production-policy run also records the first accepted scene-check time, so time spent improving an incumbent is distinguishable from time to a witness.
- Candidate representations and caps differ. The existing path uses bounded directed passage chains; SCIP uses per-stage separating-face choices and does not enforce the same per-block chain-count cap. Its optional retries exclude face assignments heuristically after unsuccessful refinement. Unknown is never labeled physically infeasible.

## Results

Seconds below exclude shared preparation. Existing-feasibility, circle and disk columns show median total times across three runs. All of their successes/failures repeat consistently. Production first/total times come from one run; `unknown (s)` gives elapsed time without an accepted result.

| Case | Ticks | Existing feasibility | Production first / total | Circle + IPOPT | Disk + IPOPT |
| --- | ---: | --- | --- | ---: | ---: |
| Cross neo | 12 | .154 | .284 / 2.162 | .790 | 1.265 |
| Wide neo | 12 | unknown (3.362) | unknown (3.945) | 2.010 | .906 |
| Interleaved obstacles | 12 | unknown (12.073) | unknown (12.035) | .679 | .364 |
| Required wall reset | 12 | .004 | .155 / .155 | .466 | .126 |
| Wall plus ceiling | 3 | .089 | .142 / .143 | .103 | .097 |
| Ladder, continuous end | 12 | .440 | unknown (.526) | .151 | .152 |
| Ladder, omitted final hit | 10 | .435 | unknown (.466) | .540 | .218 |
| Repeated support, two jumps | 24 | unknown (.448) | .317 / .318 | 7.784 | 2.920 |

Acceptance counts: existing feasibility 15/24, production-policy 4/8, circle 24/24, disk 24/24. The production-policy failures are local outcomes for these supplied seeds/contracts/caps, not general solver capability or infeasibility conclusions. In supplementary reruns, both ladder production-policy attempts were scalar-feasible but introduced an undeclared wall collision at tick 11 or 9, respectively, so the shared scene checker rejected them. The inner attempt diagnostics are saved in [baseline-policy-diagnostics.json](baseline-policy-diagnostics.json).

The repeated-support case is an important control: calling the first baseline merely “the existing solver” would incorrectly suggest a mixed-integer advantage. Production settings recover it in .32 seconds, substantially faster than either mixed-integer method. The harder circle solve also shows why extending a monolithic model to much longer histories could become expensive even without obstacles.

Objective quality is another qualification. On the required-wall case, production reaches about 3.03667, whereas the first circle and disk witnesses reach about 1.48326 and 2.47688. The mixed methods stop at feasibility and do not replace equal-time objective optimization. Their most promising role is supplying useful discrete choices and feasible seeds for subsequent optimization, especially where the current local seed/branch policy returns unknown.

## Precision findings and controls

An initial zero-clearance prototype frequently chose exact avoidance boundaries. Differences at floating-point precision then triggered real clipping and velocity resets in the independent scene checker. Those candidates were rejected, not accepted with a looser tolerance. The guarded model uses 2e-7 clearance on unselected faces, preserving exact declared contact planes and fixed initial tangency. Inertia uses the existing worker's 1e-8 branch guard. These guards make this a numerical search formulation, not a proof-complete model at every boundary.

The 14-test focused gate passes with the optional SCIP dependency and Java bridge enabled. It includes independent revalidation of all 16 saved mixed-method witnesses, checking their original/selected constraints, exact reconstructed position/velocity/Y/ground arrays, original initial state and shared finite scene. Negative controls reject missing wall/ceiling contacts and incorrect ladder membership. Unit controls cover X-before-Z asymmetry, swept versus endpoint clearance, clamp and inertia branches, nonzero world origin, discrete alternatives and unsupported facing constraints. A disk-only stationary solution is deliberately rejected by the original fixed-input scalar model.

No mod Java test suite or game replay was run. The spike adds only experimental files and leaves production code untouched.

## Artifacts and reproduction

- [summary.json](summary.json): all 80 measurements, aggregate times, platform/backend versions, method order and explicit scope qualifications.
- [manifest.json](manifest.json): the eight cases and sixteen accepted circle/disk witnesses.
- `*.case.json`: source provenance, freshly compiled numerical problem, geometry, complete plan, scalar Y history and clearance disjunctions.
- `*.witness.json`: original-state accepted trajectories, selected face rows, backend metrics, control norm range and IPOPT/shared-scene acceptance results. Each references its case by SHA-256.

Measured stack: Python 3.12 on macOS arm64, PySCIPOpt 6.3.0, SCIP 10.0.2, CasADi 3.8.1. SCIP uses one thread, seed shift 1 and a first-integer-solution limit. See the code README for isolated dependency installation and helper preparation.

```sh
PYTHONPATH="$PWD/standalone/autonomy/build/mip-site" \
PKC_RUN_TICKS_COMPILER="$PWD/standalone/autonomy/build/compiler.json" \
.venv-ipopt/bin/python -m unittest standalone.autonomy.mip_spike.tests -v

PYTHONPATH="$PWD/standalone/autonomy/build/mip-site" \
.venv-ipopt/bin/python -m standalone.autonomy.mip_spike.run \
  --seconds 12 --output standalone/autonomy/build/mip-reproduction
```

The saved three-repeat experiment used method orders `existing circle disk`, then `circle disk existing`, then `disk existing circle production`, in separate output directories. The final command above includes all four methods once. It reproduces the problem/acceptance policy, not a guarantee of identical wall-clock timings or search trajectories.

## Next useful step

Evaluate mixed-integer seeds followed by normal objective polishing, and local choice/timing variables inside one shortlisted spatial itinerary. This should use shared finite input-state/coefficient semantics rather than precompiling a huge list of complete schedules. Keep the fixed-schedule successes as regression contracts. A future formulation must explicitly add jump/landing/contact timing, mode-dependent sprint history, alternative Y outcomes and movement transitions before it can replace schedule generation. Longer-horizon scaling, objective quality and compatibility with the proposed cell-route abstraction remain open.
