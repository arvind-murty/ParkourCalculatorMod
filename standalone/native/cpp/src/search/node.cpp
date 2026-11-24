// Port of search/frontier.py's per-proposal loop (one movement type of an expansion that is not an ordinary
// decision's) with search/dfs.py's screen, eligible, time_bound, hard_bound, run_bound and configure.
#include "pkc/search/node.hpp"

#include <algorithm>
#include <cmath>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/movement/cases.hpp"
#include "pkc/movement/generation.hpp"
#include "pkc/reachability/viability.hpp"
#include "pkc/search/bounds/remaining.hpp"
#include "pkc/search/comparison.hpp"
#include "pkc/search/feasibility.hpp"
#include "pkc/search/goals.hpp"
#include "pkc/movement/normal.hpp"
#include "pkc/movement/vertical_profiles.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const Value* note(const Program& p, std::string_view key) { return get(p.annotations, key); }
const Value& note_or_null(const Program& p, std::string_view key) { return get_or_null(p.annotations, key); }
void annotate(Program& p, std::string_view key, Value value) { put(p.annotations, key, std::move(value)); }

bool supported_by(const Program& p, const std::vector<std::string>& names) {
    return p.terminal_support.value &&
           std::find(names.begin(), names.end(), *p.terminal_support.value) != names.end();
}

std::string pair_key(const NodeFrame& f, std::int64_t k) {
    const Array& names = items(at(fields(f.guide_plan), "anchors"));
    return as_text(names.at(k)) + "→" + as_text(names.at(k + 1));
}

bool latched(const NodeFrame& f, std::int64_t k) { return get(fields(f.hard_latches), pair_key(f, k)) != nullptr; }

Value with_fields(Value base, const Object& extra) {
    Object out = fields(base);
    for (const auto& [k, v] : extra) put(out, k, v);
    return object(std::move(out));
}

Value incumbent_proof(const Value& decision, std::int64_t T, const Value* proof = nullptr) {
    Object extra{{"reason", at(fields(decision), "reason")}, {"elapsedInputs", integer_value(T)},
                 {"incumbentTick", at(fields(decision), "incumbentT")}};
    return proof ? with_fields(*proof, extra) : object(std::move(extra));
}

// cases.entry_possible(profile, bounds)
bool entry_possible(const Profile& profile, const std::array<double, 4>& bounds) {
    double x[2] = {bounds[0], bounds[1]}, z[2] = {bounds[2], bounds[3]};
    auto apply = [&](const std::string& mode, const std::string& cmp, double rhs) {
        double* interval = mode == "X" ? x : z;
        if (cmp == "GE" || cmp == "EQ") interval[0] = std::max(interval[0], rhs);
        if (cmp == "LE" || cmp == "EQ") interval[1] = std::min(interval[1], rhs);
    };
    for (const auto& r : profile.position_constraints)
        if (r.t1 == 0) apply(r.mode, r.cmp, r.rhs.value);
    for (const auto& e : profile.contact_events) {
        if (!e.regions || e.regions->empty()) continue;
        Array regions;
        for (const auto& r : *e.regions) {
            Array row;
            for (const Num& n : r) row.push_back(value(n));
            regions.push_back(array(std::move(row)));
        }
        const std::int64_t offset = e.offset + (e.axis == "Z" || (e.axis == "Y" && e.role == "step"));
        if (offset != 0) continue;
        for (const auto& row : finite_rows(e.axis == "X" ? 0 : e.axis == "Y" ? 1 : 2, items(bounding_region(regions)),
                                           {{"t1", integer_value(offset)}})) {
            const Object& r = fields(row);
            apply(as_text(at(r, "mode")), as_text(at(r, "cmp")), num(at(r, "rhs")).value);
        }
    }
    return x[0] <= x[1] && z[0] <= z[1];
}

}  // namespace

std::string NodeSearch::candidate_key(const Program& program) const {
    Program physical = program;
    physical.repair_state.reset();
    return digest(text(program_search_key(physical)));
}

Value NodeSearch::time_bound(const Program& program) const {
    const NodeFrame& f = frame;
    if (is_null(f.incumbent)) return Value{nullptr};
    const std::int64_t T = program.profile.duration;
    Value decision = compare_candidates(object({{"T", integer_value(T)}}), Value{nullptr}, Object{}, f.incumbent);
    if (py_equal(at(fields(decision), "action"), text("exclude_bound"))) return incumbent_proof(decision, T);
    const std::int64_t span = std::min(anchors->audit(program, f.anchors).reached, f.group_end);
    const Value proof = remaining_bound(f.guide_plan, span, program, f.motion, f.transfers, f.hard_latches);
    const Value* ids = get(fields(proof), "proofIds");
    const Value label = object({{"T", integer_value(T)},
                                {"remainingFinishBound", object({{"ticks", at(fields(proof), "remainingTicks")},
                                                                 {"proofIds", ids ? *ids : array({})}})}});
    decision = compare_candidates(label, Value{nullptr}, Object{}, f.incumbent);
    if (py_equal(at(fields(decision), "action"), text("exclude_bound"))) return incumbent_proof(decision, T, &proof);
    return Value{nullptr};
}

Value NodeSearch::hard_bound(const Program& program) const {
    const NodeFrame& f = frame;
    const std::int64_t first = f.anchor_index, last = f.group_end;
    bool any = false;
    for (std::int64_t k = first; k <= last; ++k) any = any || latched(f, k);
    if (!any) return Value{nullptr};
    const Audit arrivals = anchors->audit(program, f.anchors);
    const bool completed = supported_by(program, f.goal_blocks);
    const Array& names = items(at(fields(f.guide_plan), "anchors"));
    for (std::int64_t k = first; k <= std::min(arrivals.reached, last); ++k) {
        if (!latched(f, k)) continue;
        const std::int64_t start = k == first ? f.span_start : arrivals.ticks.at(k - 1);
        const Value transfer = anchors->transfer_structure(program, start, as_text(names[k]), as_text(names[k + 1]),
                                                           k == last ? completed : k < arrivals.reached, k < last);
        if (!py_truthy(at(fields(transfer), "valid")))
            return object({{"reason", text("global_single_jump_latch")}, {"span", text(pair_key(f, k))},
                           {"revision", integer_value(f.latch_revision)}, {"transfer", transfer}});
    }
    return Value{nullptr};
}

Value NodeSearch::run_bound(Program& program, NodeEffects& effects) const {
    const NodeFrame& f = frame;
    if (!f.minimum_run_pruning) return Value{nullptr};
    if (const Value* restored = note(program, "restoredRun"); restored && is_object(*restored) &&
                                                              py_equal(get_or_null(fields(*restored), "frame"), f.frame_id))
        return Value{nullptr};
    bool hard_latched = true;
    for (std::int64_t k = f.anchor_index; k <= f.group_end; ++k) hard_latched = hard_latched && latched(f, k);
    if (!hard_latched || !supported_by(program, f.goal_blocks)) return Value{nullptr};
    const Object context{{"purpose", text("hard_anchor")}, {"backend", text("automatic_anchor")},
                         {"localDeferralEnabled", boolean(true)}, {"hardLatched", boolean(true)},
                         {"comparisonContextId", f.comparison_context_set ? f.comparison_context_id : Value{nullptr}}};
    // tick_label, which sets f['comparisonContextId'] on first use
    effects.comparison_context_used = true;
    f.comparison_context_set = true;
    const bool complete = supported_by(program, f.goal_blocks) && program.profile.grounded.back();
    const std::string identity = candidate_key(program);
    // A copy: the annotations below can move the program's annotation storage.
    const Value* noted = note(program, "priorityTurnaround");
    const Value turnaround = noted ? *noted : boolean(false);
    const Value label = comparison_label(
        program, f.span_start, std::nullopt, identity, py_truthy(turnaround) ? "turnaround" : "ordinary",
        {{"comparisonContext", f.comparison_context_id},
         {"basis", text(complete ? "anchor_arrival" : "open_prefix")},
         {"scope", program.solve_scope.value_or(Value{nullptr})},
         {"milestone", text(f.goal_block)},
         {"landingPhase", text(program.profile.landing_phase.empty() ? "pre" : program.profile.landing_phase)}},
        {{"validated", boolean(false)}});
    const Object& l = fields(label);
    const Value token = array({f.frame_id, integer_value(f.tick_peer_revision), integer_value(f.latch_revision),
                               at(l, "id"), at(l, "mode")});
    Value decision;
    if (const Value* cached = note(program, "comparisonCacheToken"); cached && py_equal(*cached, token)) {
        decision = note_or_null(program, "comparisonDecision");
        ++effects.cache_hits;
    } else {
        decision = assess(label, f.tick_peers, context);
        ++effects.assessments;
        if (const Value* checks = get(fields(decision), "peerChecks")) effects.peer_checks += as_int(*checks);
        annotate(program, "comparisonCacheToken", token);
    }
    annotate(program, "comparisonDecision", decision);
    std::string frame_text = is_null(f.frame_id) ? "None" : dumps(f.frame_id, false, false);
    effects.comparison_decisions.push_back(array({text(frame_text + ":" + as_text(at(l, "id"))), decision}));
    if (!py_equal(at(fields(decision), "action"), text("defer_preparation"))) return Value{nullptr};
    const Value proof = object({{"reason", text("higher_ticks_without_structural_advantage")}, {"heuristic", boolean(true)},
                                {"comparison", decision}});
    if (!frame.excluded_runs.count(identity)) {
        frame.excluded_runs.insert(identity);
        const Value* node = note(program, "decisionNode");
        effects.minimum_run_exclusions.push_back(object(
            {{"frame", f.frame_id}, {"candidate", text(identity)}, {"proof", proof}, {"program", to_value(program)},
             {"span", integer_value(f.anchor_index)}, {"decision", node ? *node : Value{nullptr}},
             {"isTurnaround", turnaround}, {"latchRevision", integer_value(f.latch_revision)}}));
    }
    return proof;
}

