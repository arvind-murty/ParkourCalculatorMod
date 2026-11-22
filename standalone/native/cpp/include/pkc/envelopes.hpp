// reachability/intervals.reachable_envelopes: conservative X/Z state boxes, interaction
// phases and travel radii for one movement model.
#pragma once

#include <cstdint>
#include <optional>

#include "pkc/value.hpp"

namespace pkc {

// Python's math.hypot(a, b) (CPython 3.12 vector_norm), bit for bit.
double python_hypot(double a, double b);

// The Python result without its `model` entry, which aliases the input problem.
Value reachable_envelopes(const Value& problem, const Value& y, std::optional<std::int64_t> parent_ticks);

}  // namespace pkc
