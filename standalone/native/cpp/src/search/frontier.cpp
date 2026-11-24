#include "pkc/search/frontier.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/scene/static_collision.hpp"

namespace pkc {
namespace {

const Object& profile_of(const Value& program) { return fields(at(fields(program), "profile")); }

Value set_key(const Value& base, std::string_view key, Value value) {
    Object out = fields(base);
    put(out, key, std::move(value));
    return object(std::move(out));
}

Value node_of(const Value& program) { return get_or_null(fields(program), "decisionNode"); }

Nullable<std::string> nullable_text(const Value& value) {
    Nullable<std::string> out;
    if (!is_null(value)) out.value = as_text(value);
    return out;
}

}  // namespace

bool restore_alternatives(Continuation& state, const std::vector<Value>& extra_live) {
    if (!state.alternatives) return false;
    std::set<std::string> succeeded;
    if (state.succeeded_nodes) succeeded.insert(state.succeeded_nodes->begin(), state.succeeded_nodes->end());
    std::vector<std::size_t> waiting;
    for (std::size_t i = 0; i < state.alternatives->items.size(); ++i) {
        const auto& [node, held] = state.alternatives->items[i];
        if (held.next < static_cast<std::int64_t>(held.programs.size()) && !succeeded.count(node)) waiting.push_back(i);
    }
    if (waiting.empty()) return false;
    const OrderedMap<Nullable<std::string>>& parents = *state.decision_parents;
    std::set<std::string> alive;
    const auto live = [&](const Value& program) {
        if (!py_truthy(program)) return;
        const Value& first = node_of(program);
        std::optional<std::string> node;
        if (py_truthy(first)) node = as_text(first);
        while (node && !node->empty() && !alive.count(*node)) {
            alive.insert(*node);
            const Nullable<std::string>* parent = parents.find(*node);
            node = parent ? parent->value : std::nullopt;
        }
    };
    for (const Value& program : state.pending) live(program);
    for (const Value& program : state.ready) live(program);
    if (state.node_in_flight) live(*state.node_in_flight);
    if (state.expansion.value) live(state.expansion.value->program);
    for (const Value& program : extra_live) live(program);
    // A side variant still held (a reserve, or a side entry not yet restored) is live, so what watches it or its
    // decision waits until the side variants are exhausted.
    if (state.side_reserves)
        for (const SideReserve& reserve : *state.side_reserves)
            for (const Value& program : reserve.programs) live(program);
    for (const auto& [_, held] : state.alternatives->items)
        if (held.side && held.next == 0) live(held.programs.at(0));
    bool restored = false;
    for (std::size_t i : waiting) {
        auto& [node, held] = state.alternatives->items[i];
        bool keep = false;
        if (held.watch) {
            for (const Nullable<std::string>& n : *held.watch)
                keep = keep || (n.value && (alive.count(*n.value) || succeeded.count(*n.value)));
        } else {
            keep = alive.count(node) || succeeded.count(node);
        }
        for (const std::string& r : held.restored) keep = keep || alive.count(r);
        if (keep) continue;
        const Value program = held.programs.at(static_cast<std::size_t>(held.next));
        ++held.next;
        held.restored.push_back(as_text(at(fields(program), "decisionNode")));
        state.pending.push_back(program);
        // Entries after this one see it alive: a held launch-face jump waits for a run-tick reserve restored now.
        live(program);
        restored = true;
    }
    return restored;
}

Frontier::Frontier(Generation& generation, FrontierOptions options, FrontierHooks hooks)
    : options_(std::move(options)), hooks_(std::move(hooks)) {
    const Object& settings = fields(options_.settings);
    // The scene also carries movementSearch. Hash its physical fields here; settings below already owns the domain
    // and deliberately excludes slices.
    Object scene, hashed_settings;
    for (const auto& [k, v] : fields(options_.scene))
        if (k != "movementSearch") scene.emplace_back(k, v);
    for (const auto& [k, v] : settings)
        if (k != "maxPrefixes" && k != "maxQueuedPrefixes" && k != "maxChoicesPerType" && k != "expansionsPerProposal")
            hashed_settings.emplace_back(k, v);
    Array roots;
    for (const Value& p : options_.initial_programs) roots.push_back(text(program_search_key(program_from_value(p))));
    Object context_value{{"version", integer_value(3)},
                         {"comparisonPolicy", integer_value(options_.comparison_policy)},
                         {"proposalPolicy", integer_value(1)},
                         {"scene", object(std::move(scene))},
                         {"settings", object(std::move(hashed_settings))},
                         {"roots", array(std::move(roots))},
                         {"threshold", real(options_.threshold)}};
    if (options_.stop_supports) {
        Array supports;
        for (const std::string& s : *options_.stop_supports) supports.push_back(text(s));
        put(context_value, "stopSupports", array(std::move(supports)));
    }
    put(context_value, "version", integer_value(4));
    put(context_value, "decisionPolicy", options_.policy_context);
    const std::string context = digest(object(std::move(context_value)));
    if (generation.report && generation.report->continuation.context != context)
        throw std::invalid_argument("Incompatible movement generation checkpoint");
    if (!generation.report) {
        Continuation& state = generation.report.emplace().continuation;
        state.version = 4;
        state.context = context;
        state.pending = options_.initial_programs;
    }
    report_ = &*generation.report;
    state_ = &report_->continuation;
    // The DFS always evaluates decision nodes.
    if (!state_->nodes) state_->nodes.emplace();
    if (!state_->decision_parents) state_->decision_parents.emplace();
    if (!state_->alternatives) state_->alternatives.emplace();
    if (!state_->succeeded_nodes) state_->succeeded_nodes.emplace();
    if (!state_->side_reserves) state_->side_reserves.emplace();
    seen_.insert(state_->seen.begin(), state_->seen.end());
    const Array& position = items(at(fields(at(fields(options_.scene), "initial")), "position"));
    const double x = num(position.at(0)).value, z = num(position.at(2)).value;
    initial_bounds_ = {x, x, z, z};
    for (const Value& t : items(at(settings, "types"))) types_.push_back(as_text(t));
    if (options_.stop_supports && !options_.stop_supports->empty()) {
        goal_blocks_ = *options_.stop_supports;
    } else {
        goal_blocks_ = {as_text(at(fields(options_.scene), "goalBlock"))};
    }
    max_prefixes_ = as_int(at(settings, "maxPrefixes"));
    max_queued_prefixes_ = as_int(at(settings, "maxQueuedPrefixes"));
    max_choices_per_type_ = as_int(at(settings, "maxChoicesPerType"));
    max_inputs_ = as_int(at(settings, "maxInputs"));
    max_interactions_ = as_int(at(settings, "maxInteractions"));
}

bool Frontier::complete(const Value& p) const {
    const Object& profile = profile_of(p);
    if (!py_truthy(items(at(profile, "grounded")).back())) return false;
    const Value& support = get_or_null(fields(p), "terminalSupport");
    if (!std::holds_alternative<std::string>(support.data) ||
        std::find(goal_blocks_.begin(), goal_blocks_.end(), as_text(support)) == goal_blocks_.end())
        return false;
    return num(at(profile, "duration")).value > 0;
}

// take_first: pop the item with the smallest key, list order breaking ties.
Value Frontier::take(std::vector<Value>& programs) const {
    std::size_t best = 0;
    Value best_key = hooks_.priority(programs[0], "");
    for (std::size_t i = 1; i < programs.size(); ++i) {
        Value key = hooks_.priority(programs[i], "");
        if (py_compare(key, best_key) < 0) {
            best = i;
            best_key = std::move(key);
        }
    }
    Value out = std::move(programs[best]);
    programs.erase(programs.begin() + static_cast<std::ptrdiff_t>(best));
    return out;
}

bool Frontier::restore() { return restore_alternatives(*state_, hooks_.live_programs()); }

// Each exclusion names the decision node whose expansion generated it, for the solve report.
void Frontier::commit(const Value& exclusions, std::int64_t screened, std::int64_t policy_rejections,
                      const Value& node) {
    for (const Value& exclusion : items(exclusions)) state_->exclusions.push_back(set_key(exclusion, "node", node));
    state_->screened += screened;
    state_->policy_rejections += policy_rejections;
}

void Frontier::hold_minimum_runs(ExpansionState& e, const Value& node, const Array& log, const Array& reserves) {
    // Log a minimum-run search's candidates and hold each reserved run count as an alternative of the decision.
    if (!state_->minimum_run_candidates) state_->minimum_run_candidates.emplace();
    state_->minimum_run_candidates->insert(state_->minimum_run_candidates->end(), log.begin(), log.end());
    for (const Value& reserve : reserves) {
        const Value& reserve_fields = items(reserve).at(0);
        const std::string key = digest(array({node, text("minimumRunTicks"), reserve_fields}));
        if (!e.queued_nodes) e.queued_nodes.emplace();
        e.queued_nodes->push_back(Nullable<std::string>{key});
        if (state_->alternatives->contains(key)) continue;
        Object held = fields(e.program);
        erase(held, "needsNodeSolve");
        erase(held, "minimumRunSearch");
        put(held, "minimumRunSearch", reserve_fields);
        put(held, "decisionNode", text(key));
        put(held, "decisionParent", node);
        HeldAlternative alternative;
        alternative.programs.push_back(object(std::move(held)));
        std::vector<Nullable<std::string>> watch;
        for (const Value& n : items(items(reserve).at(1))) watch.push_back(nullable_text(n));
        alternative.watch = std::move(watch);
        (*state_->alternatives)[key] = std::move(alternative);
        (*state_->decision_parents)[key] = nullable_text(node);
    }
}

void Frontier::minimum_runs(ExpansionState& e) {
    // minimum_runs(e) through the native node search (search/node.hpp), applied in the same order.
    const Value& search = get_or_null(fields(e.program), "minimumRunSearch");
    const Value node = py_truthy(search) ? at(fields(search), "node") : node_of(e.program);
    Value reached{nullptr}, landings{nullptr};
    if (e.reached) {
        Object r;
        for (const auto& [key, rows] : e.reached->items) r.emplace_back(key, array(Array(rows.begin(), rows.end())));
        reached = object(std::move(r));
    }
    if (e.landings) landings = array(Array(e.landings->begin(), e.landings->end()));
    Value context_id;
    const NodeSearch native = hooks_.node_search(context_id);
    const MinimumRuns result =
        pkc::minimum_runs(native, program_from_value(e.program), reached, landings,
                          options_.stop_supports.value_or(std::vector<std::string>{}), initial_bounds_, max_inputs_,
                          seen_, hooks_.budget);
    hooks_.apply(result.effects, context_id);
    for (const Value& found : result.exclusions) {
        const Object& f = fields(found);
        commit(at(f, "exclusions"), as_int(at(f, "screened")), as_int(at(f, "policyRejections")), node);
    }
    for (const MinimumRuns::Queued& queued : result.queued) {
        const Value child = to_value(queued.child);
        if (queued.meaningful)
            (*state_->decision_parents)[as_text(node_of(child))] = nullable_text(at(fields(child), "decisionParent"));
        seen_.insert(queued.identity);
        state_->seen.push_back(queued.identity);
        queue_child(e, *queued.parent, queued.side_variant, child);
    }
    hold_minimum_runs(e, node, result.log, result.reserves);
}

void Frontier::hold_same_face(ExpansionState& e) {
    // Queue each held jump back onto the launch face of an ordinary decision P (O11) at once when P queued or
    // reserved nothing else; otherwise reserve it until every other branch of P died: everything below P's node, and
    // the nodes P queued and reserved (P's node is None at frame 0's initial program).
    const Value node = node_of(e.program);
    const std::vector<Nullable<std::string>> others = e.queued_nodes.value_or(std::vector<Nullable<std::string>>{});
    const std::vector<Value> held = e.held.value_or(std::vector<Value>{});
    e.held.reset();
    for (const Value& h : held) {
        const Object& fit = fields(h);
        const Value& child = at(fit, "program");
        if (!others.empty()) {
            HeldAlternative alternative;
            alternative.programs.push_back(child);
            std::vector<Nullable<std::string>> watch;
            if (py_truthy(node)) watch.push_back(nullable_text(node));
            watch.insert(watch.end(), others.begin(), others.end());
            alternative.watch = std::move(watch);
            (*state_->alternatives)[digest(array({node, text("sameFace"), at(fields(child), "decisionNode")}))] =
                std::move(alternative);
        } else {
            (complete(child) ? state_->ready : state_->pending).push_back(child);
        }
        if (!state_->minimum_run_candidates) state_->minimum_run_candidates.emplace();
        state_->minimum_run_candidates->push_back(
            object({{"node", node},
                    {"key", text(dumps(array({text("same_face"), at(fit, "face")}), false, false))},
                    {"k", integer_value(0)},
                    {"total", at(fit, "total")},
                    {"A", Value{nullptr}},
                    {"N", Value{nullptr}},
                    {"lower", integer_value(0)},
                    {"round", integer_value(0)},
                    {"outcome", text(others.empty() ? "queued" : "held")},
                    {"nodes", array({at(fields(child), "decisionNode")})},
                    {"room", at(fit, "room")},
                    {"jump", at(fit, "jump")}}));
    }
}

void Frontier::queue_child(ExpansionState& e, const Program& parent, const std::optional<Value>& tag, Value child) {
    // A tagged side of a step or slab boost waits for the expansion's end (hold_sides): its forward ones may come later.
    bool forward = true;
    if (tag) {
        const Object& t = fields(*tag);
        const Object& row = fields(at(t, "row"));
        child = set_key(child, "sideVariant",
                        object(with(t, "row", object(with(row, "t1", integer_value(parent.profile.duration +
                                                                                    as_int(at(row, "t1"))))))));
        if (!e.sides) e.sides.emplace();
        SideMove& move = (*e.sides)[dumps(array({text(program_search_key(parent)), at(t, "key")}), false, false)];
        forward = py_truthy(at(t, "forward"));
        if (!forward) {
            move.held.push_back(child);
        } else if (complete(child)) {
            move.complete = true;
        } else {
            move.forwards.push_back(array({node_of(child), at(t, "side")}));
        }
    }
    if (forward) (complete(child) ? state_->ready : state_->pending).push_back(child);
    // What a held jump back onto the launch face of this expansion waits for (hold_same_face).
    if (!e.queued_nodes) e.queued_nodes.emplace();
    e.queued_nodes->push_back(nullable_text(node_of(child)));
}

void Frontier::hold_sides(ExpansionState& e) {
    // Reserve each move's held sides behind its forward nodes until those are solved (settle_sides). Without a forward
    // queued here (screened, already seen) or with a complete one (no node solve), queue them now.
    if (!e.sides) return;
    const OrderedMap<SideMove> sides = std::move(*e.sides);
    e.sides.reset();
    for (const auto& [_, move] : sides.items) {
        if (!move.forwards.empty() && !move.complete) {
            if (!move.held.empty()) {
                state_->side_reserves->push_back(SideReserve{move.held, move.forwards, {}});
                if (!state_->side_variants) state_->side_variants.emplace();
                for (const Value& p : move.held) {
                    const Object& tag = fields(at(fields(p), "sideVariant"));
                    state_->side_variants->push_back(
                        object({{"node", get_or_null(fields(e.program), "decisionNode")}, {"child", node_of(p)},
                                {"case", at(tag, "case")}, {"face", at(tag, "face")}, {"side", at(tag, "side")},
                                {"forwards", array(move.forwards)}}));
                }
            }
            continue;
        }
        for (const Value& child : move.held) (complete(child) ? state_->ready : state_->pending).push_back(child);
    }
}

void Frontier::settle_sides(const Value& node, const Value* result) {
    // Record a forward node's outcome: 'failed' without a result, else its sideOutcome. Once a reserve's forwards all
    // have one, queue its sides when none is slack; otherwise hold each behind the slack forwards and their node
    // alternatives until those branches die, like a node's alternatives (restore_alternatives).
    std::vector<SideReserve>& reserves = *state_->side_reserves;
    auto matches = [&](const SideReserve& r) {
        return std::any_of(r.forwards.begin(), r.forwards.end(),
                           [&](const Value& f) { return py_equal(items(f).at(0), node); });
    };
    std::vector<std::size_t> settling;
    for (std::size_t i = 0; i < reserves.size(); ++i)
        if (matches(reserves[i])) settling.push_back(i);
    std::vector<std::size_t> removed;
    for (std::size_t i : settling) {
        SideReserve& reserve = reserves[i];
        reserve.outcomes[as_text(node)] = result ? at(fields(*result), "sideOutcome") : text("failed");
        if (reserve.outcomes.items.size() < reserve.forwards.size()) continue;
        removed.push_back(i);
        Object outcomes;
        for (const auto& [n, outcome] : reserve.outcomes.items) outcomes.emplace_back(n, outcome);
        std::set<std::string> children;
        for (const Value& p : reserve.programs) children.insert(as_text(node_of(p)));
        for (Value& record : *state_->side_variants)
            if (children.count(as_text(at(fields(record), "child"))))
                record = object(with(fields(record), "outcomes", object(outcomes)));
        std::vector<std::string> slack;
        for (const Value& f : reserve.forwards) {
            const std::string n = as_text(items(f).at(0));
            if (as_text(*reserve.outcomes.find(n)) == "slack") slack.push_back(n);
        }
        if (slack.empty()) {
            for (const Value& child : reserve.programs) (complete(child) ? state_->ready : state_->pending).push_back(child);
            continue;
        }
        // A forward's held node alternatives sit beside it, not below it, so they are watched too. Entries restore in
        // insertion order, so these come after the forward's own alternatives.
        std::vector<Nullable<std::string>> watch;
        for (const std::string& n : slack) watch.push_back(Nullable<std::string>{n});
        for (const std::string& n : slack)
            if (const HeldAlternative* held = state_->alternatives->find(n))
                for (const Value& p : held->programs) watch.push_back(nullable_text(node_of(p)));
        for (const Value& child : reserve.programs) {
            const std::string key = digest(array({node_of(child), text("sideVariant")}));
            HeldAlternative alternative;
            alternative.programs = {child};
            alternative.watch = watch;
            alternative.side = slack;
            (*state_->alternatives)[key] = std::move(alternative);
            for (Value& record : *state_->side_variants)
                if (py_equal(at(fields(record), "child"), node_of(child)))
                    record = object(with(fields(record), "alternative", text(key)));
        }
    }
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) reserves.erase(reserves.begin() + static_cast<std::ptrdiff_t>(*it));
}