bool NodeSearch::eligible(Program& program, NodeEffects& effects) const {
    const Value proof = time_bound(program);  // reject_time
    if (!is_null(proof)) {
        effects.time_bound_exclusions.push_back(object({{"id", text(candidate_key(program))}, {"proof", proof}}));
        return false;
    }
    if (py_truthy(hard_bound(program)) || !is_null(run_bound(program, effects))) return false;
    return admitted(program);
}

bool NodeSearch::admitted(const Program& program) const {
    if (!anchors->within_caps(frame.request, program)) return false;  // accepted_program
    if (const Value* allowed = get(frame.request, "initialJumpInputs"); allowed && !is_null(*allowed)) {  // InitialJumpDomain
        const Profile& p = program.profile;
        std::optional<std::int64_t> first;
        for (std::int64_t t = 0; t < p.duration && !first; ++t)
            if (p.jump[t] && p.grounded[t]) first = t;
        bool ok = false;
        std::int64_t latest = 0;
        for (const auto& t : items(*allowed)) {
            ok = ok || (first && as_int(t) == *first);
            latest = std::max(latest, as_int(t));
        }
        if (!(first ? ok : p.duration <= latest)) return false;
    }
    // AnchorAdapter.program_allowed
    const std::string target = frame.group_end < static_cast<std::int64_t>(frame.anchors.size())
                                   ? frame.anchors[frame.group_end]
                                   : frame.task_block;
    const bool landed = program.terminal_support.value && anchors->owner(*program.terminal_support.value) == anchors->owner(target);
    const std::int64_t reached = landed ? std::min<std::int64_t>(frame.group_end + 1, frame.anchors.size()) : frame.anchor_index;
    return !anchors->audit(program, frame.anchors, reached).reason;
}

Value NodeSearch::screen(Program& program, NodeEffects& effects) const {
    const NodeFrame& f = frame;
    std::optional<Program> projected;
    const Value* owned = note(program, "ownedEndpoint");
    if (!(owned && py_equal(*owned, integer_value(program.profile.duration)))) {
        for (const auto& block : f.goal_blocks)
            for (auto& prefix : landing_prefixes(f.scene, program, block, false))
                if (!projected || prefix.program.profile.duration < projected->profile.duration)
                    projected = std::move(prefix.program);
    }
    Program& bounded = projected ? *projected : program;
    Value bound = time_bound(bounded);
    if (!py_truthy(bound)) bound = hard_bound(bounded);
    if (!py_truthy(bound)) bound = run_bound(bounded, effects);
    if (!is_null(bound)) return bound;
    std::vector<std::string> required;
    if (f.goal_kind == "landing" && !supported_by(bounded, f.goal_blocks)) required.push_back(f.goal_block);
    const Value viability = prefix_viability(f.scene, bounded, required, f.max_inputs, f.types);
    ++effects.viability_checked;
    if (py_truthy(at(fields(viability), "dead"))) {
        ++effects.viability_excluded;
        return viability;
    }
    auto shared = std::make_shared<const Program>(program);
    auto [proof, selection] =
        gate->check_program(shared, anchors->program_objective(program, f.guide_plan, f.anchors));
    if (!is_null(selection)) {
        std::vector<WalkOff> walk_offs = program.walk_offs.value_or(std::vector<WalkOff>{});
        const Object& s = fields(selection);
        WalkOff w;
        w.offset = as_int(at(s, "offset"));
        for (const auto& i : items(at(s, "solids"))) w.solids.push_back(as_int(i));
        for (const auto& row : items(at(s, "options"))) {
            const Object& r = fields(row);
            w.options.push_back(LegConstraint{as_text(at(r, "cmp")), as_text(at(r, "mode")), as_text(at(r, "name")),
                                              num(at(r, "rhs")), as_int(at(r, "leg")), as_int(at(r, "offset"))});
        }
        walk_offs.push_back(std::move(w));
        program.walk_offs = std::move(walk_offs);
    }
    return proof;
}

