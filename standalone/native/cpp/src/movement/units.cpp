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

Array ladder_units(const Object& schedule, const Value& region_id) {
    const std::int64_t e = as_int(at(schedule, "entryState")), x = as_int(at(schedule, "exitState"));
    const std::int64_t start = std::max<std::int64_t>(0, e - 1);
    const Array& hit = items(at(schedule, "hit"));
    const Value* trace = get(schedule, "trace");
    const Value* refreshed = trace && is_object(*trace) ? get(fields(*trace), "climbRefreshed") : nullptr;
    const Array& actual = refreshed ? items(*refreshed) : hit;
    std::vector<std::int64_t> hits;
    for (std::int64_t t = start; t < x - 1; ++t)
        if (py_truthy(actual.at(t))) hits.push_back(t);
    for (std::int64_t t = 0; t < static_cast<std::int64_t>(hit.size()); ++t)
        if ((t < start || t >= x - 1) && py_truthy(hit[t])) fail("Climb hits must end inside the ladder");
    auto ints = [](const std::vector<std::int64_t>& v) {
        Array out;
        for (auto i : v) out.push_back(integer_value(i));
        return array(std::move(out));
    };
    Array units;
    if (!hits.empty()) {
        for (std::size_t i = 0; i < hits.size(); ++i)
            if (hits[i] != hits[0] + static_cast<std::int64_t>(i)) fail("A ladder interaction cannot resume climbing after falling");
        std::int64_t first = hits.front(), end = hits.back() + 1;
        const Value& window = get_or_null(schedule, "climbWindow");
        if (!is_null(window)) {
            first = as_int(items(window)[0]);
            end = as_int(items(window)[1]);
        }
        first = std::min(first, hits.front());
        end = std::max(end, hits.back() + 1);
        if (!(start <= first && first <= hits.front() && hits.back() < end && end <= x - 1)) fail("Invalid climbing window");
        std::vector<std::int64_t> stops;
        for (std::int64_t t = first; t < end; ++t)
            if (!py_truthy(actual.at(t))) stops.push_back(t);
        if (stops.size() > 2 || std::any_of(stops.begin(), stops.end(), [&](std::int64_t t) { return t != first && t != end - 1; }))
            fail("Climbing only allows one-input endpoint stops");
        const std::int64_t climb_end = end == x - 1 ? x : end;
        if (start < first) units.push_back(movement_unit("ladder_fall", start, first, {{"region", region_id}}));
        units.push_back(movement_unit("ladder_climb", first, climb_end,
                                      {{"region", region_id}, {"hitWindow", ints({first, end})}, {"stopInputs", ints(stops)},
                                       {"hitInputs", ints(hits)}}));
        if (climb_end < x) units.push_back(movement_unit("ladder_fall", climb_end, x, {{"region", region_id}}));
    } else {
        units.push_back(movement_unit("ladder_fall", start, x, {{"region", region_id}}));
    }
    if (as_text(at(schedule, "exitKind")) == "top" && as_text(at(fields(units.back()), "type")) != "ladder_climb")
        fail("A top exit must end with ladder_climb");
    const Value* sneak = get(schedule, "sneak");
    for (auto& unit_value : units) {
        Object unit = fields(unit_value);
        const std::int64_t s = as_int(at(unit, "start")), u_end = as_int(at(unit, "end"));
        put(unit, "occupiedStates", ints({std::max(e, s), std::min(x, u_end)}));
        std::vector<std::int64_t> sneaks;
        for (std::int64_t t = s; t < u_end; ++t)
            if (sneak ? py_truthy(items(*sneak).at(t)) : (t < x ? false : (fail("IndexError: list index out of range"), false)))
                sneaks.push_back(t);
        put(unit, "sneakInputs", ints(sneaks));
        unit_value = object(std::move(unit));
    }
    return units;
}

