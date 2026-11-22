// Planar covers and geometry keys of spatial/regions.py.
#pragma once

#include <string>
#include <vector>

#include "pkc/spatial/cells.hpp"
#include "pkc/value.hpp"

namespace pkc {

// geometry_key(value): SHA-256 of the compact sorted-key JSON.
std::string geometry_key(const Value& value);

// canonical_cover(boxes): the canonical closed X/Z union of the boxes that are not
// thin, on one horizontal plane.
std::vector<NumBox> canonical_cover(const std::vector<NumBox>& boxes);

// support_components(decomposition) on decompose()'s rows, as
// [[key, owner, height, cover, cells, solids, patches], ...].
Value support_components(const Value& decomposition);

// build_geometry_graph(scene, decomposition, supports, task_goal) on decompose() and
// support_components() rows, as {regions: [[key, kind, cover, cells, owners, solids,
// transit_key]], transitions: [[key, source, target, kind, cover, portals, patches,
// movement_families]], starts, goals, context}. A null task_goal is None.
Value build_geometry_graph(const Value& scene, const Value& decomposition, const Value& supports, const Value& task_goal);

}  // namespace pkc
