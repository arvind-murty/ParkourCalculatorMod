#include "pkc/search/dfs_search.hpp"

#include <algorithm>
#include <stdexcept>
#include <tuple>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/numerics/disk.hpp"
#include "pkc/scene/static_collision.hpp"
#include "pkc/observability/turns.hpp"
#include "pkc/search/bounds/remaining.hpp"
#include "pkc/work.hpp"
#include "pkc/search/comparison.hpp"
#include "pkc/search/dfs.hpp"
#include "pkc/search/engine.hpp"
#include "pkc/search/feasibility.hpp"
#include "pkc/search/frontier.hpp"
#include "pkc/search/goals.hpp"
#include "pkc/search/ordered_route.hpp"

namespace pkc {
namespace {

// FRAME_END_SECONDS: frame-end re-solves have no deadline (user, 2026-11-23); this only bounds a hung solve, and a
// work-only run lifts it.
const double kFrameEndSeconds = 600.;

// num(value) for a budget in the evaluate args, which is infinite in a work-only run (num rejects infinity).
double budget(const Value& value) {
    if (const auto* seconds = std::get_if<double>(&value.data); seconds && *seconds == INFINITY) return INFINITY;
    return num(value).value;
}

Value set_key(const Value& base, std::string_view key, Value value) {
    Object out = fields(base);
    put(out, key, std::move(value));
    return object(std::move(out));
}

Value without(const Value& base, std::string_view key) {
    Object out = fields(base);
    erase(out, key);
    return object(std::move(out));
}

// program[key] = value on a program more than one place holds.
void annotate(const SharedValue& program, std::string_view key, Value value) {
    *program = set_key(*program, key, std::move(value));
}

const Object& profile_of(const Value& program) { return fields(at(fields(program), "profile")); }
std::int64_t duration_of(const Value& program) { return as_int(at(profile_of(program), "duration")); }

// f['goal'].get('blocks', [f['goal']['block']])
std::vector<std::string> goal_blocks(const Value& goal) {
    std::vector<std::string> out;
    const Object& g = fields(goal);
    if (const Value* blocks = get(g, "blocks")) {
        for (const Value& b : items(*blocks)) out.push_back(as_text(b));
    } else {
        out.push_back(as_text(at(g, "block")));
    }
    return out;
}

bool lands_in(const Value& program, const std::vector<std::string>& blocks) {
    const Value& support = get_or_null(fields(program), "terminalSupport");
    if (!std::holds_alternative<std::string>(support.data)) return false;
    return std::find(blocks.begin(), blocks.end(), as_text(support)) != blocks.end();
}

// tuple(a)[:n] < tuple(b)[:n] in Python's order.
int compare_prefix(const Value& a, const Value& b, std::size_t n) {
    const Array &l = items(a), &r = items(b);
    return py_compare(array(Array(l.begin(), l.begin() + std::min(n, l.size()))),
                      array(Array(r.begin(), r.begin() + std::min(n, r.size()))));
}

Value frame_parent(const Nullable<FrameParent>& parent) {
    if (!parent.value) return Value{nullptr};
    return object({{"frame", integer_value(parent.value->frame)}, {"candidate", text(parent.value->candidate)}});
}

Value witness_positions(const Value& best) { return at(fields(at(fields(best), "candidate")), "positions"); }

// sum(timings.values()) from the int 0, keeping Python's int/float kinds.
Num sum_values(const Value& timings) {
    std::vector<double> reals;
    std::int64_t whole_sum = 0;
    bool integral = true;
    for (const auto& [_, v] : fields(timings)) {
        const Num n = num(v);
        if (integral && n.integral) {
            whole_sum += static_cast<std::int64_t>(n.value);
            continue;
        }
        if (integral) reals.push_back(static_cast<double>(whole_sum));
        integral = false;
        reals.push_back(n.value);
    }
    if (integral) return whole(whole_sum);
    return flt(python_sum(reals));
}

}  // namespace

DfsSearch::DfsSearch(DfsState state, DfsOptions options, DfsHooks hooks, std::shared_ptr<InteractionGate> gate,
                     std::shared_ptr<AnchorScene> anchors, std::shared_ptr<AnchorAdapter> adapter)
    : state_(std::move(state)),
      options_(std::move(options)),
      hooks_(std::move(hooks)),
      gate_(std::move(gate)),
      anchors_(std::move(anchors)),
      adapter_(std::move(adapter)) {
    for (const Value& name : items(at(fields(options_.anchor_policy), "anchors"))) anchor_names_.push_back(as_text(name));
    for (const auto& r : state_.minimum_run_exclusions) excluded_runs_.insert({r.frame, r.candidate});
}

Value DfsSearch::latches_value() const {
    Object latches;
    for (const auto& [key, latch] : state_.hard_latches.items) latches.emplace_back(key, to_value(latch));
    return object(std::move(latches));
}

Value DfsSearch::state() const { return to_value(state_); }

Frame& DfsSearch::current() {
    if (scoring_) return *scoring_;
    if (state_.stack.empty()) fail("no frame is active");
    return *state_.stack.back();
}

// ---- progress

Snapshot DfsSearch::snapshot(RepairTargets repair) const {
    return std::make_shared<const DfsSnapshot>(DfsSnapshot{to_value(state_), std::move(repair)});
}

// Every path at which a frame sits: Python's state holds one frame dict in several lists.
std::vector<Array> DfsSearch::frame_paths(const Frame& f) const {
    std::vector<Array> out;
    const std::pair<const char*, const std::vector<FramePtr>*> groups[] = {
        {"stack", &state_.stack}, {"suspended", &state_.suspended}, {"frames", &state_.frames},
        {"goalQueue", &state_.goal_queue}};
    for (const auto& [group, frames] : groups)
        for (std::size_t i = 0; i < frames->size(); ++i)
            if ((*frames)[i].get() == &f) out.push_back({text(group), integer_value(static_cast<std::int64_t>(i))});
    return out;
}

// Every path at which a program sits, as SearchCheckpoint's identity search finds it.
std::vector<Array> DfsSearch::program_paths(const SharedValue& program) const {
    std::vector<Array> out;
    const auto at_path = [&](Array base, std::initializer_list<Value> rest) {
        base.insert(base.end(), rest.begin(), rest.end());
        out.push_back(std::move(base));
    };
    const auto record = [&](const Array& base, const Record& r) {
        if (r.program == program) at_path(base, {text("program")});
        if (r.child && *r.child == program) at_path(base, {text("child")});
    };
    const auto frame = [&](const Array& base, const Frame& f) {
        if (f.in_flight.value && f.in_flight.value->program == program) at_path(base, {text("inFlight"), text("program")});
        for (std::size_t k = 0; k < f.ready.size(); ++k)
            if (f.ready[k] == program) at_path(base, {text("ready"), integer_value(static_cast<std::int64_t>(k))});
        for (std::size_t j = 0; j < f.records.size(); ++j) {
            Array path = base;
            path.push_back(text("records"));
            path.push_back(integer_value(static_cast<std::int64_t>(j)));
            record(path, *f.records[j]);
        }
    };
    const std::pair<const char*, const std::vector<FramePtr>*> groups[] = {
        {"stack", &state_.stack}, {"suspended", &state_.suspended}, {"frames", &state_.frames},
        {"goalQueue", &state_.goal_queue}};
    for (const auto& [group, frames] : groups)
        for (std::size_t i = 0; i < frames->size(); ++i)
            frame({text(group), integer_value(static_cast<std::int64_t>(i))}, *(*frames)[i]);
    for (std::size_t j = 0; j < state_.final_attempts.size(); ++j)
        record({text("finalAttempts"), integer_value(static_cast<std::int64_t>(j))}, *state_.final_attempts[j]);
    if (state_.best.value && state_.best.value->movement_program == program)
        out.push_back({text("best"), text("movementProgram")});
    return out;
}

// A full save's targets without a repair: the stack top's in-flight program, as the saver's default.
RepairTargets DfsSearch::active_targets() {
    RepairTargets targets;
    targets.token = repair_.token + 1;
    repair_program_ = nullptr;
    repair_node_ = false;
    if (!state_.stack.empty() && state_.stack.back()->in_flight.value) {
        repair_program_ = state_.stack.back()->in_flight.value->program;
        targets.program = program_paths(repair_program_);
    }
    repair_ = targets;
    return targets;
}

void DfsSearch::publish() {
    state_.cumulative_calls = options_.prior_calls + hooks_.solver_calls();
    if (!hooks_.progress) return;
    const Snapshot state = snapshot(active_targets());
    if (hooks_.saver)
        hooks_.saver->save(state->state, state->repair);
    else
        hooks_.publish(state);
}

// `program` is the anchor-leg program the targets follow, or null for a decision check's node program.
void DfsSearch::begin_repair(RepairTargets targets, const SharedValue& program) {
    state_.cumulative_calls = options_.prior_calls + hooks_.solver_calls();
    if (!hooks_.progress) return;
    targets.token = repair_.token + 1;
    repair_ = targets;
    repair_program_ = program;
    repair_node_ = !program;
    const Snapshot state = snapshot(std::move(targets));
    if (hooks_.saver)
        hooks_.saver->save(state->state, state->repair);
    else
        hooks_.begin_repair(state);
}

void DfsSearch::report_repair(const Snapshot& state) {
    if (hooks_.saver)
        hooks_.saver->progress(state->state, state->repair);
    else
        hooks_.repair_progress(state);
}

void DfsSearch::repair_progress() {
    state_.cumulative_calls = options_.prior_calls + hooks_.solver_calls();
    if (!hooks_.progress) return;
    // The saver patches its last full save only while that save followed a program the active anchor-leg solve (if
    // any) still runs; otherwise it saves in full, as a defensive fallback.
    const InFlight* active = state_.stack.empty() || !state_.stack.back()->in_flight.value
                                 ? nullptr
                                 : &*state_.stack.back()->in_flight.value;
    const bool following = repair_program_ || repair_node_;
    if (following && !(active && (repair_node_ || active->program != repair_program_))) {
        RepairTargets targets = repair_;
        targets.patch = true;
        report_repair(snapshot(std::move(targets)));
    } else {
        report_repair(snapshot(active_targets()));
    }
}

void DfsSearch::transition(const std::string& kind, const Frame& f, Object fields_) {
    state_.transitions.push_back(
        Transition{kind, f.id, f.index, state_.session, flt(hooks_.monotonic() - options_.started), std::move(fields_)});
    // A typed arrival's tried flag and child execution owner are one checkpoint transaction. The child's
    // open/backtrack publishes both.
    if (kind != "choose" && kind != "frame_end_confirmed") publish();
}

// ---- frames and discovery

DfsSearch::FramePtr DfsSearch::frame(std::int64_t index, const std::vector<Value>& parents,
                                     const Nullable<FrameParent>& parent, const std::optional<Value>& cursor_in) {
    Array roots(parents.begin(), parents.end());
    const Value cursor = cursor_in ? *cursor_in : adapter_->initial_cursor(roots);
    Array keys;
    for (const Value& p : parents) keys.push_back(text(candidate_key(p)));
    const std::string owner = digest(object({{"parents", array(std::move(keys))}, {"cursor", cursor},
                                             {"physics", options_.motion}, {"domain", options_.domain_id}}));
    if (state_.goal_owners.contains(owner)) return nullptr;
    state_.discovery[owner] = Discovery{owner, index, parents, parent, cursor, Value{nullptr}, false};
    state_.goal_owners[owner] = {};
    if (parent.value) state_.pending_goal_owner = owner;
    expand_discovery(*state_.discovery.find(owner));
    const auto ready = std::find_if(state_.goal_queue.begin(), state_.goal_queue.end(),
                                    [&](const FramePtr& f) { return f->owner == owner; });
    if (ready != state_.goal_queue.end()) {
        FramePtr f = *ready;
        state_.goal_queue.erase(ready);
        state_.stack.push_back(f);
        if (state_.pending_goal_owner == owner) state_.pending_goal_owner.reset();
        transition("open", *f, {{"goal", f->goal}});
        return f;
    }
    publish();
    return nullptr;
}

DfsSearch::FramePtr DfsSearch::make_frame(std::int64_t index, const std::vector<Value>& parents,
                                          const Nullable<FrameParent>& parent, const Value& goal,
                                          const Value& goal_request, const Value& semantic_cursor,
                                          const std::string& owner) {
    auto f = std::make_shared<Frame>();
    f->id = state_.next_frame;
    f->index = index;
    f->parents = parents;
    f->parent = parent;
    f->goal = goal;
    f->phase = "generate";
    f->generation = {};
    f->in_flight = {};
    f->goal_request = goal_request;
    f->semantic_cursor = semantic_cursor;
    f->owner = owner;
    ++state_.next_frame;
    return f;
}

void DfsSearch::expand_discovery(Discovery& discovery) {
    DiscoveryWork& used = *state_.discovery_work;
    const Value reply = adapter_->next_goals(
        discovery.cursor, Array(discovery.parents.begin(), discovery.parents.end()), discovery.continuation,
        hooks_.check);
    const Object& r = fields(reply);
    const Object& report = fields(at(r, "report"));
    used.expansions += as_int(at(report, "expansions"));
    used.materializations += as_int(at(report, "materializations"));
    discovery.continuation = at(r, "continuation");
    discovery.exhausted = py_truthy(at(fields(discovery.continuation), "exhausted"));
    for (const Value& request_value : items(at(r, "requests"))) {
        const Object& request = fields(request_value);
        if (const Value* alias = get(request, "localSetupAlias"); alias && py_truthy(*alias)) {
            // Current-support hops/runs remain in the movement source for every next goal. They do not need an
            // extra comparison frame.
            if (!state_.local_setup_aliases) state_.local_setup_aliases.emplace();
            state_.local_setup_aliases->push_back(LocalSetupAlias{discovery.owner, at(request, "id")});
            continue;
        }
        FramePtr f = make_frame(discovery.index, discovery.parents, discovery.parent, at(request, "goal"),
                                request_value, discovery.cursor, discovery.owner);
        state_.goal_owners[discovery.owner].push_back(f->id);
        state_.goal_queue.push_back(f);
    }
}

// ---- stage views and spans

const DfsSearch::Stage& DfsSearch::stage(Frame& f) {
    auto found = stages_.find(f.id);
    if (found == stages_.end()) {
        const Value facts_value = adapter_->stage(f.goal_request, f.semantic_cursor);
        const Object& facts = fields(facts_value);
        Stage s{at(facts, "guide"), at(facts, "scoreContext"), at(facts, "cursor"), at(facts, "objective"),
                at(facts, "corridor")};
        found = stages_.emplace(f.id, std::move(s)).first;
        f.guide = found->second.guide;
        f.score_context_key = found->second.score_context;
    }
    return found->second;
}

bool DfsSearch::final_frame(const Frame& f) const { return py_truthy(at(fields(f.goal_request), "terminal")); }

Value DfsSearch::guide_objective(Frame& f) {
    // The stage's progress objective; a final frame keeps the scene objective (None).
    if (final_frame(f)) return Value{nullptr};
    return stage(f).objective;
}

std::pair<std::int64_t, std::int64_t> DfsSearch::span_info(Frame& f) {
    const Object& cursor = fields(stage(f).cursor);
    return {as_int(at(cursor, "anchorIndex")), as_int(at(cursor, "spanStart"))};
}

// A frame searches one encounter group (pairs first..last); per-pair rules use the program's own pair, capped at
// the group's last pair.
std::tuple<std::int64_t, std::int64_t, std::int64_t> DfsSearch::group_span(Frame& f) {
    const Object& cursor = fields(stage(f).cursor);
    return {as_int(at(cursor, "anchorIndex")), as_int(at(cursor, "groupEnd")), as_int(at(cursor, "spanStart"))};
}

Value DfsSearch::priority(const Value& program, Frame& f, const std::string& identity) {
    const Value& turnaround = get_or_null(fields(program), "priorityTurnaround");
    return priority_key(comparison_label(program_from_value(program), span_info(f).second, std::nullopt, identity,
                                         py_truthy(turnaround) ? "turnaround" : "ordinary", {}, {}));
}

Value DfsSearch::record_priority(const Record& r, Frame& f) {
    return priority(r.child ? **r.child : *r.program, f, r.id);
}

std::string DfsSearch::pair_key(std::int64_t k) const {
    const Array& names = items(at(fields(options_.guide_plan), "anchors"));
    return as_text(names.at(k)) + "→" + as_text(names.at(k + 1));
}

Audit DfsSearch::visited(const Program& program) const { return anchors_->audit(program, anchor_names_); }

std::int64_t DfsSearch::pair_of(const Program& program, Frame& f) {
    return std::min(visited(program).reached, std::get<1>(group_span(f)));
}

bool DfsSearch::group_latched(Frame& f) {
    const auto [first, last, start] = group_span(f);
    for (std::int64_t k = first; k <= last; ++k)
        if (!state_.hard_latches.contains(pair_key(k))) return false;
    return true;
}

Value DfsSearch::pair_transfer(const Program& program, Frame& f, std::int64_t k, const Audit& arrivals, bool complete) {
    const auto [first, last, start] = group_span(f);
    const Array& names = items(at(fields(options_.guide_plan), "anchors"));
    return anchors_->transfer_structure(program, k == first ? start : arrivals.ticks.at(k - 1), as_text(names.at(k)),
                                        as_text(names.at(k + 1)), complete, k < last);
}

// The frame and state a native screen reads; `context_id` gets the frame's comparisonContextId, or the one
// tick_label would set.
NodeSearch DfsSearch::node_search(Frame& f, Value* context_id, const Value& policy_context) {
    const Stage& view = stage(f);
    const auto [span, start] = span_info(f);
    NodeSearch search;
    search.gate = gate_.get();
    search.anchors = anchors_.get();
    NodeFrame& n = search.frame;
    n.scene = set_key(options_.scene, "goalBlock", at(fields(f.goal), "block"));
    n.frame_id = integer_value(f.id);
    n.goal_kind = as_text(at(fields(f.goal), "kind"));
    n.goal_block = as_text(at(fields(f.goal), "block"));
    n.goal_blocks = goal_blocks(f.goal);
    const Object& cursor = fields(view.cursor);
    n.anchor_index = as_int(at(cursor, "anchorIndex"));
    n.group_end = as_int(at(cursor, "groupEnd"));
    n.span_start = as_int(at(cursor, "spanStart"));
    if (f.comparison_context_id) {
        n.comparison_context_id = text(*f.comparison_context_id);
    } else {
        Array roots;
        for (const Value& p : f.parents) roots.push_back(text(candidate_key(p)));
        n.comparison_context_id = text(digest(object({{"roots", array(std::move(roots))}, {"span", integer_value(span)},
                                                      {"start", integer_value(start)}, {"goal", f.goal},
                                                      {"score", view.score_context}})));
    }
    n.comparison_context_set = f.comparison_context_id.has_value();
    n.tick_peers = f.tick_peers ? *f.tick_peers : object({});
    n.tick_peer_revision = f.tick_peer_revision.value_or(0);
    if (state_.best.value) {
        const Value best = to_value(*state_.best.value);
        const Object& b = fields(best);
        const Value* T = get(b, "T");
        const Value* plan = get(b, "plan");
        n.incumbent = object({{"T", T ? *T : plan ? get_or_null(fields(*plan), "ticks") : Value{nullptr}}});
    }
    n.hard_latches = latches_value();
    n.transfers = state_.transfer_time_bounds ? *state_.transfer_time_bounds : object({});
    n.latch_revision = state_.latch_revision;
    n.minimum_run_pruning = options_.minimum_run_pruning;
    n.guide_plan = options_.guide_plan;
    n.motion = options_.motion;
    n.turnaround_margin = at(fields(at(fields(options_.anchor_policy), "turnaround")), "spatialMargin");
    n.anchors = anchor_names_;
    n.task_block = options_.task_block;
    n.request = options_.caps;
    n.max_inputs = as_int(at(fields(options_.cfg), "maxInputs"));
    for (const Value& t : items(at(fields(options_.cfg), "types"))) n.types.push_back(as_text(t));
    n.policy_context = policy_context;
    for (const auto& [frame_id, candidate] : excluded_runs_)
        if (frame_id == f.id) n.excluded_runs.insert(candidate);
    if (context_id) *context_id = n.comparison_context_id;
    return search;
}

// The DFS state changes of native screens and eligibility, in call order.
void DfsSearch::apply(const NodeEffects& effects, Frame& f, const Value& context_id) {
    for (const Value& e : effects.time_bound_exclusions) {
        const Object& row = fields(e);
        state_.time_bound_exclusions.push_back(TimeBoundExclusion{as_text(at(row, "id")), at(row, "proof")});
    }
    if (effects.comparison_context_used) {
        if (!f.comparison_context_id) f.comparison_context_id = as_text(context_id);
        if (!state_.comparison_work) state_.comparison_work.emplace();
        state_.comparison_work->assessments += effects.assessments;
        state_.comparison_work->cache_hits += effects.cache_hits;
        state_.comparison_work->peer_checks += effects.peer_checks;
    }
    for (const Value& pair : effects.comparison_decisions) {
        if (!state_.comparison_decisions) state_.comparison_decisions.emplace();
        (*state_.comparison_decisions)[as_text(items(pair).at(0))] = items(pair).at(1);
    }
    for (const Value& e : effects.minimum_run_exclusions) {
        MinimumRunExclusion exclusion = minimum_run_exclusion_from_value(e);
        excluded_runs_.insert({f.id, exclusion.candidate});
        state_.minimum_run_exclusions.push_back(std::move(exclusion));
    }
    viability_checked_ += effects.viability_checked;
    viability_excluded_ += effects.viability_excluded;
}

// ---- bounds and eligibility

Value DfsSearch::hard_bound(const Value& program, Frame& f) {
    return node_search(f, nullptr).hard_bound(program_from_value(program));
}

std::string DfsSearch::score_key(const Value& program, Frame& f) {
    return digest(array({text(candidate_key(program)), stage(f).score_context}));
}

Value DfsSearch::time_bound(const Value& program, Frame& f) {
    if (!state_.best.value) return Value{nullptr};
    const Value decision = compare_candidates(object({{"T", integer_value(duration_of(program))}}), Value{nullptr},
                                              Object{}, to_value(*state_.best.value));
    if (as_text(at(fields(decision), "action")) != "exclude_bound") {
        // The remaining-finish bound reads the transfer bounds, filled on first use.
        state_.transfer_time_bounds = hooks_.transfer_bounds(
            state_.transfer_time_bounds ? *state_.transfer_time_bounds : object({}));
    }
    return node_search(f, nullptr).time_bound(program_from_value(program));
}

Value DfsSearch::tick_label(const Value& program, Frame& f, const std::string& identity, bool validated) {
    const auto [span, start] = span_info(f);
    if (!f.comparison_context_id) {
        Array roots;
        for (const Value& p : f.parents) roots.push_back(text(candidate_key(p)));
        f.comparison_context_id = digest(object({{"roots", array(std::move(roots))}, {"span", integer_value(span)},
                                                 {"start", integer_value(start)}, {"goal", f.goal},
                                                 {"score", stage(f).score_context}}));
    }
    const Program typed = program_from_value(program);
    const bool complete = lands_in(program, goal_blocks(f.goal)) && typed.profile.grounded.back();
    const Value& turnaround = get_or_null(fields(program), "priorityTurnaround");
    return comparison_label(
        typed, start, std::nullopt, identity.empty() ? candidate_key(program) : identity,
        py_truthy(turnaround) ? "turnaround" : "ordinary",
        {{"comparisonContext", text(*f.comparison_context_id)},
         {"basis", text(complete ? "anchor_arrival" : "open_prefix")},
         {"scope", get_or_null(fields(program), "solveScope")},
         {"milestone", at(fields(f.goal), "block")},
         {"landingPhase", text(typed.profile.landing_phase.empty() ? "pre" : typed.profile.landing_phase)}},
        {{"validated", boolean(validated)}});
}

Value DfsSearch::run_bound(const SharedValue& program, Frame& f) {
    if (!options_.minimum_run_pruning) return Value{nullptr};
    Value context_id;
    NodeSearch search = node_search(f, &context_id);
    Program typed = program_from_value(*program);
    NodeEffects effects;
    const Value proof = search.run_bound(typed, effects);
    if (effects.comparison_context_used) *program = to_value(typed);  // its comparison cache token and decision
    apply(effects, f, context_id);
    return proof;
}

Value DfsSearch::reject_time(const Value& program, Frame& f) {
    const Value proof = time_bound(program, f);
    if (!is_null(proof)) state_.time_bound_exclusions.push_back(TimeBoundExclusion{candidate_key(program), proof});
    return proof;
}

bool DfsSearch::eligible(const SharedValue& program, Frame& f) {
    if (!is_null(reject_time(*program, f))) return false;
    if (py_truthy(hard_bound(*program, f)) || !is_null(run_bound(program, f))) return false;
    return node_search(f, nullptr).admitted(program_from_value(*program));
}

bool DfsSearch::numerical_pending(const Record& r) const {
    const Value& repair_value = get_or_null(fields(*r.program), "repairState");
    if (!is_object(repair_value)) return false;
    const Object& repair = fields(repair_value);
    if (py_truthy(get_or_null(repair, "pending")) || py_truthy(get_or_null(repair, "inFlight"))) return true;
    const Value& generation = get_or_null(repair, "constraintGeneration");
    return py_truthy(generation) && !py_truthy(get_or_null(fields(generation), "exhausted"));
}

// ---- generation

void DfsSearch::generate(Frame& f) {
    const Value target_scene = set_key(options_.scene, "goalBlock", at(fields(f.goal), "block"));
    const auto expanded_of = [&f] {
        const Continuation* continuation = f.generation.continuation();
        return continuation ? continuation->expanded : 0;
    };
    const std::int64_t before = expanded_of();
    const Object& cfg = fields(options_.cfg);
    const Value settings = set_key(
        options_.cfg, "maxPrefixes",
        integer_value(std::max<std::int64_t>(0, as_int(at(cfg, "maxPrefixes")) -
                                                    (state_.expanded - options_.initial_expanded))));
    const std::int64_t span = span_info(f).first;
    const Object& initial = fields(at(fields(options_.scene), "initial"));
    const auto every_other = [](const Value& v) {
        Array out;
        const Array& all = items(v);
        for (std::size_t i = 0; i < all.size(); i += 2) out.push_back(all[i]);
        return array(std::move(out));
    };
    for (Value& root : f.parents) {
        Object r = fields(root);
        const Value* saved = get(r, "decisionPoint");
        const Value point = saved ? *saved : every_other(at(initial, "position"));
        setdefault(r, "decisionPoint", point);
        setdefault(r, "decisionVelocity", every_other(at(initial, "velocity")));
        const std::int64_t n = as_int(at(fields(at(r, "profile")), "duration"));
        setdefault(r, "decisionPointTick", integer_value(n));
        setdefault(r, "decisionPointSource", text(n ? "validated_anchor" : "original_initial"));
        put(r, "decisionTurnaround", at(fields(classify(options_.guide_plan, span, point)), "isTurnaround"));
        root = object(std::move(r));
    }
    const Value policy_context = object({{"anchor", options_.policy_settings},
                                         {"span", integer_value(span)},
                                         {"guide", at(fields(options_.guide_plan), "id")},
                                         {"score", stage(f).score_context}});
    FrontierOptions frontier_options;
    frontier_options.scene = target_scene;
    frontier_options.settings = settings;
    frontier_options.initial_programs = f.parents;
    if (get(fields(f.goal), "blocks")) frontier_options.stop_supports = goal_blocks(f.goal);
    frontier_options.policy_context = policy_context;
    frontier_options.comparison_policy = options_.comparison_policy;
    FrontierHooks h;
    h.budget = [this] {
        if (hooks_.cancelled()) throw Timeout("cancelled");
        hooks_.check();
        if (hooks_.monotonic() >= options_.deadline) throw Timeout("deadline");
    };
    h.priority = [this, &f](const Value& p, const std::string& identity) { return priority(p, f, identity); };
    h.eligible = [this, &f](Value& p) {
        auto shared = std::make_shared<Value>(p);
        const bool ok = eligible(shared, f);
        p = *shared;  // run_bound's notes on the frontier's program
        return ok;
    };
    h.live_programs = [this, &f] { return live_programs(f); };
    h.evaluate_node = [this, &f](Value& p, OrderedMap<Value>& nodes) { return evaluate_node(p, nodes, f); };
    h.node_search = [this, &f, &policy_context](Value& context_id) {
        if (state_.best.value)
            state_.transfer_time_bounds = hooks_.transfer_bounds(
                state_.transfer_time_bounds ? *state_.transfer_time_bounds : object({}));
        return node_search(f, &context_id, policy_context);
    };
    h.apply = [this, &f](const NodeEffects& effects, const Value& context_id) { apply(effects, f, context_id); };
    h.ordinary = [](const Value& p) {
        return boundary_state(program_from_value(p)).on_ground &&
               !py_truthy(get_or_null(fields(p), "decisionTurnaround"));
    };
    h.termination = [this] { return hooks_.termination(); };
    std::set<std::string> seen;
    for (const RecordPtr& r : f.records) seen.insert(r->candidate_key.empty() ? r->id : r->candidate_key);
    for (const SharedValue& p : f.ready) seen.insert(candidate_key(*p));
    Frontier source(f.generation, std::move(frontier_options), std::move(h));
    try {
        while (static_cast<std::int64_t>(f.ready.size()) < options_.batch_size) {
            const std::optional<Value> p = source.next();
            if (!p) {
                const Continuation* continuation = f.generation.continuation();
                f.source_exhausted = continuation && continuation->exhausted;
                break;
            }
            const std::string identity = candidate_key(*p);
            if (!seen.count(identity)) {
                f.ready.push_back(std::make_shared<Value>(*p));
                seen.insert(identity);
                // Score the iterator's first ready arrival immediately. Remaining proposals stay owned by its saved
                // cursor.
                break;
            }
        }
    } catch (...) {
        source.close();
        state_.expanded += expanded_of() - before;
        throw;
    }
    source.close();
    state_.expanded += expanded_of() - before;
    std::vector<std::pair<Value, SharedValue>> keyed;
    for (const SharedValue& p : f.ready) keyed.emplace_back(priority(*p, f), p);
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const auto& a, const auto& b) { return py_compare(a.first, b.first) < 0; });
    for (std::size_t i = 0; i < keyed.size(); ++i) f.ready[i] = keyed[i].second;
    ++f.batches;
    if (f.ready.empty() && !f.source_exhausted) {
        throw Timeout(f.generation.report ? f.generation.report->termination : "generation_suspended");
    }
    f.phase = "score";
    transition("generated_batch", f,
               {{"count", integer_value(static_cast<std::int64_t>(f.ready.size()))}, {"exhausted", boolean(f.source_exhausted)}});
}

