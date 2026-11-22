// Port of movement/normal.py's traces and choice builders.
#include "pkc/movement/normal.hpp"

#include <algorithm>

#include "pkc/vertical.hpp"

namespace pkc {

Trace replay(const ScalarState& initial, std::int64_t ticks, bool jumping, const std::vector<VerticalContact>& vertical,
             const std::vector<ContactEvent>& finite, const Num& threshold) {
    std::vector<bool> hit(ticks, false), jump(ticks, false);
    for (const auto& c : finite)
        if (c.axis != "Y" && c.offset >= 0 && c.offset < ticks) hit[c.offset] = true;
    if (ticks) jump[0] = jumping;
    return trace_ladder_schedule(initial, std::vector<bool>(ticks + 1, false), std::vector<bool>(ticks, false), hit, jump,
                                 vertical, threshold);
}

std::optional<std::int64_t> first_descent(const Trace& trace, double top) {
    for (std::size_t t = 0; t < trace.movement.size(); ++t)
        if (trace.movement[t].value < 0 && trace.position[t].value >= top && top > trace.position[t + 1].value)
            return static_cast<std::int64_t>(t);
    return std::nullopt;
}

namespace {

ScalarState rest(double position) {  // dict(position=..., velocity=REST_CARRY, onGround=True, ...)
    return {true, Num{position, false}, false, Num{-kGravity * kYDrag, false}};
}

}  // namespace

double jump_rise() {
    static const double rise = [] {
        Trace trace = replay(rest(0.), 20, true, {}, {}, Num{.005, false});
        double best = trace.position.front().value;
        for (const auto& p : trace.position) best = std::max(best, p.value);
        return best;
    }();
    return rise;
}

std::int64_t flat_jump_ticks() {
    static const std::int64_t ticks = *first_descent(replay(rest(0.), 30, true, {}, {}, Num{.005, false}), 0.) + 1;
    return ticks;
}

std::vector<Approach> approaches(const GenerationContext& context) {
    const ScalarState& initial = context.state;
    const std::int64_t horizon = std::min<std::int64_t>(context.max_inputs, 200);
    std::vector<Approach> out;
    for (bool jumping : initial.on_ground ? std::vector<bool>{true, false} : std::vector<bool>{false}) {
        Trace trace = replay(initial, horizon, jumping, {}, {}, context.threshold);
        if (trace.status != "invalid") out.push_back({jumping, std::move(trace)});
    }
    return out;
}

std::vector<Approach> choice_approaches(const GenerationContext& context) {
    std::vector<Approach> out;
    for (auto& a : approaches(context))
        if (a.jumping || !context.state.on_ground || context.run_tick_choices) out.push_back(std::move(a));
    return out;
}

std::optional<MovementChoice> event_choice(const GenerationContext& context, const std::string& kind,
                                           const ScalarState& initial, std::int64_t duration, bool jumping,
                                           ChoiceParts parts) {
    Trace trace = replay(initial, duration, jumping, parts.vertical, parts.finite, context.threshold);
    if (trace.status == "invalid") return std::nullopt;
    return movement_choice(kind, initial, trace, std::move(parts.finite), std::move(parts.cases),
                           std::move(parts.constraints), std::move(parts.support), parts.landing_phase,
                           std::move(parts.region_choices));
}

std::optional<MovementChoice> named_choice(const GenerationContext& context, const std::string& kind,
                                           const ScalarState& initial, CaseEvent event, bool jumping,
                                           std::vector<PositionConstraint> constraints,
                                           std::vector<ContactEvent> finite, std::optional<std::string> support,
                                           std::vector<RegionChoice> region_choices) {
    event.type = kind;
    VerticalContact contact;
    contact.movement_case = event.movement_case;
    contact.plane = event.plane;
    contact.lower_height = event.lower_height;
    contact.higher_height = event.higher_height;
    contact.clearance = event.clearance;
    contact.state = event.input + 1;
    contact.direction = -1;
    const std::int64_t duration = event.input + 1;
    ChoiceParts parts;
    parts.vertical = {contact};
    parts.finite = std::move(finite);
    parts.cases = {std::move(event)};
    parts.constraints = std::move(constraints);
    parts.support = std::move(support);
    parts.region_choices = std::move(region_choices);
    return event_choice(context, kind, initial, duration, jumping, std::move(parts));
}

}  // namespace pkc
