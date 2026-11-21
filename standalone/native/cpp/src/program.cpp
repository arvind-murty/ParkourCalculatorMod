#include "pkc/program.hpp"

#include <cmath>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace pkc {
namespace {

// Largest magnitude where a double holds every integer exactly.
constexpr std::int64_t kExactInteger = std::int64_t{1} << 53;

// Where a decode is in the input, rendered only when it fails: building the
// string for every field cost more than the decode itself.
struct Path {
    const Path* parent = nullptr;
    std::string_view key;
    std::size_t index = 0;
    bool is_index = false;

    std::string str() const {
        if (!parent) return std::string(key);
        return parent->str() + (is_index ? "[" + std::to_string(index) + "]" : "." + std::string(key));
    }
};

Path child(const Path& parent, std::string_view key) { return {&parent, key}; }
Path element(const Path& parent, std::size_t index) { return {&parent, {}, index, true}; }

[[noreturn]] void fail(const Path& path, const std::string& problem) {
    throw std::invalid_argument(path.str() + ": " + problem);
}

template <class T>
const T& as(const Value& value, const Path& path, const char* expected) {
    if (auto* held = std::get_if<T>(&value.data)) return *held;
    fail(path, std::string("expected ") + expected);
}

// Each record lists its fields once in describe(); Reader and Writer walk that
// list to decode and encode, so the two directions cannot disagree.
template <class T>
struct IsRecord : std::false_type {};
#define PKC_RECORD(T) \
    template <>       \
    struct IsRecord<T> : std::true_type {}
PKC_RECORD(ScalarState);
PKC_RECORD(PositionConstraint);
PKC_RECORD(LegConstraint);
PKC_RECORD(VerticalContact);
PKC_RECORD(ContactEvent);
PKC_RECORD(CaseEvent);
PKC_RECORD(MovementUnit);
PKC_RECORD(RegionChoice);
PKC_RECORD(WalkOff);
PKC_RECORD(Profile);
PKC_RECORD(TraceResume);
PKC_RECORD(VerticalTrace);
PKC_RECORD(Trace);
PKC_RECORD(Program);
PKC_RECORD(MovementChoice);
#undef PKC_RECORD

// Declared up front because records and containers nest in each other.
template <class T>
std::enable_if_t<IsRecord<T>::value> decode(const Value& value, T& out, const Path& path);
template <class T>
void decode(const Value& value, Nullable<T>& out, const Path& path);
template <class T>
void decode(const Value& value, std::vector<T>& out, const Path& path);
template <class T, std::size_t N>
void decode(const Value& value, std::array<T, N>& out, const Path& path);
template <class T>
std::enable_if_t<IsRecord<T>::value, Value> encode(const T& record);
template <class T>
Value encode(const Nullable<T>& value);
template <class T>
Value encode(const std::vector<T>& values);
template <class T, std::size_t N>
Value encode(const std::array<T, N>& values);

void decode(const Value& value, bool& out, const Path& path) { out = as<bool>(value, path, "bool"); }

void decode(const Value& value, std::int64_t& out, const Path& path) {
    out = as<std::int64_t>(value, path, "int");
}

void decode(const Value& value, Num& out, const Path& path) {
    if (auto* integer = std::get_if<std::int64_t>(&value.data)) {
        if (*integer > kExactInteger || *integer < -kExactInteger) fail(path, "int outside the exact double range");
        out = {static_cast<double>(*integer), true};
    } else {
        out = {as<double>(value, path, "number"), false};
    }
}

void decode(const Value& value, std::string& out, const Path& path) {
    out = as<std::string>(value, path, "str");
}

void decode(const Value& value, Value& out, const Path&) { out = value; }

void decode(const Value& value, Anchors& out, const Path& path) {
    for (const auto& [key, item] : *as<std::shared_ptr<const Object>>(value, path, "object"))
        out[key] = as<std::int64_t>(item, child(path, key), "int");
}

template <class T>
void decode(const Value& value, Nullable<T>& out, const Path& path) {
    if (std::holds_alternative<std::nullptr_t>(value.data)) {
        out.value.reset();
    } else {
        decode(value, out.value.emplace(), path);
    }
}

template <class T>
void decode(const Value& value, std::vector<T>& out, const Path& path) {
    const auto& items = *as<std::shared_ptr<const Array>>(value, path, "list");
    out.clear();
    out.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        T item{};
        decode(items[i], item, element(path, i));
        out.push_back(std::move(item));
    }
}