void Frontier::expand(ExpansionState& e) {
    // One native batch per movement type (search/node.hpp): it commits nothing when the budget stops it. Like the
    // Python loop, it stops where a ready program is emitted (the cursor check in run()).
    Value context_id;
    const NodeSearch search = hooks_.node_search(context_id);
    const Expansion batch =
        expand_type(search, program_from_value(e.program), types_.at(static_cast<std::size_t>(e.kind)), e.cursor,
                    options_.stop_supports.value_or(std::vector<std::string>{}), initial_bounds_, max_inputs_,
                    max_choices_per_type_, !state_->ready.empty(), e.reached.has_value(), seen_, hooks_.budget);
    for (const ProposalRecord& record : batch.records) hooks_.apply(record.effects, context_id);
    const Value node = node_of(e.program);
    for (const ProposalRecord& record : batch.records) {
        ++state_->generated;
        e.cursor = record.ordinal + 1;
        commit(record.exclusions, record.screened, record.policy_rejections, node);
        Value child;
        if (record.outcome == "queued" || record.outcome == "held") {
            child = to_value(*record.child);
            if (record.meaningful)
                (*state_->decision_parents)[as_text(node_of(child))] = nullable_text(at(fields(child), "decisionParent"));
            const std::string identity = program_search_key(*record.child);
            seen_.insert(identity);
            state_->seen.push_back(identity);
        }
        if (record.outcome == "held") {
            // Held, not live: it sets no N and beats no run-tick candidate.
            if (!e.held) e.held.emplace();
            e.held->push_back(set_key(at(fields(record.ordinary), "fit"), "program", child));
        } else if (record.outcome == "queued") {
            queue_child(e, program_from_value(e.program), record.side_variant, child);
            if (e.reached) {
                const Object& found = fields(record.ordinary);
                for (const Value& target : items(at(found, "targets")))
                    (*e.reached)[as_text(target)].push_back(array({at(fields(child), "decisionNode"), at(found, "landed")}));
                for (const Value& landing : items(at(found, "landings")))
                    e.landings->push_back(set_key(landing, "node", at(fields(child), "decisionNode")));
            }
        }
    }
    if (batch.finished) {
        ++e.kind;
        e.cursor = 0;
    }
}

