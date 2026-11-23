#include "pkc/observability/turns.hpp"

#include "../codec.hpp"
#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/search/anchors/policy.hpp"
#include "pkc/search/comparison.hpp"

namespace pkc {

namespace {

// `item in rows`, by Python equality.
template <class T>
bool listed(const std::vector<T>& rows, const T& item) {
    const Value encoded = to_value(item);
    for (const T& row : rows)
        if (py_equal(to_value(row), encoded)) return true;
    return false;
}

}  // namespace

void observe(Observations& state, const Value& guides, const Value& program_value, std::int64_t span,
             std::int64_t start, const Value& model, const std::string& status, const Num& elapsed,
             const std::string& kind, const Value& metrics, const QueryService& service) {
    if (!state.enabled) return;
    const Object& program = fields(program_value);
    const Value& node = get_or_null(program, "decisionNode");
    const std::string ident =
        py_truthy(node) ? as_text(node) : digest(array({model, integer_value(span), text(kind)}));
    if (!state.decisions.contains(ident)) {
        const Value& saved_point = get_or_null(program, "decisionPoint");
        Value point = saved_point;
        if (!py_truthy(saved_point)) {
            const Array& first = items(items(at(fields(guides), "points")).at(0));
            Array every_other;
            for (std::size_t i = 0; i < first.size(); i += 2) every_other.push_back(first[i]);
            point = array(std::move(every_other));
        }
        const Value label_value = classify(guides, span, point);
        const Object& label = fields(label_value);
        const Value* turnaround = get(program, "priorityTurnaround");
        const bool mode = py_truthy(turnaround ? *turnaround : at(label, "isTurnaround"));
        const Value counts_value = comparison_label(program_from_value(program_value), start, std::nullopt, ident,
                                                    mode ? "turnaround" : "ordinary", {}, {});
        const Object& counts = fields(counts_value);
        Object kept;
        for (const char* key : {"T", "J", "R", "A", "start"}) kept.emplace_back(key, at(counts, key));
        ObservationDecision event;
        event.id = ident;
        codec::decode(get_or_null(program, "decisionParent"), event.parent, codec::Path{nullptr, "decisionParent"});
        const auto count = [&](const char* key) {
            const Value* held = get(program, key);
            return held ? as_int(*held) : 0;
        };
        event.depth = count("decisionDepth");
        event.last_solved_depth = count("lastSolvedDepth");
        event.point_tick = count("decisionPointTick");
        const Value* source = get(program, "decisionPointSource");
        event.point_source = source ? as_text(*source) : "original_initial";
        event.version = as_int(at(label, "version"));
        event.guide = at(label, "guide");
        event.span = as_int(at(label, "span"));
        event.point = at(label, "point");
        event.is_turnaround = std::get<bool>(at(label, "isTurnaround").data);
        event.distances = at(label, "distances");
        event.key = priority_key(counts_value);  // of the whole label, not the kept counts
        event.counts = object(std::move(kept));
        event.mode = mode ? "turnaround" : "ordinary";
        event.kind = kind;
        state.decisions.items.emplace_back(ident, std::move(event));
    }
    ObservationDecision& event = *state.decisions.find(ident);
    ObservationQuery query{model, status, elapsed, py_truthy(metrics) ? metrics : object({}), service};
    if (!listed(event.queries, query)) event.queries.push_back(std::move(query));
}

void replay(Observations& state, const Value& guides, const std::string& ancestor, std::int64_t span,
            const Value& point, const Value& model) {
    if (!state.enabled) return;
    ObservationDecision* event = state.decisions.find(ancestor);
    if (!event) return;
    const Value label_value = classify(guides, span, point);
    const Object& label = fields(label_value);
    ObservationReplay observation{model, point, at(label, "distances"),
                                  std::get<bool>(at(label, "isTurnaround").data) != event->is_turnaround};
    if (!listed(event->replays, observation)) event->replays.push_back(std::move(observation));
}

}  // namespace pkc
