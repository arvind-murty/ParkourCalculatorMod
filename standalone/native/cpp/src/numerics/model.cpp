// Port of numerics/model.py. prepare fills defaults with setdefault, so new
// keys land at the end of each dict as in the Python.
#include "pkc/numerics/model.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "../schema.hpp"
#include "pkc/canonical.hpp"

namespace pkc {
namespace {

bool is_int(const Value& value) { return std::holds_alternative<std::int64_t>(value.data); }
bool is_bool(const Value& value) { return std::holds_alternative<bool>(value.data); }
bool is_text(const Value& value, std::string_view expected) {
    auto* held = std::get_if<std::string>(&value.data);
    return held && *held == expected;
}
bool is_one_of(const Value& value, std::initializer_list<const char*> options) {
    return std::any_of(options.begin(), options.end(), [&](const char* option) { return is_text(value, option); });
}

// Python str(value) for the error message; containers keep a plain list/dict layout.
std::string py_str(const Value& value) {
    if (auto* s = std::get_if<std::string>(&value.data)) return *s;
    if (is_null(value)) return "None";
    if (auto* b = std::get_if<bool>(&value.data)) return *b ? "True" : "False";
    if (auto* i = std::get_if<std::int64_t>(&value.data)) return std::to_string(*i);
    if (auto* d = std::get_if<double>(&value.data)) return float_repr(*d);
    return dumps(value, false, false);
}

void numbers(const Value* values, const std::string& name, std::size_t length, const std::string& error) {
    if (!values || !is_list(*values) || items(*values).size() != length) fail(error);
    for (const auto& value : items(*values)) number(value, name);
}

void booleans(const Value* values, std::size_t length, const std::string& error) {
    if (!values || !is_list(*values) || items(*values).size() != length ||
        std::any_of(items(*values).begin(), items(*values).end(), [](const Value& v) { return !is_bool(v); }))
        fail(error);
}

bool truthy_get(const Object& object, std::string_view key) {
    const Value* value = get(object, key);
    return value && py_truthy(*value);
}

Value check_vertical(const Value& config_value, std::int64_t n) {
    if (!is_object(config_value) || !is_text(get_or_null(fields(config_value), "profile"), "minecraft-1.8.9"))
        fail("vertical requires the minecraft-1.8.9 profile");
    Object config = fields(config_value);
    for (const char* key : {"position", "velocity"}) number(get_or_null(config, key), std::string("vertical.") + key);
    for (const char* key : {"onGround", "previousHorizontalCollision"})
        if (!is_bool(get_or_null(config, key))) fail(std::string("vertical.") + key + " must be a boolean");
    if (number(setdefault(config, "threshold", real(0.005)), "vertical.threshold").value < 0)
        fail("vertical.threshold must be nonnegative");
    for (const char* key : {"sneak", "jumpVelocity"}) {
        const Value& values = get_or_null(config, key);
        if (!is_list(values) || static_cast<std::int64_t>(items(values).size()) != n)
            fail(std::string("vertical.") + key + " needs one value per input tick");
    }
    for (const auto& value : items(at(config, "sneak")))
        if (!is_bool(value)) fail("vertical.sneak must contain booleans");
    for (const auto& value : items(at(config, "jumpVelocity")))
        if (!is_null(value) && number(value, "vertical.jumpVelocity").value < 0)
            fail("Jump velocities must be nonnegative or null");
    const Value& contacts = setdefault(config, "contacts", array({}));
    if (!is_list(contacts)) fail("vertical.contacts must be a list");
    std::set<std::int64_t> seen;
    for (const auto& contact_value : items(contacts)) {
        if (!is_object(contact_value)) fail("Malformed vertical contact");
        const Object& contact = fields(contact_value);
        std::int64_t state = integer(get_or_null(contact, "state"), "vertical contact state", 1, n);
        number(get_or_null(contact, "plane"), "vertical contact plane");
        const Value& direction = get_or_null(contact, "direction");
        if (!is_int(direction) || (as_int(direction) != -1 && as_int(direction) != 1) || seen.count(state))
            fail("Invalid or duplicate vertical contact");
        const Value* step = get(contact, "step");
        if ((step && !is_bool(*step)) || (step && std::get<bool>(step->data) && as_int(direction) != -1))
            fail("A prescribed step must be a lower contact");
        if (truthy_get(contact, "case")) {
            if (as_int(direction) != -1 || truthy_get(contact, "step"))
                fail("A movement case requires its own lower contact");
            if (!is_one_of(at(contact, "case"), {"normal_step", "slab_boost", "blip", "blip_up",
                                                 "corner_jump_cancel", "corner_blip", "corner_blip_up", "grind"}))
                fail("Unsupported vertical movement case");
            for (const char* key : {"lowerHeight", "higherHeight", "clearance"})
                if (const Value* value = get(contact, key)) number(*value, std::string("vertical case ") + key);
        }
        seen.insert(state);
    }
    for (const char* key : {"referencePosition", "referenceVelocity"})
        if (const Value* values = get(config, key))
            numbers(values, std::string("vertical.") + key, n + 1,
                    std::string("vertical.") + key + " needs one value per state");
    return object(std::move(config));
}

}  // namespace

Value prepare_problem(const Value& request) {
    if (!is_object(request) || !is_int(get_or_null(fields(request), "version")) ||
        as_int(at(fields(request), "version")) != 1)
        fail("Expected a version 1 continuous problem");
    Object problem = fields(request);
    const std::int64_t n = integer(get_or_null(problem, "ticks"), "ticks", 1, 100000);
    const auto ticks = static_cast<std::size_t>(n);
    for (const auto& [key, length] : std::initializer_list<std::pair<const char*, std::size_t>>{
             {"position", 2}, {"velocity", 2}, {"magnitude", ticks}, {"friction", ticks}, {"base", ticks},
             {"yaw", ticks}})
        numbers(get(problem, key), key, length, std::string(key) + " has the wrong length");
    if (const Value* fallback = get(problem, "fallbackYaw"))
        numbers(fallback, "fallbackYaw", ticks, "fallbackYaw has the wrong length");
    // Post-alarm hint (numerics/post_alarm.hpp), not physical: the parent answer's facings on the shared prefix.
    if (const Value* warm = get(problem, "warmYaw")) {
        if (!is_list(*warm) || items(*warm).empty() || items(*warm).size() > ticks) fail("warmYaw has the wrong length");
        for (const auto& value : items(*warm)) number(value, "warmYaw");
    }
    for (const auto& m : items(at(problem, "magnitude")))
        if (num(m).value < 0) fail("Acceleration magnitudes must be nonnegative");
    for (const auto& f : items(at(problem, "friction")))
        if (num(f).value < 0 || num(f).value > 1) fail("Friction must be between zero and one");
    const double threshold = number(setdefault(problem, "threshold", real(0.0)), "threshold").value;
    if (threshold < 0 || (0 < threshold && threshold <= 1e-8))
        fail("Threshold must be zero or greater than the 1e-8 branch guard");
    number(setdefault(problem, "startYaw", real(0.0)), "startYaw");
    if (number(setdefault(problem, "seconds", real(10.0)), "seconds").value <= 0) fail("seconds must be positive");
    if (!is_bool(setdefault(problem, "stopOnFeasible", boolean(false)))) fail("stopOnFeasible must be a boolean");
    const Value mode = setdefault(problem, "objectiveMode", text("configured"));
    if (!is_one_of(mode, {"configured", "constant_zero"})) fail("Unsupported numerical objective mode");
    if (is_text(mode, "constant_zero") && !std::get<bool>(at(problem, "stopOnFeasible").data))
        fail("Constant-zero objective requires feasible stopping");
    integer(setdefault(problem, "maxIterations", integer_value(10000)), "maxIterations", 1, 1000000);

    const Value contacts = setdefault(problem, "contacts", array({}));
    if (!is_list(contacts) ||
        std::any_of(items(contacts).begin(), items(contacts).end(), [](const Value& c) { return !is_object(c); }))
        fail("contacts must be a list of objects");
    std::set<std::pair<std::int64_t, std::int64_t>> seen;
    for (const auto& contact_value : items(contacts)) {
        const Object& contact = fields(contact_value);
        std::int64_t state = integer(get_or_null(contact, "state"), "contact state", 1, n);
        std::int64_t axis = integer(get_or_null(contact, "axis"), "contact axis", 0, 1);
        const Value& direction = get_or_null(contact, "direction");  // `in (-1, 1)`: -1.0 passes, True does not
        if (is_bool(direction) || !(py_equal(direction, integer_value(-1)) || py_equal(direction, integer_value(1))) ||
            seen.count({state, axis}))
            fail("Invalid or duplicate contact");
        number(get_or_null(contact, "plane"), "contact plane");
        seen.insert({state, axis});
    }

    const Value walls = setdefault(problem, "walls", array({}));
    if (!is_list(walls) ||
        std::any_of(items(walls).begin(), items(walls).end(), [](const Value& w) { return !is_object(w); }))
        fail("walls must be a list of objects");
    Array checked_walls;
    for (const auto& wall_value : items(walls)) {
        Object wall = fields(wall_value);
        const Value mode_value = get_or_null(wall, "mode");
        if (!is_one_of(mode_value, {"X", "Z", "F", "DXZ", "DZX"}))
            fail("Unsupported constraint mode: " + py_str(mode_value));
        const std::string& wall_mode = as_text(mode_value);
        const std::int64_t limit = wall_mode == "F" ? n - 1 : n;
        integer(get_or_null(wall, "t1"), "constraint t1", 0, limit);
        if (const Value& t2 = get_or_null(wall, "t2"); !is_null(t2)) {
            integer(t2, "constraint t2", 0, limit);
        } else if (wall_mode == "DXZ" || wall_mode == "DZX") {
            fail(wall_mode + " needs t2");
        }
        setdefault(wall, "op", text("PLUS"));
        if (!is_one_of(at(wall, "op"), {"PLUS", "MINUS"}) || !is_one_of(get_or_null(wall, "cmp"), {"GE", "LE", "EQ"}))
            fail("Invalid constraint comparison or operation");
        number(get_or_null(wall, "rhs"), "constraint rhs");
        checked_walls.push_back(object(std::move(wall)));
    }
    put(problem, "walls", array(std::move(checked_walls)));

    const Value& objective_value = get_or_null(problem, "objective");
    if (!is_object(objective_value)) fail("Missing objective");
    Object objective = fields(objective_value);
    integer(get_or_null(objective, "tick"), "objective tick", 0, n);
    if (!is_one_of(get_or_null(objective, "axis"), {"X", "Z"}) ||
        !is_one_of(get_or_null(objective, "sense"), {"MAX", "MIN"}))
        fail("Invalid objective axis or sense");
    setdefault(objective, "type", text("POSITION"));
    setdefault(objective, "smoothLambda", real(0.0));
    if (!is_one_of(at(objective, "type"), {"POSITION", "MOTION"}) ||
        !py_equal(at(objective, "smoothLambda"), integer_value(0)))
        fail("Only position/motion objectives with zero smooth lambda are supported");
    const Value& custom = get_or_null(objective, "customYaw");
    if (!is_null(custom)) number(custom, "customYaw");
    if (is_text(at(objective, "type"), "MOTION") &&
        (is_null(get_or_null(objective, "customYaw")) || as_int(at(objective, "tick")) == 0))
        fail("A motion objective needs customYaw and a tick greater than zero");
    put(problem, "objective", object(std::move(objective)));

    if (const Value* pattern = get(problem, "inertiaPattern")) {
        if (threshold == 0 || !is_list(*pattern) || items(*pattern).size() != ticks)
            fail("A fixed inertia pattern needs one row per tick and a positive threshold");
        for (const auto& row : items(*pattern))
            if (!is_list(row) || items(row).size() != 2 ||
                std::any_of(items(row).begin(), items(row).end(), [](const Value& b) {
                    return !is_int(b) || as_int(b) < -1 || as_int(b) > 2;
                }))
                fail("Inertia branches must be -1, 0, 1, or 2 for each axis");
    }
    if (const Value* flags = get(problem, "ladderTicks"))
        booleans(flags, ticks + 1, "ladderTicks needs one boolean per state, including the final state");
    if (const Value* pattern = get(problem, "ladderPattern")) {
        if (!get(problem, "ladderTicks")) fail("ladderPattern requires ladderTicks");
        if (!is_list(*pattern) || items(*pattern).size() != ticks) fail("ladderPattern needs one row per input tick");
        const Array& ladder = items(at(problem, "ladderTicks"));
        for (std::size_t t = 0; t < ticks; ++t) {
            const Value& row = items(*pattern)[t];
            bool valid = is_list(row) && items(row).size() == 2 &&
                         std::all_of(items(row).begin(), items(row).end(), [](const Value& b) {
                             return is_int(b) && as_int(b) >= -1 && as_int(b) <= 1;
                         });
            if (valid && !std::get<bool>(ladder[t].data))
                valid = std::none_of(items(row).begin(), items(row).end(), [](const Value& b) { return as_int(b); });
            if (!valid) fail("Ladder branches must be -1, 0 or 1, and zero outside ladder ticks");
        }
    }
    if (const Value* config = get(problem, "vertical")) put(problem, "vertical", check_vertical(*config, n));
    if (const Value* movement_value = get(problem, "movement")) {
        if (!is_object(*movement_value) || !is_text(get_or_null(fields(*movement_value), "profile"), "minecraft-1.8.9") ||
            !is_text(get_or_null(fields(*movement_value), "compiler"), "mod-input-state-v1"))
            fail("Unsupported compiled movement profile");
        const Object& movement = fields(*movement_value);
        for (const auto& [key, length] : std::initializer_list<std::pair<const char*, std::size_t>>{
                 {"horizontalCollision", ticks + 1}, {"ground", ticks}, {"sprinting", ticks}, {"airSprinting", ticks}})
            booleans(get(movement, key), length, std::string("movement.") + key + " has the wrong length or type");
        for (const char* key : {"moveForward", "moveStrafe"}) {
            const Value& values = get_or_null(movement, key);
            if (!is_list(values) || items(values).size() != ticks)
                fail(std::string("movement.") + key + " needs one value per input tick");
            for (const auto& value : items(values)) number(value, std::string("movement.") + key);
        }
        std::set<std::int64_t> expected_hits;
        for (const auto& contact : items(contacts)) expected_hits.insert(as_int(at(fields(contact), "state")));
        const Array& collisions = items(at(movement, "horizontalCollision"));
        for (std::size_t t = 1; t < collisions.size(); ++t)
            if (std::get<bool>(collisions[t].data) != static_cast<bool>(expected_hits.count(static_cast<std::int64_t>(t))))
                fail("Compiled movement collision schedule disagrees with declared contacts");
        if (const Value* vertical = get(problem, "vertical")) {
            const Object& config = fields(*vertical);
            if (std::get<bool>(collisions[0].data) != std::get<bool>(at(config, "previousHorizontalCollision").data) ||
                std::get<bool>(items(at(movement, "ground"))[0].data) != std::get<bool>(at(config, "onGround").data))
                fail("Compiled movement initial state disagrees with the vertical state");
        }
    }
    Value prepared = object(std::move(problem));
    canonical_json(prepared);  // json.dumps(problem, allow_nan=False)
    return prepared;
}

std::string fingerprint(const Value& problem) {
    Object physical;
    for (const auto& [key, value] : fields(problem))
        if (key != "seconds" && key != "yaw" && key != "fallbackYaw" && key != "stopOnFeasible" &&
            key != "objectiveMode" && key != "source" && key != "maxIterations" && key != "warmYaw")
            physical.emplace_back(key, value);
    return sha256_hex(dumps(object(std::move(physical)), true, true));
}

}  // namespace pkc
