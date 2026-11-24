// Dead airborne prefixes: reachability/viability.py.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

// prefix_viability(scene, program, required_contacts=..., terminal_tick=..., enabled_types=...); nullopt types are None.
Value prefix_viability(const Value& scene, const Program& program, const std::vector<std::string>& required_contacts,
                       std::int64_t terminal_tick, const std::optional<std::vector<std::string>>& enabled_types);

}  // namespace pkc
