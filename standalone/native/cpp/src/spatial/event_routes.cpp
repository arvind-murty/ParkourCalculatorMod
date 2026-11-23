// Port of spatial/event_routes.py's program readers and cursor.
#include "pkc/spatial/event_routes.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

#include "../schema.hpp"
#include "../value_ops.hpp"
#include "pkc/canonical.hpp"

namespace pkc {
namespace {

std::vector<double> key_of(const Value& box) {
    std::vector<double> out;
    for (const auto& n : items(box)) out.push_back(num(n).value);
    return out;
}

bool inside(const Value& box_value, const Array& point) {  // cells.contains(box, point), tolerance 0
    const Array& box = items(box_value);
    for (std::size_t a = 0; a < 3; ++a) {
        const double p = num(point[a]).value;
        if (!(num(box[a]).value <= p && p <= num(box[a + 3]).value)) return false;
    }
    return true;
}

// sorted() of distinct values, kept as Python sorts them.
Value sorted_unique(std::vector<Value> values) {
    std::sort(values.begin(), values.end(), [](const Value& a, const Value& b) { return py_compare(a, b) < 0; });
    values.erase(std::unique(values.begin(), values.end(), [](const Value& a, const Value& b) { return py_equal(a, b); }),
                 values.end());
    return array(Array(values.begin(), values.end()));
}

bool truthy_at(const Array& values, std::int64_t index) { return py_truthy(values[static_cast<std::size_t>(index)]); }

}  // namespace

Value support_episodes(const Value& data_value, const Value& profile_value, const std::vector<std::string>& required_owners) {
    const Object& data = fields(data_value);
    const Object& scene = fields(at(data, "scene"));
    const Array& groups = items(at(data, "surfaces"));
    const Array& solids_of = items(at(fields(at(scene, "collisionScene")), "solids"));
    const Object& profile = fields(profile_value);
    const Array& grounded = items(at(profile, "grounded"));
    const Array& jump = items(at(profile, "jump"));
    const Array& position = items(at(profile, "position"));
    const std::int64_t duration = as_int(at(profile, "duration"));
    std::map<std::vector<double>, std::vector<const Object*>> by_box;
    for (const auto& block : items(at(scene, "blocks")))
        by_box[key_of(solids_of[static_cast<std::size_t>(as_int(at(fields(block), "solidIndex")))])].push_back(
            &fields(block));
    const Array& start = items(at(fields(at(scene, "initial")), "position"));
    std::set<std::string> current;
    if (truthy_at(grounded, 0))
        for (const auto& s : groups)
            for (const auto& b : items(at(fields(s), "cover")))
                if (inside(b, start)) {
                    current.insert(as_text(at(fields(s), "id")));
                    break;
                }
    // (tick, named, json.dumps(event, sort_keys=True, separators=(',', ':'))), sorted.
    std::vector<std::tuple<std::int64_t, int, std::string, const Value*>> events;
    if (const Value* contacts = get(profile, "contactEvents"))
        for (const auto& e : items(*contacts)) {
            const Object& event = fields(e);
            const Value* direction = get(event, "direction");
            if (as_text(at(event, "axis")) == "Y" && direction && py_equal(*direction, integer_value(-1)))
                events.emplace_back(as_int(at(event, "offset")), 0, dumps(e, true, true), &e);
        }
    if (const Value* cases = get(profile, "caseEvents"))
        for (const auto& e : items(*cases)) events.emplace_back(as_int(at(fields(e), "input")), 1, dumps(e, true, true), &e);
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return std::tie(std::get<0>(a), std::get<1>(a), std::get<2>(a)) <
               std::tie(std::get<0>(b), std::get<1>(b), std::get<2>(b));
    });
    const std::set<std::string> required(required_owners.begin(), required_owners.end());
    Array result;
    for (const auto& [tick, named, json, event_value] : events) {
        const Object& event = fields(*event_value);
        std::vector<const Object*> blocks;
        if (const Value* boxes = get(event, "boxes"))
            for (const auto& box : items(*boxes))
                if (auto found = by_box.find(key_of(box)); found != by_box.end())
                    blocks.insert(blocks.end(), found->second.begin(), found->second.end());
        std::set<std::string> owner_set;
        std::set<std::int64_t> solid_set;
        for (const Object* b : blocks) {
            const Value* source = get(*b, "sourceBlock");
            owner_set.insert(as_text(source ? *source : at(*b, "id")));
            solid_set.insert(as_int(at(*b, "solidIndex")));
        }
        const double height = num(position[static_cast<std::size_t>(std::min(tick + 1, duration))]).value;
        std::set<std::string> surface_set;
        for (const auto& s_value : groups) {
            const Object& s = fields(s_value);
            const Array& members = items(at(s, "solids"));
            const bool touched = std::any_of(blocks.begin(), blocks.end(), [&](const Object* b) {
                return pkc::contains(members, at(*b, "solidIndex"));
            });
            if (touched && std::fabs(num(at(s, "height")).value - height) <= 1e-7) surface_set.insert(as_text(at(s, "id")));
        }
        Array owners, solids, surfaces;
        for (const auto& o : owner_set) owners.push_back(text(o));
        for (const auto& i : solid_set) solids.push_back(integer_value(i));
        for (const auto& s : surface_set) surfaces.push_back(text(s));
        if (named) {
            result.push_back(object({{"kind", text("named_contact")}, {"input", integer_value(tick)},
                                     {"case", get_or_null(event, "case")}, {"owners", array(owners)},
                                     {"solids", array(solids)}, {"surfaces", array(surfaces)}, {"event", *event_value},
                                     {"classification", text("observed_program")}}));
        } else {
            bool shared = false, owned = false;
            for (const auto& s : surface_set) shared = shared || current.count(s);
            for (const auto& o : owner_set) owned = owned || required.count(o);
            if (!(truthy_at(grounded, tick) && !truthy_at(jump, tick) && shared && !owned))
                result.push_back(object({{"kind", text("support_visit")}, {"input", integer_value(tick)},
                                         {"tick", integer_value(tick + 1)}, {"owners", array(owners)},
                                         {"solids", array(solids)}, {"surfaces", array(surfaces)},
                                         {"classification", text(surface_set.empty() ? "unknown" : "observed_program")}}));
        }
        if (!surface_set.empty() && truthy_at(grounded, std::min(tick + 1, duration))) current = surface_set;
    }
    return array(std::move(result));
}