Array movement_program(std::int64_t ticks, const Array& events_value) {
    if (events_value.empty()) return movement_program(ticks);
    integer(integer_value(ticks), "movement program ticks", 1, 200);
    std::vector<const Object*> events;
    for (const auto& e : events_value) events.push_back(&fields(e));
    std::stable_sort(events.begin(), events.end(), [](const Object* a, const Object* b) {
        return as_int(at(fields(at(*a, "schedule")), "entryState")) < as_int(at(fields(at(*b, "schedule")), "entryState"));
    });
    Array units;
    std::int64_t cursor = 0;
    static const char* kKeys[] = {"type", "start", "end", "occupiedStates", "hitInputs", "hitWindow", "stopInputs"};
    for (const Object* event : events) {
        const Object& column = fields(at(*event, "column"));
        Value region_id;
        if (const Value* id = get(column, "id")) {
            region_id = *id;
        } else {
            region_id = text("(" + std::to_string(as_int(at(column, "x"))) + ", " + std::to_string(as_int(at(column, "minY"))) +
                             ", " + std::to_string(as_int(at(column, "z"))) + ", " + std::to_string(as_int(at(column, "maxY"))) + ")");
        }
        const Object& s = fields(at(*event, "schedule"));
        const Value* templates = get(s, "generatedUnits");
        if (!templates) templates = get(s, "movementUnits");
        Array segment;
        if (templates) {
            for (const auto& u : items(*templates))
                if (is_ladder_type(as_text(at(fields(u), "type")))) segment.push_back(u);
            std::vector<std::string> kinds;
            for (const auto& u : segment) kinds.push_back(as_text(at(fields(u), "type")));
            bool bad = kinds.empty() || std::count(kinds.begin(), kinds.end(), "ladder_climb") > 1;
            for (const auto& k : kinds) bad = bad || (k != "ladder_climb" && k != "ladder_fall");
            for (std::size_t i = 1; i < kinds.size(); ++i) bad = bad || kinds[i] == kinds[i - 1];
            if (bad) fail("Generated ladder units violate the phase grammar");
            if (as_text(at(s, "exitKind")) == "top" && kinds.back() != "ladder_climb") fail("A top exit must end with ladder_climb");
            const Array expected = ladder_units(s, region_id);
            auto keyed = [&](const Array& list) {
                Array out;
                for (const auto& u : list) {
                    Array row;
                    for (const char* k : kKeys) row.push_back(get_or_null(fields(u), k));
                    out.push_back(array(std::move(row)));
                }
                return out;
            };
            if (!py_equal(keyed(segment), keyed(expected))) fail("Generated units disagree with the selected ladder inputs");
            for (auto& u : segment) u = object(with(fields(u), "region", region_id));
        } else {
            segment = ladder_units(s, region_id);
        }
        const std::int64_t first_start = as_int(at(fields(segment[0]), "start"));
        if (first_start < cursor) {
            const std::int64_t seam = first_start;
            if (seam != cursor - 1) fail("Movement units overlap");
            Object previous = fields(units.back());
            put(previous, "end", integer_value(seam));
            Array sneaks;
            if (const Value* existing = get(previous, "sneakInputs"))
                for (const auto& t : items(*existing))
                    if (as_int(t) < seam) sneaks.push_back(t);
            put(previous, "sneakInputs", array(std::move(sneaks)));
            if (const Value* hit_inputs = get(previous, "hitInputs"))
                for (const auto& t : items(*hit_inputs))
                    if (as_int(t) >= seam) fail("Competing climb contacts on a transfer seam");
            if (as_int(at(previous, "start")) == seam) {
                const Array& occupied = items(at(previous, "occupiedStates"));
                Array additional;
                for (std::int64_t t = as_int(occupied[0]); t < as_int(occupied[1]); ++t) additional.push_back(integer_value(t));
                segment[0] = object(with(fields(segment[0]), "additionalOccupiedStates", array(std::move(additional))));
                units.pop_back();
            } else {
                units.back() = object(std::move(previous));
            }
            cursor = seam;
        }
        if (cursor < first_start) units.push_back(movement_unit("normal", cursor, first_start));
        for (const auto& u : segment) units.push_back(u);
        cursor = as_int(at(fields(segment.back()), "end"));
    }
    if (cursor > ticks) fail("Movement unit exceeds program horizon");
    if (cursor < ticks) units.push_back(movement_unit("normal", cursor, ticks));
    const Array& sneak = items(at(fields(at(fields(events_value[0]), "schedule")), "sneak"));
    for (auto& unit_value : units) {
        Object unit = fields(unit_value);
        Array sneaks;
        for (std::int64_t t = as_int(at(unit, "start")); t < as_int(at(unit, "end")); ++t)
            if (py_truthy(sneak.at(t))) sneaks.push_back(integer_value(t));
        put(unit, "sneakInputs", array(std::move(sneaks)));
        unit_value = object(std::move(unit));
    }
    return units;
}

bool exit_matches(const std::string& kind, double y, const Object& region) {
    const double min_y = num(at(region, "minY")).value, max_y = num(at(region, "maxY")).value;
    if (kind == "side") return min_y <= y && y < max_y;
    if (kind == "top") return y >= max_y;
    if (kind == "bottom") return y < min_y;
    fail("Exit kind must be side, top or bottom");
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
