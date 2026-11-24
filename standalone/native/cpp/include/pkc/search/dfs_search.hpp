// The anchor DFS loop: search/dfs.py search_goals from its first publish through the main loop and its frame-end
// re-solves,
// owning the typed search state (search/dfs_state.hpp) and running each frame's movement frontier
// (search/frontier.hpp) with its decision checks. Python keeps the preamble (roots, context digest, resume checks, the
// gate) and the result dict, and serves what is not ported yet through DfsHooks: gate preparations, the Python parts
// of the anchor-leg, finish and node solve services, progress saving and the work budget. The anchor adapter's stage
// views, discovery, advance and candidate checks are native (search/anchors/adapter.hpp).
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

#include "pkc/io/search_checkpoint.hpp"
#include "pkc/reachability/service.hpp"
#include "pkc/search/anchors/adapter.hpp"
#include "pkc/search/anchors/policy.hpp"
#include "pkc/search/dfs.hpp"
#include "pkc/search/dfs_state.hpp"
#include "pkc/search/node.hpp"
#include "pkc/value.hpp"

namespace pkc {

// A published state, frozen: Python reads it field by field and the checkpoint store encodes it natively.
struct DfsSnapshot {
    Value state;
    RepairTargets repair;
};
using Snapshot = std::shared_ptr<const DfsSnapshot>;

struct DfsHooks {
    std::function<double()> monotonic;  // time.monotonic()
    std::function<bool()> cancelled;    // cancel_event is set
    // Progress saving: on_progress(state), its begin_repair(state, ...) and repair_progress(state), each given a
    // snapshot; `progress` says whether a saver is attached (on_progress is truthy).
    bool progress = false;
    std::function<void(const Snapshot& state)> publish;
    std::function<void(const Snapshot& state)> begin_repair;
    std::function<void(const Snapshot& state)> repair_progress;
    // A native checkpoint saver, when the run saves natively: it takes every snapshot instead of the three hooks
    // above, and Python is not called for progress.
    std::shared_ptr<SearchSaver> saver;
    // The work budget: check() (raises TimeoutError), counts['solverCalls'], counts[name] += by, accepted(candidate),
    // termination (null for None).
    std::function<void()> check;
    std::function<std::int64_t()> solver_calls;
    std::function<void(const std::string& name, std::int64_t by)> count;
    std::function<void(const Value& candidate)> accepted;
    std::function<Value()> termination;
    // An anchor-leg or finish solve, evaluate_program(dict(scene, goalBlock=landing), program, ..., the frame's stage
    // view, ...) with `options` {landing, policy, firstWitness, guideObjective, seconds, finalRoute (or absent),
    // progress, ownWork, candidateSeconds}, runs natively after the gate's preparation: gate.prepare(program,
    // scene=dict(scene, goalBlock=landing), require_complete=True) as prepare_leg, then solve_services without an
    // adapter (its numerical backend and seconds, on a fresh SearchWork outside the run's budget when `own_work`, as a
    // frame-end re-solve runs) as begin_leg; the native adapter serves the candidate check. `evaluate` is the
    // Python solve, on the stage view of the frame's goal request and cursor, for a route the native one leaves there
    // (passages, wall hits, ladder events). With progress, the solve reports through DfsSearch::score_progress.
    std::function<Value(const Value& program, const Value& landing)> prepare_leg;
    std::function<NodeSolve(const Value& program, const std::string& numerical, double seconds, bool own_work)>
        begin_leg;
    std::function<Value(std::int64_t frame, const Value& request, const Value& cursor, const Value& program,
                        const Value& options)>
        evaluate;
    // bounds/flight.transfer_bounds(scene, guide_plan, motion, gate, saved=..., ...): the saved records after it.
    std::function<Value(const Value& saved)> transfer_bounds;
    // A decision check's services: gate.prepare(program, scene=scene); solve_services without an adapter (the
    // native adapter serves the node view's objective and candidate check); the Python node solve, on the node view
    // of the frame's goal request and cursor, for a route the native one leaves there (see NodeHooks).
    std::function<Value(const Value& program)> prepare;
    std::function<NodeSolve(const Value& program)> begin_solve;
    std::function<Value(std::int64_t frame, const Value& request, const Value& cursor, const Value& program,
                        const std::function<void(const Value&)>& progress)>
        solve_route;
};

struct DfsOptions {
    Value scene, cfg, motion, guide_plan, anchor_policy;  // anchor_policy: the adapter's
    Value policy_settings;         // anchors.policy.semantic_settings(anchor_policy)
    Value domain_id;               // adapter.data['domainId']
    std::vector<Value> roots;      // the first frame's parents
    std::string task_block;
    Value initial_jump_inputs;  // program_filter.initial_jump_inputs, or null
    Object caps;                // accepted_program.caps
    bool minimum_run_pruning = false;
    double deadline = 0, started = 0, candidate_seconds = 0, score_seconds = 0;
    std::int64_t batch_size = 1;
    std::string numerical_backend = "ipopt";
    std::int64_t prior_calls = 0, initial_expanded = 0;
    std::int64_t comparison_policy = 0;  // comparison.POLICY_VERSION
};

class DfsSearch {
public:
    DfsSearch(DfsState state, DfsOptions options, DfsHooks hooks, std::shared_ptr<InteractionGate> gate,
              std::shared_ptr<AnchorScene> anchors, std::shared_ptr<AnchorAdapter> adapter);

