// Port of movement/vertical_profiles.trace_ladder_schedule, returning the Python's
// dict in its key order with itemized failures.
#include "pkc/movement/vertical_profiles.hpp"

#include <vector>

#include "../schema.hpp"
#include "pkc/movement/contact_combinations.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

std::vector<bool> flags(const Value& values, const std::string& name, std::optional<std::size_t> length) {  // _flags
    if (!is_list(values) || std::any_of(items(values).begin(), items(values).end(), [](const Value& v) {
            return !std::holds_alternative<bool>(v.data);
        }))
        fail(name + " must contain booleans");
    if (length && items(values).size() != *length) fail(name + " has the wrong length");
    std::vector<bool> out;
    for (const auto& v : items(values)) out.push_back(std::get<bool>(v.data));
    return out;
}

Value bools(const std::vector<bool>& values, std::size_t count) {
    Array out;
    for (std::size_t i = 0; i < count; ++i) out.push_back(boolean(values[i]));
    return array(std::move(out));
}

Value nums(const std::vector<Num>& values) {
    Array out;
    for (const auto& v : values) out.push_back(value(v));
    return array(std::move(out));
}

}  // namespace

Value trace_ladder_schedule(const Value& initial, const Value& ladder_value, const Value& sneak_value,
                            const Value& hit_value, const Value& jump, const Value& target_top_value,
                            const Value& min_landing_state, const Value& contacts, const Value& occupied_y_bounds,
                            bool stop_on_landing, const Value& threshold_value) {
    const std::vector<bool> hit = flags(hit_value, "hit", std::nullopt);
    const std::int64_t n = integer(integer_value(static_cast<std::int64_t>(hit.size())), "ticks", 1, 200);
    const auto ticks = static_cast<std::size_t>(n);
    const std::vector<bool> ladder = flags(ladder_value, "ladder", ticks + 1);
    const std::vector<bool> sneak = flags(sneak_value, "Sneak", ticks);
    if (!is_object(initial) || std::any_of(fields(initial).begin(), fields(initial).end(), [](const auto& f) {
            return f.first != "position" && f.first != "velocity" && f.first != "onGround" &&
                   f.first != "previousHorizontalCollision";
        }))
        fail("Supply a scalar initial Y state");
    const Object& start = fields(initial);
    Num y = number(get_or_null(start, "position"), "initial Y"), vy = number(get_or_null(start, "velocity"), "initial VY");
    const Value* ground_value = get(start, "onGround");
    const Value* previous_value = get(start, "previousHorizontalCollision");
    if ((ground_value && !std::holds_alternative<bool>(ground_value->data)) ||
        (previous_value && !std::holds_alternative<bool>(previous_value->data)))
        fail("Initial ground/collision flags must be boolean");
    bool ground = ground_value && std::get<bool>(ground_value->data);
    const bool previous = previous_value && std::get<bool>(previous_value->data);
    Array impulses = is_null(jump) ? Array(ticks, Value{nullptr}) : items(jump);
    if (impulses.size() != ticks) fail("Jump impulses have the wrong length");
    std::vector<std::optional<Num>> jumps;
    for (const auto& v : impulses)
        jumps.push_back(is_null(v) ? std::nullopt : std::optional<Num>(number(v, "jump impulse")));
    std::optional<Num> target_top;
    if (!is_null(target_top_value)) target_top = number(target_top_value, "target top");
    const std::int64_t min_landing = integer(min_landing_state, "minimum landing state", 0, n);
    std::optional<std::pair<Num, Num>> occupied;
    if (!is_null(occupied_y_bounds)) {
        if (!is_list(occupied_y_bounds) || items(occupied_y_bounds).size() != 2)
            fail("Occupied Y bounds require a lower and upper limit");
        const Num lower = number(items(occupied_y_bounds)[0], "occupied Y bound");
        const Num upper = number(items(occupied_y_bounds)[1], "occupied Y bound");
        if (lower.value >= upper.value) fail("Occupied Y bounds must be nonempty");
        occupied = {lower, upper};
    }
    const Value selected_value = py_truthy(contacts) ? contacts : array({});  // copy.deepcopy(contacts or [])
    if (!is_list(selected_value)) fail("Vertical contacts must be a list");
    Array tagged;
    for (const auto& contact_value : items(selected_value)) {
        const Object& contact = fields(contact_value);
        integer(get_or_null(contact, "state"), "vertical contact state", 1, n);
        number(get_or_null(contact, "plane"), "vertical contact plane");
        const Value& direction = get_or_null(contact, "direction");
        if (!std::holds_alternative<std::int64_t>(direction.data) || (as_int(direction) != -1 && as_int(direction) != 1))
            fail("Vertical contact direction must be -1 or 1");
        tagged.push_back(object(with(contact, "axis", integer_value(1))));
    }
    Array config_contacts;
    for (const auto& c : merge_contacts(tagged, false)) {
        Object untagged = fields(c);
        erase(untagged, "axis");
        config_contacts.push_back(object(std::move(untagged)));
    }
    const Num threshold = number(threshold_value, "velocity threshold");
    if (threshold.value < 0) fail("Velocity threshold must be nonnegative");

    std::vector<Num> positions{y}, velocities{vy}, moves;
    std::vector<bool> grounds{ground}, refreshes;
    Array failures;
    bool landed = false;
    auto failure = [&](std::int64_t state) {
        failures.push_back(object({{"state", integer_value(state)}, {"reason", text("occupied_y_outside_region")}}));
    };
    auto inside = [&](const Num& value) {
        return occupied->first.value <= value.value && value.value < occupied->second.value;
    };
    std::vector<VerticalContactRow> rows = vertical_rows(config_contacts);
    for (std::int64_t t = 0; t < n; ++t) {
        if (occupied && ladder[t] && !inside(y)) {
            failure(t);
            break;
        }
        if (target_top) {
            // vertical_move(config, t, vy, ladder[t])
            Num movement = std::fabs(vy.value) < threshold.value ? flt(0.0) : vy;
            if (jumps[t]) movement = *jumps[t];
            if (ladder[t]) {
                if (-0.15 > movement.value) movement = flt(-0.15);
                if (sneak[t] && movement.value < 0) movement = flt(0.0);
            }
            if (t + 1 >= min_landing && movement.value < 0 && y.value >= target_top->value &&
                add(y, movement).value < target_top->value) {
                const bool known = std::any_of(config_contacts.begin(), config_contacts.end(), [&](const Value& c) {
                    return as_int(at(fields(c), "state")) == t + 1;
                });
                if (!known) {
                    config_contacts.push_back(object({{"state", integer_value(t + 1)}, {"plane", value(*target_top)},
                                                      {"direction", integer_value(-1)}}));
                    rows = vertical_rows(config_contacts);
                }
            }
        }
        StepResult step = vertical_step(rows, jumps, sneak, threshold, t, y, vy, ladder[t], ground, 0, &failures);
        y = step.proposed;
        ground = step.ground;
        vy = vertical_carry(step.carry, ladder[t + 1], hit[t]);
        positions.push_back(y);
        velocities.push_back(vy);
        grounds.push_back(ground);
        moves.push_back(step.carry);
        refreshes.push_back(ladder[t + 1] && hit[t]);
        if (occupied && ladder[t + 1] && !inside(y)) {
            failure(t + 1);
            break;
        }
        if (ground && std::any_of(config_contacts.begin(), config_contacts.end(), [&](const Value& c_value) {
                const Object& c = fields(c_value);
                const Value* kind = get(c, "case");
                return as_int(at(c, "state")) == t + 1 && as_int(at(c, "direction")) < 0 && !(kind && py_truthy(*kind));
            })) {
            landed = true;
            if (stop_on_landing) break;
        }
    }
    const std::size_t count = moves.size();
    Array kept_contacts;
    for (const auto& c : config_contacts)
        if (as_int(at(fields(c), "state")) <= static_cast<std::int64_t>(count)) kept_contacts.push_back(c);
    const Object config{{"profile", text("minecraft-1.8.9")},
                        {"position", get_or_null(start, "position")},
                        {"velocity", get_or_null(start, "velocity")},
                        {"onGround", boolean(grounds.front())},
                        {"previousHorizontalCollision", boolean(previous)},
                        {"threshold", threshold_value},
                        {"jumpVelocity", array(Array(impulses.begin(), impulses.begin() + count))},
                        {"sneak", bools(sneak, count)},
                        {"contacts", array(std::move(kept_contacts))}};
    const std::string status = !failures.empty() ? "invalid" : landed ? "landed" : "simulated";
    return object({{"status", text(status)},
                   {"ticks", integer_value(static_cast<std::int64_t>(count))},
                   {"position", nums(positions)},
                   {"velocity", nums(velocities)},
                   {"onGround", bools(grounds, grounds.size())},
                   {"movement", nums(moves)},
                   {"climbRefreshed", bools(refreshes, refreshes.size())},
                   {"failures", array(std::move(failures))},
                   {"vertical", object(config)},
                   {"ladderTicks", bools(ladder, count + 1)},
                   {"sneak", bools(sneak, count)},
                   {"hit", bools(hit, count)},
                   {"resume", object({{"position", value(positions.back())},
                                      {"velocity", value(velocities.back())},
                                      {"onGround", boolean(grounds.back())},
                                      {"previousHorizontalCollision", boolean(count ? hit[count - 1] : previous)},
                                      {"onLadder", boolean(ladder[count])}})}});
}

}  // namespace pkc
