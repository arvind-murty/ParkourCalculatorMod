// Port of validation/numerical.py. Positions, velocities and residuals are Num
// so the returned int/float kinds match the Python's (a request's integer start
// or contact plane stays an int).
#include "pkc/validation/numerical.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

using Pair = std::array<Num, 2>;

Num neg(const Num& a) { return {-a.value, a.integral}; }
Num py_abs(const Num& a) { return {std::fabs(a.value), a.integral}; }

void finite(const Num& value, const std::string& name) {
    if (!std::isfinite(value.value)) fail(name + " must be a finite number");
}

double wrap(double value) {  // physics/angles.wrap
    value = std::fmod(value, 360);
    if (value > 180) value -= 360;
    if (value <= -180) value += 360;
    return value;
}

std::int64_t cap_branch(double value) { return value < -kCap ? -1 : value > kCap ? 1 : 0; }  // physics/vertical.branch

std::vector<Num> nums(const Value& values) {
    std::vector<Num> out;
    for (const auto& v : items(values)) out.push_back(num(v));
    return out;
}

std::vector<bool> flags(const Value& values) {
    std::vector<bool> out;
    for (const auto& v : items(values)) out.push_back(std::get<bool>(v.data));
    return out;
}

Value pair(const Pair& p) { return array({value(p[0]), value(p[1])}); }

Value bools(const std::vector<bool>& values) {
    Array out;
    for (bool v : values) out.push_back(boolean(v));
    return array(std::move(out));
}

Value num_values(const std::vector<Num>& values) {
    Array out;
    for (const auto& v : values) out.push_back(value(v));
    return array(std::move(out));
}

