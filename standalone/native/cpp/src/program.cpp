#include "pkc/program.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include "pkc/canonical.hpp"
#include "pkc/vertical.hpp"
#include "value_ops.hpp"

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
    array->reserve(N);
    for (const auto& value : values) array->push_back(encode(value));
    return {std::shared_ptr<const Array>(std::move(array))};
}

// Counts the fields a record writes so Writer allocates its object once.
class FieldCount {
public:
    template <class T>
    void req(const char*, const T&) {
        ++count;
    }

    template <class T>
    void opt(const char*, const std::optional<T>& value) {
        count += value.has_value();
    }

    std::size_t count = 0;
};

class Writer {
public:
    explicit Writer(std::size_t capacity) { fields_->reserve(capacity); }

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
    v.req("duration", p.duration);
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
    v.opt("parentTicks", p.parent_ticks);
    v.opt("prefixChain", p.prefix_chain);
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
    FieldCount fields;
    describe(record, fields, static_cast<T*>(nullptr));
    if constexpr (std::is_same_v<T, Program>) fields.count += record.annotations.size();
    Writer writer(fields.count);
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
Value to_value(const Profile& profile) { return encode(profile); }

// ---- movement/program.py: append_choice and the physics key ---------------

namespace {

const Num kDefaultThreshold{.005, false};

const Trace& trace_of(const Program& program) {
    if (!program.trace.value) pkc::fail("program trace is missing");
    return *program.trace.value;
}

// boundary_state(program, on_ladder)
ScalarState boundary_state(const Program& program, bool on_ladder) {
    const Profile& p = program.profile;
    Num vy = p.velocity.back();
    if (p.duration && on_ladder != p.ladder_ticks.back())
        vy = vertical_carry(trace_of(program).movement.back(), on_ladder, trace_of(program).hit.back());
    return {p.grounded.back(), p.position.back(),
            p.duration ? bool(trace_of(program).hit.back()) : program.initial.previous_horizontal_collision, vy};
}

bool same_state(const ScalarState& a, const ScalarState& b) {
    return a.on_ground == b.on_ground && a.position.value == b.position.value &&
           a.previous_horizontal_collision == b.previous_horizontal_collision && a.velocity.value == b.velocity.value;
}

bool same_numbers(const std::vector<Num>& a, const std::vector<Num>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].value != b[i].value) return false;
    return true;
}

std::optional<std::string> surface_of(const ContactEvent& event) {  // event.get('surface')
    return event.surface ? event.surface->value : std::nullopt;
}

bool same_plane(const std::optional<Num>& a, const std::optional<Num>& b) {
    return a.has_value() == b.has_value() && (!a || a->value == b->value);
}

// _contacts: merge_contacts([dict(e, state=e['offset'] + 1)]) without the state key.
std::vector<ContactEvent> merge_events(std::vector<ContactEvent> events) {
    std::map<std::pair<std::int64_t, std::string>, std::size_t> merged;
    for (std::size_t i = 0; i < events.size(); ++i) {
        auto [found, inserted] = merged.try_emplace({events[i].offset, events[i].axis}, i);
        if (inserted) continue;
        const ContactEvent& previous = events[found->second];
        if (!same_plane(previous.plane, events[i].plane) || previous.direction != events[i].direction ||
            surface_of(previous) != surface_of(events[i]))
            pkc::fail("Conflicting contacts on one input axis");
    }
    std::vector<ContactEvent> result;
    result.reserve(merged.size());
    for (const auto& [key, index] : merged) {
        result.push_back(std::move(events[index]));
        result.back().state.reset();
    }
    return result;
}

// movement/units.py shifted_unit
MovementUnit shifted_unit(MovementUnit unit, std::int64_t offset) {
    unit.start += offset;
    unit.end += offset;
    for (auto* field : {&unit.hit_window, &unit.occupied_states, &unit.stop_inputs, &unit.hit_inputs,
                        &unit.sneak_inputs, &unit.additional_occupied_states, &unit.jump_inputs,
                        &unit.grounded_inputs})
        if (*field)
            for (auto& t : **field) t += offset;
    if (unit.anchors)
        for (auto& [key, t] : *unit.anchors) t += offset;
    if (unit.effects)
        for (auto& effect : *unit.effects) effect = shifted_unit(std::move(effect), offset);
    return unit;
}