Value initial_cursor(const Value& data_value, const Array& roots) {
    const Object& data = fields(data_value);
    const Object& geometry = fields(at(data, "geometry"));
    Value attachments = at(geometry, "starts");
    if (std::any_of(roots.begin(), roots.end(),
                    [](const Value& p) { return py_truthy(at(fields(at(fields(p), "profile")), "duration")); })) {
        std::vector<Value> keys;
        for (const auto& p : roots) {
            const Object& program = fields(p);
            const Object& profile = fields(at(program, "profile"));
            if (!py_truthy(items(at(profile, "grounded")).back())) {
                for (const auto& r : items(at(geometry, "regions")))
                    if (as_text(at(fields(r), "kind")) == "air_volume") keys.push_back(at(fields(r), "key"));
                continue;
            }
            // terminalSupport is a face name; frame roots end on the start or an anchor, whose face name is its id.
            Value solid{nullptr};
            for (const auto& b : items(at(fields(at(data, "scene")), "blocks")))
                if (py_equal(at(fields(b), "id"), at(program, "terminalSupport"))) {
                    solid = at(fields(b), "solidIndex");
                    break;
                }
            const double y = num(items(at(profile, "position")).back()).value;
            for (const auto& s : items(at(geometry, "supports")))
                if (pkc::contains(items(at(fields(s), "solids")), solid) &&
                    std::fabs(num(at(fields(s), "height")).value - y) <= 1e-7)
                    keys.push_back(at(fields(s), "key"));
        }
        attachments = sorted_unique(std::move(keys));
    }
    return object({{"schema", integer_value(1)}, {"domainId", at(data, "domainId")}, {"occurrences", array({})},
                   {"attachments", attachments}, {"commitments", array({})}, {"outsideUnresolved", boolean(true)}});
}

