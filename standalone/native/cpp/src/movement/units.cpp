// Port of movement/units.py for units held as JSON values.
#include "pkc/movement/units.hpp"

#include <algorithm>
#include <map>

#include "../schema.hpp"

namespace pkc {
namespace {

const std::map<std::string, bool>& types() {  // MOVEMENT_TYPES
    static const std::map<std::string, bool> kTypes = {
        {"normal", false}, {"ladder_climb", true}, {"ladder_fall", true}, {"ceiling", false}, {"stepping", false}};
    return kTypes;
}

Array ints(const Array& values, std::int64_t offset) {
    Array out;
    out.reserve(values.size());
    for (const auto& v : values) out.push_back(integer_value(as_int(v) + offset));
    return out;
}

Array inputs_where(const Object& profile, const char* mask, std::int64_t start, std::int64_t end, bool optional) {
    Array out;
    const Value* flags = get(profile, mask);
    if (!flags && optional) return out;  // profile.get('sneak', [False]*n)
    const Array& values = items(flags ? *flags : at(profile, mask));
    for (std::int64_t t = start; t < end; ++t)
        if (py_truthy(values.at(t))) out.push_back(integer_value(t));
    return out;
}

}  // namespace

bool is_ladder_type(const std::string& type) {
    auto found = types().find(type);
    if (found == types().end()) fail("KeyError: '" + type + "'");
    return found->second;
}

Value movement_unit(const std::string& kind, std::int64_t start, std::int64_t end, Object parameters) {
    if (!types().count(kind)) fail("Unsupported movement type: " + kind);
    integer(integer_value(start), "unit start", 0, 199);
    integer(integer_value(end), "unit end", start + 1, 200);
    Object unit = {{"type", text(kind)}, {"start", integer_value(start)}, {"end", integer_value(end)}};
    for (auto& field : parameters) unit.push_back(std::move(field));
    return object(std::move(unit));
}

Array movement_program(std::int64_t ticks) {
    integer(integer_value(ticks), "movement program ticks", 1, 200);
    return {movement_unit("normal", 0, ticks)};  // generate_choices('normal', {}, start=0, ends=[ticks])
}

UnitMasks compile_units(std::int64_t ticks, const Array& units) {
    UnitMasks masks{std::vector<bool>(ticks + 1, false), std::vector<bool>(ticks, false),
                    std::vector<bool>(ticks, false)};
    std::int64_t cursor = 0;
    for (const auto& unit_value : units) {
        const Object& unit = fields(unit_value);
        const std::string& type = as_text(at(unit, "type"));
        std::int64_t start = as_int(at(unit, "start")), end = as_int(at(unit, "end"));
        if (!types().count(type) || start != cursor || !(cursor < end && end <= ticks))
            fail("Movement units must partition the complete input timeline");
        if (types().at(type)) {
            const Array& occupied = items(at(unit, "occupiedStates"));
            for (auto s = as_int(occupied.at(0)); s < as_int(occupied.at(1)); ++s) masks.ladder_ticks.at(s) = true;
        }
        if (const Value* extra = get(unit, "additionalOccupiedStates"))
            for (const auto& s : items(*extra)) masks.ladder_ticks.at(as_int(s)) = true;
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
                std::int64_t tick = as_int(t);
                if (type != "ladder_climb" || tick < start || tick >= end) fail("Invalid unit climb input");
                masks.hit.at(tick) = true;
            }
        if (const Value* sneaks = get(unit, "sneakInputs"))
            for (const auto& t : items(*sneaks)) {
                std::int64_t tick = as_int(t);
                if (tick < start || tick >= end) fail("Invalid unit Sneak input");
                masks.sneak.at(tick) = true;
            }
        cursor = end;
    }
    if (cursor != ticks) fail("Movement program is incomplete");
    return masks;
}

Value shifted_unit(const Value& unit_value, std::int64_t offset) {
    Object unit = fields(unit_value);
    for (auto& [key, field] : unit) {
        if (key == "start" || key == "end") {
            field = integer_value(as_int(field) + offset);
        } else if (key == "hitWindow" || key == "occupiedStates" || key == "stopInputs" || key == "hitInputs" ||
                   key == "sneakInputs" || key == "additionalOccupiedStates" || key == "jumpInputs" ||
                   key == "groundedInputs") {
            field = array(ints(items(field), offset));
        } else if (key == "anchors") {
            Object anchors = fields(field);
            for (auto& [name, tick] : anchors) tick = integer_value(as_int(tick) + offset);
            field = object(std::move(anchors));
        } else if (key == "effects") {
            Array effects;
            for (const auto& effect : items(field)) effects.push_back(shifted_unit(effect, offset));
            field = array(std::move(effects));
        }
    }
    return object(std::move(unit));
}