// ---- scoring

Object DfsSearch::evaluation_args(Frame& f, const Value& program) {
    stage(f);  // the solve's adapter
    const Value& support = get_or_null(fields(program), "terminalSupport");
    const Value* listed = get(fields(f.goal), "blocks");
    bool in_blocks = false;
    if (listed && std::holds_alternative<std::string>(support.data))
        for (const Value& b : items(*listed)) in_blocks = in_blocks || as_text(b) == as_text(support);
    const Value landing = in_blocks ? support : at(fields(f.goal), "block");
    const bool final = final_frame(f);
    const Value policy = object({{"purpose", text(final ? "terminal" : "anchor")}, {"stopOnFeasible", boolean(false)},
                                 {"rankEqualCost", boolean(true)}, {"oracleRejection", boolean(true)},
                                 {"numerical", text(options_.numerical_backend)}});
    return {{"landing", landing}, {"policy", policy}, {"firstWitness", boolean(!final)},
            {"guideObjective", guide_objective(f)}};
}

Object DfsSearch::node_service(const Value& program, std::int64_t span, Frame& f) {
    const bool latched = group_latched(f);
    const bool turnaround = py_truthy(get_or_null(fields(program), "priorityTurnaround"));
    return {{"latchRevision", integer_value(state_.latch_revision)},
            {"hardLatched", boolean(state_.hard_latches.contains(pair_key(span)))},
            {"runDeferralEnabled", boolean(options_.minimum_run_pruning && !turnaround && latched)},
            {"tickDeferralEnabled", boolean(options_.minimum_run_pruning && latched)},
            {"comparisonPolicy", integer_value(options_.comparison_policy)}};
}