void Frontier::update(const std::string& reason) {
    GenerationReport& r = *report_;
    const Continuation& s = *state_;
    r.termination = reason;
    r.expanded_prefixes = expanded_;
    r.total_expanded_prefixes = s.expanded;
    r.generated = s.generated;
    r.emitted_programs = s.emitted;
    r.screened_prefixes = s.screened;
    r.remaining_prefixes = static_cast<std::int64_t>(s.pending.size() + s.ready.size()) +
                           s.expansion.value.has_value() + s.node_in_flight.has_value();
    r.queued_complete_programs = static_cast<std::int64_t>(s.ready.size());
    r.queue_drops = r.partial_queue_drops = r.complete_queue_drops = 0;
    r.policy_rejections = s.policy_rejections;
    r.queue_policy = "persistent";
    r.types = at(fields(options_.settings), "types");
    r.explicit_program_priority = false;
    r.deferred_prefixes = std::max<std::int64_t>(0, static_cast<std::int64_t>(s.pending.size()) - max_queued_prefixes_);
    r.minimum_ticks_proven = false;
}

std::optional<Value> Frontier::next() {
    if (finished_) return std::nullopt;
    try {
        std::optional<Value> program = run();
        if (!program) finished_ = true;
        return program;
    } catch (...) {
        // The generator's finally.
        finished_ = true;
        if (!state_->exhausted && report_->termination == "running") update("consumer_closed");
        throw;
    }
}

