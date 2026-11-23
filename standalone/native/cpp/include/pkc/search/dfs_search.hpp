// The anchor DFS loop: search/dfs.py search_goals from its first publish through the main loop and finalization,
// owning the typed search state (search/dfs_state.hpp) and running each frame's movement frontier
// (search/frontier.hpp) with its decision checks. Python keeps the preamble (roots, context digest, resume checks, the
// gate) and the result dict, and serves what is not ported yet through DfsHooks: the adapter's stage views, discovery
// and advance, gate preparations, anchor-leg, finish and node solve services, progress saving and the work budget.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "pkc/reachability/service.hpp"
#include "pkc/search/anchors/policy.hpp"
#include "pkc/search/dfs.hpp"
#include "pkc/search/dfs_state.hpp"
#include "pkc/search/node.hpp"
#include "pkc/value.hpp"

namespace pkc {

struct DfsHooks {
    std::function<double()> monotonic;  // time.monotonic()
    std::function<bool()> cancelled;    // cancel_event is set
    // Progress saving: on_progress(state), its begin_repair(state, program, extra_targets) and repair_progress(state);
    // `progress` says whether a saver is attached (on_progress is truthy).
    bool progress = false;
    std::function<void(const Value& state)> publish;
    std::function<void(const Value& state, const Value& program, const Value& extra_targets)> begin_repair;
    std::function<void(const Value& state)> repair_progress;
    // The work budget: check() (raises TimeoutError), counts['solverCalls'], counts[name] += 1, accepted(candidate),
    // termination (null for None).
    std::function<void()> check;
    std::function<std::int64_t()> solver_calls;
    std::function<void(const std::string& name)> count;
    std::function<void(const Value& candidate)> accepted;
    std::function<Value()> termination;
    // The adapter: initial_cursor(parents); next_goals(cursor, parents, continuation, budget) as
    // {requests, continuation, report}; for_goal(request, cursor)'s facts for a frame as {guide, scoreContext, cursor,
    // objective, corridor}; advance(cursor, child, observed_route(...), request).
    std::function<Value(const Value& parents)> initial_cursor;
    std::function<Value(const Value& cursor, const Value& parents, const Value& continuation, std::int64_t expansions,
                        std::int64_t materializations)>
        next_goals;
    std::function<Value(std::int64_t frame, const Value& request, const Value& cursor)> stage;
    std::function<Value(std::int64_t frame, const Value& cursor, const Value& child, const Value& witness,
                        const Value& request)>
        advance;
    // evaluate_program(dict(scene, goalBlock=landing), program, ..., the frame's stage view, ...) with `options`
    // {landing, policy, firstWitness, guideObjective, seconds, finalRoute (or absent), progress}; with progress, the
    // solve reports through DfsSearch::score_progress.
    std::function<Value(std::int64_t frame, const Value& program, const Value& options)> evaluate;
    // bounds/flight.transfer_bounds(scene, guide_plan, motion, gate, saved=..., ...): the saved records after it.
    std::function<Value(const Value& saved)> transfer_bounds;
    // A decision check's services: gate.prepare(program, scene=scene); solve_services on the frame's node view
    // (for_node(program)); the Python node solve for a route the native one leaves there (see NodeHooks).
    std::function<Value(const Value& program)> prepare;
    std::function<NodeSolve(std::int64_t frame, const Value& program)> begin_solve;
    std::function<Value(std::int64_t frame, const Value& program, const std::function<void(const Value&)>& progress)>
        solve_route;
};

struct DfsOptions {
    Value scene, cfg, motion, guide_plan, anchor_policy;  // anchor_policy: the adapter's
    Value policy_settings;         // anchors.policy.semantic_settings(anchor_policy)
    Value domain_id;               // adapter.data['domainId']
    std::vector<Value> roots;      // the first frame's parents
    std::string task_block;
    Value discovery_settings;   // getattr(adapter, 'discovery_settings', {})
    Value initial_jump_inputs;  // program_filter.initial_jump_inputs, or null
    Object caps;                // accepted_program.caps
    bool minimum_run_pruning = false, retry_unknown = false;
    double deadline = 0, started = 0, candidate_seconds = 0, score_seconds = 0;
    std::int64_t batch_size = 1;
    std::string numerical_backend = "ipopt";
    std::int64_t prior_calls = 0, initial_expanded = 0;
    std::int64_t comparison_policy = 0;  // comparison.POLICY_VERSION
};

class DfsSearch {
public:
    DfsSearch(DfsState state, DfsOptions options, DfsHooks hooks, std::shared_ptr<InteractionGate> gate,
              std::shared_ptr<AnchorScene> anchors);