namespace {

QueryService query_service(const Object& s) {
    return {as_int(at(s, "latchRevision")), std::get<bool>(at(s, "hardLatched").data),
            std::get<bool>(at(s, "runDeferralEnabled").data), std::get<bool>(at(s, "tickDeferralEnabled").data),
            as_int(at(s, "comparisonPolicy"))};
}

}  // namespace

Value DfsSearch::evaluate_leg(Frame& f, const Value& program, const Value& args) {
    const Object& a = fields(args);
    const Value& landing = at(a, "landing");
    const double started = hooks_.monotonic();
    const Value policy = policy_spec(at(a, "policy"), py_truthy(at(a, "firstWitness")));
    const Value prepared = hooks_.prepare_leg(program, landing);
    if (is_null(prepared))
        return object({{"status", text("unknown")}, {"termination", text("vertical_schedule")}, {"best", kNull},
                       {"attempts", array({})}, {"contactAttempts", array({})},
                       {"work", object({{"solverCalls", integer_value(0)}})}, {"fullSceneValidated", boolean(false)}});
    Object route = fields(at(fields(prepared), "route"));
    const Value& final_route = get_or_null(a, "finalRoute");
    bool uncovered = false;
    for (const Value* r : {&at(fields(prepared), "route"), &final_route})
        if (!is_null(*r))
            for (const char* key : {"hitEvents", "ladderEvents"})
                uncovered = uncovered || py_truthy(get_or_null(fields(*r), key));
    if (uncovered) return hooks_.evaluate(f.id, f.goal_request, f.semantic_cursor, program, args);
    if (!get(route, "constraints")) put(route, "constraints", array({}));
    const double prepared_at = hooks_.monotonic();
    const Value& guide = get_or_null(a, "guideObjective");
    if (!is_null(guide)) {
        put(route, "objective", object({{"axis", text("X")}, {"sense", text("MAX")}}));
        put(route, "guideObjective", guide);
    }
    const double seconds = budget(at(a, "seconds"));
    const bool own_work = py_truthy(get_or_null(a, "ownWork"));
    NodeSolve solve = hooks_.begin_leg(program, as_text(at(fields(policy), "numerical")), seconds, own_work);
    solve.adapter_identity = stage(f).corridor;
    EvaluateHooks evaluate = solve.hooks;
    evaluate.candidate = adapter_->candidate(program, f.goal_request, f.semantic_cursor, false);
    if (py_truthy(at(a, "progress"))) evaluate.on_progress = [this](const Value& repair) { score_progress(repair); };
    EvaluateOptions eo;
    eo.seconds = seconds;
    const Value* candidate_seconds = get(a, "candidateSeconds");
    eo.candidate_seconds = candidate_seconds ? budget(*candidate_seconds) : options_.candidate_seconds;
    eo.required_landing = landing;
    eo.final_route = final_route;
    eo.policy = policy;
    eo.adapter_identity = solve.adapter_identity;
    eo.climb_families = true;
    eo.timings = {{"programCompilation", real(prepared_at - started)}};
    return evaluate_prepared(object(with(fields(options_.scene), "goalBlock", landing)), program, object(std::move(route)),
                             at(fields(prepared), "profiles"), at(fields(prepared), "vertical"), evaluate, eo);
}