namespace {

// dfs.configure and AnchorAdapter.context: `program` is the context's program, `decision` the decision expanding.
GenerationContext expansion_context(const NodeSearch& search, const std::shared_ptr<const Program>& program,
                                    const Program& decision, std::int64_t settings_max_inputs, bool run_ticks,
                                    const std::function<void()>& budget) {
    const NodeFrame& f = search.frame;
    GenerationContext context;
    context.program = program;
    context.state = boundary_state(*program);
    context.max_inputs = settings_max_inputs - program->profile.duration;
    context.threshold = Num{0., false};
    context.run_tick_choices = run_ticks;
    context.gate = search.gate;
    context.check_budget = budget;
    context.frame_id = f.frame_id;
    context.decision_point = get_or_null(decision.annotations, "decisionPoint");
    context.region_items = true;
    if (auto launch = search.anchors->launch(*program)) context.launch_face = launch->second;
    std::set<std::string> excluded;
    for (const auto& d : search.anchors->visit_state(*program).departed) excluded.insert(d);
    for (std::size_t a = f.group_end + 1; a < f.anchors.size(); ++a) excluded.insert(search.anchors->owner(f.anchors[a]));
    if (f.group_end < static_cast<std::int64_t>(f.anchors.size())) excluded.insert(search.anchors->owner(f.task_block));
    context.feet_allowed_fn = [excluded](const std::string& name) { return !excluded.count(name); };
    const Value* turnaround = get(decision.annotations, "decisionTurnaround");
    if (!(turnaround && py_truthy(*turnaround))) {
        const std::int64_t k = std::min(search.anchors->audit(decision, f.anchors).reached, f.group_end);
        const Array& points = items(at(fields(f.guide_plan), "points"));
        if (k + 1 < static_cast<std::int64_t>(points.size())) {
            const Array &a = items(points[k]), &b = items(points[k + 1]);
            const double dx = num(b[0]).value - num(a[0]).value, dz = num(b[2]).value - num(a[2]).value;
            const double length = python_hypot(dx, dz);
            if (length > 1e-12) {
                const Array& point = items(at(decision.annotations, "decisionPoint"));
                context.forward = Forward{{num(point[0]).value, num(point[1]).value},
                                          leg_class(dx / length, dz / length).allowed, num(f.turnaround_margin)};
            }
        }
    }
    return context;
}

Value exclusion(const Program& program, const Value& proof, const char* scope) {
    return object({{"id", text(digest(text(program_physics_key(program))))},
                   {"ticks", integer_value(program.profile.duration)}, {"proof", proof}, {"scope", text(scope)}});
}

struct Composed {  // a propose_choices proposal
    std::optional<Program> full, owned;
    bool projected = false;
};

Composed compose(const NodeSearch& search, const Program& parent, const MovementChoice& choice,
                 const std::vector<std::string>& supports, std::int64_t settings_max_inputs) {
    Composed out;
    out.full = append_choice(parent, choice, settings_max_inputs);
    if (!out.full) return out;
    for (const char* field : {"decisionPoint", "decisionVelocity", "decisionPointSource", "decisionPointTick",
                              "decisionTurnaround", "restoredRun"})
        if (const Value* v = get(parent.annotations, field)) annotate(*out.full, field, *v);
    const Value* turnaround = get(parent.annotations, "decisionTurnaround");
    annotate(*out.full, "priorityTurnaround", turnaround ? *turnaround : boolean(false));
    std::optional<LandingPrefix> best;  // project_owned: min(arrivals, key=(duration, terminalSupport))
    for (const auto& block : supports)
        for (auto& prefix : landing_prefixes(search.frame.scene, *out.full, block, true))
            if (!best || std::make_pair(prefix.program.profile.duration, *prefix.program.terminal_support.value) <
                             std::make_pair(best->program.profile.duration, *best->program.terminal_support.value))
                best = std::move(prefix);
    if (!best) {
        out.owned = out.full;
    } else if (best->reused) {
        annotate(*out.full, "ownedEndpoint", integer_value(best->program.profile.duration));
        out.owned = out.full;
    } else {
        annotate(best->program, "ownedEndpoint", integer_value(best->program.profile.duration));
        out.owned = std::move(best->program);
        out.projected = true;
    }
    return out;
}

struct Checked {  // check_child: both screens and acceptance, without frontier state
    std::string outcome;  // screened | rejected | seen | accepted
    Array exclusions;
    std::int64_t screened = 0, policy_rejections = 0;
};

Checked check(const NodeSearch& search, Composed& proposal, const std::array<double, 4>& initial_bounds,
              const std::set<std::string>& seen, const std::set<std::string>* queued, NodeEffects& effects) {
    Checked out;
    if (proposal.owned) {  // assess_proposal
        const Value proof = search.screen(*proposal.owned, effects);
        if (is_null(proof) && proposal.projected) {
            const Value full_proof = search.gate->check_program(std::make_shared<const Program>(*proposal.full), Value{nullptr}).first;
            if (!is_null(full_proof)) out.exclusions.push_back(exclusion(*proposal.full, full_proof, "unused_extension"));
        }
        if (!is_null(proof)) {
            out.screened = 1;
            out.exclusions.push_back(exclusion(*proposal.owned, proof, "owned_proposal"));
            out.outcome = "screened";
            return out;
        }
    }
    if (!proposal.owned || !entry_possible(proposal.owned->profile, initial_bounds)) {
        out.outcome = "rejected";
    } else if (!search.eligible(*proposal.owned, effects)) {
        out.policy_rejections = 1;
        out.exclusions.push_back(object({{"id", text(digest(text(program_search_key(*proposal.owned))))},
                                         {"ticks", integer_value(proposal.owned->profile.duration)},
                                         {"proof", object({{"reason", text("search_policy")}})}}));
        out.outcome = "rejected";
    } else {
        const std::string key = program_search_key(*proposal.owned);
        out.outcome = seen.count(key) || (queued && queued->count(key)) ? "seen" : "accepted";
    }
    return out;
}

std::optional<std::string> terminal_face(const NodeSearch& search, const Program& p) {
    if (!p.terminal_support.value) return std::nullopt;
    return search.anchors->support_face(*p.terminal_support.value);
}

// dfs.program_faces: the merged top faces a grounded program stands on at its feet.
std::vector<FaceOption> program_faces(const NodeSearch& search, const Program& program) {
    std::vector<FaceOption> out;
    const auto found = search.anchors->launch(program);
    if (!found) return out;
    for (const auto& o : search.gate->top_faces())
        if (num(o.plane).value == program.profile.position.back().value && face_name(o, search.gate->owners()) == found->second)
            out.push_back(o);
    return out;
}

std::optional<FaceOption> launch_face(const NodeSearch& search, const Program& decision) {
    const Array& point = items(at(decision.annotations, "decisionPoint"));
    const double x = num(point[0]).value, z = num(point[1]).value;
    std::optional<FaceOption> best;
    double best_key = INFINITY;
    for (auto& o : program_faces(search, decision)) {
        double key = INFINITY;
        for (const auto& rv : o.regions) {
            const Array& r = items(rv);
            key = std::min(key, python_hypot(std::max({num(r[0]).value - x, 0., x - num(r[2]).value}),
                                             std::max({num(r[1]).value - z, 0., z - num(r[3]).value})));
        }
        if (!best || key < best_key) {
            best = o;
            best_key = key;
        }
    }
    return best;
}

// landings_of(child, decision): {face: program} of an accepted child's earliest landing on each face, in order found.
// A grounded child's is its own; an airborne one's are its own proposals' (screened, effects into `effects`).
std::vector<std::pair<std::optional<std::string>, Program>> landings_of(
    const NodeSearch& search, const Program& child, const Program& decision, const std::vector<std::string>& supports,
    std::int64_t settings_max_inputs, const std::array<double, 4>& initial_bounds, const std::set<std::string>& seen,
    const std::set<std::string>* queued, NodeEffects& effects, const std::function<void()>& budget) {
    std::vector<std::pair<std::optional<std::string>, Program>> landings;
    if (child.profile.grounded.back()) {
        landings.emplace_back(terminal_face(search, child), child);
        return landings;
    }
    auto shared = std::make_shared<const Program>(child);
    const GenerationContext inner = expansion_context(search, shared, decision, settings_max_inputs, true, budget);
    std::vector<Composed> proposed;
    for (const auto& type : search.frame.types)
        for (const auto& choice : generate_choices(type, inner)) {
            Composed c = compose(search, child, choice, supports, settings_max_inputs);
            if (c.owned && c.owned->profile.grounded.back()) proposed.push_back(std::move(c));
        }
    std::stable_sort(proposed.begin(), proposed.end(), [](const Composed& a, const Composed& b) {
        return a.owned->profile.duration < b.owned->profile.duration;
    });
    for (auto& c : proposed) {
        const auto face = terminal_face(search, *c.owned);
        if (std::any_of(landings.begin(), landings.end(), [&](const auto& l) { return l.first == face; })) continue;
        const Checked landing = check(search, c, initial_bounds, seen, queued, effects);
        if ((landing.outcome == "accepted" || landing.outcome == "seen") && c.owned->profile.grounded.back()) {
            const auto landed_face = terminal_face(search, *c.owned);
            if (std::none_of(landings.begin(), landings.end(), [&](const auto& l) { return l.first == landed_face; }))
                landings.emplace_back(landed_face, std::move(*c.owned));
        }
    }
    return landings;
}

// frontier.link: the child's decision fields (set only when absent).
void link(const NodeSearch& search, const Program& parent, bool meaningful, Program& child, bool complete) {
    auto inherited = [&](const char* key) {
        const Value* v = get(parent.annotations, key);
        return v ? as_int(*v) : 0;
    };
    if (!get(child.annotations, "decisionDepth"))
        annotate(child, "decisionDepth", integer_value(inherited("decisionDepth") + (meaningful ? 1 : 0)));
    if (!get(child.annotations, "lastSolvedDepth"))
        annotate(child, "lastSolvedDepth", integer_value(inherited("lastSolvedDepth")));
    if (!get(child.annotations, "decisionParent"))
        annotate(child, "decisionParent", get_or_null(parent.annotations, meaningful ? "decisionNode" : "decisionParent"));
    if (!get(child.annotations, "decisionNode"))
        annotate(child, "decisionNode", meaningful ? text(node_identity(child, search.frame.policy_context))
                                                   : get_or_null(parent.annotations, "decisionNode"));
    if (meaningful && !complete) annotate(child, "needsNodeSolve", boolean(true));
}

}  // namespace

