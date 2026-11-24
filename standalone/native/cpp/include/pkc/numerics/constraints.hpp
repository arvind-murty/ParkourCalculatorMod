// Equivalent numerical rows: numerics/constraints.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// reduce_walls(walls): {walls, audit} with one representative per equivalent row.
Value reduce_walls(const Value& walls);

}  // namespace pkc
