// Port of search/frontier.py's per-proposal loop (one movement type of an expansion that is not an ordinary
// decision's) with search/dfs.py's screen, eligible, time_bound, hard_bound, run_bound and configure.
#include "pkc/search/node.hpp"

#include <algorithm>
#include <cmath>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/movement/generation.hpp"
#include "pkc/reachability/viability.hpp"
#include "pkc/search/bounds/remaining.hpp"
#include "pkc/search/comparison.hpp"
#include "pkc/search/feasibility.hpp"
#include "pkc/search/goals.hpp"

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
    const Object context{{"fixedTerminal", boolean(false)}};
    Value decision = compare_candidates(object({{"T", integer_value(T)}}), Value{nullptr}, context, f.incumbent);
    if (py_equal(at(fields(decision), "action"), text("exclude_bound"))) return incumbent_proof(decision, T);
    const std::int64_t span = std::min(anchors->audit(program, f.anchors).reached, f.group_end);
    const Value proof = remaining_bound(f.guide_plan, span, program, f.motion, f.transfers, f.hard_latches);
    const Value* ids = get(fields(proof), "proofIds");
    const Value label = object({{"T", integer_value(T)},
                                {"remainingFinishBound", object({{"ticks", at(fields(proof), "remainingTicks")},
                                                                 {"proofIds", ids ? *ids : array({})}})}});
    decision = compare_candidates(label, Value{nullptr}, context, f.incumbent);
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
    const Value* turnaround = note(program, "priorityTurnaround");
    const Value label = comparison_label(
        program, f.span_start, std::nullopt, identity, turnaround && py_truthy(*turnaround) ? "turnaround" : "ordinary",
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
             {"isTurnaround", turnaround ? *turnaround : boolean(false)}, {"latchRevision", integer_value(f.latch_revision)}}));
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

// dfs.configure and AnchorAdapter.context for a decision expanding itself.
GenerationContext expansion_context(const NodeSearch& search, const std::shared_ptr<const Program>& parent,
                                    std::int64_t settings_max_inputs, const std::function<void()>& budget) {
    const NodeFrame& f = search.frame;
    GenerationContext context;
    context.program = parent;
    context.state = boundary_state(*parent);
    context.max_inputs = settings_max_inputs - parent->profile.duration;
    context.threshold = Num{0., false};
    context.gate = search.gate;
    context.check_budget = budget;
    context.frame_id = f.frame_id;
    context.region_items = true;
    if (auto launch = search.anchors->launch(*parent)) context.launch_face = launch->second;
    std::set<std::string> excluded;
    for (const auto& d : search.anchors->visit_state(*parent).departed) excluded.insert(d);
    for (std::size_t a = f.group_end + 1; a < f.anchors.size(); ++a) excluded.insert(search.anchors->owner(f.anchors[a]));
    if (f.group_end < static_cast<std::int64_t>(f.anchors.size())) excluded.insert(search.anchors->owner(f.task_block));
    context.feet_allowed_fn = [excluded](const std::string& name) { return !excluded.count(name); };
    const Value* turnaround = get(parent->annotations, "decisionTurnaround");
    if (!(turnaround && py_truthy(*turnaround))) {
        const std::int64_t k = std::min(search.anchors->audit(*parent, f.anchors).reached, f.group_end);
        const Array& points = items(at(fields(f.guide_plan), "points"));
        if (k + 1 < static_cast<std::int64_t>(points.size())) {
            const Array &a = items(points[k]), &b = items(points[k + 1]);
            const double dx = num(b[0]).value - num(a[0]).value, dz = num(b[2]).value - num(a[2]).value;
            const double length = python_hypot(dx, dz);
            if (length > 1e-12) {
                const Array& point = items(at(parent->annotations, "decisionPoint"));
                context.forward = Forward{{num(point[0]).value, num(point[1]).value}, {dx / length, dz / length},
                                          num(f.turnaround_margin)};
            }
        }
    }
    return context;
}

Value exclusion(const Program& program, const Value& proof, const char* scope) {
    return object({{"id", text(digest(text(program_physics_key(program))))},
                   {"ticks", integer_value(program.profile.duration)}, {"proof", proof}, {"scope", text(scope)}});
}

}  // namespace