void Frontier::close() {
    if (finished_) return;
    finished_ = true;
    if (!state_->exhausted && report_->termination == "running") update("consumer_closed");
}

// The loop up to its next yield; nullopt where the generator returns.
std::optional<Value> Frontier::run() {
    Continuation& s = *state_;
    try {
        while (!s.pending.empty() || !s.ready.empty() || s.expansion.value || s.node_in_flight || restore()) {
            hooks_.budget();
            if (!s.ready.empty() && (!s.expansion.value || s.expansion.value->cursor % max_choices_per_type_ == 0)) {
                Value program = take(s.ready);
                if (!hooks_.eligible(program)) {
                    ++s.policy_rejections;
                    continue;
                }
                ++s.emitted;
                program = set_key(program, "terminal", text("landed"));
                update();
                return program;
            }
            if (!s.expansion.value) {
                if (expanded_ >= max_prefixes_) {
                    update("prefix_limit");
                    return std::nullopt;
                }
                if (!s.node_in_flight) restore();
                const bool in_flight = s.node_in_flight && py_truthy(*s.node_in_flight);
                Value program = in_flight ? *s.node_in_flight : take(s.pending);
                const bool allowed = hooks_.eligible(program);
                if (in_flight) *s.node_in_flight = program;  // the same dict in Python
                if (!allowed) {
                    ++s.policy_rejections;
                    s.node_in_flight.reset();
                    settle_sides(node_of(program));
                    continue;
                }
                if (complete(program)) {
                    s.ready.push_back(program);
                    continue;
                }
                const Object& p = fields(program);
                if (num(at(profile_of(program), "duration")).value >= max_inputs_ ||
                    num(at(p, "interactions")).value >= max_interactions_) {
                    s.node_in_flight.reset();
                    settle_sides(node_of(program));
                    continue;
                }
                if (py_truthy(get_or_null(p, "needsNodeSolve"))) {
                    // Query a decision when it is selected for descent. Merely constructing an unselected sibling
                    // must not spend NLPs.
                    s.node_in_flight = program;
                    update();
                    const Value result = hooks_.evaluate_node(*s.node_in_flight, *s.nodes);
                    const Object& r = fields(result);
                    program = at(r, "program");
                    s.node_in_flight = program;
                    const std::string status = as_text(at(r, "status"));
                    if (status == "suspended" || status == "error") {
                        update(status);
                        return std::nullopt;
                    }
                    s.node_in_flight.reset();
                    if (status == "oracle_rejected" || status == "exhausted") {
                        settle_sides(node_of(program));
                        update();
                        continue;
                    }
                    // Other branch choices at this node wait until this branch dies (restore_alternatives).
                    const Value& held = get_or_null(r, "alternatives");
                    if (py_truthy(held) && !s.alternatives->contains(as_text(at(fields(program), "decisionNode")))) {
                        HeldAlternative alternative;
                        alternative.programs = items(held);
                        (*s.alternatives)[as_text(at(fields(program), "decisionNode"))] = std::move(alternative);
                        for (const Value& h : items(held))
                            (*s.decision_parents)[as_text(at(fields(h), "decisionNode"))] =
                                nullable_text(get_or_null(fields(h), "decisionParent"));
                    }
                    settle_sides(node_of(program), &result);
                    Object updated = fields(program);
                    erase(updated, "needsNodeSolve");
                    program = object(std::move(updated));
                }
                const Value search = get_or_null(fields(program), "minimumRunSearch");
                ExpansionState e;
                e.program = program;
                e.kind = py_truthy(search) ? static_cast<std::int64_t>(types_.size()) : 0;
                if (is_null(search) && hooks_.ordinary(program)) {
                    // Ordinary decision: the types yield jump-now children; minimum_runs adds run ticks.
                    e.reached.emplace();
                    e.landings.emplace();
                }
                s.expansion.value = std::move(e);
                ++expanded_;
                ++s.expanded;
            }
            ExpansionState& e = *s.expansion.value;
            if (e.kind == static_cast<std::int64_t>(types_.size())) {
                if (e.reached || py_truthy(get_or_null(fields(e.program), "minimumRunSearch"))) minimum_runs(e);
                hold_same_face(e);
                hold_sides(e);
                s.expansion.value.reset();
                continue;
            }
            if (e.choice) throw Unsupported("a Python proposal in flight resumes on the Python frontier");
            expand(e);
            update();
        }
        s.exhausted = true;
        update("exhausted");
    } catch (const Timeout& error) {
        const Value termination = hooks_.termination();
        update(py_truthy(termination) ? as_text(termination) : std::string(error.what()));
    }
    return std::nullopt;
}

}  // namespace pkc
