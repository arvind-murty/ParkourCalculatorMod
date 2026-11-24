// The acceleration-disk relaxation: numerics/disk.py's model and Clarabel solve,
// with its process-wide decided-result cache and STATS counters, and the relaxed
// dispatch that sends a request to Clarabel unless IPOPT must decide.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

// spec(problem, extra_rows, objective, strict=...): canonical content, or null
// outside the supported class. `objective` may be null.
Value disk_spec(const Value& problem, const Value& extra_rows, const Value* objective, bool strict);

inline constexpr double kDiskAllowance = 1e-9;  // ALLOWANCE, added to every disk's radius

// A cut row nx*a_x[t] + nz*a_z[t] >= rhs.
struct DiskCut {
    std::int64_t tick;
    double nx, nz, rhs;
};

// The level row: `objective` (a spec objective) >= value for MAX, <= value for MIN, apart from the content's own.
struct DiskLevel {
    Value objective;
    double value;
};

using Controls = std::vector<std::array<double, 2>>;

// _build: a spec's Clarabel problem, built and set up but not solved, with the cut and level rows. `run` (_run) solves
// it and decodes its verdict: a feasible point is 'feasible', a time or iteration limit 'limit', every other status
// 'infeasible'. `update_q` replaces the linear cost for another solve. Malformed content throws std::invalid_argument.
class DiskProblem {
public:
    DiskProblem(const Value& content, double time_limit, unsigned max_iter, const std::vector<DiskCut>& cuts = {},
                const std::optional<DiskLevel>& level = std::nullopt);
    ~DiskProblem();
    DiskProblem(const DiskProblem&) = delete;
    DiskProblem& operator=(const DiskProblem&) = delete;

    Value run();
    // The solution's controls whatever the status, or nothing when one is not finite (the push steps' solve).
    std::optional<Controls> solve_controls();
    void update_q(const std::vector<double>& q);
    const std::vector<double>& cost() const { return cost_; }
    // Each tick's (a_x, a_z) columns.
    const std::vector<std::array<std::size_t, 2>>& columns() const { return columns_; }
    double assembly_seconds = 0., setup_seconds = 0.;

private:
    struct Solver;
    std::unique_ptr<Solver> solver_;
    std::vector<double> cost_;
    std::vector<std::array<std::size_t, 2>> columns_;
    double offset_ = 0., sense_ = 0.;
};

// The module's CAP and MAX_ITER, and the RETRY_CAP and RETRY_MAX_ITER of a run that hit a limit, in force.
struct DiskLimits {
    double cap = 0.1;
    unsigned max_iter = 200;
    double retry_cap = 0.5;
    unsigned retry_max_iter = 500;
};

// decide(content, seconds, cuts, level): (verdict, built problem) from one run, a limit retried once with the retry
// limits, a second limit infeasible. The built problem is null after an exception.
std::pair<Value, std::shared_ptr<DiskProblem>> disk_decide(const Value& content, double seconds,
                                                           const DiskLimits& limits,
                                                           const std::vector<DiskCut>& cuts = {},
                                                           const std::optional<DiskLevel>& level = std::nullopt);

// solve(content, seconds=..., kind=...): a feasible or infeasible verdict (a limit hit twice is infeasible), cached
// process-wide by json.dumps(content, sort_keys=True), with STATS counted per kind ('screen', 'candidate').
Value disk_cached_solve(const Value& content, double seconds, const std::string& kind, const DiskLimits& limits);
Value disk_stats();  // STATS: {kind: {counter: count}}
void disk_clear_decided();  // clear_decided(): empties the decided cache (a change of work-only mode)

// screen(model, extra_rows, objective): null when not applicable, else {outcome, status[, proof]}.
Value disk_screen(const Value& model, const Value& extra_rows, const Value* objective, const DiskLimits& limits);

// deficits(problem, controls) as [peak, values, tick of peak].
Value disk_deficits(const Value& problem, const Value& controls);

Value ipopt_seed(const Value& yaw);
// alarm_ticks(problem, shortfall) with disk.ALARM_FRACTION as `fraction`.
Value alarm_ticks(const Value& problem, const Value& shortfall, double fraction);


// RelaxedDispatch's services for one call. Python keeps confirmations and frame-end answers in the gate's cache
// (gate.cache, keyed by kind and fingerprint), so these read and write the gate's.
struct RelaxedServices {
    // numerics.solver.solve(problem, seconds=..., stop_on_feasible=..., on_incumbent=...) with the call's
    // python_executable and cancel event; an empty on_incumbent is None.
    std::function<Value(const Value& problem, double seconds, bool stop_on_feasible,
                        const std::function<void(const Value& candidate)>& on_incumbent)>
        full;
    std::function<bool()> cancelled;  // cancel_event.is_set(); empty never cancels
    // The cached answer of a kind ('confirmation', 'frameEnd') for a fingerprint, or null.
    std::function<const Value*(const std::string& kind, const std::string& fingerprint)> answer;
    std::function<void(const std::string& kind, const std::string& fingerprint, const Value& result)> remember;
    DiskLimits limits;
    double alarm_fraction = 0.1, material_gap = 1e-4;  // the module's ALARM_FRACTION and MATERIAL_GAP in force
    bool pipeline = true;  // RelaxedDispatch(post_alarm='pipeline'); false is post_alarm='ipopt'
};

enum class DispatchMode { solve, confirmed, full_strength };  // RelaxedDispatch.solve, .confirmed, .full_strength

// RelaxedDispatch's entry point `mode` for one request; an empty on_incumbent is None.
Value relaxed_dispatch(const Value& problem, double seconds, bool stop_on_feasible, DispatchMode mode,
                       const std::function<void(const Value& candidate)>& on_incumbent,
                       const RelaxedServices& services);

// _result: a numerics.solver.solve-shaped result for an answer Clarabel decided; `started` is seconds_now()'s clock.
Value decided_result(const Value& problem, double started, const char* status, const Value& candidate,
                     const std::string& termination, const Value& solver_status, const Value& relaxed);
double seconds_now();  // time.monotonic()

}  // namespace pkc