void DfsSearch::score_progress(const Value& repair) {
    Frame& f = current();
    annotate(f.in_flight.value->program, "repairState", repair);
    repair_progress();
}

DfsSearch::RecordPtr DfsSearch::score(Frame& f, const SharedValue& program, const RecordPtr& old) {
    const Value source_program = *program;
    const std::string identity =
        old ? old->id : f.in_flight.value ? f.in_flight.value->id : score_key(*program, f);
    hooks_.count(final_frame(f) ? "terminalEvaluations" : "spatialGoalEvaluations", 1);
    f.in_flight.value = InFlight{identity, program};
    transition("dispatch", f, {{"candidate", text(identity)}, {"goal", f.goal},
                               {"inputs", integer_value(duration_of(*program))}});
    const Stage& active = stage(f);
    begin_repair(RepairTargets{0, false, program_paths(f.in_flight.value->program), {}}, f.in_flight.value->program);
    Object args = evaluation_args(f, *program);
    put(args, "seconds", real(std::max(.001, std::min(options_.score_seconds, options_.deadline - hooks_.monotonic()))));
    put(args, "progress", boolean(hooks_.progress));
    scoring_ = &f;
    Value result_value;
    try {
        result_value = evaluate_leg(f, *program, object(std::move(args)));
    } catch (...) {
        scoring_ = nullptr;
        throw;
    }
    scoring_ = nullptr;
    const Object& result = fields(result_value);
    const Value& repair_result = get_or_null(result, "repairState");
    if (!hooks_.progress && !is_null(repair_result)) annotate(f.in_flight.value->program, "repairState", repair_result);
    const Program typed = program_from_value(*program);
    const std::int64_t span = pair_of(typed, f), start = span_info(f).second;
    const Value* solve_identity = get(result, "solveIdentity");
    const Value model = solve_identity ? *solve_identity : text(identity);
    const Value* feasibility_value = get(result, "nodeFeasibility");
    const Object feasibility = feasibility_value ? fields(*feasibility_value) : Object{};
    const Value* query_status = get(feasibility, "status");
    const Value* timings = get(result, "timings");
    pkc::observe(state_.observations, options_.guide_plan, *program, span, start, model,
            as_text(query_status ? *query_status : at(result, "status")), timings ? sum_values(*timings) : whole(0),
            "anchor", at(result, "work"), query_service(node_service(*program, span, f)));
    const Value& best = at(result, "best");
    if (!is_null(best)) replay_ancestors(*program, witness_positions(best), model);
    const Object& program_fields = fields(*program);
    const Value& decision_node = get_or_null(program_fields, "decisionNode");
    const std::string node_id = py_truthy(decision_node) ? as_text(decision_node) : identity;
    {
        NodeResult node;
        for (const auto& [k, v] : feasibility)
            if (k != "parent" && k != "depth" && k != "kind") node.annotations.emplace_back(k, v);
        node.parent.value.reset();
        if (const Value* parent = get(program_fields, "decisionParent"); parent && !is_null(*parent))
            node.parent.value = as_text(*parent);
        const Value* depth = get(program_fields, "decisionDepth");
        node.depth = depth ? as_int(*depth) : 0;
        node.kind = final_frame(f) ? "terminal" : "anchor";
        state_.node_results[node_id] = std::move(node);
    }
    auto record = std::make_shared<Record>();
    Value p = *program;
    if (!is_null(repair_result)) p = set_key(p, "repairState", repair_result);
    record->id = identity;
    record->program = std::make_shared<Value>(p);
    record->status = as_text(at(result, "status"));
    const Value& termination = at(result, "termination");
    if (!is_null(termination)) record->termination.value = as_text(termination);
    record->node_feasibility = feasibility_value ? *feasibility_value : Value{nullptr};
    record->objective_mode = "configured";
    if (!is_null(best)) record->arrival.value = duration_of(*program);
    if (!is_null(best)) record->value.value = num(at(fields(at(fields(best), "candidate")), "objective"));
    record->work = at(result, "work");
    record->goal = f.goal;
    record->goal_key = py_truthy(f.goal) ? text(goal_key(options_.scene, f.goal, active.corridor)) : Value{nullptr};
    if (old) record->evaluations = old->evaluations;
    record->evaluations.push_back(Evaluation{state_.session, at(result, "work")});
    record->tried = old ? old->tried : false;
    record->solve_scope = get_or_null(result, "solveScope");
    record->solve_identity = get_or_null(result, "solveIdentity");
    {
        const Value* repair = get(result, "repairState");
        if (repair && is_null(*repair)) throw std::runtime_error("AttributeError: 'NoneType' object has no attribute 'get'");
        const Object repair_fields = repair ? fields(*repair) : Object{};
        const Value* attempts = get(repair_fields, "attemptLog");
        const Value* fallback_attempts = get(result, "attempts");
        record->numerical_attempts = attempts ? *attempts : fallback_attempts ? *fallback_attempts : array({});
        const Value* contacts = get(repair_fields, "contactLog");
        const Value* fallback_contacts = get(result, "contactAttempts");
        record->contact_attempts = contacts ? *contacts : fallback_contacts ? *fallback_contacts : array({});
    }
    record->candidate_key = candidate_key(*program);
    if (const Value* restored = get(program_fields, "restoredRun");
        restored && is_object(*restored) && py_equal(get_or_null(fields(*restored), "frame"), integer_value(f.id)) &&
        is_object(record->node_feasibility) &&
        py_equal(get_or_null(fields(record->node_feasibility), "status"), text("oracle_rejected")))
        f.recovery_failure = object({{"reason", text("restored_preparation_oracle_rejected")}, {"candidate", text(identity)}});
    record->score_context_key = active.score_context;
    record->guide_id = at(fields(active.guide), "id");
    record->ticks = duration_of(*program);
    record->complete = final_frame(f);
    if (!is_null(best)) {
        record->witness = best;
        succeed(f, *program);
        Value child = commit_arrival(*program, best, source_program, false);
        Object c = fields(child);
        put(c, "settledThrough", at(profile_of(child), "duration"));
        put(c, "searchParent", text(identity));
        for (const char* k : {"decisionNode", "decisionParent", "decisionDepth", "restoredRun"})
            if (const Value* v = get(program_fields, k)) put(c, k, *v);
        const Object& candidate = fields(at(fields(best), "candidate"));
        put(c, "decisionPoint", items(at(candidate, "positions")).back());
        put(c, "decisionVelocity", items(at(candidate, "velocity")).back());
        put(c, "decisionPointTick", integer_value(duration_of(*program)));
        put(c, "decisionPointSource", text("validated_anchor"));
        const Value* depth = get(program_fields, "decisionDepth");
        put(c, "lastSolvedDepth", depth ? *depth : integer_value(0));
        erase(c, "solveScope");
        record->child = std::make_shared<Value>(object(std::move(c)));
    } else if (old && old->witness) {
        record->witness = old->witness;
        record->child = old->child;
        record->arrival = old->arrival;
        record->value = old->value;
    }
    if (old && old->child && record->child && candidate_key(**old->child) != candidate_key(**record->child))
        record->tried = false;
    if (old) {
        record->refinement_session = old->refinement_session ? *old->refinement_session : Nullable<std::int64_t>{};
        std::replace(f.records.begin(), f.records.end(), old, record);
    } else {
        f.records.push_back(record);
    }
    // Anchor legs keep only their optimized best; close losers wait in repairState as fallbacks.
    const Value* validated = get(result, "fullSceneValidated");
    const bool final_validated = validated ? py_truthy(*validated) : true;
    if (final_frame(f) && !final_validated && record->witness) {
        record->guidance_witness = record->witness;
        record->witness.reset();
        record->child.reset();
        record->status = "unknown";
        record->termination.value = "preliminary_scope_not_a_terminal_witness";
    }
    if (record->witness && final_validated) {
        const auto [first, last, group_start] = group_span(f);
        const Audit arrivals = visited(typed);
        OrderedMap<Value> transfers;
        for (std::int64_t k = first; k <= last; ++k) transfers[pair_key(k)] = pair_transfer(typed, f, k, arrivals, true);
        record->transfer_structure = *transfers.find(pair_key(last));
        if (last > first) {
            Object earlier;
            for (const auto& [k, v] : transfers.items)
                if (k != pair_key(last)) earlier.emplace_back(k, v);
            record->earlier_transfers = object(std::move(earlier));
        }
        for (const auto& [key, transfer] : transfers.items) {
            if (py_truthy(at(fields(transfer), "valid")) && !state_.hard_latches.contains(key)) {
                ++state_.latch_revision;
                state_.hard_latches[key] =
                    HardLatch{as_int(at(fields(transfer), "version")), state_.latch_revision, identity, f.id,
                              get_or_null(result, "solveIdentity"), duration_of(*program), transfer};
                transition("hard_jump_latched", f, {{"span", text(key)}, {"revision", integer_value(state_.latch_revision)}});
            }
        }
    }
    if (options_.minimum_run_pruning && record->witness && final_validated) {
        bool valid = record->transfer_structure && py_truthy(at(fields(*record->transfer_structure), "valid"));
        if (record->earlier_transfers)
            for (const auto& [_, t] : fields(*record->earlier_transfers)) valid = valid && py_truthy(at(fields(t), "valid"));
        if (valid) {
            if (!f.tick_peers) f.tick_peers = object({});
            if (remember_peer(*f.tick_peers, tick_label(**record->child, f, identity, true)))
                f.tick_peer_revision = f.tick_peer_revision.value_or(0) + 1;
        }
    }
    if (record->witness && final_validated && final_frame(f)) {
        const auto existing = std::find_if(state_.final_attempts.begin(), state_.final_attempts.end(),
                                           [&](const RecordPtr& r) { return r->id == identity; });
        if (existing != state_.final_attempts.end()) {
            *existing = record;
        } else {
            state_.final_attempts.push_back(record);
        }
    }
    f.in_flight.value.reset();
    transition("scored", f, {{"candidate", text(identity)}, {"status", text(record->status)},
                             {"arrival", record->arrival.value ? integer_value(*record->arrival.value) : Value{nullptr}}});
    const std::string status = as_text(at(result, "status"));
    if (status == "error" || status == "cancelled") throw Timeout(status);
    return record;
}