    std::string run();      // the main loop; the termination
    void finalize_best();   // finalize=True
    Value state() const;    // the state as published
    std::int64_t viability_checked() const { return viability_checked_; }
    std::int64_t viability_excluded() const { return viability_excluded_; }
    void score_progress(const Value& repair);  // an anchor-leg or finish solve's progress

private:
    using FramePtr = std::shared_ptr<Frame>;
    using RecordPtr = std::shared_ptr<Record>;

    struct Stage {
        Value guide, score_context, cursor, objective, corridor;
    };

    // search_goals' closures, in its order.
    void transition(const std::string& kind, const Frame& f, Object fields);
    FramePtr frame(std::int64_t index, const std::vector<Value>& parents, const Nullable<FrameParent>& parent,
                   const std::optional<Value>& cursor);
    FramePtr make_frame(std::int64_t index, const std::vector<Value>& parents, const Nullable<FrameParent>& parent,
                        const Value& goal, const Value& goal_request, const Value& semantic_cursor,
                        const std::string& owner);
    void expand_discovery(Discovery& discovery);
    const Stage& stage(Frame& f);
    Value guide_objective(Frame& f);
    std::pair<std::int64_t, std::int64_t> span_info(Frame& f);
    std::tuple<std::int64_t, std::int64_t, std::int64_t> group_span(Frame& f);
    Value priority(const Value& program, Frame& f, const std::string& identity = "");
    Value record_priority(const Record& r, Frame& f);
    std::string pair_key(std::int64_t k) const;
    Audit visited(const Program& program) const;
    std::int64_t pair_of(const Program& program, Frame& f);
    bool group_latched(Frame& f);
    Value pair_transfer(const Program& program, Frame& f, std::int64_t k, const Audit& arrivals, bool complete);
    Value hard_bound(const Value& program, Frame& f);
    std::string score_key(const Value& program, Frame& f);
    Value time_bound(const Value& program, Frame& f);
    Value tick_label(const Value& program, Frame& f, const std::string& identity, bool validated);
    Value run_bound(const SharedValue& program, Frame& f);
    bool eligible(const SharedValue& program, Frame& f);
    Value reject_time(const Value& program, Frame& f);
    bool numerical_pending(const Record& r) const;
    bool final_frame(const Frame& f) const;
    void generate(Frame& f);
    Object evaluation_args(Frame& f, const Value& program);
    RecordPtr score(Frame& f, const SharedValue& program, const RecordPtr& old);
    void refine(Frame& f, const RecordPtr& pending);
    RecordPtr variant_record(Frame& f, const Record& record, const Value& variant);
    bool restore_fallback(Frame& f, const Value& recovery);
    void close_frame(const FramePtr& f, bool deferred);
    void succeed(Frame& f, const Value& program);
    bool restore_alternatives(Frame& f);
    void retry_deferred_nodes(Frame& f);
    NodeSearch node_search(Frame& f, Value* context_id, const Value& policy_context = Value{nullptr});
    Value evaluate_node(Value& program, OrderedMap<Value>& nodes, Frame& f);
    Object node_service(const Value& program, std::int64_t span, Frame& f);
    void replay_ancestors(const Value& program, const Value& positions, const Value& model);
    void publish();
    void begin_repair(const Value& program, const Value& extra_targets);
    void repair_progress();
    std::vector<Value> live_programs(Frame& f);
    Value latches_value() const;
    void apply(const NodeEffects& effects, Frame& f, const Value& context_id);
    Frame& current();

    DfsState state_;
    DfsOptions options_;
    DfsHooks hooks_;
    std::shared_ptr<InteractionGate> gate_;
    std::shared_ptr<AnchorScene> anchors_;
    std::vector<std::string> anchor_names_;  // the anchor policy's anchors
    std::map<std::int64_t, Stage> stages_;
    std::set<std::pair<std::int64_t, std::string>> excluded_runs_;
    std::int64_t viability_checked_ = 0, viability_excluded_ = 0;
    Frame* scoring_ = nullptr;  // the frame whose anchor-leg or finish solve is running
};

}  // namespace pkc
