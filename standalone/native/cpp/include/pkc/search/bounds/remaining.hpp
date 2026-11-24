// Optimistic remaining time between anchor landing regions: search/bounds/remaining.py
// with bounds/flight.state_covers.
#pragma once

#include <cstdint>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

// remaining_bound(guides, span, program, motion, transfers=..., hard_latches=...); null transfers or
// hard_latches are None. `hard_latches` is read for its keys.
Value remaining_bound(const Value& guides, std::int64_t span, const Program& program, const Value& motion,
                      const Value& transfers, const Value& hard_latches);

}  // namespace pkc
