// Box helpers of spatial/cells.py, on boxes whose coordinates keep Python's
// int/float kinds.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

using NumBox = std::array<Num, 6>;  // minX, minY, minZ, maxX, maxY, maxZ
using Rect = std::array<Num, 4>;    // (u_lo, v_lo, u_hi, v_hi) on one plane

// subtract_open(box, obstacle, boundaries=...).
std::vector<NumBox> subtract_open(const NumBox& box, const NumBox& obstacle, bool boundaries = false);

std::optional<NumBox> intersection(const NumBox& a, const NumBox& b);
int dimension(const NumBox& box);

// decompose(scene, max_cells=..., deadline=..., refine_support=...) as
// {bounds, cells: [[bounds, dimension]], portals: [[cells, bounds, dimension]],
//  contacts: [[cell, solid, owner, axis, side, kind, bounds]], climbable: [[cell, [ladder...]]],
//  statistics (without elapsedSeconds), parents: [[cell, [parent...]]]}. `seconds` is the
// deadline's remaining budget; running past it throws Timeout with the Python's message.
Value decompose(const Value& scene, std::int64_t max_cells, std::optional<double> seconds, bool refine_support);

}  // namespace pkc
