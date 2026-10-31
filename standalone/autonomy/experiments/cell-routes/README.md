# Cell-route experiment

Implementation branch: `research/ipopt-cell-routes`, isolated worktree `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-cell-routes`, baseline `99ecfe9`. Bounds remain deferred. No production routing default changes. No cross-thread communication is part of this work.

The implementation follows [the experiment plan](../../CELL_ROUTE_EXPERIMENT_PLAN.md). All results below concern the continuous standalone model, not actual game replay.

## Milestone 1: geometry

Implemented deterministic expanded-AABB subtraction, cuboid merging, partial adjacency portals, physical face provenance, climbable memberships, exact boundary strata, segment coverage and horizontal SVG slices. Boundary strata preserve free configurations with no positive-volume neighborhood, including a floor and ceiling exactly one body height apart. Geometry limits/deadlines fail explicitly instead of returning an incomplete partition.

Validation: six focused geometry tests pass. They cover randomized obstacle-union agreement, disjoint volume interiors, exact contact boundaries, unequal neighbor interfaces, split/reordered/overlapping/nested boxes, and swept segment coverage. Java helper built inside this worktree using `:core:prepareRunTicksCompiler`; no numerical solves or mod test suite in this milestone.

## Milestone 2: spatial routes

Implemented bounded geometric best-first walks, finite revisit/depth/expansion/route limits, duplicate grouping and prescribed-spatial-route extraction. Search uses geometric length plus an ordering penalty for reversals/revisits, never a time bound. Geometric prefix representatives and finite limits make this a heuristic shortlist, not complete enumeration. Completed goal walks stop expanding; earlier returns remain permitted. All omissions/limits are reported.

The first broad-family representation projects a 3D route's cells onto X/Z and groups paths by ordered obstacle-midplane crossings (left/right/through plus crossing direction). The crossing axis is the dominant start-to-goal horizontal axis. Adjacent source AABBs that form one cuboid are merged before landmark extraction. This is deliberately an initial corridor signature, not a general topological classification. It can miss distinctions for U-shaped/branching routes whose meaningful choices do not cross these planes. Broad projection allows different Y schedules and support interactions without freezing a particular flight. Large projected cells can weaken filtering; benchmark this instead of assuming reduction. Signature checks prevent a grouped/projection corridor from accepting the opposite side silently.

Ordinary Y/X/Z phase tracing accounts for several crossings in one tick, while repeated states need no extra graph hop. Corridor acceptance uses the horizontal projection and ordered crossings, with physical Y/step acceptance delegated to the existing shared scene checker. This does not claim a reconstructed exact 3D step-assist phase trace.

Validation: 11 geometry/route tests pass, including all three obstacle passage sides, finite limits, invalid prescribed paths, crossing order and multiple crossings per tick. The scene coverage bounds are eroded by the player body dimensions, matching the requirement to keep the whole body inside the supplied scene.
