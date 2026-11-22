// The acceleration-disk relaxation: numerics/disk.py's model and Clarabel solve.
// Its process-wide decided-result cache and STATS counters stay in Python
// with their readers.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// spec(problem, extra_rows, objective, strict=...): canonical content, or null
// outside the supported class. `objective` may be null.
Value disk_spec(const Value& problem, const Value& extra_rows, const Value* objective, bool strict);

// _solve(content, time_limit) with disk.MAX_ITER as `max_iter`: one Clarabel solve.
// Malformed content throws std::invalid_argument.
Value disk_solve(const Value& content, double time_limit, unsigned max_iter);

// deficits(problem, controls) as [peak, values, tick of peak].
Value disk_deficits(const Value& problem, const Value& controls);

Value ipopt_seed(const Value& yaw);
// alarm_ticks(problem, shortfall) with disk.ALARM_FRACTION as `fraction`.
Value alarm_ticks(const Value& problem, const Value& shortfall, double fraction);

}  // namespace pkc