// movement/units.py compile_units, for its checks.
void compile_units(std::int64_t ticks, const std::vector<MovementUnit>& units) {
    static const std::map<std::string, bool> kTypes = {{"normal", false},     {"ladder_climb", true},
                                                       {"ladder_fall", true}, {"ceiling", false},
                                                       {"stepping", false}};
    std::int64_t cursor = 0;
    for (const auto& unit : units) {
        if (!kTypes.count(unit.type) || unit.start != cursor || !(cursor < unit.end && unit.end <= ticks))
            pkc::fail("Movement units must partition the complete input timeline");
        for (const auto* values : {&unit.jump_inputs, &unit.grounded_inputs}) {
            if (!*values) continue;
            std::vector<std::int64_t> seen;
            for (auto t : **values) {
                if (t < unit.start || t >= unit.end || std::count(seen.begin(), seen.end(), t))
                    pkc::fail("Invalid unit input control");
                seen.push_back(t);
            }
        }
        if (unit.hit_inputs)
            for (auto t : *unit.hit_inputs)
                if (unit.type != "ladder_climb" || t < unit.start || t >= unit.end) pkc::fail("Invalid unit climb input");
        if (unit.sneak_inputs)
            for (auto t : *unit.sneak_inputs)
                if (t < unit.start || t >= unit.end) pkc::fail("Invalid unit Sneak input");
        cursor = unit.end;
    }
    if (cursor != ticks) pkc::fail("Movement program is incomplete");
}

const MovementUnit* unit_at(const std::vector<MovementUnit>& units, std::int64_t tick) {
    for (const auto& unit : units)
        if (unit.start <= tick && tick < unit.end) return &unit;
    return nullptr;
}

// movement/ladder/ceiling.py own_ceiling_contacts for units without ladder ticks:
// no unit climbs, so it only strips ladder-ceiling effects and checks that no
// contact claims one. A climbing owner would need the ladder rebuild.
void own_ceiling_contacts(const std::vector<ContactEvent>& contacts, std::vector<MovementUnit>& units) {
    for (auto& unit : units) {
        if (!unit.effects) continue;
        auto& effects = *unit.effects;
        effects.erase(std::remove_if(effects.begin(), effects.end(),
                                     [](const MovementUnit& e) { return e.type == "ladder_ceiling"; }),
                      effects.end());
        if (effects.empty()) unit.effects.reset();
    }
    for (const auto& contact : contacts) {
        if (contact.axis != "Y" || contact.role != "ceiling") continue;
        const MovementUnit* owner = unit_at(units, contact.offset);
        if (owner && owner->type == "ladder_climb") throw Unsupported("ladder ceiling contact");
        if (contact.effect == "ladder_ceiling") pkc::fail("Ladder ceiling requires a climbing owner");
    }
}

// _profile
Profile assemble_profile(const Trace& trace, std::vector<ContactEvent> contacts, std::vector<CaseEvent> cases,
                         std::vector<PositionConstraint> constraints, std::vector<MovementUnit> units,
                         const std::string& landing_phase, std::vector<RegionChoice> region_choices) {
    Profile p;
    p.duration = trace.ticks;
    p.position = trace.position;
    p.velocity = trace.velocity;
    p.grounded = trace.on_ground;
    p.jump.reserve(trace.vertical.jump_velocity.size());
    for (const auto& v : trace.vertical.jump_velocity) p.jump.push_back(v.value.has_value());
    p.sneak = trace.sneak;
    p.ladder_ticks = trace.ladder_ticks;
    p.contacts = trace.vertical.contacts;
    p.contact_events = std::move(contacts);
    p.case_events = std::move(cases);
    p.position_constraints = std::move(constraints);
    p.landing_phase = landing_phase;
    p.composition = "movement_program";
    p.generated_units = std::move(units);
    if (!region_choices.empty()) p.region_choices = std::move(region_choices);
    if (trace.vertical.threshold.value != kDefaultThreshold.value) p.threshold = trace.vertical.threshold;
    return p;
}

template <class T>
void append_all(std::vector<T>& into, const std::vector<T>& from, std::size_t skip = 0) {
    into.insert(into.end(), from.begin() + static_cast<std::ptrdiff_t>(skip), from.end());
}

