// Authored passages on routes: constraints/avoidance.py.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

// face_clearance(route, leg, offset, face): no clearance against a face the route hits there, else the solver margin.
Num face_clearance(const Object& route, std::int64_t leg, std::int64_t offset, const Object& face);
// phase_states(tick, phase, axis): state coordinates of an ordinary Y/X/Z collision sweep.
std::vector<std::int64_t> phase_states(std::int64_t tick, const std::string& phase, const std::string& axis);
// blockage_counts(chain) and passage_kind(chain) over a passage's faces.
Value blockage_counts(const Array& chain);
std::string passage_kind(const Array& chain);
void validate_passages(Object& route);
std::pair<Array, Array> compile_passages(const Object& route, const Array& events);  // rows, failures

}  // namespace pkc