template <class T, std::size_t N>
void decode(const Value& value, std::array<T, N>& out, const Path& path) {
    const auto& items = *as<std::shared_ptr<const Array>>(value, path, "list");
    if (items.size() != N) fail(path, "expected " + std::to_string(N) + " items");
    for (std::size_t i = 0; i < N; ++i) decode(items[i], out[i], element(path, i));
}

class Reader {
public:
    Reader(const Value& value, const Path& path)
        : fields_(*as<std::shared_ptr<const Object>>(value, path, "object")),
          path_(path),
          used_(fields_.size()) {}

    template <class T>
    void req(const char* key, T& out) {
        const Value* value = find(key);
        if (!value) fail(path_, std::string("missing key ") + key);
        decode(*value, out, child(path_, key));
    }

    template <class T>
    void opt(const char* key, std::optional<T>& out) {
        out.reset();
        if (const Value* value = find(key)) decode(*value, out.emplace(), child(path_, key));
    }

    // Keys no field claimed: an error for typed records, annotations for Program.
    Object rest() const {
        Object rest;
        for (std::size_t i = 0; i < fields_.size(); ++i)
            if (!used_[i]) rest.push_back(fields_[i]);
        return rest;
    }

    void finish() const {
        for (std::size_t i = 0; i < fields_.size(); ++i)
            if (!used_[i]) fail(path_, "unknown key " + fields_[i].first);
    }

private:
    const Value* find(const char* key) {
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i].first == key) {
                used_[i] = true;
                return &fields_[i].second;
            }
        }
        return nullptr;
    }

    const Object& fields_;
    const Path& path_;
    std::vector<bool> used_;
};

Value encode(bool value) { return {value}; }
Value encode(std::int64_t value) { return {value}; }
Value encode(const Num& value) {
    if (value.integral) return {static_cast<std::int64_t>(value.value)};
    return {value.value};
}
Value encode(const std::string& value) { return {value}; }
Value encode(const Value& value) { return value; }

Value encode(const Anchors& anchors) {
    auto object = std::make_shared<Object>();
    for (const auto& [key, value] : anchors) object->emplace_back(key, Value{value});
    return {std::shared_ptr<const Object>(std::move(object))};
}

template <class T>
Value encode(const Nullable<T>& value) {
    if (!value.value) return {nullptr};
    return encode(*value.value);
}

template <class T>
Value encode(const std::vector<T>& values) {
    auto array = std::make_shared<Array>();
    array->reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) array->push_back(encode(static_cast<const T&>(values[i])));
    return {std::shared_ptr<const Array>(std::move(array))};
}

Value encode(const std::vector<bool>& values) {
    auto array = std::make_shared<Array>();
    array->reserve(values.size());
    for (bool value : values) array->push_back({value});
    return {std::shared_ptr<const Array>(std::move(array))};
}

template <class T, std::size_t N>
Value encode(const std::array<T, N>& values) {
    auto array = std::make_shared<Array>();
    for (const auto& value : values) array->push_back(encode(value));
    return {std::shared_ptr<const Array>(std::move(array))};
}

class Writer {
public:
    template <class T>
    void req(const char* key, const T& value) {
        fields_->emplace_back(key, encode(value));
    }

    template <class T>
    void opt(const char* key, const std::optional<T>& value) {
        if (value) fields_->emplace_back(key, encode(*value));
    }

    void extend(const Object& rest) { fields_->insert(fields_->end(), rest.begin(), rest.end()); }

    Value done() { return {std::shared_ptr<const Object>(std::move(fields_))}; }

private:
    std::shared_ptr<Object> fields_ = std::make_shared<Object>();
};