Value advance_cursor(const Value& data, const Value& graph, const Value& cursor_value, const Value& request,
                     const Value& observed) {
    const Value* event = nullptr;
    for (const auto& e : items(at(fields(graph), "events")))
        if (py_equal(at(fields(e), "id"), at(fields(request), "event"))) {
            event = &e;
            break;
        }
    if (!event) throw std::runtime_error("StopIteration");
    Object cursor = fields(cursor_value);
    const Array& occurrences = items(at(cursor, "occurrences"));
    Array grown = occurrences;
    grown.push_back(object({{"event", at(fields(*event), "id")},
                            {"occurrence", integer_value(static_cast<std::int64_t>(occurrences.size()))},
                            {"observed", observed}}));
    put(cursor, "attachments", at(fields(*event), "attachments"));
    put(cursor, "occurrences", array(std::move(grown)));
    // No selected raw path is carried forward as a hard requirement.
    put(cursor, "outsideUnresolved",
        boolean(as_text(at(fields(at(fields(data), "coverage")), "enclosure")) != "complete"));
    return object(std::move(cursor));
}

Value observed_route(const Value& data_value, const Value& profile, const Value& witness_value, const Value& after) {
    const Object& data = fields(data_value);
    Array episodes;
    const double from = num(after).value;
    const Value all = support_episodes(data_value, profile);
    for (const auto& e : items(all))
        if (num(at(fields(e), "input")).value >= from) episodes.push_back(e);
    const Object& witness = fields(witness_value);
    Value alternatives;
    if (const Value* listed = get(witness, "selectedInteractionAlternatives")) {
        alternatives = *listed;
    } else {
        const Value* selected = get(witness, "selectedInteractions");
        alternatives = array({selected ? *selected : array({})});
    }
    const Value window = array({integer_value(0), at(fields(profile), "duration")});
    Array passages;
    for (const auto& bundle : items(alternatives)) {
        Array variant;
        for (const auto& passage_value : items(bundle)) {
            const Object& passage = fields(passage_value);
            const Value* listed = get(passage, "members");
            const Value members = listed ? *listed : array({get_or_null(passage, "solidIndex")});
            Array features;
            for (const auto& face_value : items(at(passage, "faces"))) {
                const Value* bounds = get(fields(face_value), "bounds");
                const Value faces = bounds && py_truthy(*bounds) ? *bounds : array({face_value});
                Array matches;
                for (const auto& bound_value : items(faces)) {
                    const Object& bound = fields(bound_value);
                    const std::string& axis = as_text(at(bound, "axis"));
                    const std::int64_t index = axis == "X" ? 0 : axis == "Y" ? 1 : axis == "Z" ? 2 : -1;
                    if (index < 0) throw std::invalid_argument("substring not found");
                    for (const auto& f_value : items(at(data, "features"))) {
                        const Object& f = fields(f_value);
                        if (!pkc::contains(items(members), at(f, "solidIndex"))) continue;
                        if (!py_equal(at(f, "axis"), integer_value(index))) continue;
                        if (!py_equal(at(f, "side"), at(bound, "direction"))) continue;
                        const Array& planes = items(at(f, "bounds"));
                        if (py_equal(planes[static_cast<std::size_t>(as_int(at(f, "axis")))], at(bound, "plane")))
                            matches.push_back(at(f, "id"));
                    }
                }
                features.push_back(array(std::move(matches)));
            }
            variant.push_back(object({{"kind", text("passage")}, {"features", array(std::move(features))},
                                      {"gates", at(passage, "gates")}, {"resources", at(passage, "blockageCounts")},
                                      {"classification", text("selected_model")}, {"window", window}}));
        }
        passages.push_back(array(std::move(variant)));
    }
    return object({{"events", array(std::move(episodes))}, {"passageAlternatives", array(std::move(passages))},
                   {"optionalGuide", text("uncommitted")}, {"validation", text("fixed_model_evidence")},
                   {"after", after}});
}

}  // namespace pkc
