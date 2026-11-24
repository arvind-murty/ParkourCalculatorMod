// Port of movement/ladder/ceiling.own_ceiling_contacts.
#include "pkc/movement/ladder/ceiling.hpp"

#include "../../schema.hpp"

namespace pkc {
namespace {

std::optional<std::size_t> unit_at(const std::vector<Object>& units, std::int64_t tick) {  // _unit_at
    for (std::size_t i = 0; i < units.size(); ++i)
        if (as_int(at(units[i], "start")) <= tick && tick < as_int(at(units[i], "end"))) return i;
    return std::nullopt;
}

}  // namespace

std::pair<Array, Array> own_ceiling_contacts(const Array& contacts_value, const Array& units_value) {
    std::vector<Object> units;
    for (const auto& u : units_value) {
        Object unit = fields(u);
        if (const Value* effects = get(unit, "effects")) {
            Array kept;
            for (const auto& e : items(*effects))
                if (!py_equal(at(fields(e), "type"), text("ladder_ceiling"))) kept.push_back(e);
            if (kept.empty()) {
                erase(unit, "effects");
            } else {
                put(unit, "effects", array(std::move(kept)));
            }
        }
        units.push_back(std::move(unit));
    }
    Array contacts;
    for (const auto& c_value : contacts_value) {
        Object contact = fields(c_value);
        if (!py_equal(at(contact, "axis"), text("Y")) || !py_equal(get_or_null(contact, "role"), text("ceiling"))) {
            contacts.push_back(object(std::move(contact)));
            continue;
        }
        const std::int64_t tick = as_int(at(contact, "offset"));
        const auto owner = unit_at(units, tick);
        const auto following = unit_at(units, tick + 1);
        if (!owner || !py_equal(at(units[*owner], "type"), text("ladder_climb"))) {
            if (py_equal(get_or_null(contact, "effect"), text("ladder_ceiling"))) fail("Ladder ceiling requires a climbing owner");
            contacts.push_back(object(std::move(contact)));
            continue;
        }
        if (!following || !py_equal(at(units[*following], "type"), text("ladder_climb")))
            fail("Ladder ceiling must be followed by ladder_climb");
        Object& unit = units[*owner];
        if (const Value* effects = get(unit, "effects")) {
            Array kept;
            for (const auto& e : items(*effects)) {
                const Object& effect = fields(e);
                if (!py_equal(at(effect, "type"), text("ceiling")) || as_int(at(effect, "start")) != tick) kept.push_back(e);
            }
            put(unit, "effects", array(std::move(kept)));
        }
        put(contact, "effect", text("ladder_ceiling"));
        put(contact, "ownerType", text("ladder_climb"));
        put(contact, "successorType", text("ladder_climb"));
        const Value& region = get_or_null(unit, "region");
        if (!is_null(region)) {
            put(contact, "ownerRegion", region);
        } else {
            erase(contact, "ownerRegion");
        }
        Object effect{{"type", text("ladder_ceiling")},
                      {"case", text("ceiling_hit")},
                      {"start", integer_value(tick)},
                      {"end", integer_value(tick + 1)},
                      {"successorType", text("ladder_climb")},
                      {"anchors", object({{"hit", integer_value(tick + 1)}, {"nextClimb", integer_value(tick + 1)}})},
                      {"plane", at(contact, "plane")},
                      {"boxes", at(contact, "boxes")},
                      {"surface", get_or_null(contact, "surface")},
                      {"regions", at(contact, "regions")}};
        if (!is_null(region)) effect.emplace_back("region", region);
        const Value existing = get(unit, "effects") ? at(unit, "effects") : array({});
        Array effects = items(existing);
        effects.push_back(object(std::move(effect)));
        put(unit, "effects", array(std::move(effects)));
        contacts.push_back(object(std::move(contact)));
    }
    Array out_units;
    for (auto& u : units) out_units.push_back(object(std::move(u)));
    return {std::move(contacts), std::move(out_units)};
}

}  // namespace pkc