Expansion expand_type(const NodeSearch& search, const Program& parent_program, const std::string& kind, std::int64_t cursor,
                      const std::vector<std::string>& stop_supports, const std::array<double, 4>& initial_bounds,
                      std::int64_t settings_max_inputs, std::int64_t choices_per_type, bool ready, bool ordinary,
                      std::set<std::string> seen, const std::function<void()>& budget) {
    const NodeFrame& f = search.frame;
    auto parent = std::make_shared<const Program>(parent_program);
    const GenerationContext context = expansion_context(search, parent, *parent, settings_max_inputs, !ordinary, budget);
    const std::vector<MovementChoice> choices = generate_choices(kind, context);
    const std::vector<std::string> supports =
        stop_supports.empty() ? std::vector<std::string>{as_text(at(fields(f.scene), "goalBlock"))} : stop_supports;
    auto complete = [&](const Program& p) {
        return p.profile.grounded.back() && supported_by(p, supports) && p.profile.duration > 0;
    };
    const std::string next_owner = ordinary ? search.anchors->next_anchor(*parent, f.anchors, f.task_block) : std::string();
    auto target_keys = [&](const Program& child) {
        Array keys;
        for (const auto& [kind_name, name] : search.anchors->advantageous_targets(*parent, child, next_owner))
            keys.push_back(text(dumps(array({text(kind_name), name ? text(*name) : Value{nullptr}}), false, false)));
        return keys;
    };
    const std::optional<std::string> own_face = terminal_face(search, *parent);
    Expansion expansion;
    std::vector<ProposalRecord>& records = expansion.records;
    auto emits = [&](std::int64_t ordinal) { return ready && (ordinal + 1) % choices_per_type == 0; };
    for (std::size_t ordinal = 0; ordinal < choices.size(); ++ordinal) {
        budget();
        if (static_cast<std::int64_t>(ordinal) < cursor) continue;
        ProposalRecord record;
        record.ordinal = static_cast<std::int64_t>(ordinal);
        Composed proposal = compose(search, *parent, choices[ordinal], supports, settings_max_inputs);
        const Checked checked = check(search, proposal, initial_bounds, seen, nullptr, record.effects);
        record.exclusions = array(checked.exclusions);
        record.screened = checked.screened;
        record.policy_rejections = checked.policy_rejections;
        record.outcome = checked.outcome;
        if (checked.outcome == "accepted") {
            Program child = std::move(*proposal.owned);
            // Found before any frontier state changes (landings_of and the same-face fit).
            Array landed;
            Value fit{nullptr};
            const bool grounded = child.profile.grounded.back();
            Array targets;
            bool lands = false;
            if (ordinary) {
                targets = target_keys(child);
                lands = grounded || !targets.empty();  // grounded or lands_later
            }
            if (ordinary && lands) {
                const std::int64_t start = parent->profile.duration;
                const auto landings = landings_of(search, child, *parent, supports, settings_max_inputs, initial_bounds,
                                                  seen, nullptr, record.effects, budget);
                for (const auto& [face, landing] : landings)
                    landed.push_back(array({face ? text(*face) : Value{nullptr}, integer_value(landing.profile.duration - start)}));
                // A jump landing back on its launch face waits unless it naturally fits there (O11).
                const auto launch = landings.empty() || !child.profile.jump.at(start) ? std::nullopt : launch_face(search, *parent);
                bool back = launch.has_value();
                for (const auto& [_, landing] : landings) {
                    if (!back) break;
                    const auto faces = program_faces(search, landing);
                    back = faces.size() == 1 && faces[0].surface == launch->surface;
                }
                if (back) {
                    std::size_t first = 0;
                    for (std::size_t i = 1; i < landings.size(); ++i)
                        if (landings[i].second.profile.duration < landings[first].second.profile.duration) first = i;
                    const Program& landing = landings[first].second;
                    // dfs.same_face_fit: None without a guide leg or a physics model.
                    const std::int64_t k = std::min(search.anchors->audit(*parent, f.anchors).reached, f.group_end);
                    const Array& points = items(at(fields(f.guide_plan), "points"));
                    std::optional<std::array<double, 2>> leg;
                    if (k + 1 < static_cast<std::int64_t>(points.size())) {
                        const Array &a = items(points[k]), &b = items(points[k + 1]);
                        const double dx = num(b[0]).value - num(a[0]).value, dz = num(b[2]).value - num(a[2]).value;
                        const double length = python_hypot(dx, dz);
                        if (length > 1e-12) leg = std::array<double, 2>{dx / length, dz / length};
                    }
                    auto analysis = search.gate->analysis(std::make_shared<const Program>(landing));
                    if (leg && analysis) {
                        const auto [dx, dz] = *leg;
                        const std::array<double, 2> u = leg_class(dx, dz).heading;
                        const Array& point = items(at(parent->annotations, "decisionPoint"));
                        const Array& velocity = items(at(parent->annotations, "decisionVelocity"));
                        const double px = num(point[0]).value, pz = num(point[1]).value;
                        double room = -INFINITY;
                        for (const auto& rv : launch->regions) {
                            const Array& r = items(rv);
                            for (double x : {num(r[0]).value, num(r[2]).value})
                                for (double z : {num(r[1]).value, num(r[3]).value}) room = std::max(room, (x - px) * u[0] + (z - pz) * u[1]);
                        }
                        const Object& model = fields(analysis->model);
                        const Array &magnitude = items(at(model, "magnitude")), &friction = items(at(model, "friction"));
                        double speed = num(velocity[0]).value * dx + num(velocity[1]).value * dz, radius = 0.;
                        for (std::int64_t t = start; t < landing.profile.duration && t < static_cast<std::int64_t>(magnitude.size()); ++t) {
                            const double move = speed + num(magnitude[t]).value + 0.;  // travel_radii
                            radius += move;
                            speed = num(friction[t]).value * move;
                        }
                        fit = object({{"room", real(room)}, {"jump", real(radius)}, {"fits", boolean(room > kSameFaceFitFactor * radius)},
                                      {"face", text(face_name(*launch, search.gate->owners()))},
                                      {"total", integer_value(landings[first].second.profile.duration - start)}});
                    }
                }
            }
            record.meaningful = meaningful_choice(choices[ordinal]);
            record.side_variant = choices[ordinal].side_variant;
            link(search, *parent, record.meaningful, child, complete(child));
            seen.insert(program_search_key(child));
            const bool held = !is_null(fit) && !py_truthy(at(fields(fit), "fits"));
            record.outcome = held ? "held" : "queued";
            if (!held) ready = ready || complete(child);
            if (ordinary) {
                Array landings_extend;  // e['landings'] entries: landings on other faces than the decision's
                if (lands)
                    for (const auto& l : landed) {
                        const Value& owner = items(l)[0];
                        if (!is_null(owner) && !(own_face && as_text(owner) == *own_face))
                            landings_extend.push_back(object({{"ticks", items(l)[1]}, {"owner", owner}}));
                    }
                Value smallest{nullptr};
                for (const auto& l : landed)
                    if (is_null(smallest) || as_int(items(l)[1]) < as_int(smallest)) smallest = items(l)[1];
                record.ordinary = object({{"targets", array(std::move(targets))}, {"landed", smallest},
                                          {"landings", array(std::move(landings_extend))}, {"fit", fit}});
            }
            record.child = std::move(child);
        }
        records.push_back(std::move(record));
        if (emits(records.back().ordinal)) return expansion;
    }
    expansion.finished = true;
    return expansion;
}

