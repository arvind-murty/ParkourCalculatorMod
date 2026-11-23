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

// _solve(content, time_limit) with disk.MAX_ITER as `max_iter`: one Clarabel solve.
// Malformed content throws std::invalid_argument.
Value disk_solve(const Value& content, double time_limit, unsigned max_iter);

inline constexpr double kDiskCap = 0.1;          // disk.CAP
inline constexpr unsigned kDiskMaxIter = 200;     // disk.MAX_ITER

// solve(content, seconds=..., kind=...): decided results are cached process-wide by
// json.dumps(content, sort_keys=True), with STATS counted per kind ('screen', 'candidate').
// `cap` and `max_iter` are the module's CAP and MAX_ITER.
Value disk_cached_solve(const Value& content, double seconds, const std::string& kind, double cap, unsigned max_iter);
Value disk_stats();  // STATS: {kind: {counter: count}}

// screen(model, extra_rows, objective): null when not applicable, else {outcome, status[, proof | message]}.
Value disk_screen(const Value& model, const Value& extra_rows, const Value* objective, double cap, unsigned max_iter);

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
    double cap = kDiskCap, alarm_fraction = 0.1, material_gap = 1e-4;  // the module's CAP, ALARM_FRACTION, MATERIAL_GAP
    unsigned max_iter = kDiskMaxIter;                                   // and MAX_ITER in force
};

// RelaxedDispatch.solve (confirming=false) or .confirmed (confirming=true) for one request; an empty on_incumbent
// is None.
Value relaxed_dispatch(const Value& problem, double seconds, bool stop_on_feasible, bool confirming,
                       const std::function<void(const Value& candidate)>& on_incumbent,
                       const RelaxedServices& services);

}  // namespace pkc
