// Port of movement/contact_combinations.py's route contact events.
#include "pkc/movement/contact_combinations.hpp"

#include <array>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "../schema.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const double kHalfWidth = f32(0.6) / 2;  // scene/route.HALF_WIDTH
const double kBodyHeight = f32(1.8);     // scene/geometry.BODY_HEIGHT
constexpr int kTangentAxes[3][2] = {{1, 2}, {0, 2}, {0, 1}};  // spatial/surfaces.TANGENT_AXES

int axis_index(const std::string& axis) { return axis == "X" ? 0 : axis == "Y" ? 1 : 2; }

Num contact_clearance(const Object& route) {
    const Value* clearance = get(route, "supportClearance");
    return py_max(clearance ? num(*clearance) : flt(1e-6), flt(2e-7));
}

Value expanded_face(const Array& box, int axis, const Num& clearance) {
    Num half = flt(kHalfWidth);
    std::array<Num, 6> expanded = {sub(num(box[0]), half), sub(num(box[1]), flt(kBodyHeight)), sub(num(box[2]), half),
                                   add(num(box[3]), half), num(box[4]), add(num(box[5]), half)};
    int u = kTangentAxes[axis][0], v = kTangentAxes[axis][1];
    Num shrink_u = u == 1 ? whole(0) : clearance, shrink_v = v == 1 ? whole(0) : clearance;
    return array({value(add(expanded[u], shrink_u)), value(add(expanded[v], shrink_v)),
                  value(sub(expanded[u + 3], shrink_u)), value(sub(expanded[v + 3], shrink_v))});
}

Array listed(const Object& event, const char* key) {
    const Value* value = get(event, key);
    return value ? items(*value) : Array{};
}

}  // namespace