Array advantageous_candidates(InteractionGate& gate, const AnchorScene& anchors, const std::optional<std::string>& launch,
                              const Num& top, const std::string& next_owner, const std::vector<std::string>& types,
                              const Num& threshold) {
    const Object& scene = fields(gate.scene());
    auto enabled = [&](const char* type) { return std::find(types.begin(), types.end(), type) != types.end(); };
    std::map<std::string, Num> tops;
    for (const auto& b : items(at(scene, "blocks"))) tops.emplace(as_text(at(fields(b), "id")), num(at(fields(b), "top")));
    std::map<std::vector<double>, std::string> by_box;  // box_owners: the last block on a box names it
    const Array& solids = items(at(fields(at(scene, "collisionScene")), "solids"));
    for (const auto& b : items(at(scene, "blocks"))) {
        std::vector<double> key;
        for (const auto& v : items(solids.at(as_int(at(fields(b), "solidIndex"))))) key.push_back(num(v).value);
        by_box[key] = anchors.owner(as_text(at(fields(b), "id")));
    }
    const Value* hits = get(scene, "ceilingHits");
    const std::vector<FaceOption> undersides = enabled("ceiling") && !(hits && !py_truthy(*hits))
                                                   ? scene_contact_options(gate.scene(), gate.geometry(), 1, -1)
                                                   : std::vector<FaceOption>{};
    auto rest = [](const Num& y) { return ScalarState{true, y, false, Num{-kGravity * kYDrag, false}}; };
    auto landing = [](const Trace& trace, const std::vector<double>& heights) {
        std::optional<std::int64_t> best;
        for (double u : heights)
            if (auto t = first_descent(trace, u); t && (!best || *t + 1 < *best)) best = *t + 1;
        return best;
    };
    auto heights_of = [&](const std::set<std::string>& names, const std::function<bool(const std::string&)>& keep) {
        std::vector<double> out;
        for (const auto& n : names)
            if (keep(n)) out.push_back(tops.at(n).value);
        return out;
    };
    // jumps(launch, top): (kind, name) -> A, in first-added order
    std::vector<std::pair<std::pair<std::string, Value>, std::int64_t>> found;
    auto add_jump = [&](std::string kind, Value name, std::optional<std::int64_t> airtime) {
        if (!airtime) return;
        for (auto& [key, a] : found)
            if (key.first == kind && py_equal(key.second, name)) {
                if (*airtime < a) a = *airtime;
                return;
            }
        found.push_back({{std::move(kind), std::move(name)}, *airtime});
    };
    const FaceTable& kinds = gate.reachable_faces(launch);
    const std::vector<double> landing_tops = heights_of(kinds.landing, [](const std::string&) { return true; });
    const Trace trace = replay(rest(top), 200, true, {}, {}, threshold);
    if (enabled("normal"))
        add_jump("anchor", text(next_owner),
                 landing(trace, heights_of(kinds.landing, [&](const std::string& b) { return anchors.owner(b) == next_owner; })));
    for (const auto& option : undersides) {
        if (std::none_of(option.members.begin(), option.members.end(), [&](std::int64_t i) { return kinds.ceiling_solids.count(i); }))
            continue;
        std::set<std::string> owners;
        for (const auto& b : option.boxes) {
            std::vector<double> key;
            for (const auto& v : items(b)) key.push_back(num(v).value);
            if (auto o = by_box.find(key); o != by_box.end()) owners.insert(o->second);
        }
        std::string names;
        for (const auto& o : owners) names += (names.empty() ? "" : "+") + o;
        const double plane = num(option.plane).value;
        std::optional<std::int64_t> hit;
        for (std::size_t h = 0; h < trace.movement.size(); ++h)
            if (trace.movement[h].value > 0 && trace.position[h].value <= plane && plane < trace.position[h + 1].value) {
                hit = static_cast<std::int64_t>(h);
                break;
            }
        if (hit) {
            VerticalContact contact;
            contact.state = *hit + 1;
            contact.plane = num(option.plane);
            contact.direction = 1;
            add_jump("ceiling", text(names), landing(replay(rest(top), 200, true, {contact}, {}, threshold), landing_tops));
        }
        const Num clearance = sub(num(option.plane), top);
        if (0 <= clearance.value && clearance.value < f32(.42)) {
            // case_profile(top, u, grind at input 0): the duration of a grind landing on each top
            std::optional<std::int64_t> best;
            for (const auto& [name, u] : tops) {
                if (!kinds.landing.count(name)) continue;
                Array jump(200, Value{nullptr});
                jump[0] = real(kJumpVelocity);
                const Value contact = object({{"case", text("grind")}, {"plane", value(top)}, {"clearance", value(clearance)},
                                              {"state", integer_value(1)}, {"direction", integer_value(-1)}});
                const Value t = trace_ladder_schedule(
                    object({{"position", value(top)}, {"velocity", real(-kGravity * kYDrag)}, {"onGround", boolean(true)}}),
                    array(Array(201, boolean(false))), array(Array(200, boolean(false))), array(Array(200, boolean(false))),
                    array(std::move(jump)), value(u), integer_value(2), array({contact}), Value{nullptr}, true, value(threshold));
                const Object& tr = fields(t);
                if (as_text(at(tr, "status")) == "invalid" || !py_truthy(items(at(tr, "onGround")).back()) ||
                    std::fabs(num(items(at(tr, "position")).back()).value - u.value) > 1e-8)
                    continue;
                const std::int64_t duration = as_int(at(tr, "ticks"));
                if (!best || duration < *best) best = duration;
            }
            add_jump("grind", text(names), best);
        }
    }
    // corner_airtime(name, t): the fastest corner jump cancel past a convex corner of block `name` onto a ledge at or
    // below the feet: case_profile(top, ledge, the cancel at input 0)'s duration.
    auto corner_airtime = [&](const std::string& name) {
        GenerationContext context;
        context.gate = &gate;
        std::optional<std::int64_t> best;
        for (const auto& b : items(at(scene, "blocks"))) {
            const Object& block = fields(b);
            if (as_text(at(block, "id")) != name) continue;
            const std::int64_t index = as_int(at(block, "solidIndex"));
            Array boxes;
            for (const auto& option : gate.top_faces())
                if (std::count(option.members.begin(), option.members.end(), index)) {
                    boxes = option.boxes;
                    break;
                }
            for (std::int64_t xdir : {-1, 1})
                for (std::int64_t zdir : {-1, 1}) {
                    if (!convex_corner(context, block, xdir, zdir, top.value)) continue;
                    for (double u : corner_ledges(context, items(solids.at(index)), xdir, zdir, top.value, boxes)) {
                        Array jump(200, Value{nullptr});
                        jump[0] = real(kJumpVelocity);
                        const Value contact = object({{"case", text("corner_jump_cancel")}, {"plane", value(top)},
                                                      {"higherHeight", at(block, "top")}, {"state", integer_value(1)},
                                                      {"direction", integer_value(-1)}});
                        const Value t = trace_ladder_schedule(
                            object({{"position", value(top)}, {"velocity", real(-kGravity * kYDrag)}, {"onGround", boolean(true)}}),
                            array(Array(201, boolean(false))), array(Array(200, boolean(false))),
                            array(Array(200, boolean(false))), array(std::move(jump)), real(u), integer_value(2),
                            array({contact}), Value{nullptr}, true, value(threshold));
                        const Object& tr = fields(t);
                        if (as_text(at(tr, "status")) == "invalid" || !py_truthy(items(at(tr, "onGround")).back()) ||
                            std::fabs(num(items(at(tr, "position")).back()).value - u) > 1e-8)
                            continue;
                        const std::int64_t duration = as_int(at(tr, "ticks"));
                        if (!best || duration < *best) best = duration;
                    }
                }
        }
        return best;
    };
    if (enabled("stepping"))
        for (const auto& b : kinds.stepping)
            if (f32(.42) < tops.at(b).value - top.value && tops.at(b).value - top.value <= f32(.6)) {
                add_jump("slab_boost", text(anchors.support_face(b)), 2);
                add_jump("corner_jump_cancel", text(anchors.support_face(b)), corner_airtime(b));
            }
    std::map<std::string, Object> entries;  // sorted by key, as the Python returns them
    auto add = [&](const Value& key_list, std::int64_t airtime, Value via, Object bound) {
        const std::string key = dumps(key_list, false, false);
        auto it = entries.find(key);
        if (it != entries.end() && airtime >= as_int(at(it->second, "A"))) return;
        Object entry{{"key", text(key)}, {"A", integer_value(airtime)}, {"via", std::move(via)}};
        for (auto& f : bound) entry.push_back(std::move(f));
        entries[key] = std::move(entry);
    };
    for (const auto& [key, airtime] : found) add(array({text(key.first), key.second}), airtime, Value{nullptr}, {});
    const Trace fall = replay(rest(top), 200, false, {}, {}, threshold);
    std::set<std::string> candidates(kinds.landing.begin(), kinds.landing.end());
    candidates.insert(kinds.stepping.begin(), kinds.stepping.end());
    for (const auto& b : candidates) {
        const std::string name = anchors.support_face(b);
        const double u = tops.at(b).value;
        if (launch && name == *launch) continue;
        if (enabled("normal") && kinds.landing.count(b) && (u < top.value || (u == top.value && name == next_owner))) {
            const auto t = first_descent(fall, u);
            const auto limit = landing(trace, heights_of(kinds.landing, [&](const std::string& c) { return anchors.support_face(c) == name; }));
            if (!t || !limit) continue;
            add(array({text("landing"), text(name)}), *t, text(name), {{"limit", integer_value(*limit)}});
        } else if (enabled("stepping") && kinds.stepping.count(b) && 0 < u - top.value && u - top.value <= f32(.6)) {
            add(array({text("step"), text(name)}), 0, text(name), {});
        }
    }
    Array out;
    for (auto& [_, entry] : entries) out.push_back(object(std::move(entry)));
    return out;
}

