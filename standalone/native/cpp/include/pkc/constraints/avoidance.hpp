// Authored passages on routes: constraints/avoidance.py.
#pragma once

#include <utility>

#include "pkc/value.hpp"

namespace pkc {

void validate_passages(Object& route);
std::pair<Array, Array> compile_passages(const Object& route, const Array& events);  // rows, failures

}  // namespace pkc
