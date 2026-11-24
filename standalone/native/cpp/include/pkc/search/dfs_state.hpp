// The anchor DFS's search state: search/dfs.py search_goals' `state`, typed where the loop or its frontier owns it.
// Subtrees other units own (programs, solver results and witnesses, adapter goals and cursors) stay Values.
// Python aliases some objects across the state (a closed frame sits in `frames` and `suspended`, a final record in a
// frame's records and `finalAttempts`, a record's child in the incumbent), and mutates them through either path;
// shared_ptr members keep that aliasing within a session. Decoding, like a JSON round trip, aliases nothing.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pkc/ordered_map.hpp"
#include "pkc/program.hpp"
#include "pkc/search/frontier.hpp"
#include "pkc/value.hpp"

namespace pkc {

using SharedValue = std::shared_ptr<Value>;  // a program dict more than one place holds

struct FrameParent {  // frame['parent'], discovery['parent']
    std::int64_t frame = 0;
    std::string candidate;
};

struct InFlight {  // frame['inFlight']
    std::string id;
    SharedValue program;
};

struct Evaluation {  // record['evaluations'] rows
    std::int64_t session = 0;
    Value work;
};

struct Record {  // a frame's scored candidate (score, variant_record)
    std::string id;
    SharedValue program;
    std::string status;
    Nullable<std::string> termination;
    Value node_feasibility;
    std::string objective_mode;
    Nullable<std::int64_t> arrival;
    Nullable<Num> value;
    Value work, goal, goal_key;
    std::vector<Evaluation> evaluations;
    bool tried = false;
    Value solve_scope, solve_identity, numerical_attempts, contact_attempts;
    std::string candidate_key;
    Value score_context_key, guide_id;
    std::int64_t ticks = 0;
    bool complete = false;
    std::optional<Value> witness;
    std::optional<SharedValue> child;
    std::optional<Value> guidance_witness, transfer_structure, earlier_transfers;
    std::optional<Nullable<std::int64_t>> refinement_session;
    std::optional<Value> preparation_deferral, time_bound_exclusion, refinement_time_exclusion;
    std::optional<std::string> variant_of;
    std::optional<std::int64_t> restored_fallbacks;
    std::optional<Value> frame_end;  // the frame-end re-solve's record once it ran
};

struct Frame {  // make_frame
    std::int64_t id = 0, index = 0;
    std::vector<Value> parents;
    Nullable<FrameParent> parent;
    Value goal;
    std::string phase;
    Generation generation;
    std::vector<SharedValue> ready;
    std::vector<std::shared_ptr<Record>> records;
    Nullable<InFlight> in_flight;
    bool source_exhausted = false;
    std::int64_t batches = 0;
    Value goal_request, semantic_cursor;
    std::string owner;
    std::optional<Value> guide, score_context_key;
    std::optional<std::string> comparison_context_id;
    std::optional<Value> tick_peers;  // the peer index deferrals.assess reads
    std::optional<std::int64_t> tick_peer_revision;
    std::optional<Value> recovery_failure;
    std::optional<bool> validated_finish;
    std::optional<std::vector<std::string>> failed_candidates;
};

struct Transition {  // transition(kind, frame, **fields); each kind adds its own fields
    std::string kind;
    std::int64_t frame = 0, depth = 0, session = 0;
    Num elapsed_seconds;
    Object annotations;
};

struct WindowCheckpoint {
    std::int64_t tick = 0;
};

struct Retention {
    std::string policy;
};

struct Window {  // a frame's saved siblings when it opens a child
    WindowCheckpoint checkpoint;
    Value goal;
    std::int64_t frame = 0;
    std::vector<Record> candidates;  // copies
    std::vector<std::string> retained, deferred;
    std::int64_t scored = 0, unknown = 0, heuristic_drops = 0;
    Generation generation;
    Retention retention;
};

struct HardLatch {  // state['hardLatches'][pair]
    std::int64_t version = 0, revision = 0;
    std::string candidate;
    std::int64_t frame = 0;
    Value model;
    std::int64_t inputs = 0;
    Value transfer_structure;
};

struct NodeResult {  // dict(query, parent=..., depth=..., kind=...); the query's keys are the node solve's
    Nullable<std::string> parent;
    std::int64_t depth = 0;
    std::optional<std::string> kind;
    Object annotations;
};

struct QueryService {  // observability/turns.record's service
    std::int64_t latch_revision = 0;
    bool hard_latched = false, run_deferral_enabled = false, tick_deferral_enabled = false;
    std::int64_t comparison_policy = 0;
};

struct ObservationQuery {
    Value model;
    std::string status;
    Num elapsed_seconds;
    Value metrics;
    QueryService service;
};

struct ObservationReplay {
    Value model, point, distances;
    bool would_flip = false;
};

struct ObservationDecision {  // turns.record's event
    std::string id;
    Nullable<std::string> parent;
    std::int64_t depth = 0, last_solved_depth = 0, point_tick = 0;
    std::string point_source;
    std::int64_t version = 0;  // the classify label's fields
    Value guide;
    std::int64_t span = 0;
    Value point;
    bool is_turnaround = false;
    Value distances;
    Value counts;
    std::string mode;
    Value key;
    std::string kind;
    std::vector<ObservationQuery> queries;
    std::vector<ObservationReplay> replays;
    std::optional<std::int64_t> selected;
};

struct Observations {
    bool enabled = false;
    OrderedMap<ObservationDecision> decisions;
};

struct TimeBoundExclusion {
    std::string id;
    Value proof;
};

struct MinimumRunExclusion {  // run_bound's deferral, and its restoration
    std::int64_t frame = 0;
    std::string candidate;
    Value proof, program;
    std::int64_t span = 0;
    Value decision;
    bool is_turnaround = false;
    std::int64_t latch_revision = 0;
    std::optional<bool> restored;
    std::optional<Value> recovery;
    std::optional<bool> retired;
    std::optional<Value> restoration_exclusion;
};

struct Discovery {  // state['discovery'][owner]
    std::string owner;
    std::int64_t index = 0;
    std::vector<Value> parents;
    Nullable<FrameParent> parent;
    Value cursor, continuation;
    bool exhausted = false;
};

struct DiscoveryWork {
    std::int64_t expansions = 0, materializations = 0;
};

struct ComparisonWork {
    std::int64_t assessments = 0, cache_hits = 0, peer_checks = 0;
};

struct LocalSetupAlias {
    std::string owner;
    Value request;
};

struct Incumbent {  // a final record's witness copy holding the record's child as its movement program
    SharedValue movement_program;
    Value block_ids;
    Object annotations;  // the witness's keys
};

struct DfsState {
    std::int64_t version = 0;
    std::string kind, context;
    std::vector<Value> initial_roots;
    std::vector<std::shared_ptr<Frame>> stack, suspended, frames;
    std::vector<Transition> transitions;
    std::vector<Window> windows;
    std::vector<std::shared_ptr<Record>> final_attempts;
    Nullable<Incumbent> best;
    std::int64_t cumulative_calls = 0, session = 0, expanded = 0, next_frame = 0;
    std::vector<std::shared_ptr<Frame>> goal_queue;
    OrderedMap<std::vector<std::int64_t>> goal_owners;
    OrderedMap<Discovery> discovery;
    OrderedMap<HardLatch> hard_latches;
    std::int64_t latch_revision = 0;
    OrderedMap<NodeResult> node_results;
    Observations observations;
    std::vector<TimeBoundExclusion> time_bound_exclusions;
    std::vector<MinimumRunExclusion> minimum_run_exclusions;
    std::optional<DiscoveryWork> discovery_work;  // set when a session's loop starts
    std::optional<ComparisonWork> comparison_work;
    std::optional<OrderedMap<Value>> comparison_decisions;
    std::optional<std::vector<LocalSetupAlias>> local_setup_aliases;
    std::optional<std::string> pending_goal_owner;
    std::optional<Value> transfer_time_bounds;
    std::optional<Num> first_full_solution_seconds;
};

DfsState dfs_state_from_value(const Value& value);
Value to_value(const DfsState& state);
Value to_value(const Incumbent& incumbent);
Value to_value(const HardLatch& latch);
MinimumRunExclusion minimum_run_exclusion_from_value(const Value& value);
Value to_value(const ObservationQuery& query);
Value to_value(const ObservationReplay& replay);

}  // namespace pkc
