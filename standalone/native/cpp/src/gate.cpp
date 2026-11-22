// Port of InteractionGate.prepare's build. Each function names the Python it
// mirrors; results must equal Python's canonical JSON, including int/float
// kinds, so arithmetic on values that reach the output uses Num.
#include "pkc/gate.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace pkc {
namespace {

double f32(double value) { return static_cast<double>(static_cast<float>(value)); }

// physics/vertical.py, scene/route.py, scene/geometry.py, schema.py, spatial/surfaces.py
const double kHalfWidth = f32(0.6) / 2;
const double kBodyHeight = f32(1.8);
const double kStepHeight = f32(0.6);
const double kGravity = 0.08;
const double kYDrag = f32(0.98);
const double kClimbCarry = (0.2 - kGravity) * kYDrag;
const double kCap = f32(0.15);
const double kJumpVelocity = f32(.42);
constexpr double kTolerance = 1e-7;
constexpr int kTangentAxes[3][2] = {{1, 2}, {0, 2}, {0, 1}};

[[noreturn]] void fail(const std::string& message) { throw std::invalid_argument(message); }

// ---- JSON-shaped values with Python dict/list semantics -------------------

Value object(Object fields) { return {std::make_shared<const Object>(std::move(fields))}; }
Value array(Array items) { return {std::make_shared<const Array>(std::move(items))}; }
Value text(std::string value) { return {std::move(value)}; }
Value integer_value(std::int64_t value) { return {value}; }
Value real(double value) { return {value}; }
Value boolean(bool value) { return {value}; }
const Value kNull{nullptr};

const Object& fields(const Value& value) {
    auto* held = std::get_if<std::shared_ptr<const Object>>(&value.data);
    if (!held) fail("expected an object");
    return **held;
}

const Array& items(const Value& value) {
    auto* held = std::get_if<std::shared_ptr<const Array>>(&value.data);
    if (!held) fail("expected a list");
    return **held;
}

bool is_list(const Value& value) { return std::holds_alternative<std::shared_ptr<const Array>>(value.data); }
bool is_object(const Value& value) { return std::holds_alternative<std::shared_ptr<const Object>>(value.data); }
bool is_null(const Value& value) { return std::holds_alternative<std::nullptr_t>(value.data); }

const Value* get(const Object& object, std::string_view key) {
    for (const auto& [name, value] : object)
        if (name == key) return &value;
    return nullptr;
}

// dict.get(key) where an absent key reads as None.
const Value& get_or_null(const Object& object, std::string_view key) {
    const Value* value = get(object, key);
    return value ? *value : kNull;
}

const Value& at(const Object& object, std::string_view key) {
    if (const Value* value = get(object, key)) return *value;
    fail("KeyError: '" + std::string(key) + "'");
}

void put(Object& object, std::string_view key, Value value) {
    for (auto& [name, held] : object) {
        if (name == key) {
            held = std::move(value);
            return;
        }
    }
    object.emplace_back(std::string(key), std::move(value));
}

const Value& setdefault(Object& object, std::string_view key, Value value) {
    if (!get(object, key)) object.emplace_back(std::string(key), std::move(value));
    return at(object, key);
}

void erase(Object& object, std::string_view key) {
    object.erase(std::remove_if(object.begin(), object.end(), [&](const auto& field) { return field.first == key; }),
                 object.end());
}

Object with(Object object, std::string_view key, Value value) {  // dict(row, key=value)
    put(object, key, std::move(value));
    return object;
}

const std::string& as_text(const Value& value) {
    auto* held = std::get_if<std::string>(&value.data);
    if (!held) fail("expected a str");
    return *held;
}

bool numeric(const Value& value, double& out) {
    if (auto* integer = std::get_if<std::int64_t>(&value.data)) {
        out = static_cast<double>(*integer);
    } else if (auto* real_value = std::get_if<double>(&value.data)) {
        out = *real_value;
    } else if (auto* flag = std::get_if<bool>(&value.data)) {
        out = *flag;
    } else {
        return false;
    }
    return true;
}

// Python ==: numbers compare by value (1 == 1.0 == True), containers elementwise.
bool py_equal(const Array& left, const Array& right);

bool py_equal(const Value& a, const Value& b) {
    double x, y;
    if (numeric(a, x) && numeric(b, y)) return x == y;
    if (a.data.index() != b.data.index()) return false;
    if (is_null(a)) return true;
    if (auto* s = std::get_if<std::string>(&a.data)) return *s == std::get<std::string>(b.data);
    if (is_list(a)) return py_equal(items(a), items(b));
    const auto &left = fields(a), &right = fields(b);
    if (left.size() != right.size()) return false;
    for (const auto& [key, value] : left) {
        const Value* other = get(right, key);
        if (!other || !py_equal(value, *other)) return false;
    }
    return true;
}

bool py_equal(const Array& left, const Array& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (!py_equal(left[i], right[i])) return false;
    return true;
}

bool contains(const Array& values, const Value& value) {
    return std::any_of(values.begin(), values.end(), [&](const Value& item) { return py_equal(item, value); });
}

// ---- numbers with Python int/float kinds ---------------------------------

Num number(const Value& value, const std::string& name) {  // schema.number
    if (auto* integer = std::get_if<std::int64_t>(&value.data)) return {static_cast<double>(*integer), true};
    if (auto* real_value = std::get_if<double>(&value.data))
        if (std::isfinite(*real_value)) return {*real_value, false};
    fail(name + " must be a finite number");
}

std::int64_t integer(const Value& value, const std::string& name, std::int64_t lower, std::int64_t upper) {
    auto* held = std::get_if<std::int64_t>(&value.data);
    if (!held || *held < lower || *held > upper) fail(name + " is out of range");
    return *held;
}

Value value(const Num& num) {
    if (num.integral) return integer_value(static_cast<std::int64_t>(num.value));
    return real(num.value);
}

Num add(const Num& a, const Num& b) { return {a.value + b.value, a.integral && b.integral}; }
Num sub(const Num& a, const Num& b) { return {a.value - b.value, a.integral && b.integral}; }
Num mul(const Num& a, const Num& b) { return {a.value * b.value, a.integral && b.integral}; }
Num whole(std::int64_t v) { return {static_cast<double>(v), true}; }
Num flt(double v) { return {v, false}; }

// Python max(a, b) keeps a unless b is strictly larger; min likewise.
Num py_max(const Num& a, const Num& b) { return b.value > a.value ? b : a; }

// ---- scene/route.py helpers ----------------------------------------------

bool supports(const Object& block, double x, double z) {
    const auto& bounds = items(at(block, "bounds"));
    double a = number(bounds[0], "").value, b = number(bounds[1], "").value;
    double c = number(bounds[2], "").value, d = number(bounds[3], "").value;
    return x + kHalfWidth > a && x - kHalfWidth < b && z + kHalfWidth > c && z - kHalfWidth < d;
}

std::array<Num, 4> center_bounds(const Object& block) {  // landing/initial/contact kinds on bounds-only blocks
    const auto& bounds = items(at(block, "bounds"));
    Num half = flt(kHalfWidth);
    return {sub(number(bounds[0], ""), half), add(number(bounds[1], ""), half), sub(number(bounds[2], ""), half),
            add(number(bounds[3], ""), half)};
}

Object objective_spec(const Value* value) {
    Object spec;
    if (value == nullptr || is_null(*value)) {
        spec = {{"axis", text("X")}, {"sense", text("MAX")}};
    } else if (is_object(*value)) {
        spec = fields(*value);
    } else {
        fail("Objective must contain only axis and sense");
    }
    for (const auto& field : spec)
        if (field.first != "axis" && field.first != "sense") fail("Objective must contain only axis and sense");
    auto one_of = [&](const char* key, const char* a, const char* b) {
        const Value& v = get_or_null(spec, key);
        auto* s = std::get_if<std::string>(&v.data);
        return s && (*s == a || *s == b);
    };
    if (!one_of("axis", "X", "Z") || !one_of("sense", "MAX", "MIN"))
        fail("Objective requires axis X/Z and sense MAX/MIN");
    return spec;
}

// movement/units.py compile_units plus validate_ladder_events' checks for a leg
// without ladder events.
void check_units(std::int64_t ticks, const Array& units, const Object* profile) {
    static const std::map<std::string, bool> kTypes = {{"normal", false},     {"ladder_climb", true},
                                                       {"ladder_fall", true}, {"ceiling", false},
                                                       {"stepping", false}};
    std::vector<bool> ladder(ticks + 1, false);
    std::int64_t cursor = 0;
    for (const auto& unit_value : units) {
        const auto& unit = fields(unit_value);
        auto type = kTypes.find(as_text(at(unit, "type")));
        std::int64_t start = std::get<std::int64_t>(at(unit, "start").data);
        std::int64_t end = std::get<std::int64_t>(at(unit, "end").data);
        if (type == kTypes.end() || start != cursor || !(cursor < end && end <= ticks))
            fail("Movement units must partition the complete input timeline");
        if (type->second) {
            const auto& occupied = items(at(unit, "occupiedStates"));
            for (auto s = std::get<std::int64_t>(occupied[0].data); s < std::get<std::int64_t>(occupied[1].data); ++s)
                ladder.at(s) = true;
        }
        if (const Value* extra = get(unit, "additionalOccupiedStates"))
            for (const auto& s : items(*extra)) ladder.at(std::get<std::int64_t>(s.data)) = true;
        for (const char* field : {"jumpInputs", "groundedInputs"}) {
            const Value* values = get(unit, field);
            if (!values) continue;
            std::vector<std::int64_t> seen;
            for (const auto& t : items(*values)) {
                auto* tick = std::get_if<std::int64_t>(&t.data);
                if (!tick || *tick < start || *tick >= end || std::count(seen.begin(), seen.end(), *tick))
                    fail("Invalid unit input control");
                seen.push_back(*tick);
            }
        }
        if (const Value* hits = get(unit, "hitInputs"))
            for (const auto& t : items(*hits)) {
                auto tick = std::get<std::int64_t>(t.data);
                if (type->first != "ladder_climb" || tick < start || tick >= end) fail("Invalid unit climb input");
            }
        if (const Value* sneaks = get(unit, "sneakInputs"))
            for (const auto& t : items(*sneaks)) {
                auto tick = std::get<std::int64_t>(t.data);
                if (tick < start || tick >= end) fail("Invalid unit Sneak input");
            }
        cursor = end;
    }
    if (cursor != ticks) fail("Movement program is incomplete");
    if (profile) {
        for (const auto& unit_value : units) {
            const auto& unit = fields(unit_value);
            std::int64_t start = std::get<std::int64_t>(at(unit, "start").data);
            std::int64_t end = std::get<std::int64_t>(at(unit, "end").data);
            for (auto [field, mask] : {std::pair{"jumpInputs", "jump"}, std::pair{"groundedInputs", "grounded"},
                                       std::pair{"sneakInputs", "sneak"}}) {
                const Value* inputs = get(unit, field);
                const Value* flags = get(*profile, mask);
                if (!inputs || !flags) continue;
                const Array& listed = items(*inputs);
                std::size_t next = 0;
                bool same = true;
                for (std::int64_t t = start; t < end; ++t) {
                    if (!std::get<bool>(items(*flags).at(t).data)) continue;
                    same = same && next < listed.size() && py_equal(listed[next], integer_value(t));
                    ++next;
                }
                if (!same || next != listed.size()) fail("Movement unit controls disagree with scalar profile");
            }
        }
    }
    if (std::find(ladder.begin(), ladder.end(), true) != ladder.end())
        fail("Movement units disagree with ladder occupancy");
}

// movement/contact_combinations.merge_contacts on (state, axis) keys. A given
// axis stands in for every contact's own (validate_vertical_schedule merges
// dict(c, axis=1)). Values are immutable, so a contact nothing merges into is
// shared rather than deep-copied.
Array merge_contacts(const Array& contacts, bool scene, std::optional<std::int64_t> axis = std::nullopt) {
    static const char* kKeys[] = {"plane", "direction", "step", "case", "lowerHeight",
                                  "higherHeight", "clearance", "movementCase"};
    struct Entry {
        Value first;
        std::optional<Object> merged;  // first's fields once a scene union extends them
    };
    std::map<std::pair<std::int64_t, std::int64_t>, Entry> merged;
    for (const auto& contact_value : contacts) {
        const auto& contact = fields(contact_value);
        std::pair<std::int64_t, std::int64_t> key{std::get<std::int64_t>(at(contact, "state").data),
                                                  axis ? *axis : std::get<std::int64_t>(at(contact, "axis").data)};
        auto [found, inserted] = merged.try_emplace(key, Entry{contact_value, std::nullopt});
        if (inserted) continue;
        Entry& entry = found->second;
        const Object& previous = entry.merged ? *entry.merged : fields(entry.first);
        bool conflict = false;
        for (const char* k : kKeys) conflict |= !py_equal(get_or_null(previous, k), get_or_null(contact, k));
        if (!scene) conflict |= !py_equal(get_or_null(previous, "surface"), get_or_null(contact, "surface"));
        if (conflict) fail("Conflicting contacts on one input axis");
        if (scene) {
            if (!entry.merged) entry.merged = fields(entry.first);
            for (const char* field : {"boxes", "regions"}) {
                Array union_values = items(setdefault(*entry.merged, field, array({})));
                if (const Value* extra = get(contact, field))
                    for (const auto& v : items(*extra))
                        if (!contains(union_values, v)) union_values.push_back(v);
                put(*entry.merged, field, array(std::move(union_values)));
            }
        }
    }
    Array result;
    result.reserve(merged.size());
    for (auto& [key, entry] : merged) result.push_back(entry.merged ? object(std::move(*entry.merged)) : entry.first);
    return result;
}

std::array<Num, 6> box_of(const Value& value, const std::string& name) {
    const auto& box = items(value);
    if (box.size() != 6) fail("A contact needs an actual finite collision box");
    std::array<Num, 6> out;
    for (int i = 0; i < 6; ++i) out[i] = number(box[i], name);
    return out;
}

Value expanded_face(const std::array<Num, 6>& box, int axis, const Num& clearance) {
    Num half = flt(kHalfWidth);
    std::array<Num, 6> expanded = {sub(box[0], half), sub(box[1], flt(kBodyHeight)), sub(box[2], half),
                                   add(box[3], half), box[4], add(box[5], half)};
    int u = kTangentAxes[axis][0], v = kTangentAxes[axis][1];
    Num shrink_u = u == 1 ? whole(0) : clearance, shrink_v = v == 1 ? whole(0) : clearance;
    return array({value(add(expanded[u], shrink_u)), value(add(expanded[v], shrink_v)),
                  value(sub(expanded[u + 3], shrink_u)), value(sub(expanded[v + 3], shrink_v))});
}

int axis_index(const std::string& axis) { return axis == "X" ? 0 : axis == "Y" ? 1 : 2; }

// movement/contact_combinations.validate_contact_events
void validate_contact_events(Object& route) {
    Array events = items(setdefault(route, "contactEvents", array({})));
    const auto& airtimes = items(at(route, "airtimes"));
    std::vector<std::tuple<std::int64_t, std::int64_t, std::string>> keyed;  // per event, in order
    for (auto& event_value : events) {
        // Python edits the event in place; copy it only when a key changes.
        const Object* event = &fields(event_value);
        std::optional<Object> edited;
        auto edit = [&]() -> Object& {
            if (!edited) event = &edited.emplace(*event);
            return *edited;
        };
        const auto* axis_text = std::get_if<std::string>(&get_or_null(*event, "axis").data);
        if (!axis_text || (*axis_text != "X" && *axis_text != "Y" && *axis_text != "Z"))
            fail("A contact event requires an X/Y/Z face");
        std::string axis_name = *axis_text;
        std::int64_t leg = integer(get_or_null(*event, "leg"), "contact leg", 0, airtimes.size() - 1);
        std::int64_t offset = integer(get_or_null(*event, "offset"), "contact input", 0,
                                      std::get<std::int64_t>(airtimes[leg].data) - 1);
        const auto* direction_value = std::get_if<std::int64_t>(&get_or_null(*event, "direction").data);
        if (!direction_value || (*direction_value != -1 && *direction_value != 1))
            fail("Contact direction must be -1 or 1");
        std::int64_t direction = *direction_value;
        int axis = axis_index(axis_name);
        if (const Value* box = get(*event, "box")) {
            Value held = *box;
            Object& changed = edit();
            erase(changed, "box");
            Num clearance = py_max(number(get(route, "supportClearance") ? at(route, "supportClearance")
                                                                          : real(1e-6), "supportClearance"),
                                   flt(2e-7));
            put(changed, "boxes", array({held}));
            put(changed, "surface", kNull);
            put(changed, "regions", array({expanded_face(box_of(held, "contact box"), axis, clearance)}));
        }
        const Value* boxes = get(*event, "boxes");
        if (!boxes || !is_list(*boxes) || items(*boxes).empty())
            fail("A contact needs its actual finite collision boxes");
        double width = axis == 1 ? kBodyHeight : kHalfWidth;
        for (const auto& box_value : items(*boxes)) {
            if (!is_list(box_value)) fail("A contact needs an actual finite collision box");
            auto box = box_of(box_value, "contact box");
            for (int a = 0; a < 3; ++a)
                if (box[a].value >= box[a + 3].value) fail("Contact box must have positive volume");
            Num plane = direction > 0 ? sub(box[axis], flt(width))
                                      : add(box[axis + 3], axis == 1 ? whole(0) : flt(width));
            if (!py_equal(get_or_null(*event, "plane"), value(plane)))
                fail("Contact plane must match its actual finite face");
        }
        const Value* regions = get(*event, "regions");
        if (!regions || !is_list(*regions) || items(*regions).empty())
            fail("A contact needs at least one finite region");
        for (const auto& region_value : items(*regions)) {
            if (!is_list(region_value) || items(region_value).size() != 4)
                fail("A contact region must be a finite tangent rectangle");
            const auto& region = items(region_value);
            std::array<double, 4> r;
            for (int i = 0; i < 4; ++i) r[i] = number(region[i], "contact region").value;
            if (r[0] > r[2] || r[1] > r[3]) fail("A contact region must be a finite tangent rectangle");
        }
        if (!get(*event, "role")) put(edit(), "role", text("contact"));
        const std::string& role = as_text(at(*event, "role"));
        if (role != "contact" && role != "reversal" && role != "climb" && role != "ceiling" && role != "landing" &&
            role != "step")
            fail("Unsupported contact role");
        if (((role == "reversal" || role == "climb") && axis == 1) ||
            ((role == "ceiling" || role == "landing" || role == "step") && axis != 1))
            fail("Contact role does not match its axis");
        if ((role == "ceiling" && direction != 1) || ((role == "landing" || role == "step") && direction != -1))
            fail("Contact role does not match its direction");
        keyed.emplace_back(offset + 1, leg, std::move(axis_name));
        if (edited) event_value = object(std::move(*edited));
    }
    if (!events.empty() && !get(route, "sharedMovement"))
        fail("Contact combinations require shared movement compilation");
    // merge_contacts with axis=(leg, axis): only its conflict check matters here.
    static const char* kKeys[] = {"plane", "direction", "step", "case", "lowerHeight",
                                  "higherHeight", "clearance", "movementCase", "surface"};
    for (std::size_t i = 0; i < keyed.size(); ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (keyed[i] == keyed[j]) {
                for (const char* k : kKeys)
                    if (!py_equal(get_or_null(fields(events[j]), k), get_or_null(fields(events[i]), k)))
                        fail("Conflicting contacts on one input axis");
                break;  // the first event with this key is the one merged into
            }
    put(route, "contactEvents", array(std::move(events)));
}

// scene/route.prepare_route on the route compile_block_path builds: one
// movement leg on two bounds-only blocks with no entries, exits, constraints,
// passages, wall hits or ladder events (all constructed empty).
Object prepare_route(Object route) {
    put(route, "objective", object(objective_spec(get(route, "objective"))));
    if (const Value* required = get(route, "terminalSupportRequired"))
        if (!std::holds_alternative<bool>(required->data)) fail("terminalSupportRequired must be boolean");
    if (number(get(route, "physicsThreshold") ? at(route, "physicsThreshold") : real(.005), "physicsThreshold")
            .value < 0)
        fail("physicsThreshold must be nonnegative");
    const auto& blocks = items(at(route, "blocks"));
    for (const auto& block_value : blocks) {
        const auto& block = fields(block_value);
        const auto& bounds = items(at(block, "bounds"));
        if (bounds.size() != 4) fail("Block bounds must be [minX, maxX, minZ, maxZ]");
        for (const auto& v : bounds) number(v, "block bound");
        if (number(bounds[0], "").value >= number(bounds[1], "").value ||
            number(bounds[2], "").value >= number(bounds[3], "").value)
            fail("Block bounds are empty");
        number(get_or_null(block, "top"), "block top");
    }
    const auto& airtimes = items(at(route, "airtimes"));
    for (const auto& airtime : airtimes) integer(airtime, "airtime", 1, 200);  // kind 'movement'
    for (const auto& phase : items(at(route, "landingPhases"))) {
        const auto* s = std::get_if<std::string>(&phase.data);
        if (!s || (*s != "pre" && *s != "post")) fail("Landing phases must be pre or post");
    }
    Object initial = fields(at(route, "initial"));
    for (const char* key : {"position", "velocity"}) {
        const Value& values = get_or_null(initial, key);
        if (!is_list(values) || items(values).size() != 3)
            fail(std::string("Initial ") + key + " must be [X,Y,Z]");
        for (const auto& v : items(values)) number(v, std::string("initial ") + key);
    }
    number(setdefault(initial, "yaw", real(0.0)), "initial yaw");
    const auto& position = items(at(initial, "position"));
    const auto& start = fields(blocks[0]);
    if (std::abs(number(position[1], "").value - number(at(start, "top"), "").value) > 1e-7)
        fail("The initial feet height must equal the start block top");
    if (!supports(start, number(position[0], "").value, number(position[2], "").value))
        fail("The initial player must overlap the start block");
    put(route, "initial", object(std::move(initial)));
    Object inputs = fields(setdefault(route, "inputs", object({})));
    for (auto [key, fallback] : {std::pair{"forward", f32(0.98)}, std::pair{"strafe", 0.0}})
        if (std::abs(number(setdefault(inputs, key, real(fallback)), key).value) > 1)
            fail("Movement inputs must be at the scaled moveFlying range");
    for (auto [key, fallback] :
         {std::pair{"sprint", true}, std::pair{"incomingSprint", false}, std::pair{"force45", false}})
        if (!std::holds_alternative<bool>(setdefault(inputs, key, boolean(fallback)).data))
            fail(std::string(key) + " must be boolean");
    if (std::abs(number(at(inputs, "forward"), "").value - f32(0.98)) > 1e-7 ||
        number(at(inputs, "strafe"), "").value != 0)
        fail("The first route family uses forward movement without strafe");
    put(route, "inputs", object(std::move(inputs)));
    integer(setdefault(route, "maxTotalRunTicks", integer_value(40)), "maxTotalRunTicks", 0, 1000);
    for (const auto& bounds_value : items(at(route, "runBounds"))) {
        const auto& bounds = items(bounds_value);
        if (bounds.size() != 2) fail("Run bounds must be [min,max]");
        std::int64_t lo = integer(bounds[0], "minimum run ticks", 0, 1000);
        integer(bounds[1], "maximum run ticks", lo, 1000);
    }
    if (number(setdefault(route, "supportClearance", real(1e-6)), "supportClearance").value < 0)
        fail("Support clearance must be nonnegative");
    setdefault(route, "constraints", array({}));
    erase(route, "passageCaps");                   // validate_passages
    setdefault(route, "passages", array({}));
    setdefault(route, "hitEvents", array({}));     // validate_hits
    setdefault(route, "ceilingEvents", array({}));
    setdefault(route, "ladderEvents", array({}));  // validate_ladder_events
    std::int64_t ticks = std::get<std::int64_t>(airtimes[0].data);
    const Value* profiles = get(route, "verticalProfiles");
    Array units;
    const Object* profile = nullptr;
    if (profiles && !items(*profiles).empty()) {
        profile = &fields(items(*profiles)[0]);
        if (as_text(at(*profile, "composition")) != "movement_program")
            fail("native gate preparation supports movement_program profiles only");
        units = items(at(*profile, "generatedUnits"));
    } else {
        // movement_program(ticks, []) for a leg without a profile: one normal unit.
        units.push_back(object({{"type", text("normal")}, {"start", integer_value(0)}, {"end", integer_value(ticks)}}));
    }
    check_units(ticks, units, profile);
    put(route, "movementUnits", array({array(units)}));
    validate_contact_events(route);
    return route;
}

// movement/contact_combinations: finite_rows(axis, bounding_region(regions), t1=t1)
void finite_rows(Array& rows, int axis, const Array& regions, std::int64_t t1) {
    auto pick = [&](int k, bool smallest) {
        Num best = number(items(regions[0])[k], "");
        for (std::size_t i = 1; i < regions.size(); ++i) {
            Num candidate = number(items(regions[i])[k], "");
            if (smallest ? candidate.value < best.value : candidate.value > best.value) best = candidate;
        }
        return best;
    };
    std::array<Num, 4> region = {pick(0, true), pick(1, true), pick(2, false), pick(3, false)};
    for (int k = 0; k < 2; ++k) {
        int a = kTangentAxes[axis][k];
        if (a == 1) continue;
        for (const char* cmp : {"GE", "LE"}) {
            bool le = std::string_view(cmp) == "LE";
            rows.push_back(object({{"mode", text(std::string(1, "XYZ"[a]))},
                                   {"cmp", text(cmp)},
                                   {"rhs", value(region[k + 2 * le])},
                                   {"name", text("contact:region")},
                                   {"t1", integer_value(t1)}}));
        }
    }
}

struct Schedule {
    Object plan;
    std::vector<bool> ladder;  // plan.get('ladderTicks', [False]*(n+1))
};

// scene/route.schedule(route, [0]) for the prepared route.
Schedule schedule(const Object& route) {
    const auto& run_bounds = items(at(route, "runBounds"));
    const auto& first_bounds = items(run_bounds[0]);
    integer(integer_value(0), "run ticks", std::get<std::int64_t>(first_bounds[0].data),
            std::get<std::int64_t>(first_bounds[1].data));
    const auto& profile = fields(items(at(route, "verticalProfiles"))[0]);
    std::int64_t airtime = std::get<std::int64_t>(items(at(route, "airtimes"))[0].data);
    const auto& profile_grounded = items(at(profile, "grounded"));
    Array grounded(profile_grounded.begin(), profile_grounded.end() - 1);
    Array jump = items(at(profile, "jump"));
    std::int64_t state = airtime;
    std::vector<std::tuple<std::int64_t, std::int64_t, std::string>> supports_at = {{0, 0, "initial"}};
    const Value* required = get(route, "terminalSupportRequired");
    if (!required || std::get<bool>(required->data)) {
        bool post = as_text(items(at(route, "landingPhases"))[0]) == "post";
        supports_at.emplace_back(post ? state : state - 1, 1, "landing");
    }
    Object event = {{"leg", integer_value(0)},  {"landed", integer_value(0)},  {"jump", integer_value(0)},
                    {"landing", integer_value(state)}, {"runTicks", integer_value(0)}, {"kind", text("movement")}};
    if (0 > std::get<std::int64_t>(at(route, "maxTotalRunTicks").data))
        fail("Allocation exceeds the total run-tick limit");
    Array walls;
    Num clearance = number(at(route, "supportClearance"), "");
    auto sorted_supports = supports_at;
    std::sort(sorted_supports.begin(), sorted_supports.end());
    sorted_supports.erase(std::unique(sorted_supports.begin(), sorted_supports.end()), sorted_supports.end());
    const auto& blocks = items(at(route, "blocks"));
    for (const auto& [tick, index, kind] : sorted_supports) {
        auto bounds = center_bounds(fields(blocks.at(index)));
        for (int a = 0; a < 2; ++a) {
            const char* axis = a == 0 ? "X" : "Z";
            Num lower = add(flt(std::nextafter(bounds[2 * a].value, INFINITY)), clearance);
            Num upper = sub(flt(std::nextafter(bounds[2 * a + 1].value, -INFINITY)), clearance);
            std::string name = kind + ":" + std::to_string(index) + ":" + axis;
            walls.push_back(object({{"mode", text(axis)}, {"t1", integer_value(tick)}, {"cmp", text("GE")},
                                    {"rhs", value(lower)}, {"name", text(name + ":lo")}}));
            walls.push_back(object({{"mode", text(axis)}, {"t1", integer_value(tick)}, {"cmp", text("LE")},
                                    {"rhs", value(upper)}, {"name", text(name + ":hi")}}));
        }
    }
    // compile_passages and compile_hits add nothing: the route has no passages, wall hits or ceilings.
    Array scene_contacts;
    std::vector<bool> ladder;
    std::vector<bool> sneak;
    const auto& route_events = items(at(route, "contactEvents"));
    // dict(row, t1=offset + row['t1']) with offset 0 and an int t1 equals the row itself.
    const auto& position_rows = items(at(profile, "positionConstraints"));
    walls.insert(walls.end(), position_rows.begin(), position_rows.end());
    for (const auto& case_value : items(at(profile, "caseEvents"))) {
        const auto& c = fields(case_value);
        const std::string& kind = as_text(at(c, "case"));
        std::int64_t input = std::get<std::int64_t>(at(c, "input").data);
        if (kind == "normal_step" || kind == "slab_boost") {
            bool owned = std::any_of(route_events.begin(), route_events.end(), [&](const Value& e_value) {
                const auto& e = fields(e_value);
                if (std::get<std::int64_t>(at(e, "leg").data) != 0 || !py_equal(get_or_null(e, "role"), text("step")) ||
                    std::get<std::int64_t>(at(e, "offset").data) != input || !py_equal(at(e, "plane"), at(c, "plane")))
                    return false;
                const auto& event_boxes = items(at(e, "boxes"));
                for (const auto& b : items(at(c, "boxes")))
                    if (contains(event_boxes, b)) return true;
                return false;
            });
            if (owned) continue;
        }
        scene_contacts.push_back(object({{"state", integer_value(input + 1)}, {"axis", integer_value(1)},
                                         {"direction", integer_value(-1)}, {"plane", at(c, "plane")},
                                         {"boxes", at(c, "boxes")}, {"movementCase", text(kind)}}));
    }
    const auto& occupied = items(at(profile, "ladderTicks"));
    const auto& selected_sneak = items(at(profile, "sneak"));
    if (static_cast<std::int64_t>(occupied.size()) != airtime + 1 ||
        static_cast<std::int64_t>(selected_sneak.size()) != airtime)
        fail("Profile medium/input arrays have inconsistent lengths");
    auto any_true = [](const Array& flags) {
        return std::any_of(flags.begin(), flags.end(), [](const Value& v) { return std::get<bool>(v.data); });
    };
    if (any_true(occupied) || any_true(selected_sneak)) {
        ladder.assign(state + 1, false);
        sneak.assign(state, false);
        for (std::size_t t = 0; t < occupied.size(); ++t) ladder[t] = ladder[t] || std::get<bool>(occupied[t].data);
        for (std::size_t t = 0; t < selected_sneak.size(); ++t)
            sneak[t] = sneak[t] || std::get<bool>(selected_sneak[t].data);
    }
    // compile_contact_events
    Array horizontal, vertical;
    Num margin = py_max(clearance, flt(2e-7));
    for (const auto& e_value : route_events) {
        const auto& e = fields(e_value);
        std::int64_t offset = std::get<std::int64_t>(at(e, "offset").data);
        std::int64_t tick = offset;  // events[leg]['jump'] is 0
        const std::string& axis_text = as_text(at(e, "axis"));
        int axis = axis_index(axis_text);
        std::int64_t direction = std::get<std::int64_t>(at(e, "direction").data);
        bool step = as_text(at(e, "role")) == "step";
        Object scene_contact = {{"state", integer_value(tick + 1)}, {"axis", integer_value(axis)},
                                {"plane", at(e, "plane")},          {"direction", integer_value(direction)},
                                {"boxes", at(e, "boxes")},          {"regions", at(e, "regions")}};
        if (step) scene_contact.emplace_back("step", boolean(true));
        scene_contacts.push_back(object(std::move(scene_contact)));
        if (axis == 1) {  // tagged axis=1 for verticalContacts below
            Object v = {{"state", integer_value(tick + 1)}, {"plane", at(e, "plane")},
                        {"direction", integer_value(direction)}, {"axis", integer_value(1)}};
            if (step) v.emplace_back("step", boolean(true));
            vertical.push_back(object(std::move(v)));
        } else {
            horizontal.push_back(object({{"state", integer_value(tick + 1)}, {"axis", integer_value(axis / 2)},
                                         {"plane", at(e, "plane")}, {"direction", integer_value(direction)}}));
        }
        std::int64_t region_offset = offset + (axis_text == "Z" || (axis_text == "Y" && step));
        finite_rows(walls, axis, items(at(e, "regions")), region_offset);
        if (as_text(at(e, "role")) == "reversal" && tick + 2 <= state) {
            Num rhs = sub(number(at(e, "plane"), ""), mul(whole(direction), margin));
            walls.push_back(object({{"mode", text(axis_text)}, {"t1", integer_value(tick + 2)},
                                    {"cmp", text(direction > 0 ? "LE" : "GE")}, {"rhs", value(rhs)},
                                    {"name", text("contact:reverse")}}));
        }
    }
    Schedule result;
    Object& plan = result.plan;
    put(plan, "ticks", integer_value(state));
    put(plan, "grounded", array(std::move(grounded)));
    put(plan, "jump", array(std::move(jump)));
    put(plan, "events", array({object(std::move(event))}));
    Array checks;
    for (const auto& [tick, index, kind] : supports_at)
        checks.push_back(array({integer_value(tick), integer_value(index), text(kind)}));
    put(plan, "supportChecks", array(std::move(checks)));
    put(plan, "exitChecks", array({}));
    put(plan, "walls", array(std::move(walls)));
    put(plan, "allocation", array({integer_value(0)}));
    put(plan, "stage", text("jump"));
    put(plan, "passageFailures", array({}));
    put(plan, "contacts", array(merge_contacts(horizontal, false)));
    put(plan, "sceneContacts", array(merge_contacts(scene_contacts, true)));
    if (!ladder.empty()) {
        Array l, s;
        for (bool v : ladder) l.push_back(boolean(v));
        for (bool v : sneak) s.push_back(boolean(v));
        put(plan, "ladderTicks", array(std::move(l)));
        put(plan, "sneak", array(std::move(s)));
        result.ladder = ladder;
    } else {
        result.ladder.assign(state + 1, false);
    }
    if (!vertical.empty()) put(plan, "verticalContacts", array(merge_contacts(vertical, false)));
    put(plan, "movementUnits", items(at(route, "movementUnits"))[0]);  // shifted_unit(unit, 0) leaves units equal
    put(plan, "movementComposition", boolean(true));
    return result;
}

struct VerticalContactRow {
    std::int64_t state, direction;
    Num plane;
    bool step = false;
    std::optional<std::string> movement_case;
    std::optional<Num> lower_height, higher_height, clearance;
};

// physics/vertical.py vertical_step for one tick.
struct StepResult {
    Num proposed, carry;
    bool ground;
    bool failed;
};

StepResult vertical_step(const std::vector<VerticalContactRow>& contacts, const std::vector<bool>& jumps,
                         const std::vector<bool>& sneak, const Num& threshold, std::int64_t tick, const Num& position,
                         const Num& velocity, bool on_ladder, bool grounded) {
    Num movement = std::abs(velocity.value) < threshold.value ? flt(0.0) : velocity;
    bool jumping = jumps[tick];
    if (jumping) movement = flt(kJumpVelocity);
    if (on_ladder) {
        if (-0.15 > movement.value) movement = flt(-0.15);
        if (sneak[tick] && movement.value < 0) movement = flt(0.0);
    }
    Num proposed = add(position, movement), carry = movement;
    bool ground = false, failed = jumping && !grounded;
    const double tolerance = kTolerance;
    for (const auto& contact : contacts) {
        if (contact.state != tick + 1) continue;
        const Num& plane = contact.plane;
        if (contact.movement_case) {
            const std::string& kind = *contact.movement_case;
            double delta = sub(plane, position).value;
            double lower = contact.lower_height ? contact.lower_height->value : position.value;
            double upper = contact.higher_height ? contact.higher_height->value : plane.value;
            double m = movement.value, y = position.value;
            bool valid = false;
            if (kind == "normal_step") {
                valid = grounded && !jumping && 0 < delta && delta <= kStepHeight + tolerance;
            } else if (kind == "slab_boost") {
                valid = grounded && jumping && m < delta && delta <= kStepHeight + tolerance;
            } else if (kind == "blip" || kind == "blip_up" || kind == "corner_blip_up") {
                valid = !grounded && m < 0 && y >= lower && proposed.value < lower;
                if (kind == "blip") {
                    valid = valid && 0 <= delta && delta <= kStepHeight + tolerance;
                } else {
                    valid = valid && plane.value == y;
                    double gap = upper - y;
                    valid = valid && (kind == "blip_up" ? y > upper : 0 <= gap && gap <= kStepHeight + tolerance);
                }
            } else if (kind == "corner_jump_cancel") {
                double gap = upper - y;
                valid = grounded && jumping && kJumpVelocity < gap && gap <= kStepHeight + tolerance &&
                        plane.value == y;
            } else if (kind == "grind") {
                double clearance = contact.clearance ? contact.clearance->value : -1;
                valid = grounded && jumping && 0 <= clearance && clearance < m && plane.value == y;
            }
            if (valid) {
                proposed = plane;
                carry = flt(0.0);
                ground = kind == "normal_step" || kind == "blip" || kind == "blip_up" || kind == "corner_blip_up";
            } else {
                failed = true;
            }
        } else if (contact.step) {
            double rise = sub(plane, position).value;
            bool valid = (grounded || movement.value < 0) && movement.value < rise && 0 < rise &&
                         rise <= kStepHeight + tolerance;
            if (valid) {
                proposed = plane;
                carry = flt(0.0);
                ground = movement.value < 0;
            } else {
                failed = true;
            }
        } else {
            double direction = static_cast<double>(contact.direction);
            double approach = direction * sub(position, plane).value;
            double reach = direction * sub(proposed, plane).value;
            if (approach > tolerance || reach < 0) {
                failed = true;
            } else {
                proposed = plane;
                carry = flt(0.0);
                ground = direction < 0 && movement.value < 0;
            }
        }
    }
    if (!std::isfinite(proposed.value) || !std::isfinite(carry.value)) fail("Non-finite vertical replay");
    return {proposed, carry, ground, failed};
}

std::optional<Num> optional_number(const Object& object, std::string_view key, const std::string& name) {
    if (const Value* v = get(object, key)) return number(*v, name);
    return std::nullopt;
}

// validation/vertical.validate_vertical_schedule (with numerics.prepare's
// checks and validation.numerical._replay for its synthetic problem).
Object validate_vertical_schedule(const Object& route, const Object& profile, const Schedule& schedule) {
    const Object& plan = schedule.plan;
    std::int64_t n = std::get<std::int64_t>(at(plan, "ticks").data);
    Array contacts = items(at(profile, "contacts"));  // event jump is 0, so states are unchanged
    if (const Value* extra = get(plan, "verticalContacts"))
        contacts.insert(contacts.end(), items(*extra).begin(), items(*extra).end());
    Array merged = merge_contacts(contacts, false, 1);
    const auto& initial = fields(at(route, "initial"));
    std::vector<bool> jumps;
    Array jump_velocity;
    for (const auto& j : items(at(plan, "jump"))) {
        jumps.push_back(std::get<bool>(j.data));
        jump_velocity.push_back(jumps.back() ? real(kJumpVelocity) : kNull);
    }
    std::vector<bool> sneak(n, false);
    Array sneak_values;
    if (const Value* s = get(plan, "sneak")) {
        sneak_values = items(*s);
        for (std::int64_t t = 0; t < n; ++t) sneak[t] = std::get<bool>(sneak_values[t].data);
    } else {
        for (std::int64_t t = 0; t < n; ++t) sneak_values.push_back(boolean(false));
    }
    Array vertical_contacts;
    std::vector<VerticalContactRow> rows;
    std::vector<std::int64_t> seen_states;
    static const char* kCases[] = {"normal_step", "slab_boost", "blip", "blip_up",
                                   "corner_jump_cancel", "corner_blip_up", "grind"};
    for (const auto& c_value : merged) {
        const Object& c = fields(c_value);
        // numerics.prepare's vertical contact checks
        std::int64_t state = integer(get_or_null(c, "state"), "vertical contact state", 1, n);
        Num plane = number(get_or_null(c, "plane"), "vertical contact plane");
        const auto* direction = std::get_if<std::int64_t>(&get_or_null(c, "direction").data);
        if (!direction || (*direction != -1 && *direction != 1) ||
            std::count(seen_states.begin(), seen_states.end(), state))
            fail("Invalid or duplicate vertical contact");
        const Value& step_value = get(c, "step") ? at(c, "step") : boolean(false);
        if (!std::holds_alternative<bool>(step_value.data) || (std::get<bool>(step_value.data) && *direction != -1))
            fail("A prescribed step must be a lower contact");
        bool step = std::get<bool>(step_value.data);
        VerticalContactRow row{state, *direction, plane, step, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
        const Value& case_value = get_or_null(c, "case");
        bool has_case = !is_null(case_value) && !(std::holds_alternative<std::string>(case_value.data) &&
                                                   as_text(case_value).empty());
        if (has_case) {
            if (*direction != -1 || step) fail("A movement case requires its own lower contact");
            const auto* kind = std::get_if<std::string>(&case_value.data);
            if (!kind || std::none_of(std::begin(kCases), std::end(kCases), [&](const char* k) { return *kind == k; }))
                fail("Unsupported vertical movement case");
            row.movement_case = *kind;
            row.lower_height = optional_number(c, "lowerHeight", "vertical case lowerHeight");
            row.higher_height = optional_number(c, "higherHeight", "vertical case higherHeight");
            row.clearance = optional_number(c, "clearance", "vertical case clearance");
        }
        seen_states.push_back(state);
        rows.push_back(row);
        if (get(c, "axis")) {  // the plan's verticalContacts carry axis; the vertical problem drops it
            Object untagged = c;
            erase(untagged, "axis");
            vertical_contacts.push_back(object(std::move(untagged)));
        } else {
            vertical_contacts.push_back(c_value);
        }
    }
    const Value& threshold_value = get(route, "physicsThreshold") ? at(route, "physicsThreshold") : real(.005);
    Num threshold = number(threshold_value, "vertical.threshold");
    if (threshold.value < 0) fail("vertical.threshold must be nonnegative");
    const Value& y0 = items(at(initial, "position"))[1];
    const Value& vy0 = items(at(initial, "velocity"))[1];
    Object vertical = {{"profile", text("minecraft-1.8.9")}, {"position", y0},
                       {"velocity", vy0},                    {"onGround", boolean(true)},
                       {"previousHorizontalCollision", boolean(false)},
                       {"threshold", threshold_value},       {"jumpVelocity", array(std::move(jump_velocity))},
                       {"sneak", array(std::move(sneak_values))},
                       {"contacts", array(std::move(vertical_contacts))}};
    // Synthetic horizontal replay: x advances .1 per tick, .05 into each hit state,
    // with zero friction and a zero inertia threshold.
    std::vector<bool> hit_states(n + 2, false);
    for (const auto& c : items(at(plan, "contacts")))
        hit_states.at(std::get<std::int64_t>(at(fields(c), "state").data)) = true;
    std::vector<double> planes(n + 2, NAN);
    double x = 0.;
    for (std::int64_t t = 0; t < n; ++t) {
        x += hit_states[t + 1] ? .05 : .1;
        if (hit_states[t + 1]) planes[t + 1] = x;
    }
    const std::vector<bool>& ladder = schedule.ladder;
    double controls[2] = {.1 * std::cos(0.0), .1 * std::sin(0.0)};
    double positions[2] = {0., 0.}, velocities[2] = {0., 0.};
    bool missed_or_wrong = false;
    std::vector<bool> previous_hits = {false};
    std::vector<Num> y = {number(y0, "vertical.position")}, vy = {number(vy0, "vertical.velocity")};
    std::vector<bool> grounded = {true};
    bool vertical_failed = false;
    for (std::int64_t t = 0; t < n; ++t) {
        bool hit = false;
        double next_positions[2], next_velocities[2];
        for (int axis = 0; axis < 2; ++axis) {
            double displacement = velocities[axis] + controls[axis];
            if (ladder[t]) displacement = std::max(-kCap, std::min(kCap, displacement));
            double proposed = positions[axis] + displacement;
            double carry = 0. * displacement;
            if (axis == 0 && hit_states[t + 1]) {
                double approach = positions[axis] - planes[t + 1];
                double reach = proposed - planes[t + 1];
                if (approach > kTolerance || reach < 0) missed_or_wrong = true;
                if (approach <= kTolerance && reach >= 0) {
                    hit = hit || reach > 0;
                    proposed = planes[t + 1];
                    carry = 0.;
                }
            }
            if (!std::isfinite(proposed)) fail("replayed position must be a finite number");
            next_positions[axis] = proposed;
            next_velocities[axis] = carry;
        }
        std::copy(next_positions, next_positions + 2, positions);
        std::copy(next_velocities, next_velocities + 2, velocities);
        previous_hits.push_back(hit);
        StepResult step = vertical_step(rows, jumps, sneak, threshold, t, y[t], vy[t], ladder[t], grounded[t]);
        y.push_back(step.proposed);
        double carry = ladder[t + 1] && previous_hits[t + 1] ? kClimbCarry : (step.carry.value - kGravity) * kYDrag;
        vy.push_back(flt(carry));
        grounded.push_back(step.ground);
        vertical_failed |= step.failed;
    }
    bool checked_feasible = !missed_or_wrong && !vertical_failed;
    const auto& profile_grounded = items(at(profile, "grounded"));
    bool terminal_ground = std::get<bool>(profile_grounded.back().data);
    Array y_values, vy_values, ground_values;
    for (const auto& v : y) y_values.push_back(value(v));
    for (const auto& v : vy) vy_values.push_back(value(v));
    for (bool g : grounded) ground_values.push_back(boolean(g));
    bool matched = py_equal(y_values, items(at(profile, "position"))) &&
                   py_equal(vy_values, items(at(profile, "velocity")));
    const auto& plan_grounded = items(at(plan, "grounded"));  // expected: plan grounded + [terminal]
    bool ground_matched = ground_values.size() == plan_grounded.size() + 1 &&
                          py_equal(ground_values.back(), boolean(terminal_ground));
    for (std::size_t t = 0; ground_matched && t < plan_grounded.size(); ++t)
        ground_matched = py_equal(ground_values[t], plan_grounded[t]);
    bool feasible = matched && checked_feasible && ground_matched;
    return {{"feasible", boolean(feasible)},
            {"vertical", object(std::move(vertical))},
            {"y", array(std::move(y_values))},
            {"vy", array(std::move(vy_values))},
            {"onGround", array(std::move(ground_values))}};
}

}  // namespace

std::optional<Value> gate_prepare(const Value& scene_value, const Program& program, bool require_complete) {
    const Object& scene = fields(scene_value);
    // movement/program.compile_program
    if (!program.profile.airtime) fail("An empty prefix is not a complete movement route");
    const std::string& goal = as_text(at(scene, "goalBlock"));
    if (require_complete && (!program.profile.grounded.back() || !program.terminal_support.value ||
                             *program.terminal_support.value != goal))
        fail("An open prefix is not a completed goal route");
    if (!program.ladder_events.empty()) fail("native gate preparation does not support ladder events");
    if (as_text(at(scene, "sceneModel")) != "collision_boxes")
        fail("native gate preparation supports collision_boxes scenes only");
    // movement/profiles.compile_block_path(scene, [start, goal], [edge])
    const std::string& start_id = as_text(at(scene, "startBlock"));
    Array blocks;
    std::size_t index = 0;
    for (const std::string* id : {&start_id, &goal}) {
        const Object* found = nullptr;  // {b['id']: b} keeps the last block with an id
        for (const auto& block : items(at(scene, "blocks")))
            if (py_equal(at(fields(block), "id"), text(*id))) found = &fields(block);
        if (!found) fail("Unknown support in candidate path");
        blocks.push_back(object({{"id", text(*id + "@" + std::to_string(index++))},
                                 {"bounds", at(*found, "bounds")},
                                 {"top", at(*found, "top")}}));
    }
    Object profile = fields(to_value(program.profile));
    put(profile, "sourceBlock", at(fields(blocks[0]), "id"));
    put(profile, "targetBlock", at(fields(blocks[1]), "id"));
    put(profile, "profileKind", text("movement"));
    Object route = {{"version", integer_value(1)},
                    {"initial", at(scene, "initial")},
                    {"blocks", array(blocks)},
                    {"inputs", at(scene, "inputs")},
                    {"objective", at(scene, "objective")},
                    {"airtimes", array({integer_value(program.profile.airtime)})},
                    {"kinds", array({text("movement")})},
                    {"landingPhases", array({text(program.profile.landing_phase)})},
                    {"contactEntries", array({kNull})},
                    {"walkOffExits", array({kNull})},
                    {"maxTotalRunTicks", at(scene, "maxTotalRunTicks")},
                    {"runBounds", array({array({integer_value(0), at(scene, "maxRunTicksPerJump")})})},
                    {"supportClearance", at(scene, "supportClearance")}};
    route = prepare_route(std::move(route));
    put(route, "profileDerivation",
        object({{"kind", text("selected_height_profiles")}, {"suppliedAirtimes", boolean(false)},
                {"verticalModel", text("parkour_solver.physics.vertical")}, {"solidSideFaces", boolean(false)}}));
    put(route, "sharedMovement", boolean(true));
    put(route, "ceilingEvents", array({}));
    put(route, "ladderEvents", array({}));
    put(route, "verticalProfiles", array({object(profile)}));
    Array contact_events;
    for (const auto& event : items(at(profile, "contactEvents")))
        contact_events.push_back(object(with(fields(event), "leg", integer_value(0))));
    put(route, "contactEvents", array(std::move(contact_events)));
    // compile_program's edits to the compiled route
    put(route, "maxTotalRunTicks", integer_value(0));
    put(route, "runBounds", array({array({integer_value(0), integer_value(0)})}));
    if (!require_complete) put(route, "terminalSupportRequired", boolean(false));
    if (program.threshold) put(route, "physicsThreshold", value(*program.threshold));
    if (program.guide_seed_yaw) {
        Array yaw;
        for (const auto& v : *program.guide_seed_yaw) yaw.push_back(value(v));
        put(route, "warmYaw", array(std::move(yaw)));
    }
    // InteractionGate.prepare's build
    route = prepare_route(std::move(route));
    Schedule plan = schedule(route);
    Object vertical = validate_vertical_schedule(route, profile, plan);
    if (!std::get<bool>(at(vertical, "feasible").data)) return std::nullopt;
    Object plan_vertical = fields(at(vertical, "vertical"));
    put(plan_vertical, "referencePosition", at(vertical, "y"));
    put(plan_vertical, "referenceVelocity", at(vertical, "vy"));
    put(plan.plan, "vertical", object(std::move(plan_vertical)));
    return object({{"route", object(std::move(route))},
                   {"profiles", array({object(std::move(profile))})},
                   {"plan", object(std::move(plan.plan))},
                   {"vertical", object(std::move(vertical))}});
}

}  // namespace pkc
