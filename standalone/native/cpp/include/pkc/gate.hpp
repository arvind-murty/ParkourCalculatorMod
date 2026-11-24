// Native InteractionGate model preparation (reachability/service.py).
#pragma once

#include <optional>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

// InteractionGate.prepare's build for an open or complete program prefix:
// compile_program -> prepare_route -> schedule([0]) -> validate_vertical_schedule.
// Returns {route, profiles, plan, vertical}, or nullopt when the vertical
// schedule is infeasible. Supports the routes compile_program builds on a
// collision_boxes scene without ladder events (production never builds those);
// anything else throws std::invalid_argument, as do the Python validators.
std::optional<Value> gate_prepare(const Value& scene, const Program& program, bool require_complete);

}  // namespace pkc
