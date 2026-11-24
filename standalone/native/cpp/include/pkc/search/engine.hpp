// One node solve after the gate's preparation: search/engine.evaluate_program from solve_scope on, for routes the
// native repair loop covers (no passages, wall hits or ladder events). The gate, the adapter's candidate check, the
// relaxed dispatch and the work budget stay in Python as hooks.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "pkc/search/ordered_route.hpp"
#include "pkc/value.hpp"

namespace pkc {

struct EvaluateHooks {
    SolveHooks solve;                   // compiler, numerics.solver.solve, the node's ScopedWork, cancel event
    NumericalSolve numerical, confirm;  // RelaxedDispatch.solve and .confirmed; empty for IPOPT
    // InteractionGate.analyze_model(route, plan, vertical, scope=scope)
    std::function<Value(const Value& route, const Value& plan, const Value& vertical, const Value& scope)> analyze_model;
    // InteractionGate.reachable_faces(face) as {ticks, solids (sorted)}
    std::function<Value(const std::string& face)> reachable_faces;
    // The adapter scoped to `scope` and bound to the program: candidate(plan, candidate, vertical, partial=True)
    std::function<Value(const Value& plan, const Value& candidate, const Value& vertical, const Value& scope)> candidate;
    std::function<void()> check_generation;  // variants.check_generation(cancel_event, work.check, deadline)
    std::function<void(const Value& state)> on_progress;  // may be empty
    std::function<void(const Value& counts)> record_counts;  // adds numerical_counts to the parent and scoped work
    std::function<void(const Value& best)> accepted;  // the parent work's accepted
    std::function<Value()> work_summary;               // the scoped work's local summary
};

struct EvaluateOptions {
    double seconds = 3, candidate_seconds = 1;
    Value supplied_scope, required_landing, final_route;  // each may be null
    Value policy;            // feasibility.policy_spec's validated policy
    Value adapter_identity;  // the adapter's corridor.to_dict(), or null
    bool node_query = false, climb_families = false;
    Object timings;  // the marks taken before the call; this call adds its own
};

// evaluate_program's result for a preparation's route (with any guide objective applied), profiles and vertical
// schedule.
Value evaluate_prepared(const Value& scene, const Value& program, const Value& route, const Value& profiles,
                        const Value& vertical, const EvaluateHooks& hooks, const EvaluateOptions& options);

// engine.numerical_counts(diagnostics) as an ordered {counter: count}.
Value numerical_counts(const Array& diagnostics);

}  // namespace pkc