// _extend: trace only the new ticks from the parent's end state.
std::optional<std::pair<Profile, Trace>> extend(const Program& program, const MovementChoice& choice) {
    const Profile &old = program.profile, &tail = choice.profile;
    const Trace& parent = trace_of(program);
    const std::int64_t cut = old.duration, size = tail.duration;
    std::vector<ContactEvent> shifted = tail.contact_events;
    for (auto& c : shifted) c.offset += cut;
    std::vector<ContactEvent> contacts = merge_events(std::move(shifted));
    std::vector<bool> hit(size, false);
    for (const auto& c : contacts)
        if (c.axis != "Y" && c.offset >= cut && c.offset < cut + size) hit[c.offset - cut] = true;
    Trace piece = trace_ladder_schedule(boundary_state(program, false), tail.ladder_ticks, tail.sneak, hit, tail.jump,
                                        tail.contacts, program.threshold.value_or(kDefaultThreshold));
    if (piece.status == "invalid" || piece.ticks != size) return std::nullopt;
    if (!same_numbers(piece.position, tail.position) || !same_numbers(piece.velocity, tail.velocity) ||
        piece.on_ground != tail.grounded)
        return std::nullopt;
    bool landed = piece.status == "landed";
    for (const auto& c : parent.vertical.contacts)
        landed = landed || (c.direction < 0 && !c.movement_case && parent.on_ground.at(c.state));

    Trace trace;
    trace.status = landed ? "landed" : "simulated";
    trace.ticks = cut + size;
    trace.position = parent.position;
    append_all(trace.position, piece.position, 1);
    trace.velocity.assign(parent.velocity.begin(), parent.velocity.begin() + cut);
    append_all(trace.velocity, piece.velocity);
    trace.on_ground = parent.on_ground;
    append_all(trace.on_ground, piece.on_ground, 1);
    trace.movement = parent.movement;
    append_all(trace.movement, piece.movement);
    trace.climb_refreshed = parent.climb_refreshed;
    append_all(trace.climb_refreshed, piece.climb_refreshed);
    trace.vertical = parent.vertical;
    append_all(trace.vertical.jump_velocity, piece.vertical.jump_velocity);
    append_all(trace.vertical.sneak, piece.vertical.sneak);
    for (auto c : piece.vertical.contacts) {
        c.state += cut;
        trace.vertical.contacts.push_back(std::move(c));
    }
    trace.ladder_ticks = parent.ladder_ticks;
    append_all(trace.ladder_ticks, piece.ladder_ticks, 1);
    trace.sneak = parent.sneak;
    append_all(trace.sneak, piece.sneak);
    trace.hit = parent.hit;
    append_all(trace.hit, piece.hit);
    trace.resume = piece.resume;

    std::vector<CaseEvent> cases = old.case_events;
    for (CaseEvent event : tail.case_events) {
        event.input += cut;
        for (auto& [key, t] : event.anchors) t += cut;
        cases.push_back(std::move(event));
    }
    std::vector<PositionConstraint> constraints = old.position_constraints;
    for (PositionConstraint row : tail.position_constraints) {
        row.t1 += cut;  // t2 is not shifted, as in the Python
        constraints.push_back(std::move(row));
    }
    std::vector<RegionChoice> region_choices = old.region_choices.value_or(std::vector<RegionChoice>{});
    for (RegionChoice entry : tail.region_choices.value_or(std::vector<RegionChoice>{})) {
        entry.offset += cut;
        for (auto& rows : entry.options)
            for (auto& row : rows) row.offset += cut;
        region_choices.push_back(std::move(entry));
    }
    std::vector<MovementUnit> units;
    units.reserve(tail.generated_units.size());
    for (const auto& unit : tail.generated_units) units.push_back(shifted_unit(unit, cut));
    try {
        own_ceiling_contacts(contacts, units);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
    compile_units(size, tail.generated_units);
    std::vector<MovementUnit> all_units = old.generated_units;
    append_all(all_units, units);
    std::vector<ContactEvent> all_contacts = old.contact_events;
    append_all(all_contacts, contacts);
    Profile profile = assemble_profile(trace, std::move(all_contacts), std::move(cases), std::move(constraints),
                                       std::move(all_units), tail.landing_phase, std::move(region_choices));
    return std::pair{std::move(profile), std::move(trace)};
}

bool skip_repair_state(const Value& state) {  // a repair state scoped to a solve identity is not inherited
    const Value* context = get(fields(state), "context");
    if (!context) return false;
    const Value* scope = get(fields(*context), "scope");
    if (!scope || !py_truthy(*scope)) return false;
    const Object& outer = fields(*scope);
    if (const Value* identity = get(outer, "solveIdentity"); identity && py_truthy(*identity)) return true;
    const Value* inner = get(outer, "scope");
    if (!inner || !py_truthy(*inner)) return false;
    const Value* identity = get(fields(*inner), "solveIdentity");
    return identity && py_truthy(*identity);
}

std::string hex(const std::array<std::uint8_t, 32>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (auto b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 0xF];
    }
    return out;
}

