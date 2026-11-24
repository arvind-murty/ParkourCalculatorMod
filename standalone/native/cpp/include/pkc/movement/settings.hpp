// movement/settings.py: the declared movement domain settings.
#pragma once

#include <string>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

// movement.generation.GENERATORS' names, in registration order.
inline const std::vector<std::string> kMovementTypes = {"ceiling", "stepping", "normal", "ladder_climb", "ladder_fall"};

// movement_settings(scene) with `registered` as GENERATORS' names; invalid settings throw std::invalid_argument with
// the Python's message.
Value movement_settings(const Value& scene, const std::vector<std::string>& registered = kMovementTypes);

}  // namespace pkc
