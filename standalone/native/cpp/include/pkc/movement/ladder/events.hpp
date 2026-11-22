// Ladder events on routes: movement/ladder/events.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// validate_ladder_events(route): sets route['movementUnits']. Routes with
// ladder events throw Unsupported; they are prepared in Python.
void validate_ladder_events(Object& route);

}  // namespace pkc
