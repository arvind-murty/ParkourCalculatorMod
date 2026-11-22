// Port of validation/vertical.py: the plan's vertical schedule replayed through
// numerics.prepare and validation.numerical.validate on a synthetic horizontal
// problem (x advances .1 per tick, .05 into each hit state).
#include "pkc/validation/vertical.hpp"

#include <set>

#include "../schema.hpp"
#include "pkc/movement/contact_combinations.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/validation/numerical.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

Value slice(const Array& values, std::int64_t first, std::int64_t end) {  // values[first:end]
    const auto size = static_cast<std::int64_t>(values.size());
    first = std::min(std::max(first, std::int64_t{0}), size);
    end = std::min(std::max(end, first), size);
    return array(Array(values.begin() + first, values.begin() + end));
}

bool is_program(const Object& profile) {
    const Value* composition = get(profile, "composition");
    return composition && std::holds_alternative<std::string>(composition->data) &&
           as_text(*composition) == "movement_program";
}

}  // namespace

Value validate_vertical_schedule(const Value& route_value, const Value& profiles_value, const Value& plan_value) {
    const Object& route = fields(route_value);
    const Array& profiles = items(profiles_value);
    const Object& plan = fields(plan_value);
    const std::int64_t n = as_int(at(plan, "ticks"));
    const Array& events = items(at(plan, "events"));
    const std::size_t pairs = std::min(events.size(), profiles.size());
    Array contacts;
    for (std::size_t i = 0; i < pairs; ++i) {
        const Object& event = fields(events[i]);
        const Object& profile = fields(profiles[i]);
        const std::int64_t jump = as_int(at(event, "jump"));
        if (!is_null(at(event, "landing")))
            for (const auto& contact : items(at(profile, "contacts")))
                contacts.push_back(object(with(fields(contact), "state",
                                               value(add(whole(jump), num(at(fields(contact), "state")))))));
        for (std::int64_t tick = as_int(at(event, "landed")); tick < jump; ++tick)
            contacts.push_back(object({{"state", integer_value(tick + 1)},
                                       {"plane", items(at(profile, "position"))[0]},
                                       {"direction", integer_value(-1)}}));
    }
    Array tagged;
    for (const auto& c : contacts) tagged.push_back(object(with(fields(c), "axis", integer_value(1))));
    if (const Value* extra = get(plan, "verticalContacts"))
        tagged.insert(tagged.end(), items(*extra).begin(), items(*extra).end());
    Array untagged;
    for (const auto& c : merge_contacts(tagged, false)) {
        Object fields_without_axis = fields(c);
        erase(fields_without_axis, "axis");
        untagged.push_back(object(std::move(fields_without_axis)));
    }
    const Object& initial = fields(at(route, "initial"));
    Array jumps;
    for (const auto& j : items(at(plan, "jump"))) jumps.push_back(py_truthy(j) ? real(kJumpVelocity) : Value{nullptr});
    const Value* sneak = get(plan, "sneak");
    const Value* threshold = get(route, "physicsThreshold");
    Object vertical{{"profile", text("minecraft-1.8.9")},
                    {"position", items(at(initial, "position"))[1]},
                    {"velocity", items(at(initial, "velocity"))[1]},
                    {"onGround", boolean(true)},
                    {"previousHorizontalCollision", boolean(false)},
                    {"threshold", threshold ? *threshold : real(.005)},
                    {"jumpVelocity", array(std::move(jumps))},
                    {"sneak", sneak ? *sneak : array(Array(n, boolean(false)))},
                    {"contacts", array(std::move(untagged))}};
    std::set<std::int64_t> hit_states;
    if (const Value* hits = get(plan, "contacts"))
        for (const auto& c : items(*hits)) hit_states.insert(as_int(at(fields(c), "state")));
    double x = 0.;
    Array synthetic_hits;
    for (std::int64_t t = 0; t < n; ++t) {
        const bool hit = hit_states.count(t + 1);
        x += hit ? .05 : .1;
        if (hit)
            synthetic_hits.push_back(object({{"state", integer_value(t + 1)}, {"axis", integer_value(0)},
                                             {"direction", integer_value(1)}, {"plane", real(x)}}));
    }
    const Value* ladder = get(plan, "ladderTicks");
    const Value vertical_value = object(vertical);
    const Value problem = prepare_problem(object({
        {"version", integer_value(1)},
        {"ticks", integer_value(n)},
        {"position", array({integer_value(0), integer_value(0)})},
        {"velocity", array({integer_value(0), integer_value(0)})},
        {"magnitude", array(Array(n, real(.1)))},
        {"friction", array(Array(n, integer_value(0)))},
        {"base", array(Array(n, integer_value(0)))},
        {"yaw", array(Array(n, integer_value(0)))},
        {"contacts", array(std::move(synthetic_hits))},
        {"ladderTicks", ladder ? *ladder : array(Array(n + 1, boolean(false)))},
        {"objective", object({{"axis", text("X")}, {"sense", text("MAX")}, {"tick", integer_value(n)}})},
        {"vertical", vertical_value},
    }));
    const Value checked_value = validate(problem, at(fields(problem), "yaw"));
    const Object& checked = fields(checked_value);
    bool terminal_ground = true;
    if (!profiles.empty() && as_text(at(plan, "stage")) == "jump") {
        // profiles[len(plan['events']) - 1]: no events reads the last profile.
        const std::size_t last = events.empty() ? profiles.size() - 1 : events.size() - 1;
        const Object& last_profile = fields(profiles.at(last));
        if (is_program(last_profile)) terminal_ground = py_truthy(items(at(last_profile, "grounded")).back());
    }
    bool matched = true;
    const Array& y = items(at(checked, "y"));
    const Array& vy = items(at(checked, "vy"));
    for (std::size_t i = 0; i < pairs; ++i) {
        const Object& event = fields(events[i]);
        const Object& profile = fields(profiles[i]);
        if (is_program(profile) && !is_null(at(event, "landing"))) {
            const std::int64_t first = as_int(at(event, "jump")), end = as_int(at(event, "landing")) + 1;
            matched = matched && py_equal(slice(y, first, end), at(profile, "position")) &&
                      py_equal(slice(vy, first, end), at(profile, "velocity"));
        }
    }
    Array expected_ground = items(at(plan, "grounded"));
    expected_ground.push_back(boolean(terminal_ground));
    const bool feasible = matched && py_truthy(at(checked, "feasible")) &&
                          py_equal(at(checked, "onGround"), array(std::move(expected_ground)));
    return object({{"feasible", boolean(feasible)},
                   {"vertical", vertical_value},
                   {"y", at(checked, "y")},
                   {"vy", at(checked, "vy")},
                   {"onGround", at(checked, "onGround")}});
}

}  // namespace pkc