void DfsSearch::refine(Frame& f, const RecordPtr& pending) {
    pending->refinement_session = Nullable<std::int64_t>{state_.session};
    const Value preparation = run_bound(pending->program, f);
    if (!is_null(preparation)) {
        pending->preparation_deferral = preparation;
        return;
    }
    const Value proof = reject_time(*pending->program, f);
    if (!is_null(proof)) {
        pending->refinement_time_exclusion = proof;
        transition("candidate_exclusion", f, {{"candidate", text(pending->id)}, {"proof", proof}, {"refinement", boolean(true)}});
        return;
    }
    score(f, pending->program, pending);
}

DfsSearch::RecordPtr DfsSearch::variant_record(Frame& f, const Record& record, const Value& variant) {
    const Value source_program = without(*record.program, "repairState");
    Value best = at(fields(variant), "best");
    if (!is_null(record.solve_scope)) best = set_key(best, "validationScope", record.solve_scope);
    const std::int64_t at_tick = duration_of(source_program);
    Object c = fields(commit_arrival(source_program, best, source_program, false));
    put(c, "settledThrough", integer_value(at_tick));
    const Object& candidate = fields(at(fields(best), "candidate"));
    put(c, "decisionPoint", items(at(candidate, "positions")).at(at_tick));
    put(c, "decisionVelocity", items(at(candidate, "velocity")).at(at_tick));
    put(c, "decisionPointTick", integer_value(at_tick));
    put(c, "decisionPointSource", text("validated_anchor"));
    const Value* depth = get(fields(source_program), "decisionDepth");
    put(c, "lastSolvedDepth", depth ? *depth : integer_value(0));
    const Value child = object(std::move(c));
    const std::string key = candidate_key(child);
    for (const RecordPtr& r : f.records)
        if (r->child && py_truthy(**r->child) && candidate_key(**r->child) == key) return nullptr;
    auto alternate = std::make_shared<Record>(record);
    alternate->id = digest(array({text(record.id), text(key)}));
    alternate->program = std::make_shared<Value>(child);
    alternate->child = std::make_shared<Value>(child);
    alternate->witness = best;
    alternate->tried = false;
    alternate->variant_of = record.id;
    alternate->arrival.value = at_tick;
    alternate->value.value = num(at(candidate, "objective"));
    alternate->restored_fallbacks.reset();
    alternate->frame_end.reset();
    return alternate;
}

