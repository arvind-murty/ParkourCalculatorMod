// Port of movement/ladder/events.py validate_ladder_events for routes without
// ladder events: it still assigns every leg its movement units.
#include "pkc/movement/ladder/events.hpp"

#include <algorithm>
#include <string>

#include "../../schema.hpp"
#include "pkc/movement/units.hpp"

namespace pkc {

void validate_ladder_events(Object& route) {
    const Value& entries = setdefault(route, "ladderEvents", array({}));
    if (!is_list(entries)) fail("ladderEvents must be a list");
    if (!items(entries).empty()) throw Unsupported("ladder events are prepared in Python");
    const Array& airtimes = items(at(route, "airtimes"));
    const Value* profiles_value = get(route, "verticalProfiles");
    const Array profiles = profiles_value ? items(*profiles_value) : Array{};
    Array all_units;
    for (std::size_t leg = 0; leg < airtimes.size(); ++leg) {
        std::int64_t ticks = as_int(airtimes[leg]);
        const Object empty;
        const Object& profile = leg < profiles.size() ? fields(profiles[leg]) : empty;
        const Value* templates_value = get(profile, "generatedUnits");
        if (!templates_value) templates_value = get(profile, "movementUnits");
        std::optional<Array> templates;
        if (templates_value) templates = items(*templates_value);
        if (!templates && (py_truthy(get_or_null(profile, "caseEvents")) ||
                           py_truthy(get_or_null(profile, "contactEvents"))))
            templates = profile_program(profile);
        Array units;
        if (py_equal(get_or_null(profile, "composition"), text("movement_program"))) {
            if (!templates) fail("TypeError: 'NoneType' object is not iterable");
            units = *templates;
        } else {
            units = movement_program(ticks);
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
            std::int64_t start = as_int(at(unit, "start")), end = as_int(at(unit, "end"));
            for (auto [field, mask] : {std::pair{"jumpInputs", "jump"}, std::pair{"groundedInputs", "grounded"},
                                       std::pair{"sneakInputs", "sneak"}}) {
                const Value* listed = get(unit, field);
                const Value* flags = get(profile, mask);
                if (!listed || !flags) continue;
                Array expected;
                for (std::int64_t t = start; t < end; ++t)
                    if (py_truthy(items(*flags).at(t))) expected.push_back(integer_value(t));
                if (!py_equal(*listed, array(std::move(expected))))
                    fail("Movement unit controls disagree with scalar profile");
            }
        }
        if (std::find(compiled.ladder_ticks.begin(), compiled.ladder_ticks.end(), true) != compiled.ladder_ticks.end())
            fail("Movement units disagree with ladder occupancy");
        all_units.push_back(array(std::move(units)));
    }
    put(route, "movementUnits", array(std::move(all_units)));
}

}  // namespace pkc
