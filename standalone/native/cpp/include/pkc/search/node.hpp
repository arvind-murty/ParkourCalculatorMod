// One DFS node expansion per movement type (search/frontier.py's per-proposal
// loop with search/dfs.py's screen, eligibility and configure), batched: the
// proposals of one type are generated, screened, accepted and linked natively
// and returned in order with the state changes the Python applies.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pkc/program.hpp"
#include "pkc/reachability/service.hpp"
#include "pkc/search/anchors/policy.hpp"
#include "pkc/value.hpp"

namespace pkc {

inline constexpr double kSameFaceFitFactor = .7;  // policy.SAME_FACE_FIT_FACTOR

// The frame and search state dfs reads (dfs.node_frame builds it).
struct NodeFrame {
    Value scene;  // the frame's target scene
    Value frame_id;
    std::string goal_kind, goal_block;
    std::vector<std::string> goal_blocks;  // f['goal'].get('blocks', [block])
    std::int64_t anchor_index = 0, group_end = 0, span_start = 0;
    Value comparison_context_id, tick_peers;
    // Whether f already holds comparisonContextId: run_bound reads f.get(...) before tick_label first sets it.
    mutable bool comparison_context_set = false;
    std::int64_t tick_peer_revision = 0;
    Value incumbent;  // {T} of state['best'], or null
    Value hard_latches, transfers;
    std::int64_t latch_revision = 0;
    bool minimum_run_pruning = false;
    Value guide_plan, motion, turnaround_margin;
    std::vector<std::string> anchors;  // the anchor policy's
    std::string task_block;
    Object request;  // the declared caps and initialJumpInputs (search/domain.InitialJumpDomain; null: any)
    std::int64_t max_inputs = 0;
    std::vector<std::string> types;
    Value policy_context;
    mutable std::set<std::string> excluded_runs;  // candidates of this frame already in minimumRunExclusions
};

// State changes of dfs screens and eligibility, in call order.
struct NodeEffects {
    Array time_bound_exclusions, comparison_decisions, minimum_run_exclusions;
    std::int64_t assessments = 0, cache_hits = 0, peer_checks = 0, viability_checked = 0, viability_excluded = 0;
    bool comparison_context_used = false;  // tick_label read the frame's comparisonContextId
};

struct NodeSearch {
    NodeFrame frame;
    InteractionGate* gate = nullptr;
    const AnchorScene* anchors = nullptr;

    std::string candidate_key(const Program& program) const;
    Value time_bound(const Program& program) const;
    Value hard_bound(const Program& program) const;
    Value run_bound(Program& program, NodeEffects& effects) const;
    bool eligible(Program& program, NodeEffects& effects) const;
    bool admitted(const Program& program) const;  // accepted_program and the adapter's program_allowed
    Value screen(Program& program, NodeEffects& effects) const;  // check_program may add a walk-off selection
};

// frontier.minimum_runs(e) without its frontier state changes: the frontier commits `exclusions` (one entry per
// screened proposal, in order) and queues `queued` (search keys and whether each choice was meaningful), appends
// `log` to minimumRunCandidates and holds each reserve [fields, watch]; `effects` are the DFS state changes.
// `reached` (e['reached']) and `landings` (e['landings']) are null for a held minimum-run search.
struct MinimumRuns {
    Array exclusions;
    NodeEffects effects;
    struct Queued {
        Program child;
        bool meaningful = false;
        std::string identity;
        std::shared_ptr<const Program> parent;  // the program the child was appended to
        std::optional<Value> side_variant;      // its choice's side-variant tag
    };
    std::vector<Queued> queued;
    Array log, reserves;
};
MinimumRuns minimum_runs(const NodeSearch& search, const Program& program, const Value& reached, const Value& landings,
                         const std::vector<std::string>& stop_supports, const std::array<double, 4>& initial_bounds,
                         std::int64_t settings_max_inputs, const std::set<std::string>& seen,
                         const std::function<void()>& budget);

// anchors/policy.advantageous_candidates(scene, gate.reachable_faces, launch, top, next_owner, types, threshold):
// the entries {key, A, via[, limit]} sorted by key. A null launch is a program on no face.
Array advantageous_candidates(InteractionGate& gate, const AnchorScene& anchors, const std::optional<std::string>& launch,
                              const Num& top, const std::string& next_owner, const std::vector<std::string>& types,
                              const Num& threshold);

// One proposal's outcome, in ordinal order.
struct ProposalRecord {
    std::int64_t ordinal = 0;
    std::string outcome;            // screened | rejected | seen | queued | held (a same-face jump that does not fit)
    std::optional<Program> child;   // the linked child, when queued
    bool meaningful = false;
    Value exclusions;               // frontier exclusions this proposal adds (node set by the caller)
    std::int64_t screened = 0, policy_rejections = 0;
    NodeEffects effects;
    std::optional<Value> side_variant;  // the queued child's choice's side-variant tag
    // An ordinary decision's accepted child: {targets (advantageous keys as JSON), landed (its earliest landing's
    // ticks after the decision, or null), landings (e['landings'] entries without their node), fit (or null)}.
    Value ordinary;
};

// The frontier's loop over the proposals of `kind` from `cursor` for an expansion of `parent`; an `ordinary` decision
// also finds each accepted child's landings, same-face fit and advantageous targets for its minimum-run bookkeeping. `seen` holds the queued search keys; `budget` is called per
// proposal and may throw (nothing is returned then). Like the Python loop, it stops after a proposal that brings
// the cursor to a multiple of `choices_per_type` while a complete program is ready, so the frontier can emit it;
// `finished` says whether the type's proposals ran out instead.
struct Expansion {
    std::vector<ProposalRecord> records;
    bool finished = false;
};
Expansion expand_type(const NodeSearch& search, const Program& parent, const std::string& kind, std::int64_t cursor,
                      const std::vector<std::string>& stop_supports, const std::array<double, 4>& initial_bounds,
                      std::int64_t settings_max_inputs, std::int64_t choices_per_type, bool ready, bool ordinary,
                      std::set<std::string> seen, const std::function<void()>& budget);

}  // namespace pkc