bool DfsSearch::restore_fallback(Frame& f, const Value& recovery) {
    if (!f.failed_candidates) return false;
    const std::vector<std::string> failed = *f.failed_candidates;
    const auto by_id = [&](const std::string& id) -> RecordPtr {
        for (const RecordPtr& r : f.records)
            if (r->id == id) return r;
        return nullptr;
    };
    for (const std::string& candidate : failed) {
        RecordPtr original = by_id(candidate);
        while (original && original->variant_of && !original->variant_of->empty()) original = by_id(*original->variant_of);
        if (!original) continue;
        Array fallbacks;
        if (const Value* repair = get(fields(*original->program), "repairState"))
            if (const Value* held = get(fields(*repair), "fallbackVariants")) fallbacks = items(*held);
        while (original->restored_fallbacks.value_or(0) < static_cast<std::int64_t>(fallbacks.size())) {
            const Value variant = fallbacks.at(original->restored_fallbacks.value_or(0));
            original->restored_fallbacks = original->restored_fallbacks.value_or(0) + 1;
            RecordPtr alternate = variant_record(f, *original, variant);
            if (alternate) {
                f.records.push_back(alternate);
                transition("restore_alternative", f, {{"candidate", text(alternate->id)}, {"recovery", recovery}});
                return true;
            }
        }
    }
    return false;
}

void DfsSearch::close_frame(const FramePtr& f) {
    state_.stack.pop_back();
    const auto old = std::find_if(state_.frames.begin(), state_.frames.end(),
                                  [&](const FramePtr& v) { return v->id == f->id; });
    if (old != state_.frames.end()) state_.frames.erase(old);
    state_.frames.push_back(f);
    if (!f->validated_finish.value_or(false)) {
        bool rejected = false;
        for (const RecordPtr& r : f->records)
            rejected = rejected || (is_object(r->node_feasibility) &&
                                    py_equal(get_or_null(fields(r->node_feasibility), "status"), text("oracle_rejected")));
        if (rejected || (f->recovery_failure && py_truthy(*f->recovery_failure))) {
            FramePtr parent;
            if (f->parent.value)
                for (const FramePtr& v : state_.stack)
                    if (v->id == f->parent.value->frame) {
                        parent = v;
                        break;
                    }
            if (parent) {
                parent->recovery_failure =
                    object({{"reason", text("required_descendant_oracle_rejected")}, {"frame", integer_value(f->id)}});
                if (!parent->failed_candidates) parent->failed_candidates.emplace();
                auto& failed = *parent->failed_candidates;
                if (std::find(failed.begin(), failed.end(), f->parent.value->candidate) == failed.end())
                    failed.push_back(f->parent.value->candidate);
            }
        }
    }
    if (std::any_of(f->records.begin(), f->records.end(), [&](const RecordPtr& r) { return numerical_pending(*r); }))
        state_.suspended.push_back(f);
    transition("backtrack", *f, {{"parent", frame_parent(f->parent)}});
}

std::vector<Value> DfsSearch::live_programs(Frame& f) {
    // Frame programs outside the frontier that can still produce a group-end arrival.
    std::vector<Value> out;
    for (const SharedValue& p : f.ready) out.push_back(*p);
    if (f.in_flight.value) out.push_back(*f.in_flight.value->program);
    for (const RecordPtr& r : f.records)
        if (!r->witness && numerical_pending(*r)) out.push_back(*r->program);
    return out;
}

void DfsSearch::succeed(Frame& f, const Value& program) {
    // A validated group-end arrival: its decision node and every ancestor keep their held alternatives.
    Continuation* continuation = f.generation.continuation();
    if (!continuation || !continuation->succeeded_nodes) return;
    std::vector<std::string>& nodes = *continuation->succeeded_nodes;
    std::set<std::string> done(nodes.begin(), nodes.end());
    const Value& first = get_or_null(fields(program), "decisionNode");
    std::optional<std::string> node;
    if (py_truthy(first)) node = as_text(first);
    while (node && !node->empty() && !done.count(*node)) {
        nodes.push_back(*node);
        done.insert(*node);
        const Nullable<std::string>* parent = continuation->decision_parents->find(*node);
        node = parent ? parent->value : std::nullopt;
    }
}

bool DfsSearch::restore_alternatives(Frame& f) {
    Continuation* continuation = f.generation.continuation();
    if (!continuation || !continuation->alternatives || !pkc::restore_alternatives(*continuation, live_programs(f)))
        return false;
    continuation->exhausted = false;
    f.source_exhausted = false;
    return true;
}

// ---- decision checks

Value DfsSearch::evaluate_node(Value& program, OrderedMap<Value>& nodes, Frame& f) {
    // evaluate_node natively (search/dfs.hpp), recording into the frame's continuation and this search's state.
    stage(f);  // the check's adapter
    const std::int64_t span = pair_of(program_from_value(program), f), start = span_info(f).second;
    const std::string identity = as_text(at(fields(program), "decisionNode"));
    Object entry;
    NodeHooks h;
    h.prepare = [&](const Value&) { return hooks_.prepare(program); };
    h.begin = [&] {
        entry = {{"status", text("suspended")},
                 {"parent", get_or_null(fields(program), "decisionParent")},
                 {"repairState", get_or_null(fields(program), "repairState")}};
        nodes[identity] = object(entry);
        RepairTargets targets;
        std::vector<Array> entries;
        for (const Array& path : frame_paths(f)) {
            Array continuation = path;
            for (const char* key : {"generation", "continuation"}) continuation.push_back(text(key));
            Array in_flight = continuation, node = continuation;
            in_flight.push_back(text("nodeInFlight"));
            node.push_back(text("nodes"));
            node.push_back(text(identity));
            targets.program.push_back(std::move(in_flight));
            entries.push_back(std::move(node));
        }
        targets.targets.push_back(std::move(entries));
        begin_repair(std::move(targets), nullptr);
    };
    h.begin_solve = [&] {
        // The frame's node view: for_node(program)'s decision objective and candidate check.
        NodeSolve solve = hooks_.begin_solve(program);
        solve.objective = anchors_->program_objective(program_from_value(program), options_.guide_plan, anchor_names_);
        solve.adapter_identity = stage(f).corridor;
        solve.hooks.candidate = adapter_->candidate(program, f.goal_request, f.semantic_cursor, true);
        return solve;
    };
    h.assign = [&](const Value& repair) { program = set_key(program, "repairState", repair); };
    if (hooks_.progress)
        h.progress = [&](const Value& repair) {
            program = set_key(program, "repairState", repair);
            put(entry, "repairState", repair);
            nodes[identity] = object(entry);
            repair_progress();
        };
    h.settle = [&](const Value& repair) {
        program = set_key(program, "repairState", repair);
        put(entry, "repairState", repair);
        nodes[identity] = object(entry);
    };
    h.observe = [&](const Value& model, const std::string& status, double elapsed, const Value& metrics) {
        pkc::observe(state_.observations, options_.guide_plan, program, span, start, model, status, Num{elapsed, false},
                     "node", metrics, query_service(node_service(program, span, f)));
    };
    h.replay = [&](const Value& positions, const Value& model) { replay_ancestors(program, positions, model); };
    h.store = [&](const std::string& key, const Value& record, const Value& node_result, const Value& recovery) {
        if (!is_null(recovery)) f.recovery_failure = recovery;
        nodes[key] = record;
        NodeResult node;
        for (const auto& [k, v] : fields(node_result)) {
            if (k == "parent") {
                if (!is_null(v)) node.parent.value = as_text(v);
            } else if (k == "depth") {
                node.depth = as_int(v);
            } else if (k == "kind") {
                node.kind = as_text(v);
            } else {
                node.annotations.emplace_back(k, v);
            }
        }
        state_.node_results[key] = std::move(node);
        publish();
    };
    h.solve_route = [&](const Value& p, const std::function<void(const Value&)>& progress) {
        return hooks_.solve_route(f.id, f.goal_request, f.semantic_cursor, p, progress);
    };
    NodeOptions o;
    const Value* saved = nodes.find(identity);
    o.saved = saved ? *saved : Value{nullptr};
    o.guide_plan = options_.guide_plan;
    o.span = span;
    o.solve_interval = as_int(at(fields(options_.anchor_policy), "solveEveryDecisionLevels"));
    o.numerical = options_.numerical_backend;
    o.frame = integer_value(f.id);
    o.candidate_seconds = options_.candidate_seconds;
    // solve_route covers the routes the native node solve leaves to Python.
    const NodeEvaluation evaluated = *pkc::evaluate_node(options_.scene, program, h, o);
    return evaluated.input_program ? set_key(evaluated.result, "program", program) : evaluated.result;
}

void DfsSearch::replay_ancestors(const Value& program, const Value& positions, const Value& model) {
    Value ancestor = get_or_null(fields(program), "decisionParent");
    while (py_truthy(ancestor)) {
        const ObservationDecision* event = state_.observations.decisions.find(as_text(ancestor));
        if (!event) break;
        const std::int64_t span = event->span;
        const Value point = items(positions).at(event->point_tick);
        const Value parent = event->parent.value ? text(*event->parent.value) : Value{nullptr};
        pkc::replay(state_.observations, options_.guide_plan, as_text(ancestor), span, point, model);
        ancestor = parent;
    }
}

// ---- frame ends and the main loop

