// Scalar replay of a fixed continuous request: validation/numerical.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// validate(problem, yaw) for a prepared problem. Errors throw std::invalid_argument
// with the Python's message.
Value validate(const Value& problem, const Value& yaw);

// validate_controls(problem, controls): replay of a relaxed (|a| <= magnitude) candidate.
Value validate_controls(const Value& problem, const Value& controls);

}  // namespace pkc
