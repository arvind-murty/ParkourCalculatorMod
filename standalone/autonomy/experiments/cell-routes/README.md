# Cell-route experiment

Implementation branch: `research/ipopt-cell-routes`, isolated worktree `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-cell-routes`, baseline `99ecfe9`. Bounds remain deferred. No production routing default changes. No cross-thread communication is part of this work.

The implementation follows [the experiment plan](../../CELL_ROUTE_EXPERIMENT_PLAN.md). All results below concern the continuous standalone model, not actual game replay.

## Milestone 1: geometry

Implemented deterministic expanded-AABB subtraction, cuboid merging, partial adjacency portals, physical face provenance, climbable memberships, exact boundary strata, segment coverage and horizontal SVG slices. Boundary strata preserve free configurations with no positive-volume neighborhood, including a floor and ceiling exactly one body height apart. Geometry limits/deadlines fail explicitly instead of returning an incomplete partition.

Validation: six focused geometry tests pass. They cover randomized obstacle-union agreement, disjoint volume interiors, exact contact boundaries, unequal neighbor interfaces, split/reordered/overlapping/nested boxes, and swept segment coverage. Java helper built inside this worktree using `:core:prepareRunTicksCompiler`; no numerical solves or mod test suite in this milestone.
