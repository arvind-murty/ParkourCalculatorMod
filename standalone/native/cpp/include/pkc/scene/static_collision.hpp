// Finite-scene collision replay: scene/static_collision.py.
#pragma once

#include <optional>
#include <stdexcept>

#include "pkc/value.hpp"

namespace pkc {

// Thrown when a replay runs past its budget (Python's TimeoutError).
struct Timeout : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// check_scene(request, deadline) for one `sceneCheck` message; `seconds` is the
// budget left when the call starts. Invalid requests throw std::invalid_argument
// with the Python's message.
Value check_scene(const Value& request, std::optional<double> seconds);

}  // namespace pkc
