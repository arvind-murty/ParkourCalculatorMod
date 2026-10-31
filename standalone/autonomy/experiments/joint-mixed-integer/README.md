# Joint schedule and angle evidence

2026-10-31. Source implementation: `2fdcf75` on `spike/ipopt-mixed-integer`. [Formulation, scope and reproduction](../../mip_spike/JOINT_SCHEDULE.md). Production planner code is unchanged.

The supplied route is `start -> middle -> goal`, with two ordinary 12-input jumps and zero through two additional run inputs. The finite wall lies between the middle and goal supports. The initial state is at rest. No run allocation, departure times, passage, gate times or saved facing seed is supplied.

The joint model selects allocation `[2, 0]`, jump inputs 2 and 14, and a 26-input complete trajectory. It goes around the negative-Z side of the wall using the face chain `X- -> Z- -> X+`, a wide neo. The gate position ticks are 16 and 18. The first transition requires same-state outside conditions; the second uses consecutive states according to collision order.

| Horizon | Joint SCIP result | Accepted |
| --- | --- | --- |
| 24 | infeasible | no |
| 25 | infeasible | no |
| 26 | time limit with incumbent | yes |

The accepted 26-input solve continued positional objective optimization for its eight-second horizon budget. Its final X objective is approximately 7.855623. The raw joint solution passes original scalar and full shared scene acceptance before the IPOPT API is called. Position and horizontal velocity differ from scalar simulation by less than 9e-14; Y carry agrees exactly. The IPOPT API accepts that already-valid seed without numerical repair.

The one-shot run took about 8.44 seconds including subprocess startup and acceptance. This is an inspectable correctness example, not a controlled performance comparison. It does not claim a globally optimal objective or a physical minimum-duration certificate. Numerical guards, fixed-route assumptions and supported-movement/passage limits apply.

For a separate small enumeration control, all six allocations within the same run budget were individually supplied to the same dynamics/geometry formulation, with a six-second per-allocation budget:

| Allocation | Result | Accepted |
| --- | --- | --- |
| `[0, 0]` | infeasible | no |
| `[0, 1]` | infeasible | no |
| `[1, 0]` | infeasible | no |
| `[0, 2]` | infeasible | no |
| `[1, 1]` | time limit, no incumbent | no; unknown |
| `[2, 0]` | first feasible solution | yes |

The joint model did not enumerate those six allocations. It used four local departure selectors at horizon 26, linked by run bounds, while choosing angles and passage gates in the same model. Enumeration exists only as a reference experiment. The unknown `[1, 1]` result does not establish its infeasibility or uniqueness of the selected allocation.

Artifacts:

- [request.json](request.json): original finite geometry and supplied support route.
- [case.json](case.json): normalized geometry, shared footprints, local coefficients and derived per-jump vertical phases; no selected global schedule.
- [joint-result.json](joint-result.json): all horizon attempts, raw mixed-integer trajectory, selected gates, materialized problem/plan, original scalar trajectory and scene acceptance.
- [enumerated-allocations.json](enumerated-allocations.json): full per-allocation reference outcomes.
- [summary.json](summary.json): compact results, provenance, checksums and explicit non-certification flags.

The focused package gate passes 33 tests, including the earlier fixed-schedule spike. New coverage includes all allocations in a small domain with independently compiled dynamics, a forced run-allocation choice, different heights, repeated supports, nonzero initial state, departure after the last grounded movement leaves the edge, unintended floor/ceiling contact rejection, safe reach bounds, 729 one-input clearance comparisons, per-physical-block caps and interleaved obstacle transitions. The saved joint trajectory is freshly recompiled and independently revalidated; changing its run allocation invalidates it.

Next work is broader scaling measurement and then conditional wall-hit/input-state transitions. Ceiling, stepping, ladder and run-off scheduling remain outside this implemented subset. The production orchestration/replay path has not been switched to this backend.
