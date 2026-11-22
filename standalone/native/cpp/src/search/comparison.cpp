// Port of search/comparison.py (anchor-search purposes) and search/deferrals.assess.
#include "pkc/search/comparison.hpp"

#include "../schema.hpp"

namespace pkc {
namespace {

double n(const Value& v) { return num(v).value; }

Value negated(const Value& v) {  // -(v or 0)
    if (!py_truthy(v)) return integer_value(0);
    const Num x = num(v);
    return value(Num{-x.value, x.integral});
}

Array count_protections(const Object& candidate, const Object& peer) {  // _count_protections
    Array reasons;
    if (n(at(candidate, "J")) > n(at(peer, "J"))) reasons.push_back(text("more_jump_inputs"));
    if (n(at(candidate, "tickCost")) < n(at(peer, "tickCost")))
        reasons.push_back(text(py_equal(at(candidate, "mode"), text("turnaround")) ? "fewer_air_inputs" : "fewer_run_inputs"));
    return reasons;
}

Value counts(const Object& label) {
    return object({{"T", at(label, "T")}, {"J", at(label, "J")}, {"R", at(label, "R")}, {"A", at(label, "A")}});
}

}  // namespace

Value comparison_label(const Program& program, std::int64_t start, std::optional<std::int64_t> through,
                       const std::string& identity, const std::string& mode, const Object& context,
                       const Object& evidence) {
    const Profile& p = program.profile;
    const std::int64_t end = through.value_or(p.duration);
    if (!(0 <= start && start <= end && end <= p.duration)) fail("Comparison interval lies outside the program");
    if (mode != "ordinary" && mode != "turnaround") fail("Comparison mode must be ordinary or turnaround");
    Array schedule;
    std::int64_t jumps = 0, runs = 0, air = 0;
    for (std::int64_t t = start; t < end; ++t) {
        const char* c = p.grounded[t] && p.jump[t] ? "J" : p.grounded[t] ? "R" : "A";
        (*c == 'J' ? jumps : *c == 'R' ? runs : air) += 1;
        schedule.push_back(text(c));
    }
    Object result{{"id", text(identity)},       {"T", integer_value(end)},       {"through", integer_value(end)},
                  {"start", integer_value(start)}, {"mode", text(mode)},          {"J", integer_value(jumps)},
                  {"R", integer_value(runs)},     {"A", integer_value(air)},      {"schedule", array(std::move(schedule))},
                  {"tickCost", integer_value(mode == "turnaround" ? air : runs)}};
    for (const auto& [k, v] : context) put(result, k, v);
    for (const auto& [k, v] : evidence) put(result, k, v);
    return object(std::move(result));
}

Value priority_key(const Value& label_value) {
    const Object& label = fields(label_value);
    const Value& cost = get_or_null(label, "tickCost");
    const Value* id = get(label, "id");
    return array({at(label, "T"), negated(get_or_null(label, "J")), py_truthy(cost) ? cost : integer_value(0),
                  id ? *id : text("")});
}

Value compare_candidates(const Value& candidate_value, const Value& peer_value, const Object& context,
                         const Value& incumbent_value) {
    if (py_equal(get_or_null(context, "purpose"), text("common_clock")))
        fail("The native comparison has no common_clock purpose");
    const Object& candidate = fields(candidate_value);
    const bool has_peer = !is_null(peer_value);
    Object result{{"version", integer_value(1)},
                  {"action", text("keep")},
                  {"candidateId", get_or_null(candidate, "id")},
                  {"peerId", has_peer ? get_or_null(fields(peer_value), "id") : Value{nullptr}},
                  {"contextId", get_or_null(context, "comparisonContextId")},
                  {"priorityKey", priority_key(candidate_value)},
                  {"protectionReasons", array({})},
                  {"peerLookupAllowed", boolean(false)},
                  {"elapsedT", at(candidate, "T")}};
    const Value& bound = get_or_null(candidate, "remainingFinishBound");
    const Object remaining = py_truthy(bound) ? fields(bound) : Object{};
    const Value* ticks = get(remaining, "ticks");
    const Value* fallback = get(remaining, "remainingTicks");
    const Num optimistic = add(num(at(candidate, "T")), ticks ? num(*ticks) : fallback ? num(*fallback) : whole(0));
    if (!is_null(incumbent_value) && !py_truthy(get_or_null(context, "fixedTerminal"))) {
        const Object& incumbent = fields(incumbent_value);
        const Value* tick = get(incumbent, "T");
        Value from_plan{nullptr};
        if (!tick) {
            const Value& plan = get_or_null(incumbent, "plan");
            if (!is_null(plan)) from_plan = get_or_null(fields(plan), "ticks");
        }
        const Value& incumbent_tick = tick ? *tick : from_plan;
        if (!is_null(incumbent_tick)) {
            const Value* proofs = get(remaining, "proofIds");
            put(result, "optimisticFinish", value(optimistic));
            put(result, "incumbentT", incumbent_tick);
            put(result, "boundProofIds", proofs ? *proofs : array({}));
            if (optimistic.value > n(incumbent_tick)) {
                put(result, "action", text("exclude_bound"));
                put(result, "reason", text(n(at(candidate, "T")) > n(incumbent_tick)
                                               ? "elapsed_inputs_exceed_incumbent_finish"
                                               : "optimistic_remaining_time_exceeds_incumbent"));
                return object(std::move(result));
            }
        }
    }
    bool enabled = py_truthy(get_or_null(context, "localDeferralEnabled"));
    if (py_equal(get_or_null(context, "backend"), text("automatic_anchor")) && !py_truthy(get_or_null(context, "hardLatched")))
        enabled = false;
    if (!enabled) return object(std::move(result));
    auto known = [](const Object& label) {
        for (const char* k : {"J", "R", "A", "tickCost"})
            if (is_null(get_or_null(label, k))) return false;
        return true;
    };
    const bool available = known(candidate) && !py_equal(get_or_null(candidate, "basis"), text("open_prefix"));
    put(result, "peerLookupAllowed", boolean(available));
    if (!has_peer) return object(std::move(result));
    const Object& peer = fields(peer_value);
    bool compatible = available && known(peer) && py_truthy(get_or_null(peer, "validated"));
    for (const char* key : {"mode", "comparisonContext", "scope", "milestone", "landingPhase"})
        compatible = compatible && py_equal(get_or_null(candidate, key), get_or_null(peer, key));
    if (!compatible) {
        put(result, "action", text("keep_incomparable"));
        put(result, "compatibilityReason", text("different_or_unavailable_comparison_context"));
        return object(std::move(result));
    }
    const Array reasons = count_protections(candidate, peer);
    put(result, "candidateCounts", counts(candidate));
    put(result, "peerCounts", counts(peer));
    put(result, "protectionReasons", array(reasons));
    if (!reasons.empty()) {
        put(result, "action", text("keep_protected"));
    } else if (n(at(candidate, "T")) == n(at(peer, "T"))) {
        put(result, "action", text("keep_equal_time"));
    } else if (n(at(peer, "T")) < n(at(candidate, "T"))) {
        put(result, "action", text("defer_preparation"));
    }
    return object(std::move(result));
}

Value assess(const Value& label, const Value& peers, const Object& context) {
    const Value decision = compare_candidates(label, Value{nullptr}, context, Value{nullptr});
    if (!py_truthy(at(fields(decision), "peerLookupAllowed"))) return decision;
    std::optional<Value> protected_decision;
    std::int64_t checks = 0;
    for (const auto& [_, peer] : fields(peers)) {
        ++checks;
        const Value checked = compare_candidates(label, peer, context, Value{nullptr});
        const Value& action = at(fields(checked), "action");
        if (py_equal(action, text("defer_preparation")))
            return object(with(fields(checked), "peerChecks", integer_value(checks)));
        if (py_equal(action, text("keep_protected"))) protected_decision = checked;
    }
    return object(with(fields(protected_decision ? *protected_decision : decision), "peerChecks", integer_value(checks)));
}

}  // namespace pkc