MinimumRuns minimum_runs(const NodeSearch& search, const Program& program, const Value& reached_value,
                         const Value& landings_value, const std::vector<std::string>& stop_supports,
                         const std::array<double, 4>& initial_bounds, std::int64_t settings_max_inputs,
                         const std::set<std::string>& seen, const std::function<void()>& budget) {
    const NodeFrame& f = search.frame;
    const std::int64_t max_runs = flat_jump_ticks() - 1;  // MAX_ADVANTAGEOUS_RUN_TICKS
    const std::vector<std::string> supports =
        stop_supports.empty() ? std::vector<std::string>{as_text(at(fields(f.scene), "goalBlock"))} : stop_supports;
    auto complete = [&](const Program& p) {
        return p.profile.grounded.back() && supported_by(p, supports) && p.profile.duration > 0;
    };
    MinimumRuns out;
    const Value* search_fields = get(program.annotations, "minimumRunSearch");
    const bool held = search_fields && !is_null(*search_fields);
    const Value node = held ? at(fields(*search_fields), "node") : get_or_null(program.annotations, "decisionNode");
    const std::string upcoming = search.anchors->next_anchor(program, f.anchors, f.task_block);
    const std::optional<std::string> here = terminal_face(search, program);
    auto lands = [&](const Program& child) { return child.profile.grounded.back() ? terminal_face(search, child) : std::nullopt; };
    auto text_or_null = [](const std::optional<std::string>& v) { return v ? text(*v) : Value{nullptr}; };
    auto target_keys = [&](const Program& base, const Program& child) {
        const std::string next = search.anchors->next_anchor(base, f.anchors, f.task_block);
        std::set<std::string> keys;
        for (const auto& [kind, name] : search.anchors->advantageous_targets(base, child, next))
            keys.insert(dumps(array({text(kind), text_or_null(name)}), false, false));
        return keys;
    };
    auto lands_later = [&](const Program& base, const Program& child) {
        return !child.profile.grounded.back() && !target_keys(base, child).empty();
    };
    std::set<std::string> queued_ids;

    struct Item {
        std::shared_ptr<const Program> base;
        MovementChoice choice;
        Composed proposal;
        bool checked = false, has_child = false;
        std::string reason;  // "" is None
        std::optional<std::set<std::string>> keys;
    };
    std::vector<std::vector<std::shared_ptr<const Program>>> bases{{std::make_shared<const Program>(program)}};
    std::vector<std::shared_ptr<std::vector<Item>>> levels;

    auto check_item = [&](Item& item) -> Item& {  // check(item)
        if (!item.checked) {
            const Checked c = check(search, item.proposal, initial_bounds, seen, &queued_ids, out.effects);
            out.exclusions.push_back(object({{"exclusions", array(c.exclusions)}, {"screened", integer_value(c.screened)},
                                             {"policyRejections", integer_value(c.policy_rejections)}}));
            if (c.outcome == "accepted" || c.outcome == "seen") {
                item.has_child = true;
                link(search, *item.base, meaningful_choice(item.choice), *item.proposal.owned, complete(*item.proposal.owned));
            }
            item.reason = c.outcome == "accepted" ? "" : c.outcome;
            item.checked = true;
        }
        return item;
    };
    std::function<std::vector<Item>&(std::size_t)> level = [&](std::size_t k) -> std::vector<Item>& {
        while (levels.size() <= k) {
            if (levels.size() == bases.size()) {
                std::set<std::string> ids;
                std::vector<std::shared_ptr<const Program>> found;
                for (auto& item : *levels.back()) {
                    const Program* child = item.proposal.owned ? &*item.proposal.owned : nullptr;
                    if (item.choice.profile.jump.at(0) || meaningful_choice(item.choice) || (lands(*child) == upcoming))
                        continue;
                    check_item(item);
                    if (item.has_child) {
                        const std::string key = program_search_key(*item.proposal.owned);
                        if (ids.insert(key).second) found.push_back(std::make_shared<const Program>(*item.proposal.owned));
                    }
                }
                bases.push_back(std::move(found));
            }
            auto items = std::make_shared<std::vector<Item>>();
            for (const auto& base : bases[levels.size()]) {
                const GenerationContext context = expansion_context(search, base, program, settings_max_inputs, true, budget);
                for (const auto& type : f.types)
                    for (auto& choice : generate_choices(type, context)) {
                        Composed c = compose(search, *base, choice, supports, settings_max_inputs);
                        if (!c.owned) continue;
                        Item item;
                        item.base = base;
                        item.choice = std::move(choice);
                        item.proposal = std::move(c);
                        items->push_back(std::move(item));
                    }
            }
            levels.push_back(items);
        }
        return *levels[k];
    };
    std::map<std::string, std::vector<std::pair<std::optional<std::string>, std::int64_t>>> peeked;
    auto landing = [&](const Program& child) -> const std::vector<std::pair<std::optional<std::string>, std::int64_t>>& {
        const std::string key = program_search_key(child);
        auto found = peeked.find(key);
        if (found == peeked.end()) {
            std::vector<std::pair<std::optional<std::string>, std::int64_t>> times;
            for (auto& [owner, landed] : landings_of(search, child, program, supports, settings_max_inputs, initial_bounds,
                                                     seen, nullptr, out.effects, budget))
                times.emplace_back(owner, landed.profile.duration - program.profile.duration);
            found = peeked.emplace(key, std::move(times)).first;
        }
        return found->second;
    };
    struct Found {
        Item* item;
    };
    auto built = [&](std::int64_t k, const std::vector<Found>& found) -> std::pair<Value, Value> {
        std::optional<std::int64_t> best;
        for (const auto& f2 : found)
            for (const auto& [_, t] : landing(*f2.item->proposal.owned))
                if (!best || t < *best) best = t;
        if (!best) return {Value{nullptr}, Value{nullptr}};
        return {integer_value(*best), integer_value(*best - k)};
    };
    auto decision_node = [](const Item& item) { return get_or_null(item.proposal.owned->annotations, "decisionNode"); };
    auto enqueue = [&](Item& item, const std::shared_ptr<Array>& landed) {
        const Program& child = *item.proposal.owned;
        const std::string key = program_search_key(child);
        if (item.reason.empty() && !queued_ids.count(key)) {
            out.queued.push_back({child, meaningful_choice(item.choice), key, item.base, item.choice.side_variant});
            queued_ids.insert(key);
        }
        if (child.profile.grounded.back() || lands_later(*item.base, child))
            for (const auto& [owner, _] : landing(child))
                if (owner) landed->push_back(object({{"owner", text(*owner)}, {"node", decision_node(item)}}));
    };
    auto key_kind = [](const Value& entry) { return as_text(items(loads(as_text(at(fields(entry), "key"))))[0]); };
    auto realize = [&](const Value& entry_value, std::int64_t k) {
        const Object& entry = fields(entry_value);
        std::vector<Found> found;
        for (auto& item : level(k)) {
            const Program& child = *item.proposal.owned;
            const bool jump = item.choice.profile.jump.at(0);
            bool wanted;
            if (is_null(get_or_null(entry, "via"))) {
                if (jump && !item.keys) item.keys = target_keys(*item.base, child);
                wanted = jump && item.keys->count(as_text(at(entry, "key")));
            } else {
                const auto target = lands(child);
                const std::string via = as_text(at(entry, "via"));
                wanted = !jump && target == via && (via == upcoming || meaningful_choice(item.choice));
            }
            if (wanted && check_item(item).has_child) found.push_back({&item});
        }
        return found;
    };
    auto runs = [&](const Value& entry, std::int64_t k) {
        const std::string kind = key_kind(entry);
        return kind == "landing" || kind == "step" ? k + 1 : k;
    };
    auto nodes_of = [&](const std::vector<Found>& found) {
        Array nodes;
        for (const auto& f2 : found) nodes.push_back(decision_node(*f2.item));
        return nodes;
    };
    auto record = [&](const Value& entry, Value k, Value total, Value N, std::int64_t lower, std::int64_t number,
                      const char* outcome, Array nodes = {}, std::optional<Value> A = std::nullopt) {
        out.log.push_back(object({{"node", node}, {"key", at(fields(entry), "key")}, {"k", std::move(k)},
                                  {"total", std::move(total)}, {"A", A ? *A : get_or_null(fields(entry), "A")},
                                  {"N", std::move(N)}, {"lower", integer_value(lower)}, {"round", integer_value(number)},
                                  {"outcome", text(outcome)}, {"nodes", array(std::move(nodes))}}));
    };
    struct Reserve {
        Object fields;
        Array watch;
        std::shared_ptr<Array> kept, landed;
    };
    std::vector<Reserve> reserves;
    auto reserve = [&](Array watch, Object fields, std::shared_ptr<Array> kept = nullptr, std::shared_ptr<Array> landed = nullptr) {
        Object all{{"node", node}};
        for (auto& f2 : fields) all.push_back(std::move(f2));
        reserves.push_back({std::move(all), std::move(watch), std::move(kept), std::move(landed)});
    };
    using Landings = Array;
    auto source = [&](const Landings& landings, const std::set<std::string>& dead) -> std::optional<std::pair<std::int64_t, std::string>> {
        for (int pass = 0; pass < 2; ++pass) {
            std::optional<std::pair<std::int64_t, std::string>> best;
            for (const auto& l : landings) {
                const std::string owner = as_text(at(fields(l), "owner"));
                if (pass == 0 ? owner != upcoming : dead.count(owner)) continue;
                const std::pair<std::int64_t, std::string> t{as_int(at(fields(l), "ticks")), owner};
                if (!best || t < *best) best = t;
            }
            if (best) return best;
        }
        return std::nullopt;
    };
    auto candidates = [&] {
        return advantageous_candidates(*search.gate, *search.anchors, terminal_face(search, program),
                                       program.profile.position.back(), upcoming, f.types, Num{0., false});
    };
    auto take_entry = [&](const Value& entry_value, std::int64_t start, std::int64_t N, std::int64_t number,
                          const std::shared_ptr<Array>& landed) {
        const std::int64_t A = as_int(at(fields(entry_value), "A"));
        for (std::int64_t k = start; k <= max_runs; ++k) {
            if (k + A >= N) break;
            const auto found = realize(entry_value, k);
            if (found.empty()) continue;
            const Array nodes = nodes_of(found);
            for (const auto& f2 : found) enqueue(*f2.item, landed);
            const auto [total, built_A] = built(k, found);
            record(entry_value, integer_value(k), total, integer_value(N), 0, number, "kept", nodes, built_A);
            if (k + 1 <= max_runs && k + 1 + A < N)
                reserve(nodes, {{"kind", text("next")}, {"entry", entry_value}, {"start", integer_value(k + 1)},
                                {"N", integer_value(N)}, {"round", integer_value(number)}});
            return;
        }
        record(entry_value, Value{nullptr}, Value{nullptr}, integer_value(N), 0, number, "unreached");
    };
    auto jumped = [&] {
        std::set<std::optional<std::string>> faces;
        for (auto& item : level(0))
            if (item.choice.profile.jump.at(0)) faces.insert(lands(*item.proposal.owned));
        return faces;
    };
    auto dead_value = [](const std::set<std::string>& dead) {
        Array out2;
        for (const auto& d : dead) out2.push_back(text(d));
        return array(std::move(out2));
    };
    std::function<void(const std::set<std::string>&, std::int64_t, std::int64_t, const std::shared_ptr<Array>&, std::int64_t,
                       const std::shared_ptr<Array>&, const Landings&, bool)>
        round_ = [&](const std::set<std::string>& dead, std::int64_t lower, std::int64_t N, const std::shared_ptr<Array>& kept,
                     std::int64_t number, const std::shared_ptr<Array>& landed, const Landings& landings, bool widen) {
            struct Pair {
                std::int64_t total, A;
                std::string key;
                std::int64_t k;
                Value entry;
            };
            std::vector<Pair> pairs;
            std::optional<std::set<std::optional<std::string>>> reachable;
            for (const auto& entry_original : candidates()) {
                Value entry = entry_original;
                const Object& e = fields(entry);
                const std::string kind = key_kind(entry);
                const std::int64_t A = as_int(at(e, "A"));
                std::vector<std::int64_t> counts;
                if (is_null(get_or_null(e, "via"))) {
                    for (std::int64_t k = 1; k <= max_runs; ++k)
                        if (lower <= k + A && k + A < N) counts.push_back(k);
                } else if (lower) {
                    continue;  // run-offs and step-ups are offered once per decision, in its first round
                } else if (kind == "step") {
                    counts = {0, 1};
                } else {
                    std::int64_t limit = as_int(at(e, "limit"));
                    for (const auto& l : landings)
                        if (py_equal(at(fields(l), "owner"), at(e, "via"))) limit = std::min(limit, as_int(at(fields(l), "ticks")));
                    entry = object(with(e, "limit", integer_value(limit)));
                    if (!reachable) reachable = jumped();
                    if (reachable->count(std::optional<std::string>(as_text(at(e, "via")))))
                        for (std::int64_t k = 0; k < max_runs; ++k)
                            if (k + 1 + A < limit) counts.push_back(k);
                }
                for (auto k : counts) pairs.push_back({runs(entry, k) + A, A, as_text(at(e, "key")), k, entry});
            }
            std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
                return std::tie(a.total, a.A, a.key, a.k) < std::tie(b.total, b.A, b.key, b.k);
            });
            std::set<std::string> resolved;
            std::vector<std::pair<std::vector<std::string>, Array>> beaten;
            for (const auto& pair : pairs) {
                if (resolved.count(pair.key)) continue;
                const Object& e = fields(pair.entry);
                const bool jump_entry = is_null(get_or_null(e, "via"));
                std::set<std::string> beater_nodes;
                bool beaten_here = false;
                for (const auto& m : *kept) {
                    const Object& mo = fields(m);
                    const std::int64_t mt = as_int(at(mo, "total")), ma = as_int(at(mo, "A"));
                    if (jump_entry && mt <= pair.total && ma <= pair.A && !(mt == pair.total && ma == pair.A)) {
                        beaten_here = true;
                        for (const auto& n2 : items(at(mo, "nodes"))) beater_nodes.insert(as_text(n2));
                    }
                }
                if (beaten_here) {
                    resolved.insert(pair.key);
                    record(pair.entry, integer_value(runs(pair.entry, pair.k)), integer_value(pair.total), integer_value(N), lower,
                           number, "dominated");
                    std::vector<std::string> watch(beater_nodes.begin(), beater_nodes.end());
                    auto it = std::find_if(beaten.begin(), beaten.end(), [&](const auto& b) { return b.first == watch; });
                    if (it == beaten.end()) {
                        beaten.push_back({watch, {}});
                        it = std::prev(beaten.end());
                    }
                    it->second.push_back(object({{"entry", pair.entry}, {"start", integer_value(pair.k)}}));
                    continue;
                }
                const auto found = realize(pair.entry, pair.k);
                if (found.empty()) continue;
                resolved.insert(pair.key);
                const Array nodes = nodes_of(found);
                for (const auto& f2 : found) enqueue(*f2.item, landed);
                if (!jump_entry) {
                    record(pair.entry, integer_value(runs(pair.entry, pair.k)), integer_value(pair.total), get_or_null(e, "limit"),
                           lower, number, "kept", nodes);
                    continue;
                }
                const auto [landed_total, landed_A] = built(pair.k, found);
                record(pair.entry, integer_value(pair.k), landed_total, integer_value(N), lower, number, "kept", nodes, landed_A);
                if (!is_null(landed_total))
                    kept->push_back(object({{"key", text(pair.key)}, {"total", landed_total}, {"A", landed_A}, {"nodes", array(nodes)}}));
                if (pair.k + 1 <= max_runs && pair.k + 1 + pair.A < N)
                    reserve(nodes, {{"kind", text("next")}, {"entry", pair.entry}, {"start", integer_value(pair.k + 1)},
                                    {"N", integer_value(N)}, {"round", integer_value(number)}});
            }
            for (const auto& pair : pairs)
                if (resolved.insert(pair.key).second) {
                    const Object& e = fields(pair.entry);
                    const Value* limit = get(e, "limit");
                    record(pair.entry, Value{nullptr}, Value{nullptr},
                           limit ? *limit : is_null(get_or_null(e, "via")) ? integer_value(N) : Value{nullptr}, lower, number,
                           "unreached");
                }
            for (const auto& [watch, entries] : beaten) {
                Array w;
                for (const auto& n2 : watch) w.push_back(text(n2));
                reserve(w, {{"kind", text("dominated")}, {"entries", array(entries)}, {"N", integer_value(N)},
                            {"round", integer_value(number)}});
            }
            if (widen) {
                const std::string blocked = source(landings, dead)->second;
                if (blocked != upcoming) {
                    Array watch;
                    for (const auto& l : landings)
                        if (as_text(at(fields(l), "owner")) == blocked) watch.push_back(at(fields(l), "node"));
                    for (const auto& l : *landed)
                        if (as_text(at(fields(l), "owner")) == blocked) watch.push_back(at(fields(l), "node"));
                    reserve(watch, {{"kind", text("widen")}, {"dead", dead_value(dead)}, {"landings", array(landings)},
                                    {"lower", integer_value(N)}, {"kept", Value{nullptr}}, {"landed", Value{nullptr}},
                                    {"round", integer_value(number + 1)}},
                            kept, landed);
                }
            }
        };
    auto scan = [&](const std::set<std::string>& dead, std::int64_t start, std::int64_t lower, const std::shared_ptr<Array>& kept,
                    std::int64_t number, const std::shared_ptr<Array>& landed, const Landings& landings) {
        struct Lander {
            Item* item;
            std::string owner;
            std::int64_t ticks;
        };
        auto landers = [&](std::int64_t k, const std::function<bool(const std::string&)>& wanted) {
            std::vector<Lander> found;
            for (auto& item : level(k)) {
                const Program& child = *item.proposal.owned;
                const bool jump = item.choice.profile.jump.at(0);
                if (jump && k == 0) continue;
                if (!child.profile.grounded.back()) {
                    if (jump && lands_later(*item.base, child) && check_item(item).has_child) {
                        std::optional<std::pair<std::int64_t, std::string>> best;
                        for (const auto& [owner, t] : landing(*item.proposal.owned))
                            if (owner && wanted(*owner) && (!best || std::make_pair(t, *owner) < *best)) best = std::make_pair(t, *owner);
                        if (best) found.push_back({&item, best->second, best->first});
                    }
                    continue;
                }
                const auto target = lands(child);
                if (!target || !wanted(*target) ||
                    (!jump && (k == max_runs || (*target != upcoming && !meaningful_choice(item.choice)))))
                    continue;
                if (check_item(item).has_child)
                    found.push_back({&item, *target, item.proposal.owned->profile.duration - program.profile.duration});
            }
            return found;
        };
        std::vector<Lander> found;
        std::string target_name;
        std::int64_t k = 0;
        bool hit = false;
        const std::vector<std::pair<std::string, std::function<bool(const std::string&)>>> targets{
            {"next", [&](const std::string& o) { return o == upcoming; }},
            {"block", [&](const std::string& o) { return !dead.count(o) && (!here || o != *here) && o != upcoming; }}};
        for (const auto& [name, wanted] : targets) {
            for (k = start; k <= max_runs; ++k) {
                found = landers(k, wanted);
                if (!found.empty()) break;
            }
            if (!found.empty()) {
                target_name = name;
                hit = true;
                break;
            }
        }
        if (!hit) {
            record(object({{"key", text(dumps(array({text("scan")}), false, false))}, {"A", Value{nullptr}}}), Value{nullptr},
                   Value{nullptr}, Value{nullptr}, lower, number, "unreached");
            return;
        }
        Array nodes;
        for (const auto& l : found) nodes.push_back(decision_node(*l.item));
        for (const auto& l : found) enqueue(*l.item, landed);
        auto scan_runs = [&](const Item& item) { return item.choice.profile.jump.at(0) ? k : k + 1; };
        const Lander* fastest = &found.front();
        for (const auto& l : found)
            if (l.ticks < fastest->ticks) fastest = &l;
        const std::int64_t N = fastest->ticks, R = scan_runs(*fastest->item);
        for (const auto& l : found)
            kept->push_back(object({{"key", text(dumps(array({text("scan"), text(l.owner)}), false, false))},
                                    {"total", integer_value(l.ticks)}, {"A", integer_value(l.ticks - scan_runs(*l.item))},
                                    {"nodes", array({decision_node(*l.item)})}}));
        record(object({{"key", text(dumps(array({text("scan"), text(target_name)}), false, false))}, {"A", integer_value(N - R)}}),
               integer_value(R), integer_value(N), integer_value(N), lower, number, "kept", nodes);
        if (k + 1 <= max_runs) {
            std::set<std::string> next_dead = dead;
            if (target_name != "next")
                for (const auto& l : found) next_dead.insert(l.owner);
            reserve(nodes, {{"kind", text("scan")}, {"target", text(target_name)}, {"dead", dead_value(next_dead)},
                            {"start", integer_value(k + 1)}, {"lower", integer_value(N)}, {"kept", Value{nullptr}},
                            {"landed", Value{nullptr}}, {"round", integer_value(number + 1)}},
                    kept, landed);
        }
        round_(dead, lower, N, kept, number, landed, landings, false);
    };
    auto texts = [](const Value& v) {
        std::set<std::string> out2;
        for (const auto& x : items(v)) out2.insert(as_text(x));
        return out2;
    };
    auto shared_array = [](const Value& v) { return std::make_shared<Array>(items(v)); };
    const std::string kind = held ? as_text(at(fields(*search_fields), "kind")) : "";
    if (kind == "next") {
        const Object& s2 = fields(*search_fields);
        take_entry(at(s2, "entry"), as_int(at(s2, "start")), as_int(at(s2, "N")), as_int(at(s2, "round")), std::make_shared<Array>());
    } else if (kind == "dominated") {
        const Object& s2 = fields(*search_fields);
        for (const auto& h2 : items(at(s2, "entries")))
            take_entry(at(fields(h2), "entry"), as_int(at(fields(h2), "start")), as_int(at(s2, "N")), as_int(at(s2, "round")),
                       std::make_shared<Array>());
    } else if (kind == "scan") {
        const Object& s2 = fields(*search_fields);
        scan(texts(at(s2, "dead")), as_int(at(s2, "start")), as_int(at(s2, "lower")), shared_array(at(s2, "kept")),
             as_int(at(s2, "round")), shared_array(at(s2, "landed")), Array{});
    } else {
        Landings landings;
        std::int64_t lower = 0, number = 0;
        std::shared_ptr<Array> kept, landed;
        std::set<std::string> dead;
        if (kind == "widen") {
            const Object& s2 = fields(*search_fields);
            landings = items(at(s2, "landings"));
            lower = as_int(at(s2, "lower"));
            kept = shared_array(at(s2, "kept"));
            landed = shared_array(at(s2, "landed"));
            number = as_int(at(s2, "round"));
            dead = texts(at(s2, "dead"));
            dead.insert(source(landings, dead)->second);
        } else {
            landings = items(landings_value);
            landed = std::make_shared<Array>();
            kept = std::make_shared<Array>();
            std::set<std::string> entries;
            for (const auto& entry : candidates())
                if (is_null(get_or_null(fields(entry), "via"))) entries.insert(as_text(at(fields(entry), "key")));
            for (const auto& [key, reached] : fields(reached_value)) {
                if (!entries.count(key)) continue;
                std::optional<std::int64_t> A;
                Array nodes;
                for (const auto& r : items(reached)) {
                    nodes.push_back(items(r)[0]);
                    if (!is_null(items(r)[1]) && (!A || as_int(items(r)[1]) < *A)) A = as_int(items(r)[1]);
                }
                if (A)
                    kept->push_back(object({{"key", text(key)}, {"total", integer_value(*A)}, {"A", integer_value(*A)},
                                            {"nodes", array(std::move(nodes))}}));
            }
        }
        const auto found = source(landings, dead);
        if (!found) {
            scan(dead, 0, lower, kept, number, landed, landings);
        } else {
            round_(dead, lower, found->first, kept, number, landed, landings, true);
        }
    }
    for (auto& r : reserves) {
        if (r.kept) put(r.fields, "kept", array(*r.kept));
        if (r.landed) put(r.fields, "landed", array(*r.landed));
        out.reserves.push_back(array({object(std::move(r.fields)), array(std::move(r.watch))}));
    }
    return out;
}

}  // namespace pkc
