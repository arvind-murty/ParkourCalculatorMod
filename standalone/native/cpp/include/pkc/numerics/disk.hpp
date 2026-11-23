// The acceleration-disk relaxation: numerics/disk.py's model and Clarabel solve,
// with its process-wide decided-result cache and STATS counters, and the relaxed
// dispatch that sends a request to Clarabel unless IPOPT must decide.
#pragma once

#include <functional>
#include <string>

#include "pkc/value.hpp"

namespace pkc {

// spec(problem, extra_rows, objective, strict=...): canonical content, or null
// outside the supported class. `objective` may be null.
Value disk_spec(const Value& problem, const Value& extra_rows, const Value* objective, bool strict);

// _solve(content, time_limit, max_iter): one Clarabel solve. A feasible point is 'feasible', a time or iteration
// limit 'limit', every other status 'infeasible'. Malformed content throws std::invalid_argument.
Value disk_solve(const Value& content, double time_limit, unsigned max_iter);

// The module's CAP and MAX_ITER, and the RETRY_CAP and RETRY_MAX_ITER of a run that hit a limit, in force.
struct DiskLimits {
    double cap = 0.1;
    unsigned max_iter = 200;
    double retry_cap = 0.5;
    unsigned retry_max_iter = 500;
};

// solve(content, seconds=..., kind=...): a feasible or infeasible verdict (a limit hit twice is infeasible), cached
// process-wide by json.dumps(content, sort_keys=True), with STATS counted per kind ('screen', 'candidate').
Value disk_cached_solve(const Value& content, double seconds, const std::string& kind, const DiskLimits& limits);
Value disk_stats();  // STATS: {kind: {counter: count}}

// screen(model, extra_rows, objective): null when not applicable, else {outcome, status[, proof]}.
Value disk_screen(const Value& model, const Value& extra_rows, const Value* objective, const DiskLimits& limits);

// deficits(problem, controls) as [peak, values, tick of peak].
Value disk_deficits(const Value& problem, const Value& controls);

Value ipopt_seed(const Value& yaw);
// alarm_ticks(problem, shortfall) with disk.ALARM_FRACTION as `fraction`.
Value alarm_ticks(const Value& problem, const Value& shortfall, double fraction);


// RelaxedDispatch's services for one call. Python keeps confirmations in the gate's cache (gate.cache), so these
// read and write the gate's.
struct RelaxedServices {
    // numerics.solver.solve(problem, seconds=..., stop_on_feasible=..., on_incumbent=...) with the call's
    // python_executable and cancel event; an empty on_incumbent is None.
    std::function<Value(const Value& problem, double seconds, bool stop_on_feasible,
                        const std::function<void(const Value& candidate)>& on_incumbent)>
        full;
    std::function<bool()> cancelled;  // cancel_event.is_set(); empty never cancels
    std::function<const Value*(const std::string& fingerprint)> confirmation;  // the cached confirmation, or null
    std::function<void(const std::string& fingerprint, const Value& result)> remember_confirmation;
    DiskLimits limits;
    double alarm_fraction = 0.1, material_gap = 1e-4;  // the module's ALARM_FRACTION and MATERIAL_GAP in force
};

// RelaxedDispatch.solve (confirming=false) or .confirmed (confirming=true) for one request; an empty on_incumbent
// is None.
Value relaxed_dispatch(const Value& problem, double seconds, bool stop_on_feasible, bool confirming,
                       const std::function<void(const Value& candidate)>& on_incumbent,
                       const RelaxedServices& services);

}  // namespace pkc