Expansion expand_type(const NodeSearch& search, const Program& parent_program, const std::string& kind, std::int64_t cursor,
                      const std::vector<std::string>& stop_supports, const std::array<double, 4>& initial_bounds,
                      std::int64_t settings_max_inputs, std::int64_t choices_per_type, bool ready,
                      std::set<std::string> seen, const std::function<void()>& budget) {
    const NodeFrame& f = search.frame;
    auto parent = std::make_shared<const Program>(parent_program);
    const GenerationContext context = expansion_context(search, parent, settings_max_inputs, budget);
    const std::vector<MovementChoice> choices = generate_choices(kind, context);
    const std::vector<std::string> supports =
        stop_supports.empty() ? std::vector<std::string>{as_text(at(fields(f.scene), "goalBlock"))} : stop_supports;
    auto complete = [&](const Program& p) {
        return p.profile.grounded.back() && supported_by(p, supports) && p.profile.duration > 0;
    };
    Expansion expansion;
    std::vector<ProposalRecord>& records = expansion.records;
    auto emits = [&](std::int64_t ordinal) { return ready && (ordinal + 1) % choices_per_type == 0; };
    for (std::size_t ordinal = 0; ordinal < choices.size(); ++ordinal) {
        budget();
        if (static_cast<std::int64_t>(ordinal) < cursor) continue;
        ProposalRecord record;
        record.ordinal = static_cast<std::int64_t>(ordinal);
        Array exclusions;
        // propose_choices: compose, inherit the decision fields, project onto the owned landing.
        std::optional<Program> full = append_choice(*parent, choices[ordinal], settings_max_inputs);
        std::optional<Program> owned;
        bool projected = false;
        if (full) {
            for (const char* field : {"decisionPoint", "decisionVelocity", "decisionPointSource", "decisionPointTick",
                                      "decisionTurnaround", "restoredRun"})
                if (const Value* v = get(parent->annotations, field)) annotate(*full, field, *v);
            const Value* turnaround = get(parent->annotations, "decisionTurnaround");
            annotate(*full, "priorityTurnaround", turnaround ? *turnaround : boolean(false));
            std::optional<LandingPrefix> best;  // min(arrivals, key=(duration, terminalSupport))
            for (const auto& block : supports)
                for (auto& prefix : landing_prefixes(f.scene, *full, block, true))
                    if (!best || std::make_pair(prefix.program.profile.duration, *prefix.program.terminal_support.value) <
                                     std::make_pair(best->program.profile.duration, *best->program.terminal_support.value))
                        best = std::move(prefix);
            if (best) {
                annotate(best->program, "ownedEndpoint", integer_value(best->program.profile.duration));
                if (best->reused) {
                    annotate(*full, "ownedEndpoint", integer_value(best->program.profile.duration));
                    owned = *full;
                } else {
                    owned = std::move(best->program);
                    projected = true;
                }
            } else {
                owned = *full;
            }
        }
        if (owned) {  // assess_proposal
            Value proof = search.screen(*owned, record.effects);
            if (is_null(proof) && projected) {
                auto [full_proof, selection] = search.gate->check_program(std::make_shared<const Program>(*full), Value{nullptr});
                (void)selection;  // the full program is not kept
                if (!is_null(full_proof)) exclusions.push_back(exclusion(*full, full_proof, "unused_extension"));
            }
            if (!is_null(proof)) {
                record.screened = 1;
                exclusions.push_back(exclusion(*owned, proof, "owned_proposal"));
                record.outcome = "screened";
                record.exclusions = array(std::move(exclusions));
                records.push_back(std::move(record));
                if (emits(records.back().ordinal)) return expansion;
                continue;
            }
        }
        // accept
        if (!owned || !entry_possible(owned->profile, initial_bounds)) {
            record.outcome = "rejected";
        } else if (!search.eligible(*owned, record.effects)) {
            record.policy_rejections = 1;
            exclusions.push_back(object({{"id", text(digest(text(program_search_key(*owned))))},
                                         {"ticks", integer_value(owned->profile.duration)},
                                         {"proof", object({{"reason", text("search_policy")}})}}));
            record.outcome = "rejected";
        } else if (seen.count(program_search_key(*owned))) {
            record.outcome = "seen";
        } else {
            // link
            Program child = std::move(*owned);
            record.meaningful = meaningful_choice(choices[ordinal]);
            auto inherited = [&](const char* key) {
                const Value* v = get(parent->annotations, key);
                return v ? as_int(*v) : 0;
            };
            if (!get(child.annotations, "decisionDepth"))
                annotate(child, "decisionDepth", integer_value(inherited("decisionDepth") + (record.meaningful ? 1 : 0)));
            if (!get(child.annotations, "lastSolvedDepth"))
                annotate(child, "lastSolvedDepth", integer_value(inherited("lastSolvedDepth")));
            if (!get(child.annotations, "decisionParent"))
                annotate(child, "decisionParent",
                         get_or_null(parent->annotations, record.meaningful ? "decisionNode" : "decisionParent"));
            if (!get(child.annotations, "decisionNode"))
                annotate(child, "decisionNode", record.meaningful ? text(node_identity(child, f.policy_context))
                                                                  : get_or_null(parent->annotations, "decisionNode"));
            if (record.meaningful && !complete(child)) annotate(child, "needsNodeSolve", boolean(true));
            ready = ready || complete(child);
            seen.insert(program_search_key(child));
            record.outcome = "queued";
            record.child = std::move(child);
        }
        record.exclusions = array(std::move(exclusions));
        records.push_back(std::move(record));
        if (emits(records.back().ordinal)) return expansion;
    }
    expansion.finished = true;
    return expansion;
}

}  // namespace pkc
