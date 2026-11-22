// Port of constraints/hit_events.py's route validation and rows.
#include "pkc/constraints/hit_events.hpp"

#include <set>
#include <string>
#include <tuple>

#include "../schema.hpp"
#include "pkc/movement/contact_combinations.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const double kHalfWidth = f32(0.6) / 2;  // scene/route.HALF_WIDTH

Array listed(const Object& object, const char* key) {
    const Value* value = get(object, key);
    return value ? items(*value) : Array{};
}

// hit.get('avoidOffsets', range(airtime + 1))
std::vector<std::int64_t> avoid_offsets(const Object& hit, std::int64_t airtime) {
    std::vector<std::int64_t> offsets;
    if (const Value* listed_offsets = get(hit, "avoidOffsets")) {
        for (const auto& t : items(*listed_offsets)) offsets.push_back(as_int(t));
    } else {
        for (std::int64_t t = 0; t <= airtime; ++t) offsets.push_back(t);
    }
    return offsets;
}

}  // namespace

void validate_hits(Object& route) {
    const Value& hits = setdefault(route, "hitEvents", array({}));
    if (!is_list(hits)) fail("hitEvents must be a list");
    const Array& airtimes = items(at(route, "airtimes"));
    std::set<std::tuple<std::int64_t, std::int64_t, std::string>> seen;
    for (const auto& hit_value : items(hits)) {
        if (!is_object(hit_value)) fail("Invalid selected wall hit");
        const Object& hit = fields(hit_value);
        const auto* axis = std::get_if<std::string>(&get_or_null(hit, "axis").data);
        const Value& direction = get_or_null(hit, "direction");
        if (!axis || (*axis != "X" && *axis != "Z") ||
            !(py_equal(direction, integer_value(-1)) || py_equal(direction, integer_value(1))))
            fail("Invalid selected wall hit");
        std::int64_t leg = integer(get_or_null(hit, "leg"), "hit leg", 0, airtimes.size() - 1);
        std::int64_t airtime = as_int(airtimes[leg]);
        std::int64_t offset = integer(get_or_null(hit, "offset"), "hit input offset", 0, airtime - 2);
        number(get_or_null(hit, "plane"), "hit plane");
        const Value& box_value = get_or_null(hit, "box");
        if (!is_list(box_value) || items(box_value).size() != 6) fail("A selected wall hit needs its finite box");
        const Array& box = items(box_value);
        for (const auto& v : box) number(v, "hit box");
        for (int i = 0; i < 3; ++i)
            if (num(box[i]).value >= num(box[i + 3]).value) fail("Invalid hit box");
        int a = *axis == "X" ? 0 : 2;
        Num plane = num(direction).value > 0 ? sub(num(box[a]), flt(kHalfWidth)) : add(num(box[a + 3]), flt(kHalfWidth));
        if (!py_equal(at(hit, "plane"), value(plane))) fail("Hit plane must match the actual expanded face");
        if (!seen.emplace(leg, offset, *axis).second) fail("Duplicate selected contact axis");
        if (const Value* states = get(hit, "avoidOffsets"))
            for (const auto& state : items(*states)) integer(state, "hit avoidance state", 0, airtime);
    }
    if (!items(hits).empty() && !py_truthy(get_or_null(route, "sharedMovement")))
        fail("Selected wall hits require shared movement compilation");
    const Value& ceilings = setdefault(route, "ceilingEvents", array({}));
    if (!is_list(ceilings)) fail("ceilingEvents must be a list of objects");
    for (const auto& c : items(ceilings))
        if (!is_object(c)) fail("ceilingEvents must be a list of objects");
    for (const auto& contact_value : items(ceilings)) {
        const Object& contact = fields(contact_value);
        std::int64_t leg = integer(get_or_null(contact, "leg"), "ceiling leg", 0, airtimes.size() - 1);
        integer(get_or_null(contact, "offset"), "ceiling input offset", 0, as_int(airtimes[leg]) - 2);
        const Value& boxes = get_or_null(contact, "boxes");
        const Value& regions = get_or_null(contact, "regions");
        if (!is_list(boxes) || items(boxes).empty() || !is_list(regions) || items(regions).empty())
            fail("A ceiling needs its member boxes and regions");
        for (const auto& box_value : items(boxes)) {
            if (!is_list(box_value) || items(box_value).size() != 6 ||
                !py_equal(get_or_null(contact, "plane"), value(sub(num(items(box_value)[1]), flt(f32(1.8))))))
                fail("Ceiling plane must match the actual underside");
            const Array& box = items(box_value);
            for (const auto& v : box) number(v, "ceiling box");
            for (int i = 0; i < 3; ++i)
                if (num(box[i]).value >= num(box[i + 3]).value) fail("Invalid ceiling box");
        }
        for (const auto& region : items(regions)) {
            if (!is_list(region) || items(region).size() != 4) fail("A ceiling region must be a finite tangent rectangle");
            for (const auto& v : items(region)) number(v, "ceiling region");
        }
        if (!py_truthy(get_or_null(route, "sharedMovement"))) fail("Selected ceilings require shared movement compilation");
    }
}

