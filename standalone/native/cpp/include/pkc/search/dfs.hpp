// The anchor DFS's decision check: search/dfs.py's evaluate_node, with commit_arrival and candidate_key. The search
// state it records into (the node table, checkpoint progress, observations) stays in Python as hooks.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "pkc/search/engine.hpp"
#include "pkc/value.hpp"

namespace pkc {

// dfs.candidate_key(program).
std::string candidate_key(const Value& program);

// dfs.decision_family(program, parents).
std::string decision_family(const Value& program, const std::vector<Value>& parents);

// dfs.commit_arrival(program, best, source_program=..., keep_region_choices=...); a null `source_program` is None.
Value commit_arrival(const Value& program, const Value& best, const Value& source_program, bool keep_region_choices);

// The node solve's Python services, started once the check has begun.
struct NodeSolve {
    EvaluateHooks hooks;  // without on_progress, which the check sets
    double seconds = 0;
    Value objective;         // the node view's objective (None reads as null)
    Value adapter_identity;  // the node view's corridor.to_dict(), or null
};

struct NodeHooks {
    // The gate's preparation of the program for this scene (require_complete=False), or null.
    std::function<Value(const Value& program)> prepare;
    std::function<void()> begin;  // nodes[identity] = entry; begin_repair(program, (entry,))
    std::function<NodeSolve()> begin_solve;
    std::function<void(const Value& repair)> assign;    // program['repairState'] = repair
    std::function<void(const Value& repair)> progress;  // the check's progress callback; empty without on_progress
    std::function<void(const Value& repair)> settle;    // program['repairState'] = entry['repairState'] = repair
    // observe(observations, guide_plan, program, span=..., model=..., status=..., elapsed=..., metrics=...)
    std::function<void(const Value& model, const std::string& status, double elapsed, const Value& metrics)> observe;
    // replay the validated positions into the program's ancestors' observations
    std::function<void(const Value& positions, const Value& model)> replay;
    // f['recoveryFailure'] = recovery (when not null); nodes[identity] = record; nodeResults[identity] = node_result;
    // publish()
    std::function<void(const std::string& identity, const Value& record, const Value& node_result, const Value& recovery)>
        store;
    // The node solve in Python (evaluate_program with the node view) for a route the native one leaves there, with the
    // check's progress callback (empty without one); unset, evaluate_node returns nullopt for such a route.
    std::function<Value(const Value& program, const std::function<void(const Value& repair)>& progress)> solve_route;
};

struct NodeOptions {
    Value saved;       // nodes.get(identity), or null
    Value guide_plan;  // adapter.guide_plan
    std::int64_t span = 0, solve_interval = 1;
    std::string numerical = "ipopt";
    Value frame;  // f['id']
    double candidate_seconds = 1;
    bool retry_unresolved = false;
};

struct NodeEvaluation {
    Value result;        // {status, program, alternatives?, deferUnknown?}
    bool input_program;  // `program` is the caller's program, with every assign applied
};

// evaluate_node(program, nodes, unused, f). A prepared route with passages, wall hits or ladder events is solved by
// `hooks.solve_route`, or without it gives nullopt before the check begins (only a saved repair state may have been
// assigned).
std::optional<NodeEvaluation> evaluate_node(const Value& scene, const Value& program, const NodeHooks& hooks,
                                            const NodeOptions& options);

}  // namespace pkc
