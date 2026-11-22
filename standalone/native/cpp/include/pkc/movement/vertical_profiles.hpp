// Scalar Y traces of a movement schedule: movement/vertical_profiles.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// trace_ladder_schedule(initial, ladder, sneak, hit, jump=..., target_top=..., min_landing_state=...,
// contacts=..., occupied_y_bounds=..., stop_on_landing=..., threshold=...) on JSON values; a null
// `jump`, `target_top`, `contacts` or `occupied_y_bounds` is the Python's None. Errors throw
// std::invalid_argument with the Python's message.
Value trace_ladder_schedule(const Value& initial, const Value& ladder, const Value& sneak, const Value& hit,
                            const Value& jump, const Value& target_top, const Value& min_landing_state,
                            const Value& contacts, const Value& occupied_y_bounds, bool stop_on_landing,
                            const Value& threshold);

}  // namespace pkc