bool DfsSearch::frame_end(Frame& f, Record& chosen) {
    // Re-solve a chosen relaxed witness at full strength before the search commits to it (user, 2026-11-23): true with
    // the record holding the answer, false on failure, when the frame takes its next option. Outside the run's deadline
    // and call budget: the solve runs on its own work, bounded only against a hung solve.
    const Value program = without(*chosen.program, "repairState");
    Object args = evaluation_args(f, program);
    put(args, "policy", object(with(with(fields(at(args, "policy")), "rankEqualCost", boolean(false)), "numerical",
                                    text("full_strength"))));
    const double seconds = work_only() ? INFINITY : kFrameEndSeconds;
    put(args, "seconds", real(seconds));
    put(args, "candidateSeconds", real(seconds));
    put(args, "progress", boolean(false));
    put(args, "ownWork", boolean(true));
    put(args, "finalRoute", at(fields(*chosen.witness), "route"));
    const double began = hooks_.monotonic();
    const Value result_value = evaluate_leg(f, program, object(std::move(args)));
    const Object& result = fields(result_value);
    if (as_text(at(result, "status")) == "cancelled" || hooks_.cancelled()) {
        chosen.tried = false;  // untried again, so a resume repeats this solve
        throw Timeout("cancelled");
    }
    Array attempts;
    if (const Value* listed = get(result, "attempts"))
        for (const Value& a : items(*listed))
            if (const Value* diagnostics = get(fields(a), "diagnostics"))
                for (const Value& d : items(*diagnostics))
                    if (const Value* record = get(fields(d), "frameEnd")) attempts.push_back(*record);
    const Value* launches = get(fields(at(result, "work")), "workerLaunches");
    const std::int64_t ipopt_calls = launches ? as_int(*launches) : 0;
    hooks_.count("frameEnds", 1);
    hooks_.count("frameEndIpoptCalls", ipopt_calls);
    Object record{{"termination", at(result, "termination")}, {"attempts", array(std::move(attempts))},
                  {"ipoptCalls", integer_value(ipopt_calls)}, {"seconds", real(hooks_.monotonic() - began)}};
    const Value& best = at(result, "best");
    if (is_null(best) || (final_frame(f) && !py_truthy(get_or_null(result, "fullSceneValidated")))) {
        chosen.frame_end = object(with(record, "status", text("failed")));
        hooks_.count("frameEndRejections", 1);
        transition("frame_end_rejected", f, {{"candidate", text(chosen.id)}, {"termination", at(result, "termination")}});
        return false;
    }
    Object child = fields(commit_arrival(program, best, program, false));
    // The search fields score and variant_record set on the relaxed witness's child.
    const Object& relaxed_child = fields(**chosen.child);
    for (const char* k : {"settledThrough", "searchParent", "decisionNode", "decisionParent", "decisionDepth",
                          "restoredRun", "decisionPointTick", "decisionPointSource", "lastSolvedDepth"})
        if (const Value* v = get(relaxed_child, k)) put(child, k, *v);
    erase(child, "solveScope");
    const std::int64_t tick = as_int(at(child, "decisionPointTick"));
    const Object& candidate = fields(at(fields(best), "candidate"));
    put(child, "decisionPoint", items(at(candidate, "positions")).at(tick));
    put(child, "decisionVelocity", items(at(candidate, "velocity")).at(tick));
    chosen.witness = best;
    chosen.child = std::make_shared<Value>(object(std::move(child)));
    chosen.value.value = num(at(candidate, "objective"));
    chosen.frame_end = object(with(record, "status", text("confirmed")));
    transition("frame_end_confirmed", f, {{"candidate", text(chosen.id)}, {"objective", at(candidate, "objective")}});
    return true;
}

Value DfsSearch::witness_rank(const Record& record) const {
    // Ticks, then the objective the frame's witnesses optimize: a guide objective at an anchor.
    const Object& route = fields(at(fields(*record.witness), "route"));
    const Value* guide = get(route, "guideObjective");
    return candidate_rank(*record.witness, guide ? *guide : at(route, "objective"));
}

bool DfsSearch::improves(const Record& record) const {
    // A finish ranked no better than the incumbent cannot become it: its relaxed value bounds its answer.
    const Value objective = at(fields(options_.scene), "objective");
    return !state_.best.value || py_compare(candidate_rank(*record.witness, objective),
                                            candidate_rank(to_value(*state_.best.value), objective)) < 0;
}

void DfsSearch::commit_finish(Frame& f, Record& chosen) {
    const Value& node = get_or_null(fields(*chosen.program), "decisionNode");
    if (std::holds_alternative<std::string>(node.data))
        if (ObservationDecision* event = state_.observations.decisions.find(as_text(node)))
            event->selected = event->selected.value_or(0) + 1;
    transition("choose", f, {{"candidate", text(chosen.id)},
                             {"arrival", chosen.arrival.value ? integer_value(*chosen.arrival.value) : Value{nullptr}},
                             {"key", record_priority(chosen, f)}});
    for (const FramePtr& ancestor : state_.stack) ancestor->validated_finish = true;
    Incumbent candidate{*chosen.child, Value{}, fields(*chosen.witness)};
    erase(candidate.annotations, "movementProgram");
    erase(candidate.annotations, "blockIds");
    const Object& scene = fields(options_.scene);
    candidate.block_ids = array({at(scene, "startBlock"), at(scene, "goalBlock")});
    const Value objective = at(scene, "objective");
    const Value candidate_value = to_value(candidate);
    if (!state_.best.value || py_compare(candidate_rank(candidate_value, objective),
                                         candidate_rank(to_value(*state_.best.value), objective)) < 0) {
        state_.best.value = candidate;
        if (!state_.first_full_solution_seconds)
            state_.first_full_solution_seconds = flt(hooks_.monotonic() - options_.started);
        hooks_.accepted(candidate_value);
        transition("incumbent", f, {{"candidate", text(chosen.id)},
                                    {"objective", at(fields(at(fields(*chosen.witness), "candidate")), "objective")}});
    }
}

void DfsSearch::confirm_at_budget_end() {
    // The run budget ended inside a final frame: confirm its best scored finish that can beat the incumbent, falling
    // back to the next (user, 2026-11-23). The rest of its queue waits for a resume.
    if (state_.stack.empty() || options_.numerical_backend == "ipopt") return;
    const FramePtr f = state_.stack.back();
    if (!final_frame(*f)) return;
    std::vector<std::pair<Value, RecordPtr>> options;
    for (const RecordPtr& r : f->records)
        if (r->witness && !r->tried && !(r->preparation_deferral && py_truthy(*r->preparation_deferral)) && improves(*r))
            options.emplace_back(array({witness_rank(*r), record_priority(*r, *f)}), r);
    std::stable_sort(options.begin(), options.end(),
                     [](const auto& a, const auto& b) { return py_compare(a.first, b.first) < 0; });
    for (const auto& [_, chosen] : options) {
        chosen->tried = true;
        const bool confirmed = chosen->frame_end && py_equal(get_or_null(fields(*chosen->frame_end), "status"),
                                                              text("confirmed"));
        if (confirmed || frame_end(*f, *chosen)) {
            commit_finish(*f, *chosen);
            return;
        }
    }
}