Array merge_contacts(const Array& contacts, bool scene, std::optional<std::int64_t> axis) {
    static const char* kKeys[] = {"plane", "direction", "step", "case", "lowerHeight",
                                  "higherHeight", "clearance", "movementCase"};
    struct Entry {
        Value first;
        std::optional<Object> merged;  // first's fields once a scene union extends them
    };
    // Keyed by (state, axis) as the Python dict is: the axis is a number (route contacts) or a name (contact events).
    struct KeyLess {
        bool operator()(const std::pair<std::int64_t, Value>& a, const std::pair<std::int64_t, Value>& b) const {
            if (a.first != b.first) return a.first < b.first;
            double x, y;
            if (numeric(a.second, x) && numeric(b.second, y)) return x < y;
            return as_text(a.second) < as_text(b.second);
        }
    };
    std::map<std::pair<std::int64_t, Value>, Entry, KeyLess> merged;
    for (const auto& contact_value : contacts) {
        const auto& contact = fields(contact_value);
        std::pair<std::int64_t, Value> key{as_int(at(contact, "state")), axis ? integer_value(*axis) : at(contact, "axis")};
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

void validate_contact_events(Object& route) {
    const Value& listed_events = setdefault(route, "contactEvents", array({}));
    if (!is_list(listed_events)) fail("contactEvents must be a list");
    Array events = items(listed_events);
    const Array& airtimes = items(at(route, "airtimes"));
    std::vector<std::tuple<std::int64_t, std::int64_t, std::string>> keys;  // merge key per event
    for (auto& event_value : events) {
        if (!is_object(event_value)) fail("A contact event requires an X/Y/Z face");
        Object event = fields(event_value);
        const auto* axis_text = std::get_if<std::string>(&get_or_null(event, "axis").data);
        if (!axis_text || (*axis_text != "X" && *axis_text != "Y" && *axis_text != "Z"))
            fail("A contact event requires an X/Y/Z face");
        const std::string axis_name = *axis_text;
        std::int64_t leg = integer(get_or_null(event, "leg"), "contact leg", 0, airtimes.size() - 1);
        std::int64_t duration = as_int(airtimes[leg]);
        std::int64_t offset = integer(get_or_null(event, "offset"), "contact input", 0, duration - 1);
        const auto* direction_value = std::get_if<std::int64_t>(&get_or_null(event, "direction").data);
        if (!direction_value || (*direction_value != -1 && *direction_value != 1))
            fail("Contact direction must be -1 or 1");
        const std::int64_t direction = *direction_value;  // copied: the edits below can move the event's fields
        int axis = axis_index(axis_name);
        if (const Value* box = get(event, "box")) {
            Value held = *box;
            erase(event, "box");
            put(event, "boxes", array({held}));
            put(event, "surface", kNull);
            put(event, "regions", array({expanded_face(items(held), axis, contact_clearance(route))}));
        }
        const Value* boxes = get(event, "boxes");
        if (!boxes || !is_list(*boxes) || items(*boxes).empty()) fail("A contact needs its actual finite collision boxes");
        double width = axis == 1 ? kBodyHeight : kHalfWidth;
        for (const auto& box_value : items(*boxes)) {
            if (!is_list(box_value) || items(box_value).size() != 6) fail("A contact needs an actual finite collision box");
            const Array& box = items(box_value);
            std::array<Num, 6> b;
            for (int i = 0; i < 6; ++i) b[i] = number(box[i], "contact box");
            for (int a = 0; a < 3; ++a)
                if (b[a].value >= b[a + 3].value) fail("Contact box must have positive volume");
            Num plane = direction > 0 ? sub(b[axis], flt(width)) : add(b[axis + 3], axis == 1 ? whole(0) : flt(width));
            if (!py_equal(get_or_null(event, "plane"), value(plane))) fail("Contact plane must match its actual finite face");
        }
        const Value* regions = get(event, "regions");
        if (!regions || !is_list(*regions) || items(*regions).empty()) fail("A contact needs at least one finite region");
        for (const auto& region_value : items(*regions)) {
            if (!is_list(region_value) || items(region_value).size() != 4 ||
                num(items(region_value)[0]).value > num(items(region_value)[2]).value ||
                num(items(region_value)[1]).value > num(items(region_value)[3]).value)
                fail("A contact region must be a finite tangent rectangle");
            for (const auto& v : items(region_value)) number(v, "contact region");
        }
        const std::string role = as_text(setdefault(event, "role", text("contact")));
        if (role != "contact" && role != "reversal" && role != "climb" && role != "ceiling" && role != "landing" &&
            role != "step")
            fail("Unsupported contact role");
        if (((role == "reversal" || role == "climb") && axis == 1) ||
            ((role == "ceiling" || role == "landing" || role == "step") && axis != 1))
            fail("Contact role does not match its axis");
        if ((role == "ceiling" && direction != 1) || ((role == "landing" || role == "step") && direction != -1))
            fail("Contact role does not match its direction");
        for (const auto& t : listed(event, "avoidOffsets")) integer(t, "contact avoidance input", 0, duration);
        Array rows = listed(event, "avoidBefore");
        for (const auto& row : listed(event, "avoidAfter")) rows.push_back(row);
        for (const auto& row_value : rows) {
            const Object& row = fields(row_value);
            const auto* mode = std::get_if<std::string>(&get_or_null(row, "mode").data);
            const auto* cmp = std::get_if<std::string>(&get_or_null(row, "cmp").data);
            if (!mode || (*mode != "X" && *mode != "Z") || !cmp || (*cmp != "GE" && *cmp != "LE" && *cmp != "EQ"))
                fail("Contact avoidance requires positional rows");
            integer(get_or_null(row, "t1"), "avoidance state", 0, duration);
            number(get_or_null(row, "rhs"), "avoidance boundary");
        }
        keys.emplace_back(offset + 1, leg, axis_name);
        event_value = object(std::move(event));
    }
    if (!events.empty() && !py_truthy(get_or_null(route, "sharedMovement")))
        fail("Contact combinations require shared movement compilation");
    // merge_contacts with axis=(leg, axis): only its conflict check matters.
    static const char* kKeys[] = {"plane", "direction", "step", "case", "lowerHeight",
                                  "higherHeight", "clearance", "movementCase", "surface"};
    for (std::size_t i = 0; i < keys.size(); ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (keys[i] == keys[j]) {
                for (const char* k : kKeys)
                    if (!py_equal(get_or_null(fields(events[j]), k), get_or_null(fields(events[i]), k)))
                        fail("Conflicting contacts on one input axis");
                break;  // the first event with this key is the one merged into
            }
    put(route, "contactEvents", array(std::move(events)));
}

Value bounding_region(const Array& regions) {
    auto pick = [&](int k, bool smallest) {
        Value best = items(regions.at(0))[k];
        for (std::size_t i = 1; i < regions.size(); ++i) {
            const Value& candidate = items(regions[i])[k];
            if (smallest ? num(candidate).value < num(best).value : num(candidate).value > num(best).value)
                best = candidate;
        }
        return best;
    };
    return array({pick(0, true), pick(1, true), pick(2, false), pick(3, false)});
}

Array finite_rows(int axis, const Array& region, const Object& at) {
    Array rows;
    for (int k = 0; k < 2; ++k) {
        int a = kTangentAxes[axis][k];
        if (a == 1) continue;
        for (const char* cmp : {"GE", "LE"}) {
            bool le = std::string(cmp) == "LE";
            Object row = {{"mode", text(std::string(1, "XYZ"[a]))}, {"cmp", text(cmp)},
                          {"rhs", region.at(k + 2 * le)}, {"name", text("contact:region")}};
            for (const auto& field : at) row.push_back(field);
            rows.push_back(object(std::move(row)));
        }
    }
    return rows;
}

CompiledContacts compile_contact_events(const Object& route, const Array& events, std::int64_t ticks) {
    CompiledContacts out;
    Num margin = py_max(num(at(route, "supportClearance")), flt(2e-7));
    for (const auto& event_value : listed(route, "contactEvents")) {
        const Object& event = fields(event_value);
        std::int64_t leg = as_int(at(event, "leg"));
        if (leg >= static_cast<std::int64_t>(events.size())) continue;
        const Object& at_leg = fields(events[leg]);
        if (is_null(get_or_null(at_leg, "landing")) && is_null(get_or_null(at_leg, "end"))) continue;
        std::int64_t jump = as_int(at(at_leg, "jump"));
        std::int64_t offset = as_int(at(event, "offset"));
        std::int64_t tick = jump + offset;
        const std::string& axis_name = as_text(at(event, "axis"));
        int axis = axis_index(axis_name);
        bool step = as_text(at(event, "role")) == "step";
        Object scene = {{"state", integer_value(tick + 1)}, {"axis", integer_value(axis)},
                        {"plane", at(event, "plane")},      {"direction", at(event, "direction")},
                        {"boxes", at(event, "boxes")},      {"regions", at(event, "regions")}};
        if (step) scene.emplace_back("step", boolean(true));
        out.scene.push_back(object(std::move(scene)));
        if (axis == 1) {
            Object vertical = {{"state", integer_value(tick + 1)}, {"plane", at(event, "plane")},
                               {"direction", at(event, "direction")}};
            if (step) vertical.emplace_back("step", boolean(true));
            out.vertical.push_back(object(std::move(vertical)));
        } else {
            out.horizontal.push_back(object({{"state", integer_value(tick + 1)}, {"axis", integer_value(axis / 2)},
                                             {"plane", at(event, "plane")}, {"direction", at(event, "direction")}}));
        }
        std::int64_t region_offset = offset + (axis_name == "Z" || (axis_name == "Y" && step));
        for (auto& row : finite_rows(axis, items(bounding_region(items(at(event, "regions")))),
                                     {{"t1", integer_value(jump + region_offset)}}))
            out.rows.push_back(std::move(row));
        std::int64_t direction = as_int(at(event, "direction"));
        for (const auto& avoid : listed(event, "avoidOffsets"))
            if (axis != 1)
                out.rows.push_back(object({{"mode", text(axis_name)}, {"t1", integer_value(jump + as_int(avoid))},
                                           {"cmp", text(direction > 0 ? "LE" : "GE")}, {"rhs", at(event, "plane")},
                                           {"name", text("contact:avoid")}}));
        Array avoidance = listed(event, "avoidBefore");
        for (const auto& row : listed(event, "avoidAfter")) avoidance.push_back(row);
        for (const auto& row : avoidance)
            out.rows.push_back(object(with(with(fields(row), "t1", integer_value(jump + as_int(at(fields(row), "t1")))),
                                           "name", text("contact:avoidance"))));
        if (as_text(at(event, "role")) == "reversal" && tick + 2 <= ticks)
            out.rows.push_back(object({{"mode", text(axis_name)}, {"t1", integer_value(tick + 2)},
                                       {"cmp", text(direction > 0 ? "LE" : "GE")},
                                       {"rhs", value(sub(num(at(event, "plane")), mul(whole(direction), margin)))},
                                       {"name", text("contact:reverse")}}));
    }
    return out;
}

std::vector<FaceOption> scene_contact_options(const Value& scene_value, const SceneGeometry& geometry, int axis,
                                              std::int64_t sign, std::optional<std::vector<std::int64_t>> members) {
    const Object& scene = fields(scene_value);
    if (!members) {
        members.emplace();
        for (const auto& b : items(at(scene, "blocks"))) members->push_back(as_int(at(fields(b), "solidIndex")));
    }
    double clearance = 1e-6;  // contact_clearance(scene)
    if (const Value* given = get(scene, "supportClearance")) clearance = num(*given).value;
    clearance = std::max(clearance, 2e-7);
    const Array& solids = items(at(fields(at(scene, "collisionScene")), "solids"));
    std::vector<FaceOption> out;
    const Value options = geometry.contact_options(axis, sign, real(clearance), *members);
    for (const auto& row_value : items(options)) {
        const Array& row = items(row_value);
        FaceOption option{as_text(row[0]), row[1], {}, {}, items(row[3])};
        for (const auto& m : items(row[2])) {
            option.members.push_back(as_int(m));
            option.boxes.push_back(solids.at(as_int(m)));
        }
        out.push_back(std::move(option));
    }
    return out;
}

SolidOwners solid_owners(const Value& scene) {
    SolidOwners result;
    for (const auto& b : items(at(fields(scene), "blocks"))) {
        const Object& block = fields(b);
        const Value* source = get(block, "sourceBlock");
        result[as_int(at(block, "solidIndex"))].insert(as_text(source ? *source : at(block, "id")));
    }
    return result;
}

std::string face_name(const FaceOption& option, const SolidOwners& owners) {
    std::set<std::string> names;  // byte order is code-point order for UTF-8, as Python's sorted()
    for (auto member : option.members)
        if (auto found = owners.find(member); found != owners.end()) names.insert(found->second.begin(), found->second.end());
    std::string joined;
    for (const auto& name : names) joined += (joined.empty() ? "" : "+") + name;
    return joined;
}

std::set<std::string> face_owners(const std::vector<FaceOption>& top_faces, const SolidOwners& owners,
                                  const std::optional<std::string>& name) {
    if (!name) return {};
    std::set<std::string> found;
    for (const auto& option : top_faces)
        if (face_name(option, owners) == *name)
            for (auto member : option.members)
                if (auto o = owners.find(member); o != owners.end()) found.insert(o->second.begin(), o->second.end());
    if (found.empty()) found.insert(*name);
    return found;
}

}  // namespace pkc