template <class R, class V>
void describe(R& s, V& v, ScalarState*) {
    v.req("onGround", s.on_ground);
    v.req("position", s.position);
    v.req("previousHorizontalCollision", s.previous_horizontal_collision);
    v.req("velocity", s.velocity);
}

template <class R, class V>
void describe(R& c, V& v, PositionConstraint*) {
    v.req("cmp", c.cmp);
    v.req("mode", c.mode);
    v.opt("name", c.name);
    v.req("rhs", c.rhs);
    v.req("t1", c.t1);
    v.opt("t2", c.t2);
}

template <class R, class V>
void describe(R& c, V& v, LegConstraint*) {
    v.req("cmp", c.cmp);
    v.req("mode", c.mode);
    v.req("name", c.name);
    v.req("rhs", c.rhs);
    v.req("leg", c.leg);
    v.req("offset", c.offset);
}

template <class R, class V>
void describe(R& c, V& v, VerticalContact*) {
    v.req("state", c.state);
    v.req("direction", c.direction);
    v.req("plane", c.plane);
    v.opt("case", c.movement_case);
    v.opt("clearance", c.clearance);
    v.opt("lowerHeight", c.lower_height);
    v.opt("higherHeight", c.higher_height);
}

template <class R, class V>
void describe(R& e, V& v, ContactEvent*) {
    v.req("axis", e.axis);
    v.req("direction", e.direction);
    v.req("offset", e.offset);
    v.opt("leg", e.leg);
    v.opt("state", e.state);
    v.opt("solidIndex", e.solid_index);
    v.opt("plane", e.plane);
    v.opt("box", e.box);
    v.opt("boxes", e.boxes);
    v.opt("regions", e.regions);
    v.opt("surface", e.surface);
    v.opt("role", e.role);
    v.opt("blockId", e.block_id);
    v.opt("effect", e.effect);
    v.opt("ownerRegion", e.owner_region);
    v.opt("ownerType", e.owner_type);
    v.opt("successorType", e.successor_type);
}

template <class R, class V>
void describe(R& e, V& v, CaseEvent*) {
    v.req("type", e.type);
    v.req("case", e.movement_case);
    v.req("input", e.input);
    v.req("plane", e.plane);
    v.req("anchors", e.anchors);
    v.req("boxes", e.boxes);
    v.opt("lowerBox", e.lower_box);
    v.opt("clearance", e.clearance);
    v.opt("lowerHeight", e.lower_height);
    v.opt("higherHeight", e.higher_height);
}

template <class R, class V>
void describe(R& u, V& v, MovementUnit*) {
    v.req("type", u.type);
    v.req("start", u.start);
    v.req("end", u.end);
    v.opt("groundedInputs", u.grounded_inputs);
    v.opt("jumpInputs", u.jump_inputs);
    v.opt("sneakInputs", u.sneak_inputs);
    v.opt("hitInputs", u.hit_inputs);
    v.opt("hitWindow", u.hit_window);
    v.opt("occupiedStates", u.occupied_states);
    v.opt("stopInputs", u.stop_inputs);
    v.opt("additionalOccupiedStates", u.additional_occupied_states);
    v.opt("case", u.movement_case);
    v.opt("region", u.region);
    v.opt("successorType", u.successor_type);
    v.opt("anchors", u.anchors);
    v.opt("plane", u.plane);
    v.opt("boxes", u.boxes);
    v.opt("regions", u.regions);
    v.opt("surface", u.surface);
    v.opt("effects", u.effects);
}

template <class R, class V>
void describe(R& c, V& v, RegionChoice*) {
    v.req("offset", c.offset);
    v.req("owner", c.owner);
    v.req("surface", c.surface);
    v.req("options", c.options);
}

template <class R, class V>
void describe(R& w, V& v, WalkOff*) {
    v.req("offset", w.offset);
    v.req("options", w.options);
    v.req("solids", w.solids);
}

