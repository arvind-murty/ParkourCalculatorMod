// The movement frontier's persistent continuation: search/frontier.py persistent_programs on the path the anchor DFS
// takes under PKC_NATIVE_CORE, where each movement type's proposals are expanded natively (search/node.hpp). The
// Python proposal path (screen, context_adapter, Python minimum runs) is not ported; its in-flight proposal fields
// are kept so Python-mode states round-trip.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pkc/ordered_map.hpp"
#include "pkc/program.hpp"
#include "pkc/search/node.hpp"
#include "pkc/value.hpp"

namespace pkc {

struct ExpansionState {  // continuation['expansion']: one decision's expansion, a movement type at a time
    Value program;
    std::int64_t kind = 0, cursor = 0;  // kind == len(types): the types are done
    // An ordinary decision's jump-now children for minimum_runs: advantageous key -> [[node, landed ticks]], and the
    // landings on other faces.
    std::optional<OrderedMap<std::vector<Value>>> reached;
    std::optional<std::vector<Value>> landings;
    std::optional<std::vector<Nullable<std::string>>> queued_nodes;  // what a held same-face jump waits for
    std::optional<std::vector<Value>> held;                          // same-face fits with their program
    std::optional<Value> choice, child, full_program, projected;     // the Python proposal path's proposal
    std::optional<bool> proposal_checked;
};

struct HeldAlternative {  // continuation['alternatives'][node]
    std::vector<Value> programs;
    std::vector<std::string> restored;  // decision nodes of the programs restored so far
    std::int64_t next = 0;
    std::optional<std::vector<Nullable<std::string>>> watch;  // the nodes whose branches must die first (else the key)
};

struct Continuation {  // persistent_programs' state
    std::int64_t version = 0;
    std::string context;
    std::vector<Value> pending, ready;
    Nullable<ExpansionState> expansion;
    std::vector<std::string> seen;
    std::int64_t generated = 0, expanded = 0, emitted = 0, screened = 0, policy_rejections = 0;
    bool exhausted = false;
    std::vector<Value> exclusions;
    // Decision checks (a node evaluator): node solve records by decision node, deferred unknown checks, the decision
    // tree, held alternatives, nodes below a validated group-end arrival.
    std::optional<OrderedMap<Value>> nodes;
    std::optional<std::vector<Value>> deferred_nodes;
    std::optional<OrderedMap<Nullable<std::string>>> decision_parents;
    std::optional<OrderedMap<HeldAlternative>> alternatives;
    std::optional<std::vector<std::string>> succeeded_nodes;
    std::optional<std::vector<Value>> minimum_run_candidates;
    std::optional<Value> node_in_flight;
};

struct GenerationReport {  // persistent_programs' update()
    std::string termination;
    std::int64_t expanded_prefixes = 0, total_expanded_prefixes = 0, generated = 0, emitted_programs = 0,
                 screened_prefixes = 0, remaining_prefixes = 0, deferred_nodes = 0, queued_complete_programs = 0,
                 queue_drops = 0, partial_queue_drops = 0, complete_queue_drops = 0, policy_rejections = 0;
    std::string queue_policy;
    Value types;
    bool explicit_program_priority = false;
    std::int64_t deferred_prefixes = 0;
    Continuation continuation;
    bool minimum_ticks_proven = false;
};

struct Generation {  // a frame's generation: {} until its frontier first reports
    std::optional<GenerationReport> report;

    Continuation* continuation() { return report ? &report->continuation : nullptr; }
    const Continuation* continuation() const { return report ? &report->continuation : nullptr; }
};

// restore_alternatives(state, extra_live): queues each held decision-node alternative whose branch died; whether
// anything was queued.
bool restore_alternatives(Continuation& state, const std::vector<Value>& extra_live);

struct FrontierHooks {
    std::function<void()> budget;  // throws Timeout when cancelled, out of work or past the deadline
    std::function<Value(const Value& program, const std::string& identity)> priority;  // priority_key
    std::function<bool(Value& program)> eligible;                                      // program_filter
    std::function<std::vector<Value>()> live_programs;
    // node_evaluator(program, state['nodes'], False) on the program in flight, which its hooks update in place.
    std::function<Value(Value& program, OrderedMap<Value>& nodes)> evaluate_node;
    // native_expansion's node_search(): the native screen and eligibility on the frame's current state, with the
    // comparison context id its effects record; apply(effects, context_id) makes those DFS state changes.
    std::function<NodeSearch(Value& context_id)> node_search;
    std::function<void(const NodeEffects& effects, const Value& context_id)> apply;
    std::function<bool(const Value& program)> ordinary;  // run_policy['ordinary']
    std::function<Value()> termination;                  // work.termination
};

struct FrontierOptions {
    Value scene;     // the target scene
    Value settings;  // movementSearch settings, maxPrefixes already reduced by the search's expansions
    std::vector<Value> initial_programs;
    std::optional<std::vector<std::string>> stop_supports;
    Value policy_context;
    double threshold = 0;
    std::int64_t comparison_policy = 0;  // comparison.POLICY_VERSION
};

// persistent_programs(scene, settings=..., report=..., continuation=report.get('continuation'), ...) as a pull
// iterator over the frame's generation: next() runs to the next emitted program, nullopt once the generator
// returned; close() closes it. It works in place on the report's continuation where Python works on a deep copy that
// the report takes at the first update(): nothing reads the report before then.
class Frontier {
public:
    Frontier(Generation& generation, FrontierOptions options, FrontierHooks hooks);

    std::optional<Value> next();
    void close();

private:
    bool complete(const Value& program) const;
    Value take(std::vector<Value>& items) const;
    bool restore();
    void commit(const Value& exclusions, std::int64_t screened, std::int64_t policy_rejections, const Value& node);
    void minimum_runs(ExpansionState& e);
    void hold_minimum_runs(ExpansionState& e, const Value& node, const Array& log, const Array& reserves);
    void hold_same_face(ExpansionState& e);
    void expand(ExpansionState& e);
    void update(const std::string& reason = "running");
    std::optional<Value> run();

    GenerationReport* report_;
    Continuation* state_;
    FrontierOptions options_;
    FrontierHooks hooks_;
    std::set<std::string> seen_;
    std::int64_t expanded_ = 0;
    std::array<double, 4> initial_bounds_{};
    std::vector<std::string> types_;
    std::vector<std::string> goal_blocks_;  // stop_supports or [scene['goalBlock']]
    std::int64_t max_prefixes_ = 0, max_queued_prefixes_ = 0, max_choices_per_type_ = 0, max_inputs_ = 0,
                 max_interactions_ = 0;
    bool finished_ = false;
};

}  // namespace pkc