std::string unhex(const std::string& text) {
    if (text.size() != 64) pkc::fail("prefix chain hash must be 64 hex digits");
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        pkc::fail("prefix chain hash must be hex");
    };
    std::string out(32, '\0');
    for (std::size_t i = 0; i < 32; ++i) out[i] = static_cast<char>(digit(text[2 * i]) * 16 + digit(text[2 * i + 1]));
    return out;
}

std::string raw(const std::array<std::uint8_t, 32>& bytes) { return std::string(bytes.begin(), bytes.end()); }

// _chain: sha256 chain over states start..through, resumed from anchor = [state, hex]
// when given; returns the raw hashes at through-1 and through.
std::pair<std::string, std::string> chain(const Profile& profile, std::int64_t through, const Value* anchor) {
    static const std::string kSeed = raw(sha256("pkc-program-v1"));
    std::int64_t start = 0;
    std::string h = kSeed;
    if (anchor && !is_null(*anchor)) {
        const Array& pair = items(*anchor);
        start = std::get<std::int64_t>(pair.at(0).data) + 1;
        h = unhex(as_text(pair.at(1)));
    }
    const std::int64_t n = profile.duration;
    if (through < start) return {h, h};
    struct Owned {
        Array contacts, contact_events, case_events, region_choices;
        std::vector<std::pair<std::string, Value>> rows;  // canonical bytes, row
    };
    std::vector<Owned> owned(static_cast<std::size_t>(through - start + 1));
    auto slot = [&](std::int64_t t) -> Owned* {
        t = std::min(t, n);
        return start <= t && t <= through ? &owned[static_cast<std::size_t>(t - start)] : nullptr;
    };
    for (const auto& c : profile.contacts)
        if (Owned* o = slot(c.state)) o->contacts.push_back(encode(c));
    for (const auto& c : profile.contact_events)
        if (Owned* o = slot(c.offset + 1)) o->contact_events.push_back(encode(c));
    for (const auto& c : profile.case_events)
        if (Owned* o = slot(c.input + 1)) o->case_events.push_back(encode(c));
    if (profile.region_choices)
        for (const auto& c : *profile.region_choices)
            if (Owned* o = slot(c.offset + 1)) o->region_choices.push_back(encode(c));
    for (const auto& row : profile.position_constraints) {
        std::int64_t t = !row.t2 || *row.t2 < row.t1 ? row.t1 : *row.t2;
        if (Owned* o = slot(t)) {
            PositionConstraint unnamed = row;
            unnamed.name.reset();
            Value value = encode(unnamed);
            o->rows.emplace_back(canonical_json(value), std::move(value));
        }
    }
    std::string before = h;
    for (std::int64_t t = start; t <= through; ++t) {
        Owned& o = owned[static_cast<std::size_t>(t - start)];
        std::stable_sort(o.rows.begin(), o.rows.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        Array rows;
        rows.reserve(o.rows.size());
        for (auto& [bytes, row] : o.rows) rows.push_back(std::move(row));
        Object record{{"contacts", array(std::move(o.contacts))},
                      {"contactEvents", array(std::move(o.contact_events))},
                      {"caseEvents", array(std::move(o.case_events))},
                      {"rows", array(std::move(rows))},
                      {"regionChoices", array(std::move(o.region_choices))},
                      {"t", integer_value(t)},
                      {"position", encode(profile.position.at(t))},
                      {"velocity", encode(profile.velocity.at(t))},
                      {"grounded", boolean(profile.grounded.at(t))},
                      {"ladder", boolean(profile.ladder_ticks.at(t))},
                      {"jump", t ? boolean(profile.jump.at(t - 1)) : kNull},
                      {"sneak", t ? boolean(profile.sneak.at(t - 1)) : kNull}};
        before = h;
        h = raw(sha256(h + canonical_json(object(std::move(record)))));
    }
    return {before, h};
}

// prefix_chain: [n-1, hex] over a program's states before its last; null for an empty one.
Value prefix_chain(const Program& program) {
    const std::int64_t n = program.profile.duration;
    if (!n) return kNull;
    const Value* anchor = program.prefix_chain ? &*program.prefix_chain : nullptr;
    std::string hash = chain(program.profile, n - 1, anchor).second;
    std::array<std::uint8_t, 32> bytes{};
    std::copy(hash.begin(), hash.end(), bytes.begin());
    return array({integer_value(n - 1), text(hex(bytes))});
}

}  // namespace

