// Run-allocation search over one scheduled route: search/ordered_route.py, with
// the Python-side services it calls (compiler, numerical solver, work budget)
// as hooks. Routes with passages, wall hits or ladder events throw Unsupported.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

// A numerical solve: (problem, seconds, stop_on_feasible, on_incumbent) -> a numerics.solver.solve result.
using NumericalSolve =
    std::function<Value(const Value& problem, double seconds, bool stop_on_feasible,
                        const std::function<void(const Value& candidate)>& on_incumbent)>;

// The services the node solve calls back into. Empty work hooks mean no work accounting; an empty `cancelled`
// never cancels. A hook raising Python's TimeoutError throws Timeout; ValueError std::invalid_argument; RuntimeError
// and OSError std::runtime_error.
struct SolveHooks {
    std::function<Value(const Value& route, const Value& plan, double seconds)> compile;
    std::function<Value(const Value& route, double seconds)> derive_footprints, derive_standing_footprints;
    // compiler.check_scene(geometry, positions, movement, ground, seconds, contacts=..., **contract)
    std::function<Value(const Value& geometry, const Value& positions, const Value& movement, const Value& ground,
                        double seconds, const Value& contacts, const Object& contract)>
        check_scene;
    NumericalSolve solve;  // numerics.solver.solve
    std::function<void()> work_check;
    std::function<void(const Value& problem)> work_proposal, work_before_solve;
    std::function<void(const Value& best)> work_accepted;
    std::function<bool()> cancelled;

    bool is_cancelled() const { return cancelled && cancelled(); }
};

struct RouteSearch {
    double seconds = 60, candidate_seconds = 2;
    std::int64_t retries = 1;
    bool improve = false, rank_equal_cost = false, optimize_goal = false;
    std::optional<bool> stop_on_feasible;
    NumericalSolve numerical;                             // empty: hooks.solve
    std::function<void(const Value& best)> on_candidate;  // may be empty
};

// find_route(request, compiler, ...).
Value find_route(const Value& request, const SolveHooks& hooks, const RouteSearch& search);

// ranking.candidate_rank(best, objective) as [ticks, signed objective].
Value candidate_rank(const Value& best, const Value& objective);
bool rank_less(const Value& a, const Value& b);  // Python list comparison of two ranks

}  // namespace pkc
