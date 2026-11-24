// The post-alarm pipeline: numerics/post_alarm.py, which answers an alarmed relaxed optimum with Clarabel alone
// (docs/reference/NUMERICAL_SOLVER.md#post-alarm-pipeline).
#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "pkc/numerics/disk.hpp"
#include "pkc/value.hpp"

namespace pkc {

inline constexpr double kPostAlarmFull = 1e-6;  // FULL: a tick this little short is full strength

// run(problem, relaxed_candidate, relaxed, seconds=..., cancel_event=...): a numerics.solver.solve-shaped result with
// `confirmation`; `relaxed` is the dispatch's disk record including the disk solve's `objective` and `alarmTicks`.
// The services' cancelled hook and limits serve it; it calls no IPOPT.
Value post_alarm_run(const Value& problem, const Value& relaxed_candidate, const Value& relaxed, double seconds,
                     const RelaxedServices& services);

// facings(problem, controls): the facings (degrees) of acceleration vectors.
Value post_alarm_facings(const Value& problem, const Value& controls);

// A tick's sector (lo, hi), in the Python dict's insertion order.
using Sectors = std::vector<std::pair<std::int64_t, std::pair<double, double>>>;
std::vector<DiskCut> sector_cuts(const Sectors& sectors, const Value& magnitude);
std::vector<Sectors> sector_children(const Sectors& sectors, std::int64_t tick, const std::array<double, 2>& controls);

}  // namespace pkc
