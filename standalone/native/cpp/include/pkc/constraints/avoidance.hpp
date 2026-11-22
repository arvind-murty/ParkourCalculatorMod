// Authored passages on routes: constraints/avoidance.py.
#pragma once

#include <cstdint>
#include <utility>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

// face_clearance(route, leg, offset, face, clearance): no clearance against a face the route hits there.
Num face_clearance(const Object& route, std::int64_t leg, std::int64_t offset, const Object& face, const Num& clearance);
void validate_passages(Object& route);
std::pair<Array, Array> compile_passages(const Object& route, const Array& events);  // rows, failures

}  // namespace pkc
