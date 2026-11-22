// Independent check of a selected vertical/contact schedule: validation/vertical.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// validate_vertical_schedule(route, profiles, plan): {feasible, vertical, y, vy, onGround}.
Value validate_vertical_schedule(const Value& route, const Value& profiles, const Value& plan);

}  // namespace pkc