// The vertical config's contacts as vertical_step rows; prepare checked them.
std::vector<VerticalContactRow> vertical_rows(const Object& config) {
    std::vector<VerticalContactRow> rows;
    for (const auto& contact_value : items(at(config, "contacts"))) {
        const Object& c = fields(contact_value);
        VerticalContactRow row{as_int(at(c, "state")), as_int(at(c, "direction")), num(at(c, "plane")),
                               false, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
        if (const Value* kind = get(c, "case"); kind && py_truthy(*kind)) {
            row.movement_case = as_text(*kind);
            if (const Value* v = get(c, "lowerHeight")) row.lower_height = num(*v);
            if (const Value* v = get(c, "higherHeight")) row.higher_height = num(*v);
            if (const Value* v = get(c, "clearance")) row.clearance = num(*v);
        } else if (const Value* step = get(c, "step")) {
            row.step = std::get<bool>(step->data);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// _replay: the one recurrence/contact/wall/vertical check; controls[t] is tick t's acceleration.
Object replay(const Object& problem, const Array& yaw_values, const std::vector<Num>& yaw,
              const std::vector<Pair>& controls) {
    const std::int64_t n = as_int(at(problem, "ticks"));
    const std::vector<Num> start = nums(at(problem, "position")), speed = nums(at(problem, "velocity"));
    std::vector<Pair> positions{{start[0], start[1]}}, velocities{{speed[0], speed[1]}};
    std::map<std::pair<std::int64_t, std::int64_t>, const Object*> contacts;
    for (const auto& c : items(at(problem, "contacts")))
        contacts[{as_int(at(fields(c), "state")), as_int(at(fields(c), "axis"))}] = &fields(c);
    const Num threshold = num(at(problem, "threshold"));
    const std::vector<Num> friction = nums(at(problem, "friction"));
    Array missed, wrong_side, mismatches, caps, cap_mismatches, movement_mismatches, vertical_failures;
    const Value* ladder_ticks = get(problem, "ladderTicks");
    const std::vector<bool> ladder = ladder_ticks ? flags(*ladder_ticks) : std::vector<bool>(n + 1, false);
    const Value* vertical_value = get(problem, "vertical");
    const Object* vertical = vertical_value ? &fields(*vertical_value) : nullptr;
    const Value* movement_value = get(problem, "movement");
    const Object* movement = movement_value ? &fields(*movement_value) : nullptr;
    const Value* inertia = get(problem, "inertiaPattern");
    const Value* ladder_pattern = get(problem, "ladderPattern");
    std::vector<bool> previous_hits{vertical   ? std::get<bool>(at(*vertical, "previousHorizontalCollision").data)
                                    : movement ? std::get<bool>(items(at(*movement, "horizontalCollision"))[0].data)
                                               : false};
    std::vector<Num> y, vy;
    std::vector<bool> grounded;
    std::vector<VerticalContactRow> rows;
    std::vector<std::optional<Num>> jumps;
    std::vector<bool> sneak;
    Num vertical_threshold;
    if (vertical) {
        y.push_back(num(at(*vertical, "position")));
        vy.push_back(num(at(*vertical, "velocity")));
        grounded.push_back(std::get<bool>(at(*vertical, "onGround").data));
        rows = vertical_rows(*vertical);
        for (const auto& j : items(at(*vertical, "jumpVelocity")))
            jumps.push_back(is_null(j) ? std::nullopt : std::optional<Num>(num(j)));
        sneak = flags(at(*vertical, "sneak"));
        vertical_threshold = num(at(*vertical, "threshold"));
    }
    for (std::int64_t t = 0; t < n; ++t) {
        Pair move, next_velocity;
        Array tick_caps;
        bool hit = false;
        for (std::int64_t axis = 0; axis < 2; ++axis) {
            const Num incoming = velocities[t][axis];
            const bool zero = std::fabs(incoming.value) < threshold.value;
            if (inertia) {
                const std::int64_t branch = as_int(items(items(*inertia)[t])[axis]);
                if ((branch == 0 && !zero) ||
                    ((branch == -1 || branch == 1) && mul(whole(branch), incoming).value < threshold.value))
                    mismatches.push_back(object({{"tick", integer_value(t)}, {"axis", integer_value(axis)},
                                                 {"incoming", value(incoming)}, {"branch", integer_value(branch)}}));
            }
            Num displacement = add(zero ? whole(0) : incoming, controls[t][axis]);
            tick_caps.push_back(integer_value(ladder[t] ? cap_branch(displacement.value) : 0));
            if (ladder_pattern && ladder[t]) {
                const std::int64_t b = as_int(items(items(*ladder_pattern)[t])[axis]);
                const double d = displacement.value;
                const bool valid = b == -1  ? d <= -kCap + kTolerance
                                   : b == 1 ? d >= kCap - kTolerance
                                            : -kCap - kTolerance <= d && d <= kCap + kTolerance;
                if (!valid)
                    cap_mismatches.push_back(object({{"tick", integer_value(t)}, {"axis", integer_value(axis)},
                                                     {"raw", value(displacement)}, {"branch", integer_value(b)}}));
            }
            if (ladder[t]) displacement = py_max(flt(-kCap), py_min(flt(kCap), displacement));  // clamp
            Num proposed = add(positions[t][axis], displacement);
            Num carry = mul(friction[t], displacement);
            if (auto found = contacts.find({t + 1, axis}); found != contacts.end()) {
                const Object& contact = *found->second;
                const Num direction = num(at(contact, "direction")), plane = num(at(contact, "plane"));
                const Num approach = mul(direction, sub(positions[t][axis], plane));
                const Num reach = mul(direction, sub(proposed, plane));
                if (approach.value > kTolerance)
                    wrong_side.push_back(object({{"state", integer_value(t + 1)}, {"axis", integer_value(axis)},
                                                 {"violation", value(approach)}}));
                if (reach.value < 0)
                    missed.push_back(object({{"state", integer_value(t + 1)}, {"axis", integer_value(axis)},
                                             {"shortBy", value(neg(reach))}}));
                if (approach.value <= kTolerance && reach.value >= 0) {
                    hit |= reach.value > 0;
                    proposed = plane;
                    carry = flt(0.0);
                }
            }
            finite(proposed, "replayed position");
            finite(carry, "replayed velocity");
            move[axis] = proposed;
            next_velocity[axis] = carry;
        }
        positions.push_back(move);
        velocities.push_back(next_velocity);
        caps.push_back(array(std::move(tick_caps)));
        previous_hits.push_back(hit);
        if (movement && hit != std::get<bool>(items(at(*movement, "horizontalCollision"))[t + 1].data))
            movement_mismatches.push_back(
                object({{"state", integer_value(t + 1)}, {"reason", text("horizontal_collision_schedule")}}));
        if (vertical) {
            StepResult step = vertical_step(rows, jumps, sneak, vertical_threshold, t, y[t], vy[t], ladder[t],
                                            grounded[t], kTolerance, &vertical_failures);
            y.push_back(step.proposed);
            vy.push_back(vertical_carry(step.carry, ladder[t + 1], previous_hits[t + 1]));
            grounded.push_back(step.ground);
            if (movement && grounded[t] != std::get<bool>(items(at(*movement, "ground"))[t].data))
                movement_mismatches.push_back(object({{"state", integer_value(t)}, {"reason", text("ground_schedule")}}));
        }
    }
    Num worst = flt(0.0);
    Value worst_name{nullptr};
    if (vertical) {
        for (const auto& [key, values, label] : {std::tuple{"referencePosition", &y, "Y"},
                                                  std::tuple{"referenceVelocity", &vy, "VY"}}) {
            const Value* expected = get(*vertical, key);
            if (!expected) continue;
            for (std::size_t state = 0; state < items(*expected).size(); ++state) {
                Num violation = py_abs(sub((*values)[state], num(items(*expected)[state])));
                if (violation.value > worst.value) {
                    worst = violation;
                    worst_name = text(std::string(label) + "@" + std::to_string(state));
                }
            }
        }
    }
    for (const auto& wall_value : items(at(problem, "walls"))) {
        const Object& wall = fields(wall_value);
        const std::string& mode = as_text(at(wall, "mode"));
        const std::int64_t t1 = as_int(at(wall, "t1"));
        const Value& t2 = get_or_null(wall, "t2");
        auto sign = [&] { return whole(as_text(at(wall, "op")) == "PLUS" ? 1 : -1); };
        Num lhs;
        if (mode == "X" || mode == "Z") {
            const int axis = mode == "X" ? 0 : 1;
            lhs = positions[t1][axis];
            if (!is_null(t2)) lhs = add(lhs, mul(sign(), positions[as_int(t2)][axis]));
        } else if (mode == "F") {
            lhs = yaw[t1];
            if (!is_null(t2)) lhs = add(lhs, mul(sign(), yaw[as_int(t2)]));
        } else {
            const Pair &a = positions[t1], &b = positions[as_int(t2)];
            const Num dx = py_abs(sub(a[0], b[0])), dz = py_abs(sub(a[1], b[1]));
            lhs = mode == "DXZ" ? sub(dx, dz) : sub(dz, dx);
        }
        Num residual = sub(lhs, num(at(wall, "rhs")));
        if (mode == "F") residual = flt(wrap(residual.value));
        finite(residual, "constraint residual");
        const std::string& cmp = as_text(at(wall, "cmp"));
        const Num violation = cmp == "EQ" ? py_abs(residual) : py_max(flt(0.0), cmp == "LE" ? residual : neg(residual));
        if (violation.value > worst.value) {
            worst = violation;
            const Value* name = get(wall, "name");
            worst_name = name ? *name : text(mode + "@" + std::to_string(t1));
        }
    }
    const Object& objective = fields(at(problem, "objective"));
    const std::int64_t tick = as_int(at(objective, "tick"));
    Num score;
    const Value& custom = get_or_null(objective, "customYaw");
    if (is_null(custom)) {
        score = positions[tick][as_text(at(objective, "axis")) == "X" ? 0 : 1];
    } else {
        const double angle = radians(num(custom).value);
        Pair point = positions[tick];
        if (as_text(at(objective, "type")) == "MOTION" && tick > 0)
            point = {sub(point[0], positions[tick - 1][0]), sub(point[1], positions[tick - 1][1])};
        score = flt(-std::sin(angle) * point[0].value + std::cos(angle) * point[1].value);
    }
    finite(score, "objective");
    const bool feasible = worst.value <= kTolerance && missed.empty() && wrong_side.empty() && mismatches.empty() &&
                          cap_mismatches.empty() && vertical_failures.empty() && movement_mismatches.empty();
    Array position_values, velocity_values;
    for (const auto& p : positions) position_values.push_back(pair(p));
    for (const auto& v : velocities) velocity_values.push_back(pair(v));
    Object result{{"feasible", boolean(feasible)},
                  {"violation", value(worst)},
                  {"worstConstraint", worst_name},
                  {"objective", value(score)},
                  {"missedContacts", array(std::move(missed))},
                  {"wrongSideContacts", array(std::move(wrong_side))},
                  {"patternMismatches", array(std::move(mismatches))},
                  {"yaw", array(yaw_values)},
                  {"positions", array(std::move(position_values))},
                  {"velocity", array(std::move(velocity_values))}};
    if (movement) {
        put(result, "movementMismatches", array(std::move(movement_mismatches)));
        put(result, "horizontalCollision", bools(previous_hits));
    }
    if (ladder_ticks) {
        put(result, "ladderPattern", array(std::move(caps)));
        put(result, "ladderPatternMismatches", array(std::move(cap_mismatches)));
        put(result, "horizontalCollision", bools(previous_hits));
    }
    if (vertical) {
        put(result, "y", num_values(y));
        put(result, "vy", num_values(vy));
        put(result, "onGround", bools(grounded));
        put(result, "horizontalCollision", bools(previous_hits));
        put(result, "verticalFailures", array(std::move(vertical_failures)));
        put(result, "resume", object({{"y", value(y.back())},
                                      {"vy", value(vy.back())},
                                      {"onGround", boolean(grounded.back())},
                                      {"previousHorizontalCollision", boolean(previous_hits.back())}}));
    }
    return result;
}

}  // namespace

Value validate(const Value& problem_value, const Value& yaw_value) {
    const Object& problem = fields(problem_value);
    const std::int64_t n = as_int(at(problem, "ticks"));
    if (!is_list(yaw_value) || static_cast<std::int64_t>(items(yaw_value).size()) != n)
        fail("Wrong returned facing count");
    std::vector<Num> yaw;
    for (const auto& v : items(yaw_value)) yaw.push_back(number(v, "returned facing"));
    const std::vector<Num> base = nums(at(problem, "base")), magnitude = nums(at(problem, "magnitude"));
    std::vector<Pair> controls;
    for (std::int64_t t = 0; t < n; ++t) {
        const double angle = base[t].value + radians(yaw[t].value);
        controls.push_back({flt(magnitude[t].value * std::cos(angle)), flt(magnitude[t].value * std::sin(angle))});
    }
    return object(replay(problem, items(yaw_value), yaw, controls));
}

Value validate_controls(const Value& problem_value, const Value& controls_value) {
    const Object& problem = fields(problem_value);
    const std::int64_t n = as_int(at(problem, "ticks"));
    const auto ticks = static_cast<std::size_t>(n);
    if (!is_list(controls_value) || items(controls_value).size() != ticks ||
        std::any_of(items(controls_value).begin(), items(controls_value).end(),
                    [](const Value& c) { return !is_list(c) || items(c).size() != 2; }))
        fail("Wrong returned control count");
    std::vector<Pair> controls;
    for (const auto& c : items(controls_value))
        controls.push_back({number(items(c)[0], "returned control"), number(items(c)[1], "returned control")});
    const std::vector<Num> base = nums(at(problem, "base")), magnitude = nums(at(problem, "magnitude"));
    std::vector<Num> yaw;
    Array yaw_values;
    for (std::size_t t = 0; t < ticks; ++t) {
        yaw.push_back(flt(wrap(degrees(std::atan2(controls[t][1].value, controls[t][0].value) - base[t].value))));
        yaw_values.push_back(value(yaw.back()));
    }
    Object result = replay(problem, yaw_values, yaw, controls);
    // Full-magnitude controls exceed the disk by rounding; only a real excess fails.
    std::size_t worst = 0;
    std::vector<double> excess;
    for (std::size_t t = 0; t < ticks; ++t) {
        excess.push_back(python_hypot(controls[t][0].value, controls[t][1].value) - magnitude[t].value);
        if (excess[t] > excess[worst]) worst = t;
    }
    if (excess[worst] > kTolerance) {
        put(result, "feasible", boolean(false));
        if (excess[worst] > num(at(result, "violation")).value) {
            put(result, "violation", real(excess[worst]));
            put(result, "worstConstraint", text("acceleration_disk@" + std::to_string(worst)));
        }
    }
    Array copies;
    for (const auto& c : items(controls_value)) copies.push_back(array(items(c)));
    put(result, "controls", array(std::move(copies)));
    return object(std::move(result));
}

}  // namespace pkc