std::string DfsSearch::run() {
    std::string termination = "exhausted";
    state_.discovery_work = DiscoveryWork{};
    if (state_.stack.empty() && state_.next_frame == 0 && state_.goal_owners.items.empty())
        frame(0, options_.roots, {}, std::nullopt);
    for (std::size_t i = 0; i < state_.discovery.items.size(); ++i)
        if (!state_.discovery.items[i].second.exhausted) expand_discovery(state_.discovery.items[i].second);
    if (state_.pending_goal_owner) {
        const auto ready = std::find_if(state_.goal_queue.begin(), state_.goal_queue.end(),
                                        [&](const FramePtr& f) { return f->owner == *state_.pending_goal_owner; });
        if (ready != state_.goal_queue.end()) {
            state_.stack.push_back(*ready);
            state_.goal_queue.erase(ready);
            state_.pending_goal_owner.reset();
        }
    }
    const auto resumable_frame = [&](const Frame& f) {
        return std::any_of(f.records.begin(), f.records.end(), [&](const RecordPtr& r) { return numerical_pending(*r); });
    };
    FramePtr resumable;
    for (auto it = state_.suspended.rbegin(); it != state_.suspended.rend(); ++it)
        if (resumable_frame(**it)) {
            resumable = *it;
            break;
        }
    if (state_.stack.empty() && resumable) {
        FramePtr f = resumable;
        state_.suspended.erase(std::find(state_.suspended.begin(), state_.suspended.end(), f));
        f->phase = "score";
        state_.stack.push_back(f);
    }
    std::set<std::int64_t> resumable_ids;
    for (const FramePtr& f : state_.suspended)
        if (resumable_frame(*f)) resumable_ids.insert(f->id);
    const auto first_record = [](const Frame& f, const auto& pred) -> RecordPtr {
        for (const RecordPtr& r : f.records)
            if (pred(*r)) return r;
        return nullptr;
    };
    const auto refinement_session_differs = [&](const Record& r) {
        return !r.refinement_session || !r.refinement_session->value ||
               *r.refinement_session->value != state_.session;
    };
    try {
        while (!state_.stack.empty() || !resumable_ids.empty() || !state_.goal_queue.empty()) {
            hooks_.check();
            if (state_.stack.empty()) {
                if (!state_.goal_queue.empty()) {
                    state_.stack.push_back(state_.goal_queue.front());
                    state_.goal_queue.erase(state_.goal_queue.begin());
                    continue;
                }
                FramePtr f = *std::find_if(state_.suspended.begin(), state_.suspended.end(),
                                           [&](const FramePtr& v) { return resumable_ids.count(v->id); });
                resumable_ids.erase(f->id);
                state_.suspended.erase(std::find(state_.suspended.begin(), state_.suspended.end(), f));
                f->phase = !f->ready.empty() ? "score" : !f->source_exhausted ? "generate" : "select";
                state_.stack.push_back(f);
            }
            if (hooks_.cancelled()) throw Timeout("cancelled");
            const FramePtr f = state_.stack.back();
            // An uninterrupted run would have finished a solve the cancellation cut off; resume it before any other
            // work so a continued search repeats that run exactly.
            if (RecordPtr interrupted = first_record(*f, [&](const Record& r) {
                    return r.termination.value && *r.termination.value == "cancelled" && numerical_pending(r) &&
                           refinement_session_differs(r);
                })) {
                refine(*f, interrupted);
                continue;
            }
            if (f->phase == "generate") generate(*f);
            if (f->phase == "score") {
                while (!f->ready.empty() || f->in_flight.value) {
                    SharedValue program;
                    RecordPtr previous;
                    if (f->in_flight.value) {
                        hooks_.check();
                        const InFlight item = *f->in_flight.value;
                        program = item.program;
                        previous = first_record(*f, [&](const Record& r) { return r.id == item.id; });
                    } else {
                        // Every queued arrival is scored, in priority order.
                        program = f->ready.front();
                        hooks_.check();
                        f->ready.erase(std::find(f->ready.begin(), f->ready.end(), program));
                        const std::string key = candidate_key(*program);
                        previous = first_record(*f, [&](const Record& r) {
                            return (r.candidate_key.empty() ? r.id : r.candidate_key) == key;
                        });
                    }
                    if (!eligible(program, *f)) {
                        f->in_flight.value.reset();
                        transition("candidate_exclusion", *f, {{"candidate", text(candidate_key(*program))}});
                        continue;
                    }
                    score(*f, program, previous);
                }
                f->phase = "select";
            }
            // A frame chooses only once its whole queue is examined: every arrival scored, every interrupted solve
            // refined and its generator exhausted. Reserved work (deferred slower runs, held alternatives) waits for a
            // failure below (user, 2026-11-23).
            if (!f->ready.empty()) {
                f->phase = "score";
                continue;
            }
            if (RecordPtr pending = first_record(*f, [&](const Record& r) {
                    return !(r.refinement_time_exclusion && py_truthy(*r.refinement_time_exclusion)) &&
                           !(r.preparation_deferral && py_truthy(*r.preparation_deferral)) && numerical_pending(r) &&
                           refinement_session_differs(r);
                })) {
                refine(*f, pending);
                continue;
            }
            if (!f->source_exhausted) {
                f->phase = "generate";
                continue;
            }
            std::vector<RecordPtr> options;
            for (const RecordPtr& r : f->records)
                if (r->witness && !r->tried && !(r->preparation_deferral && py_truthy(*r->preparation_deferral)))
                    options.push_back(r);
            if (!options.empty()) {
                // Fewest ticks, then objective, at every frame (user, 2026-11-23).
                RecordPtr chosen = options.front();
                Value chosen_key = array({witness_rank(*chosen), record_priority(*chosen, *f)});
                for (std::size_t i = 1; i < options.size(); ++i) {
                    Value key = array({witness_rank(*options[i]), record_priority(*options[i], *f)});
                    if (py_compare(key, chosen_key) < 0) {
                        chosen = options[i];
                        chosen_key = std::move(key);
                    }
                }
                chosen->tried = true;
                const Value preparation = run_bound(*chosen->child, *f);
                const Value structural = hard_bound(**chosen->child, *f);
                if (py_truthy(preparation) || py_truthy(structural)) {
                    const Value proof = py_truthy(preparation) ? preparation : structural;
                    chosen->preparation_deferral = proof;
                    transition("candidate_exclusion", *f, {{"candidate", text(chosen->id)}, {"proof", proof}});
                    continue;
                }
                const Value proof = reject_time(**chosen->child, *f);
                if (!is_null(proof)) {
                    chosen->time_bound_exclusion = proof;
                    transition("candidate_exclusion", *f, {{"candidate", text(chosen->id)}, {"proof", proof}});
                    continue;
                }
                // A finish that cannot beat the incumbent needs no answer.
                if (options_.numerical_backend != "ipopt" && (!final_frame(*f) || improves(*chosen)) &&
                    !(chosen->frame_end &&
                      py_equal(get_or_null(fields(*chosen->frame_end), "status"), text("confirmed"))) &&
                    !frame_end(*f, *chosen))
                    continue;
                if (final_frame(*f)) {
                    // The frame stays open: its next finish may still beat this one's answer.
                    commit_finish(*f, *chosen);
                    continue;
                }
                const Value& node = get_or_null(fields(*chosen->program), "decisionNode");
                if (std::holds_alternative<std::string>(node.data))
                    if (ObservationDecision* event = state_.observations.decisions.find(as_text(node)))
                        event->selected = event->selected.value_or(0) + 1;
                transition("choose", *f, {{"candidate", text(chosen->id)},
                                          {"arrival", chosen->arrival.value ? integer_value(*chosen->arrival.value) : Value{nullptr}},
                                          {"key", record_priority(*chosen, *f)}});
                Window window;
                window.checkpoint.tick = *chosen->arrival.value;
                window.goal = f->goal;
                window.frame = f->id;
                for (const RecordPtr& r : f->records) {
                    Record copy = *r;
                    copy.program = std::make_shared<Value>(*r->program);
                    if (r->child) copy.child = std::make_shared<Value>(**r->child);
                    window.candidates.push_back(std::move(copy));
                }
                for (const RecordPtr& r : options) window.retained.push_back(r->id);
                for (const SharedValue& p : f->ready) window.deferred.push_back(candidate_key(*p));
                window.scored = static_cast<std::int64_t>(options.size());
                for (const RecordPtr& r : f->records) window.unknown += !r->witness;
                window.heuristic_drops = 0;
                window.generation = f->generation;
                window.retention.policy = "saved_dfs_siblings_no_cap_eviction";
                state_.windows.push_back(std::move(window));
                const Value cursor =
                    adapter_->advance(f->semantic_cursor, **chosen->child, *chosen->witness, f->goal_request);
                frame(f->index + 1, {**chosen->child}, Nullable<FrameParent>{FrameParent{f->id, chosen->id}}, cursor);
                continue;
            }
            if (restore_alternatives(*f)) {
                transition("restore_node_alternative", *f, {});
                f->phase = "generate";
                continue;
            }
            if (f->recovery_failure && py_truthy(*f->recovery_failure) && !f->validated_finish.value_or(false)) {
                const Value recovery = *f->recovery_failure;
                f->recovery_failure.reset();
                if (restore_fallback(*f, recovery)) {
                    f->phase = "select";
                    continue;
                }
                std::vector<std::pair<Value, std::size_t>> deferred;
                for (std::size_t i = 0; i < state_.minimum_run_exclusions.size(); ++i) {
                    const MinimumRunExclusion& r = state_.minimum_run_exclusions[i];
                    if (r.frame == f->id && !r.restored.value_or(false) && !r.retired.value_or(false))
                        deferred.emplace_back(priority(r.program, *f, r.candidate), i);
                }
                std::stable_sort(deferred.begin(), deferred.end(),
                                 [](const auto& a, const auto& b) { return py_compare(a.first, b.first) < 0; });
                bool cached = false;
                for (const auto& [_, i] : deferred) {
                    MinimumRunExclusion& saved = state_.minimum_run_exclusions[i];
                    const Value restored_run =
                        object({{"frame", integer_value(f->id)}, {"candidate", text(saved.candidate)}});
                    const Value p = set_key(saved.program, "restoredRun", restored_run);
                    Value excluded = time_bound(p, *f);
                    if (!py_truthy(excluded)) excluded = hard_bound(p, *f);
                    if (py_truthy(excluded)) {
                        saved.retired = true;
                        saved.restoration_exclusion = excluded;
                        continue;
                    }
                    saved.restored = true;
                    saved.recovery = recovery;
                    const RecordPtr prior = first_record(*f, [&](const Record& r) {
                        return r.witness && candidate_key(r.child ? **r.child : *r.program) == saved.candidate;
                    });
                    if (prior) {
                        annotate(prior->program, "restoredRun", restored_run);
                        annotate(*prior->child, "restoredRun", restored_run);
                        prior->preparation_deferral.reset();
                        prior->tried = false;
                        cached = true;
                    } else if (lands_in(p, goal_blocks(f->goal))) {
                        f->ready.push_back(std::make_shared<Value>(p));
                    } else {
                        if (Continuation* continuation = f->generation.continuation()) {
                            continuation->pending.push_back(p);
                            continuation->exhausted = false;
                            f->source_exhausted = false;
                        }
                    }
                    transition("restore_run", *f, {{"candidate", text(saved.candidate)}, {"recovery", recovery},
                                                   {"cachedWitness", boolean(cached)}});
                    break;
                }
                if (cached || !f->ready.empty() || !f->source_exhausted) {
                    f->phase = cached ? "select" : !f->ready.empty() ? "score" : "generate";
                    continue;
                }
            }
            close_frame(f);
        }
        if (state_.stack.empty() && !state_.suspended.empty()) termination = "numerical_unknown";
    } catch (const Timeout& error) {
        const Value work_termination = hooks_.termination();
        termination = py_truthy(work_termination) ? as_text(work_termination) : error.what();
        if (termination != "cancelled" && termination != "error") {
            try {
                confirm_at_budget_end();
            } catch (const Timeout&) {
                termination = "cancelled";
            }
        }
    }
    publish();
    if (termination == "exhausted")
        for (const auto& [_, d] : state_.discovery.items)
            if (!d.exhausted) {
                termination = "discovery_work_limit";
                break;
            }
    return termination;
}

}  // namespace pkc
