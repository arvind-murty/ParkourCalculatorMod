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

}  // namespace pkc