Array profile_program(const Object& profile) {
    const std::int64_t n = as_int(at(profile, "duration"));
    Array cases;
    if (const Value* listed = get(profile, "caseEvents")) cases = items(*listed);
    std::stable_sort(cases.begin(), cases.end(), [](const Value& a, const Value& b) {
        return as_int(at(fields(a), "input")) < as_int(at(fields(b), "input"));
    });
    Array ceiling;
    auto ceiling_case = [](const Value& offset) {
        return object({{"type", text("ceiling")}, {"case", text("ceiling_hit")}, {"input", offset},
                       {"anchors", object({{"hit", integer_value(as_int(offset) + 1)}})}});
    };
    if (const Value* events = get(profile, "contactEvents"))
        for (const auto& e : items(*events)) {
            const Object& event = fields(e);
            if (py_equal(get_or_null(event, "role"), text("ceiling")) &&
                !py_equal(get_or_null(event, "effect"), text("ladder_ceiling")))
                ceiling.push_back(ceiling_case(at(event, "offset")));
        }
    if (const Value* event = get(profile, "ceilingEvent"); event && py_truthy(*event) && ceiling.empty())
        ceiling.push_back(ceiling_case(at(fields(*event), "offset")));
    const std::size_t listed = cases.size();
    for (const auto& c : ceiling) {
        bool taken = false;
        for (std::size_t i = 0; i < listed; ++i)
            taken = taken || py_equal(at(fields(cases[i]), "input"), at(fields(c), "input"));
        if (!taken) cases.push_back(c);
    }
    std::map<std::int64_t, const Object*> spans;  // {c['input']: c}: the last case at an input wins
    for (const auto& c : cases) spans[as_int(at(fields(c), "input"))] = &fields(c);
    std::vector<Object> units;
    for (std::int64_t t = 0; t < n; ++t) {
        auto found = spans.find(t);
        const Object* c = found == spans.end() ? nullptr : found->second;
        if (!c && !units.empty() && as_text(at(units.back(), "type")) == "normal") {
            put(units.back(), "end", integer_value(t + 1));
        } else {
            Object parameters;
            if (c) {
                parameters = {{"case", at(*c, "case")}};
                const Value* anchors = get(*c, "anchors");
                parameters.emplace_back("anchors", anchors ? *anchors : object({}));
            }
            units.push_back(fields(movement_unit(c ? as_text(at(*c, "type")) : "normal", t, t + 1,
                                                 std::move(parameters))));
        }
    }
    Array result;
    for (auto& unit : units) {
        std::int64_t start = as_int(at(unit, "start")), end = as_int(at(unit, "end"));
        put(unit, "jumpInputs", array(inputs_where(profile, "jump", start, end, false)));
        put(unit, "groundedInputs", array(inputs_where(profile, "grounded", start, end, false)));
        put(unit, "sneakInputs", array(inputs_where(profile, "sneak", start, end, true)));
        result.push_back(object(std::move(unit)));
    }
    return result;
}

Array overlay_program(const Array& units, const Array& templates) {
    auto sliced = [](const Value& unit, std::int64_t first, std::int64_t end) {
        Object chosen = with(with(fields(unit), "start", integer_value(first)), "end", integer_value(end));
        for (const char* key : {"jumpInputs", "groundedInputs", "sneakInputs"})
            if (const Value* listed = get(chosen, key)) {
                Array kept;
                for (const auto& t : items(*listed))
                    if (first <= as_int(t) && as_int(t) < end) kept.push_back(t);
                put(chosen, key, array(std::move(kept)));
            }
        return object(std::move(chosen));
    };
    std::int64_t previous_end = 0;
    for (const auto& t : templates) {
        std::int64_t start = as_int(at(fields(t), "start")), end = as_int(at(fields(t), "end"));
        if (start < previous_end || end <= start) fail("Movement templates must be ordered and disjoint");
        previous_end = end;
    }
    Array result;
    for (const auto& unit : units) {
        const Object& u = fields(unit);
        if (is_ladder_type(as_text(at(u, "type")))) {
            result.push_back(unit);
            continue;
        }
        std::int64_t cursor = as_int(at(u, "start")), unit_end = as_int(at(u, "end"));
        for (const auto& t : templates) {
            std::int64_t first = std::max(cursor, as_int(at(fields(t), "start")));
            std::int64_t end = std::min(unit_end, as_int(at(fields(t), "end")));
            if (end <= first) continue;
            if (first > cursor) result.push_back(sliced(unit, cursor, first));
            result.push_back(sliced(t, first, end));
            cursor = end;
        }
        if (cursor < unit_end) result.push_back(sliced(unit, cursor, unit_end));
    }
    return result;
}

}  // namespace pkc