template <class R, class V>
void describe(R& p, V& v, Profile*) {
    v.req("airtime", p.airtime);
    v.req("position", p.position);
    v.req("velocity", p.velocity);
    v.req("grounded", p.grounded);
    v.req("jump", p.jump);
    v.req("sneak", p.sneak);
    v.req("ladderTicks", p.ladder_ticks);
    v.req("contacts", p.contacts);
    v.req("contactEvents", p.contact_events);
    v.req("caseEvents", p.case_events);
    v.req("positionConstraints", p.position_constraints);
    v.opt("constraintOwners", p.constraint_owners);
    v.req("generatedUnits", p.generated_units);
    v.opt("regionChoices", p.region_choices);
    v.req("composition", p.composition);
    v.req("landingPhase", p.landing_phase);
    v.opt("threshold", p.threshold);
}

template <class R, class V>
void describe(R& r, V& v, TraceResume*) {
    v.req("onGround", r.on_ground);
    v.req("onLadder", r.on_ladder);
    v.req("previousHorizontalCollision", r.previous_horizontal_collision);
    v.req("position", r.position);
    v.req("velocity", r.velocity);
}

template <class R, class V>
void describe(R& t, V& v, VerticalTrace*) {
    v.req("contacts", t.contacts);
    v.req("jumpVelocity", t.jump_velocity);
    v.req("sneak", t.sneak);
    v.req("onGround", t.on_ground);
    v.req("previousHorizontalCollision", t.previous_horizontal_collision);
    v.req("position", t.position);
    v.req("velocity", t.velocity);
    v.req("threshold", t.threshold);
    v.req("profile", t.profile);
}

template <class R, class V>
void describe(R& t, V& v, Trace*) {
    v.req("status", t.status);
    v.req("ticks", t.ticks);
    v.req("position", t.position);
    v.req("velocity", t.velocity);
    v.req("movement", t.movement);
    v.req("onGround", t.on_ground);
    v.req("ladderTicks", t.ladder_ticks);
    v.req("hit", t.hit);
    v.req("sneak", t.sneak);
    v.req("climbRefreshed", t.climb_refreshed);
    v.req("failures", t.failures);
    v.req("resume", t.resume);
    v.req("vertical", t.vertical);
}

template <class R, class V>
void describe(R& p, V& v, Program*) {
    v.req("initial", p.initial);
    v.req("profile", p.profile);
    v.req("movementUnits", p.movement_units);
    v.req("ladderEvents", p.ladder_events);
    v.req("terminal", p.terminal);
    v.req("terminalSupport", p.terminal_support);
    v.req("interactions", p.interactions);
    v.req("trace", p.trace);
    v.opt("threshold", p.threshold);
    v.opt("guideSeedYaw", p.guide_seed_yaw);
    v.opt("committedThrough", p.committed_through);
    v.opt("settledThrough", p.settled_through);
    v.opt("walkOffs", p.walk_offs);
    v.opt("repairState", p.repair_state);
    v.opt("solveScope", p.solve_scope);
}

template <class R, class V>
void describe(R& c, V& v, MovementChoice*) {
    v.req("unit", c.unit);
    v.req("units", c.units);
    v.req("initial", c.initial);
    v.req("profile", c.profile);
    v.req("ladderEvents", c.ladder_events);
    v.req("terminalSupport", c.terminal_support);
    v.req("interaction", c.interaction);
}

template <class T>
std::enable_if_t<IsRecord<T>::value> decode(const Value& value, T& out, const Path& path) {
    Reader reader(value, path);
    describe(out, reader, static_cast<T*>(nullptr));
    if constexpr (std::is_same_v<T, Program>) {
        out.annotations = reader.rest();
    } else {
        reader.finish();
    }
}

template <class T>
std::enable_if_t<IsRecord<T>::value, Value> encode(const T& record) {
    Writer writer;
    describe(record, writer, static_cast<T*>(nullptr));
    if constexpr (std::is_same_v<T, Program>) writer.extend(record.annotations);
    return writer.done();
}

}  // namespace

Program program_from_value(const Value& value) {
    Program program;
    decode(value, program, Path{nullptr, "program"});
    return program;
}

MovementChoice choice_from_value(const Value& value) {
    MovementChoice choice;
    decode(value, choice, Path{nullptr, "choice"});
    return choice;
}

Value to_value(const Program& program) { return encode(program); }
Value to_value(const MovementChoice& choice) { return encode(choice); }

}  // namespace pkc