CompiledHits compile_hits(const Object& route, const Array& events) {
    CompiledHits out;
    Num margin = py_max(num(at(route, "supportClearance")), flt(2e-7));
    const Array& airtimes = items(at(route, "airtimes"));
    for (const auto& hit_value : listed(route, "hitEvents")) {
        const Object& hit = fields(hit_value);
        std::int64_t leg = as_int(at(hit, "leg"));
        if (leg >= static_cast<std::int64_t>(events.size())) continue;
        const Object& event = fields(events[leg]);
        std::int64_t jump = as_int(at(event, "jump"));
        bool open = is_null(get_or_null(event, "landing"));
        std::vector<std::int64_t> offsets = avoid_offsets(hit, as_int(airtimes[leg]));
        std::set<std::int64_t> states;
        for (auto o : offsets) states.insert(jump + o);
        if (std::find(offsets.begin(), offsets.end(), 0) != offsets.end())
            for (std::int64_t t = as_int(at(event, "landed")); t < jump; ++t) states.insert(t);
        const std::string& axis = as_text(at(hit, "axis"));
        std::int64_t direction = as_int(at(hit, "direction"));
        std::string name = "wall_hit:" + std::to_string(leg) + ":" + axis;
        for (auto tick : states) {
            if (open && tick > jump) continue;
            out.rows.push_back(object({{"mode", text(axis)}, {"t1", integer_value(tick)},
                                       {"cmp", text(direction > 0 ? "LE" : "GE")}, {"rhs", at(hit, "plane")},
                                       {"name", text(name + ":avoid")}}));
        }
        if (open) continue;
        std::int64_t tick = jump + as_int(at(hit, "offset"));
        Object contact = {{"state", integer_value(tick + 1)}, {"axis", integer_value(axis == "X" ? 0 : 1)},
                          {"direction", integer_value(direction)}, {"plane", at(hit, "plane")}};
        out.contacts.push_back(object(contact));
        out.scene_contacts.push_back(
            object(with(with(contact, "axis", integer_value(axis == "X" ? 0 : 2)), "boxes", array({at(hit, "box")}))));
        std::string tangent = axis == "X" ? "Z" : "X";
        int index = tangent == "Z" ? 2 : 0;
        std::int64_t at_tick = axis == "X" ? tick : tick + 1;
        const Array& box = items(at(hit, "box"));
        Num half = flt(kHalfWidth);
        out.rows.push_back(object({{"mode", text(tangent)}, {"t1", integer_value(at_tick)}, {"cmp", text("GE")},
                                   {"rhs", value(add(sub(num(box[index]), half), margin))},
                                   {"name", text(name + ":finite_lo")}}));
        out.rows.push_back(object({{"mode", text(tangent)}, {"t1", integer_value(at_tick)}, {"cmp", text("LE")},
                                   {"rhs", value(sub(add(num(box[index + 3]), half), margin))},
                                   {"name", text(name + ":finite_hi")}}));
        out.rows.push_back(object({{"mode", text(axis)}, {"t1", integer_value(tick + 2)},
                                   {"cmp", text(direction > 0 ? "LE" : "GE")},
                                   {"rhs", value(sub(num(at(hit, "plane")), mul(whole(direction), margin)))},
                                   {"name", text(name + ":reverse")}}));
    }
    for (const auto& contact_value : listed(route, "ceilingEvents")) {
        const Object& contact = fields(contact_value);
        std::int64_t leg = as_int(at(contact, "leg"));
        if (leg >= static_cast<std::int64_t>(events.size()) || is_null(get_or_null(fields(events[leg]), "landing")))
            continue;
        std::int64_t jump = as_int(at(fields(events[leg]), "jump"));
        std::int64_t tick = jump + as_int(at(contact, "offset"));
        out.scene_contacts.push_back(object({{"state", integer_value(tick + 1)}, {"axis", integer_value(1)},
                                             {"plane", at(contact, "plane")}, {"direction", integer_value(1)},
                                             {"boxes", at(contact, "boxes")}, {"regions", at(contact, "regions")}}));
        for (auto& row : finite_rows(1, items(bounding_region(items(at(contact, "regions")))), {{"t1", integer_value(tick)}}))
            out.rows.push_back(std::move(row));
        for (const auto& choice : listed(contact, "avoidBefore"))
            out.rows.push_back(
                object(with(fields(choice), "t1", integer_value(jump + as_int(at(fields(choice), "t1"))))));
    }
    return out;
}

}  // namespace pkc