    std::string run();      // the main loop; the termination
    Value state() const;    // the state as published
    // The state as a frozen snapshot with these repair targets (none for the result's).
    Snapshot snapshot(RepairTargets repair = {}) const;
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
    Value evaluate_leg(Frame& f, const Value& program, const Value& args);  // see DfsHooks::evaluate
    RecordPtr score(Frame& f, const SharedValue& program, const RecordPtr& old);
    void refine(Frame& f, const RecordPtr& pending);
    RecordPtr variant_record(Frame& f, const Record& record, const Value& variant);
    bool restore_fallback(Frame& f, const Value& recovery);
    void close_frame(const FramePtr& f);
    bool frame_end(Frame& f, Record& chosen);
    Value witness_rank(const Record& record) const;
    bool improves(const Record& record) const;
    void commit_finish(Frame& f, Record& chosen);
    void confirm_at_budget_end();
    void succeed(Frame& f, const Value& program);
    bool restore_alternatives(Frame& f);
    NodeSearch node_search(Frame& f, Value* context_id, const Value& policy_context = Value{nullptr});
    Value evaluate_node(Value& program, OrderedMap<Value>& nodes, Frame& f);
    Object node_service(const Value& program, std::int64_t span, Frame& f);
    void replay_ancestors(const Value& program, const Value& positions, const Value& model);
    void publish();
    void begin_repair(RepairTargets targets, const SharedValue& program);
    void repair_progress();
    void report_repair(const Snapshot& state);  // to the native saver, else the repair_progress hook
    std::vector<Array> frame_paths(const Frame& f) const;
    std::vector<Array> program_paths(const SharedValue& program) const;
    RepairTargets active_targets();
    std::vector<Value> live_programs(Frame& f);
    Value latches_value() const;
    void apply(const NodeEffects& effects, Frame& f, const Value& context_id);
    Frame& current();

    DfsState state_;
    DfsOptions options_;
    DfsHooks hooks_;
    std::shared_ptr<InteractionGate> gate_;
    std::shared_ptr<AnchorScene> anchors_;
    std::shared_ptr<AnchorAdapter> adapter_;
    std::vector<std::string> anchor_names_;  // the anchor policy's anchors
    std::map<std::int64_t, Stage> stages_;
    std::set<std::pair<std::int64_t, std::string>> excluded_runs_;
    std::int64_t viability_checked_ = 0, viability_excluded_ = 0;
    Frame* scoring_ = nullptr;  // the frame whose anchor-leg or finish solve is running
    // The saver's base: the last full save's repair targets, and the program they follow (null for none, the
    // decision check's flag for a node program).
    RepairTargets repair_;
    SharedValue repair_program_;
    bool repair_node_ = false;
};

}  // namespace pkc
