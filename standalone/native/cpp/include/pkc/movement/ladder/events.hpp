// Ladder events on routes: movement/ladder/events.py.
#pragma once

#include <cstdint>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

// validate_ladder_events(route): checks the ladder events and sets route['movementUnits'].
void validate_ladder_events(Object& route);

struct CompiledLadders {  // compile_ladder_events' (rows, contacts, scene_contacts, ladder, sneak)
    Array rows, contacts, scene_contacts;
    std::vector<bool> ladder, sneak;
};
CompiledLadders compile_ladder_events(const Object& route, const Array& events, std::int64_t ticks);

}  // namespace pkc
