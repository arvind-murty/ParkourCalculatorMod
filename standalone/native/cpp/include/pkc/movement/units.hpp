// Movement units on JSON values: movement/units.py.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

struct UnitMasks {  // compile_units' result
    std::vector<bool> ladder_ticks, hit, sneak;
};

bool is_ladder_type(const std::string& type);  // MOVEMENT_TYPES[type]['ladder']; throws for an unknown type
Value movement_unit(const std::string& kind, std::int64_t start, std::int64_t end, Object parameters = {});
// movement_program(ticks, events); the overload without events is a leg without ladders.
Array movement_program(std::int64_t ticks);
Array movement_program(std::int64_t ticks, const Array& events);
// ladder_units(schedule, region_id): a null region_id is None.
Array ladder_units(const Object& schedule, const Value& region_id);
bool exit_matches(const std::string& kind, double y, const Object& region);
UnitMasks compile_units(std::int64_t ticks, const Array& units);
Value shifted_unit(const Value& unit, std::int64_t offset);
Array profile_program(const Object& profile);
Array overlay_program(const Array& units, const Array& templates);

}  // namespace pkc
