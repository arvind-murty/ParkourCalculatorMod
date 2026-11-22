// Box helpers of spatial/cells.py, on boxes whose coordinates keep Python's
// int/float kinds.
#pragma once

#include <array>
#include <vector>

#include "pkc/program.hpp"

namespace pkc {

using NumBox = std::array<Num, 6>;  // minX, minY, minZ, maxX, maxY, maxZ
using Rect = std::array<Num, 4>;    // (u_lo, v_lo, u_hi, v_hi) on one plane

// subtract_open(box, obstacle, boundaries=...).
std::vector<NumBox> subtract_open(const NumBox& box, const NumBox& obstacle, bool boundaries = false);

}  // namespace pkc
