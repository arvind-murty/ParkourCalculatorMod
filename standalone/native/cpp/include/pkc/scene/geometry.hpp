// Scene geometry: scene/geometry.py.
#pragma once

#include <array>
#include <utility>

#include "pkc/value.hpp"

namespace pkc {

// normalize_geometry(scene): the scene with derived collision supports and a
// validated collisionScene. Errors throw std::invalid_argument with the Python's
// message.
Value normalize_geometry(const Value& scene);

// support_center_interval(lo, hi): first and last player centre strictly
// overlapping [lo, hi] on one axis.
std::pair<double, double> support_center_interval(double lo, double hi);

// route_footprints(blocks, standing): bridge footprints [minX, maxX, minZ, maxZ].
Value route_footprints(const Value& blocks, bool standing);

}  // namespace pkc
