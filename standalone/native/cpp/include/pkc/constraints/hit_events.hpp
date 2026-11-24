// Selected wall hits and ceilings on routes: constraints/hit_events.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

void validate_hits(Object& route);
struct CompiledHits {
    Array contacts, scene_contacts, rows;
};
CompiledHits compile_hits(const Object& route, const Array& events);

}  // namespace pkc
