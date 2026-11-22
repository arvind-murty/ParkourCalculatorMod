// Port of movement/ladder/events.py.
#include "pkc/movement/ladder/events.hpp"

#include <algorithm>
#include <map>
#include <string>

#include "../../schema.hpp"
#include "pkc/movement/units.hpp"
#include "pkc/scene/ladders.hpp"

namespace pkc {

void validate_ladder_events(Object& route) {
    const Value entries_value = setdefault(route, "ladderEvents", array({}));
    if (!is_list(entries_value)) fail("ladderEvents must be a list");
    const Array& entries = items(entries_value);
    const Array& airtimes = items(at(route, "airtimes"));
    std::map<std::int64_t, std::vector<const Object*>> seen;
    for (const auto& event_value : entries) {
        const Object& event = fields(event_value);
        const Value& leg_value = get_or_null(event, "leg");
        if (!std::holds_alternative<std::int64_t>(leg_value.data) || as_int(leg_value) < 0 ||
            as_int(leg_value) >= static_cast<std::int64_t>(airtimes.size()))
            fail("Ladder interaction has an invalid transition");
        const std::int64_t leg = as_int(leg_value);
        const Object& s = fields(at(event, "schedule"));
        if (!py_equal(at(s, "ticks"), airtimes[leg]) || !py_truthy(get_or_null(route, "sharedMovement")))
            fail("Ladder interaction requires matching duration and shared movement");
        if (static_cast<std::int64_t>(items(at(s, "ladderTicks")).size()) != as_int(at(s, "ticks")) + 1 ||
            static_cast<std::int64_t>(items(at(s, "sneak")).size()) != as_int(at(s, "ticks")))
            fail("Ladder schedule arrays have inconsistent lengths");
        for (const Object* previous : seen[leg]) {
            const Array& a = items(at(*previous, "ladderTicks"));
            const Array& b = items(at(s, "ladderTicks"));
            for (std::size_t t = 0; t < std::min(a.size(), b.size()); ++t)
                if (py_truthy(a[t]) && py_truthy(b[t])) fail("Ladder episodes may not overlap occupied states");
            if (!py_equal(at(*previous, "sneak"), at(s, "sneak")) ||
                !py_equal(at(fields(at(*previous, "trace")), "position"), at(fields(at(s, "trace")), "position")))
                fail("Episodes in one flight must share the complete input and Y timeline");
        }
        seen[leg].push_back(&s);
    }
    const Value* profiles_value = get(route, "verticalProfiles");
    const Array profiles = profiles_value ? items(*profiles_value) : Array{};
    Array all_units;
    for (std::size_t leg = 0; leg < airtimes.size(); ++leg) {
        const std::int64_t ticks = as_int(airtimes[leg]);
        Array events;
        for (const auto& e : entries)
            if (as_int(at(fields(e), "leg")) == static_cast<std::int64_t>(leg)) events.push_back(e);
        const Object empty;
        const Object& profile = leg < profiles.size() ? fields(profiles[leg]) : empty;
        const Value* templates_value = get(profile, "generatedUnits");
        if (!templates_value) templates_value = get(profile, "movementUnits");
        std::optional<Array> templates;
        if (templates_value) templates = items(*templates_value);
        if (!templates && (py_truthy(get_or_null(profile, "caseEvents")) || py_truthy(get_or_null(profile, "contactEvents"))))
            templates = profile_program(profile);
        Array units;
        if (py_equal(get_or_null(profile, "composition"), text("movement_program"))) {
            for (const auto& e : events) {
                const Object& event = fields(e);
                ladder_units(fields(at(event, "schedule")), at(fields(at(event, "column")), "id"));
            }
            if (!templates) fail("TypeError: 'NoneType' object is not iterable");
            units = *templates;
        } else {
            units = movement_program(ticks, events);
            if (templates) {
                compile_units(ticks, *templates);
                Array kept;
                for (const auto& u : *templates)
                    if (as_text(at(fields(u), "type")).rfind("ladder_", 0) != 0) kept.push_back(u);
                units = overlay_program(units, kept);
            }
        }
        UnitMasks compiled = compile_units(ticks, units);
        for (const auto& unit_value : units) {
            const Object& unit = fields(unit_value);
            const std::int64_t start = as_int(at(unit, "start")), end = as_int(at(unit, "end"));
            for (auto [field, mask] : {std::pair{"jumpInputs", "jump"}, std::pair{"groundedInputs", "grounded"},
                                       std::pair{"sneakInputs", "sneak"}}) {
                const Value* listed = get(unit, field);
                const Value* flags = get(profile, mask);
                if (!listed || !flags) continue;
                Array expected;
                for (std::int64_t t = start; t < end; ++t)
                    if (py_truthy(items(*flags).at(t))) expected.push_back(integer_value(t));
                if (!py_equal(*listed, array(std::move(expected)))) fail("Movement unit controls disagree with scalar profile");
            }
        }
        for (std::int64_t t = 0; t <= ticks; ++t) {
            bool expected = false;
            for (const auto& e : events)
                expected = expected || py_truthy(items(at(fields(at(fields(e), "schedule")), "ladderTicks")).at(t));
            if (compiled.ladder_ticks.at(t) != expected) fail("Movement units disagree with ladder occupancy");
        }
        all_units.push_back(array(std::move(units)));
    }
    put(route, "movementUnits", array(std::move(all_units)));
}

CompiledLadders compile_ladder_events(const Object& route, const Array& events, std::int64_t ticks) {
    CompiledLadders out;
    out.ladder.assign(ticks + 1, false);
    out.sneak.assign(ticks, false);
    const Value* profiles_value = get(route, "verticalProfiles");
    const Array profiles = profiles_value ? items(*profiles_value) : Array{};
    const Value* ladder_events = get(route, "ladderEvents");
    if (!ladder_events) return out;
    const Num clearance = num(at(route, "supportClearance"));
    const Value margin = value(clearance.value < 2e-7 ? flt(2e-7) : clearance);  // max(clearance, 2e-7)
    for (const auto& selected_value : items(*ladder_events)) {
        const Object& selected = fields(selected_value);
        const std::int64_t leg = as_int(at(selected, "leg"));
        if (leg >= static_cast<std::int64_t>(events.size())) continue;
        const Object& event = fields(events[leg]);
        const Object& s = fields(at(selected, "schedule"));
        if (is_null(at(event, "landing"))) continue;
        const std::int64_t offset = as_int(at(event, "jump"));
        const std::int64_t s_ticks = as_int(at(s, "ticks"));
        std::vector<bool> occupied;
        if (leg < static_cast<std::int64_t>(profiles.size()) &&
            py_equal(get_or_null(fields(profiles[leg]), "composition"), text("movement_program"))) {
            for (const auto& v : items(at(s, "ladderTicks"))) occupied.push_back(py_truthy(v));
        } else {
            occupied = compile_units(s_ticks, movement_program(s_ticks, Array{selected_value})).ladder_ticks;
        }
        // ladder[offset:offset+ticks+1] = [a or b for a, b in zip(slice, occupied)]
        for (std::int64_t i = 0; i < s_ticks + 1 && offset + i < static_cast<std::int64_t>(out.ladder.size()) &&
                                 i < static_cast<std::int64_t>(occupied.size()); ++i)
            out.ladder[offset + i] = out.ladder[offset + i] || occupied[i];
        // sneak[offset:offset+ticks] = s['sneak'] (a list slice assignment, which can change the length)
        std::vector<bool> replacement;
        for (const auto& v : items(at(s, "sneak"))) replacement.push_back(py_truthy(v));
        const std::size_t lo = std::min<std::size_t>(offset, out.sneak.size());
        const std::size_t hi = std::min<std::size_t>(offset + s_ticks, out.sneak.size());
        out.sneak.erase(out.sneak.begin() + lo, out.sneak.begin() + std::max(lo, hi));
        out.sneak.insert(out.sneak.begin() + lo, replacement.begin(), replacement.end());
        const Array built = items(schedule_constraints(at(selected, "schedule"), at(selected, "column"), at(selected, "wall"),
                                                       at(selected, "entryFace"), at(selected, "exitFace"),
                                                       integer_value(offset), margin));
        for (const auto& r : items(built[0])) out.rows.push_back(r);
        for (const auto& c : items(built[1])) out.contacts.push_back(c);
        for (const auto& c : items(built[2])) out.scene_contacts.push_back(c);
        if (as_int(at(s, "entryState")) == 0) {
            for (std::int64_t state = as_int(at(event, "landed")); state < offset; ++state) {
                out.ladder.at(state) = true;
                const Value y0 = items(at(fields(at(s, "trace")), "position"))[0];
                const Array& bounds = items(items(region_rectangles(at(selected, "column"), y0)).at(0));
                for (const auto& [axis, lo_v, hi_v] : {std::tuple<const char*, Value, Value>{"X", bounds[0], bounds[1]},
                                                       std::tuple<const char*, Value, Value>{"Z", bounds[2], bounds[3]}}) {
                    out.rows.push_back(object({{"mode", text(axis)}, {"t1", integer_value(state)}, {"cmp", text("GE")},
                                               {"rhs", value(add(num(lo_v), flt(1e-6)))}, {"name", text("ladder:run_inside")}}));
                    out.rows.push_back(object({{"mode", text(axis)}, {"t1", integer_value(state)}, {"cmp", text("LE")},
                                               {"rhs", value(sub(num(hi_v), flt(1e-6)))}, {"name", text("ladder:run_inside")}}));
                }
            }
        }
    }
    return out;
}

}  // namespace pkc
