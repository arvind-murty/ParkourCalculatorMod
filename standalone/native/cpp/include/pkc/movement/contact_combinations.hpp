// Contact events on JSON values: movement/contact_combinations.py.
#pragma once

#include <cstdint>
#include <optional>

#include "pkc/value.hpp"

namespace pkc {

// merge_contacts(contacts, scene=scene) on (state, axis) keys; `axis` stands in for every contact's own.
Array merge_contacts(const Array& contacts, bool scene, std::optional<std::int64_t> axis = std::nullopt);
void validate_contact_events(Object& route);  // edits the route in place, as the Python does
Value bounding_region(const Array& regions);
// finite_rows(axis, region, **at): `at` holds the trailing keys (t1, or leg and offset).
Array finite_rows(int axis, const Array& region, const Object& at);
struct CompiledContacts {
    Array rows, horizontal, scene, vertical;
};
CompiledContacts compile_contact_events(const Object& route, const Array& events, std::int64_t ticks);

}  // namespace pkc