std::optional<Program> append_choice(const Program& program, const MovementChoice& choice, std::int64_t max_inputs) {
    const Profile &old = program.profile, &tail = choice.profile;
    const std::int64_t cut = old.duration, size = tail.duration;
    if (size < 1 || size > 200) pkc::fail("movement choice duration is out of range");
    if (cut + size > std::min<std::int64_t>(max_inputs, 200)) return std::nullopt;
    if (tail.ladder_ticks.empty()) pkc::fail("movement choice ladderTicks is empty");
    if (!same_state(choice.initial, boundary_state(program, tail.ladder_ticks[0])))
        pkc::fail("Movement choice consumes a different scalar boundary state");
    auto any_true = [](const std::vector<bool>& flags) {
        return std::find(flags.begin(), flags.end(), true) != flags.end();
    };
    if (!program.ladder_events.empty() || !choice.ladder_events.empty() || any_true(old.ladder_ticks) ||
        any_true(tail.ladder_ticks))
        throw Unsupported("ladder appends rebuild the program");
    if (!program.trace.value) throw Unsupported("the first append rebuilds the program");
    auto built = extend(program, choice);
    if (!built) return std::nullopt;
    auto& [profile, trace] = *built;
    std::vector<std::int64_t> owners =
        old.constraint_owners.value_or(std::vector<std::int64_t>(old.position_constraints.size(), 0));
    for (auto owner : tail.constraint_owners.value_or(std::vector<std::int64_t>(tail.position_constraints.size(), 0)))
        owners.push_back(owner ? cut + owner : 0);
    profile.constraint_owners = std::move(owners);

    Program result;
    result.initial = program.initial;
    result.movement_units = profile.generated_units;
    result.profile = std::move(profile);
    result.terminal = "open";
    result.terminal_support = choice.terminal_support;
    result.interactions = program.interactions + (choice.interaction ? 1 : 0);
    result.trace.value = std::move(trace);
    result.parent_ticks = cut;
    result.threshold = program.threshold;
    result.guide_seed_yaw = program.guide_seed_yaw;
    if (program.repair_state && !skip_repair_state(*program.repair_state)) result.repair_state = program.repair_state;
    result.solve_scope = program.solve_scope;
    result.committed_through = program.committed_through;
    result.settled_through = program.settled_through;
    result.walk_offs = program.walk_offs;
    result.prefix_chain = prefix_chain(program);
    return result;
}

std::string program_physics_key(const Program& program) {
    const Profile& profile = program.profile;
    const Value* anchor = program.prefix_chain ? &*program.prefix_chain : nullptr;
    std::string h = chain(profile, profile.duration, anchor).second;
    Object rest{{"duration", integer_value(profile.duration)},
                {"composition", text(profile.composition)},
                {"landingPhase", text(profile.landing_phase)}};
    if (profile.threshold) rest.emplace_back("threshold", encode(*profile.threshold));
    Value tail = array({object(std::move(rest)), encode(program.terminal_support),
                        encode(program.threshold.value_or(kDefaultThreshold)), encode(program.ladder_events)});
    return hex(sha256(h + canonical_json(tail)));
}

}  // namespace pkc
