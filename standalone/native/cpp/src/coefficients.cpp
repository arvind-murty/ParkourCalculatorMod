// Port of physics/coefficients.py. Replies must equal the Python bridge's
// canonical JSON; every coefficient is a float from the generated tables.
#include "pkc/coefficients.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "value_ops.hpp"

namespace pkc {
namespace {

struct SharedRow {
    std::array<bool, 6> key;
    std::array<double, 5> values;  // magnitude, friction, base, moveForward, moveStrafe
    std::optional<double> jump_velocity;
};

struct LegacyRow {
    std::uint32_t bits;
    std::array<bool, 5> key;
    std::array<double, 3> values;  // magnitude, friction, base
};

#include "coefficient_tables.inc"

// The ValueError PythonCompiler._exchange makes from a table KeyError.
template <std::size_t N>
[[noreturn]] void missing_row(const std::array<bool, N>& key, std::optional<std::uint32_t> bits = std::nullopt) {
    std::string text = "KeyError: (";
    if (bits) text += std::to_string(*bits) + ", ";
    for (std::size_t i = 0; i < N; ++i) text += (i ? ", " : "") + std::string(key[i] ? "True" : "False");
    fail(text + ")");
}

std::vector<bool> flags(const Value& value, const std::string& name, std::size_t size = 0) {  // _flags
    if (!is_list(value)) fail(name + " must be a list of booleans");
    std::vector<bool> out;
    out.reserve(items(value).size());
    for (const auto& item : items(value)) {
        auto* flag = std::get_if<bool>(&item.data);
        if (!flag) fail(name + " must be a list of booleans");
        out.push_back(*flag);
    }
    if (out.size() < size) fail(name + " is shorter than the movement schedule");
    return out;
}

Value bools(const std::vector<bool>& values) {
    Array out;
    out.reserve(values.size());
    for (bool value : values) out.push_back(boolean(value));
    return array(std::move(out));
}

struct Schedule {
    std::vector<bool> grounded, jump;
};

Schedule schedule(const Object& request) {  // _schedule
    const Value &grounded = get_or_null(request, "grounded"), &jump = get_or_null(request, "jump");
    const Value* inputs = get(request, "inputs");
    if (is_null(grounded) || is_null(jump) || !inputs || !is_object(*inputs)) fail("Invalid movement schedule");
    Schedule out{flags(grounded, "grounded"), flags(jump, "jump")};
    if (out.jump.empty() || out.jump.size() != out.grounded.size()) fail("Invalid movement schedule");
    return out;
}

bool policy(const Object& inputs, const char* key) {  // _policy: absent is false, present must be a bool
    const Value* value = get(inputs, key);
    if (!value) return false;
    auto* flag = std::get_if<bool>(&value->data);
    if (!flag) fail(std::string(key) + " must be boolean");
    return *flag;
}

Value shared(const Object& request) {
    Schedule s = schedule(request);
    const Object& inputs = fields(at(request, "inputs"));
    bool sprint_key = policy(inputs, "sprint"), force45 = policy(inputs, "force45");
    std::size_t n = s.jump.size();
    const Value& sneak_value = get_or_null(request, "sneak");
    std::vector<bool> sneak = is_null(sneak_value) ? std::vector<bool>(n, false) : flags(sneak_value, "sneak", n);
    sneak.resize(n);
    const Value& ladder = get_or_null(request, "ladderTicks");
    if (!is_null(ladder)) flags(ladder, "ladderTicks", n);
    const Value& vertical = get_or_null(request, "vertical");
    std::vector<bool> hits(n + 1, false);
    if (!is_null(vertical)) {
        const Value* previous = is_object(vertical) ? get(fields(vertical), "previousHorizontalCollision") : nullptr;
        if (!previous || !std::holds_alternative<bool>(previous->data))
            fail("Vertical state needs previousHorizontalCollision");
        hits[0] = std::get<bool>(previous->data);
    }
    const Value& contacts = get_or_null(request, "contacts");
    if (py_truthy(contacts)) {
        for (const auto& contact_value : items(contacts)) {
            const Object& contact = fields(contact_value);
            const Value* axis = get(contact, "axis");
            if (axis && py_equal(*axis, integer_value(1))) continue;
            const Value* state = get(contact, "state");
            const auto* tick = state ? std::get_if<std::int64_t>(&state->data) : nullptr;
            if (!tick || !(0 < *tick && *tick <= static_cast<std::int64_t>(n))) fail("Invalid prescribed wall state");
            hits[*tick] = true;
        }
    }
    if (!is_null(vertical)) {
        const Object& state = fields(vertical);
        if (!py_equal(get_or_null(state, "profile"), text("minecraft-1.8.9")) ||
            !py_equal(get_or_null(state, "onGround"), boolean(s.grounded[0])))
            fail("Vertical profile/initial ground state disagrees with the fixed movement inputs");
    }
    std::array<Array, 6> columns;  // magnitude, friction, base, moveForward, moveStrafe, jumpVelocity
    for (auto& column : columns) column.reserve(n);
    std::vector<bool> sprinting, air_sprinting;
    bool previous = policy(inputs, "incomingSprint");
    int cooldown = 0;
    for (std::size_t t = 0; t < n; ++t) {
        cooldown = std::max(0, cooldown - 1);
        bool fires = s.jump[t] && s.grounded[t] && cooldown == 0;
        if (fires) cooldown = 10;
        if (!s.jump[t]) cooldown = 0;
        bool diagonal = force45 && !(s.jump[t] && s.grounded[t]);
        // With W always held, the double-tap path cannot start sprint after the first
        // input and a held key reapplies sprint when its 600-tick timer expires.
        bool current = (previous || sprint_key) && !sneak[t] && !hits[t];
        std::array<bool, 6> key = {s.grounded[t], diagonal, sneak[t], current, previous, fires};
        const auto* row = std::find_if(std::begin(kSharedRows), std::end(kSharedRows),
                                       [&](const SharedRow& candidate) { return candidate.key == key; });
        if (row == std::end(kSharedRows)) missing_row(key);
        for (int i = 0; i < 5; ++i) columns[i].push_back(real(row->values[i]));
        columns[5].push_back(row->jump_velocity ? real(*row->jump_velocity) : kNull);
        sprinting.push_back(current);
        air_sprinting.push_back(previous);
        previous = current;
    }
    Object movement = {{"profile", text(kSharedProfileLabel)},
                       {"compiler", text(kSharedCompilerLabel)},
                       {"horizontalCollision", bools(hits)},
                       {"ground", bools(s.grounded)},
                       {"sprinting", bools(sprinting)},
                       {"airSprinting", bools(air_sprinting)},
                       {"moveForward", array(std::move(columns[3]))},
                       {"moveStrafe", array(std::move(columns[4]))}};
    Object result = {{"magnitude", array(std::move(columns[0]))},
                     {"friction", array(std::move(columns[1]))},
                     {"base", array(std::move(columns[2]))},
                     {"movement", object(std::move(movement))}};
    if (!is_null(vertical)) {
        // The bridge returns the request's vertical record with these two fields replaced.
        Object replied = fields(vertical);
        put(replied, "jumpVelocity", array(std::move(columns[5])));
        put(replied, "sneak", bools(sneak));
        result.emplace_back("vertical", object(std::move(replied)));
    }
    result.emplace_back("model", text(kSharedModelLabel));
    return object(std::move(result));
}

Value legacy(const Object& request) {
    Schedule s = schedule(request);
    const Object& inputs = fields(at(request, "inputs"));
    const Value* forward_value = get(inputs, "forward");
    const Value* strafe = get(inputs, "strafe");
    double forward = 0.;
    bool number = !forward_value || (!std::holds_alternative<bool>(forward_value->data) && numeric(*forward_value, forward));
    if (!number || (strafe && !py_equal(*strafe, integer_value(0))) ||
        std::abs(forward - kLegacyForward) > kLegacyForwardTolerance)
        fail("Legacy coefficients cover forward movement without strafe");
    // legacy_forward_bits: Gson reads a float field as the parsed double cast to float.
    float narrowed = static_cast<float>(forward);
    std::uint32_t bits;
    std::memcpy(&bits, &narrowed, sizeof bits);
    bool sprint = policy(inputs, "sprint"), force45 = policy(inputs, "force45");
    bool previous = policy(inputs, "incomingSprint");
    std::array<Array, 3> columns;
    for (std::size_t t = 0; t < s.jump.size(); ++t) {
        std::array<bool, 5> key = {s.grounded[t], s.jump[t], sprint, t == 0 ? previous : sprint, force45};
        const auto* row = std::find_if(std::begin(kLegacyRows), std::end(kLegacyRows), [&](const LegacyRow& candidate) {
            return candidate.bits == bits && candidate.key == key;
        });
        if (row == std::end(kLegacyRows)) missing_row(key, bits);
        for (int i = 0; i < 3; ++i) columns[i].push_back(real(row->values[i]));
    }
    return object({{"magnitude", array(std::move(columns[0]))},
                   {"friction", array(std::move(columns[1]))},
                   {"base", array(std::move(columns[2]))},
                   {"model", text(kLegacyModel)}});
}

}  // namespace

Value compile_coefficients(const Value& request) {
    const Object& message = fields(request);
    return py_truthy(get_or_null(message, "sharedMovement")) ? shared(message) : legacy(message);
}

}  // namespace pkc
