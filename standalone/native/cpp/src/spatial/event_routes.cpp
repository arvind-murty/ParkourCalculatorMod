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
#include "pkc/movement/settings.hpp"
#include "pkc/scene/geometry.hpp"
#include "pkc/spatial/cells.hpp"
#include "pkc/spatial/regions.hpp"

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

NumBox num_box(const Value& v) {
    NumBox b;
    for (std::size_t i = 0; i < 6; ++i) b[i] = num(items(v)[i]);
    return b;
}

Value box_value(const NumBox& b) {
    Array out;
    for (const auto& n : b) out.push_back(value(n));
    return array(std::move(out));
}

bool flat(const NumBox& z) { return !(z[0].value < z[3].value && z[2].value < z[5].value); }

// surface_groups' connected(a, b): equal heights and a common positive-area body-overlap region.
bool connected(const Object& a, const Object& b) {
    if (num(at(a, "height")).value != num(at(b, "height")).value) return false;
    for (const auto& x_value : items(at(a, "cover")))
        for (const auto& y_value : items(at(b, "cover"))) {
            const NumBox x = num_box(x_value), y = num_box(y_value);
            if (flat(x) || flat(y)) continue;
            double lo[3], hi[3];
            bool overlap = true;
            for (int i = 0; i < 3; ++i) {
                lo[i] = std::max(x[i].value, y[i].value);
                hi[i] = std::min(x[i + 3].value, y[i + 3].value);
                overlap = overlap && lo[i] <= hi[i];
            }
            if (overlap && lo[0] < hi[0] && lo[2] < hi[2]) return true;
        }
    return false;
}

}  // namespace

Value surface_groups(const Value& supports) {
    // Rows [key, owner, height, cover, cells, solids, patches] as SupportComponent fields.
    std::map<std::string, Object> pending;
    for (const auto& row_value : items(supports)) {
        const Array& r = items(row_value);
        pending[as_text(r[0])] = Object{{"key", r[0]}, {"owner", r[1]}, {"height", r[2]}, {"cover", r[3]},
                                        {"cells", r[4]}, {"solids", r[5]}};
    }
    std::vector<std::pair<std::string, Value>> result;
    while (!pending.empty()) {
        Object first = pending.begin()->second;
        pending.erase(pending.begin());
        std::vector<Object> members{first}, queue{first};
        while (!queue.empty()) {
            const Object current = queue.back();
            queue.pop_back();
            std::vector<std::string> adjacent;  // pending's keys, already sorted
            for (const auto& [k, s] : pending)
                if (connected(current, s)) adjacent.push_back(k);
            for (const auto& k : adjacent) {
                Object other = pending.at(k);
                pending.erase(k);
                members.push_back(other);
                queue.push_back(other);
            }
        }
        std::vector<NumBox> boxes;
        std::set<std::string> owners, keys;
        std::set<std::int64_t> solids;
        for (const auto& m : members) {
            for (const auto& b : items(at(m, "cover"))) boxes.push_back(num_box(b));
            owners.insert(as_text(at(m, "owner")));
            keys.insert(as_text(at(m, "key")));
            for (const auto& i : items(at(m, "solids"))) solids.insert(as_int(i));
        }
        Array cover, owner_list, key_list, solid_list;
        for (const auto& b : canonical_cover(boxes)) cover.push_back(box_value(b));
        for (const auto& o : owners) owner_list.push_back(text(o));
        for (const auto& k : keys) key_list.push_back(text(k));
        for (std::int64_t i : solids) solid_list.push_back(integer_value(i));
        const Value& height = at(first, "height");
        const std::string id =
            "surface:" + geometry_key(array({height, array(cover), array(owner_list)}));
        result.emplace_back(id, object({{"id", text(id)}, {"height", height}, {"cover", array(std::move(cover))},
                                        {"components", array(std::move(key_list))}, {"owners", array(std::move(owner_list))},
                                        {"solids", array(std::move(solid_list))}}));
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Array out;
    for (auto& [id, group] : result) out.push_back(std::move(group));
    return array(std::move(out));
}

Value build_route_domain(const Value& scene_value, const Value& task, std::optional<double> seconds,
                         std::int64_t max_cells, const std::vector<std::string>& registered) {
    const Object& scene = fields(scene_value);
    const Value d = decompose(scene_value, max_cells, seconds, true);
    const Value supports = support_components(d);
    const Value graph = build_geometry_graph(scene_value, d, supports, task);
    const Object& g = fields(graph);
    // RouteGraph.to_dict(): its regions, transitions and supports as their dataclasses' dicts.
    Array regions, transitions, support_dicts;
    for (const auto& row : items(at(g, "regions"))) {
        const Array& r = items(row);
        regions.push_back(object({{"key", r[0]}, {"kind", r[1]}, {"cover", r[2]}, {"cells", r[3]}, {"owners", r[4]},
                                  {"solids", r[5]}, {"predicate", Value{nullptr}}, {"transit_key", r[6]}}));
    }
    for (const auto& row : items(at(g, "transitions"))) {
        const Array& r = items(row);
        transitions.push_back(object({{"key", r[0]}, {"source", r[1]}, {"target", r[2]}, {"kind", r[3]}, {"cover", r[4]},
                                      {"portals", r[5]}, {"patches", r[6]}, {"movement_families", r[7]},
                                      {"capability", text("not_ruled_out")}, {"evidence", text("geometry_only")}}));
    }
    for (const auto& row : items(supports)) {
        const Array& r = items(row);
        support_dicts.push_back(object({{"key", r[0]}, {"owner", r[1]}, {"height", r[2]}, {"cover", r[3]},
                                        {"cells", r[4]}, {"solids", r[5]}, {"patches", r[6]}}));
    }
    const Value geometry = object({{"schema", integer_value(1)}, {"context", at(g, "context")},
                                   {"regions", array(std::move(regions))}, {"transitions", array(std::move(transitions))},
                                   {"supports", array(std::move(support_dicts))}, {"starts", at(g, "starts")},
                                   {"goals", at(g, "goals")}, {"transfers", object({})},
                                   {"flight_proposals", object({})}});
    // Each solid's six expanded faces, owned by the last block on the solid.
    std::map<std::int64_t, Value> owners;
    for (const auto& b : items(at(scene, "blocks"))) {
        const Value* source = get(fields(b), "sourceBlock");
        owners[as_int(at(fields(b), "solidIndex"))] = source ? *source : at(fields(b), "id");
    }
    std::vector<std::pair<std::string, Value>> features;
    const Array& solids = items(at(fields(at(scene, "collisionScene")), "solids"));
    for (std::size_t i = 0; i < solids.size(); ++i) {
        const auto box = expanded_box(num_box(solids[i]));
        for (int axis = 0; axis < 3; ++axis)
            for (int side : {-1, 1}) {
                const Num plane = box[static_cast<std::size_t>(side < 0 ? axis : axis + 3)];
                Array bounds;
                for (const auto& n : box) bounds.push_back(value(n));
                bounds[static_cast<std::size_t>(axis)] = bounds[static_cast<std::size_t>(axis + 3)] = value(plane);
                const Value& owner = owners.at(static_cast<std::int64_t>(i));
                const Value bounds_value = array(std::move(bounds));
                const std::string id = "face:" + geometry_key(object({{"owner", owner}, {"axis", integer_value(axis)},
                                                                      {"side", integer_value(side)},
                                                                      {"bounds", bounds_value}}));
                features.emplace_back(id, object({{"id", text(id)}, {"owner", owner}, {"axis", integer_value(axis)},
                                                  {"side", integer_value(side)}, {"bounds", bounds_value},
                                                  {"solidIndex", integer_value(static_cast<std::int64_t>(i))}}));
            }
    }
    std::stable_sort(features.begin(), features.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Array feature_list;
    for (auto& [id, f] : features) feature_list.push_back(std::move(f));
    const Object cfg = fields(movement_settings(scene_value, registered));
    Object input_domain;
    for (const auto& [k, v] : cfg)
        if (k != "maxPrefixes" && k != "maxQueuedPrefixes" && k != "maxChoicesPerType" && k != "expansionsPerProposal")
            input_domain.emplace_back(k, v);
    Object scene_copy;
    for (const auto& [k, v] : scene)
        if (k != "movementSearch") scene_copy.emplace_back(k, v);
    Object data{{"schema", integer_value(1)},
                {"scene", object(std::move(scene_copy))},
                {"body", object({{"halfWidth", real(static_cast<double>(static_cast<float>(0.6)) / 2)},
                                 {"height", real(static_cast<double>(static_cast<float>(1.8)))}})},
                {"task", task},
                {"inputDomain", object(std::move(input_domain))},
                {"mechanics", at(cfg, "types")},
                {"geometry", geometry},
                {"surfaces", surface_groups(supports)},
                {"features", array(std::move(feature_list))},
                {"coverage", object({{"local", text("complete")}, {"enclosure", text("unknown")},
                                     {"outsideAttachment", text("unresolved")}, {"bounds", at(fields(d), "bounds")}})}};
    data.emplace_back("domainId", text(geometry_key(object(data))));
    return loads(dumps(object(std::move(data)), true, true));
}

Value build_event_graph(const Value& data_value) {
    const Object& data = fields(data_value);
    const Object& raw = fields(at(data, "geometry"));
    const Object& scene = fields(at(data, "scene"));
    const Value task = py_truthy(at(data, "task"))
                           ? at(data, "task")
                           : object({{"kind", text("landing")}, {"block", at(scene, "goalBlock")}});
    const Object& t = fields(task);
    std::vector<std::pair<std::string, Value>> events;
    for (const auto& surface_value : items(at(data, "surfaces"))) {
        const Object& surface = fields(surface_value);
        std::vector<std::string> names;
        for (const auto& b : items(at(scene, "blocks")))
            if (contains(items(at(surface, "solids")), at(fields(b), "solidIndex"))) names.push_back(as_text(at(fields(b), "id")));
        std::sort(names.begin(), names.end());
        if (as_text(at(t, "kind")) == "landing" && names.size() == 1 && names[0] == as_text(at(t, "block")))
            // This exact contact already is the task; do not score it twice under a generic visit and an identical
            // terminal request.
            continue;
        if (names.empty()) throw std::out_of_range("list index out of range");
        Array blocks;
        for (const auto& n : names) blocks.push_back(text(n));
        const std::string id = "visit:" + as_text(at(surface, "id"));
        events.emplace_back(id, object({{"id", text(id)}, {"kind", text("support_visit")}, {"surface", at(surface, "id")},
                                        {"attachments", at(surface, "components")}, {"cover", at(surface, "cover")},
                                        {"goal", object({{"kind", text("landing")}, {"block", blocks[0]},
                                                         {"blocks", array(blocks)}, {"surface", at(surface, "id")}})},
                                        {"terminal", boolean(false)}}));
    }
    // Keep an explicit owner-specific task even on a shared coplanar surface.
    const std::string finish = "finish:" + geometry_key(task);
    events.emplace_back(finish, object({{"id", text(finish)}, {"kind", text("finish")}, {"attachments", at(raw, "goals")},
                                        {"cover", array({})}, {"goal", task}, {"terminal", boolean(true)}}));
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Array listed;
    for (auto& [id, e] : events) listed.push_back(std::move(e));
    Object result{{"schema", integer_value(1)}, {"domainId", at(data, "domainId")}, {"events", array(std::move(listed))},
                  {"backingContext", at(raw, "context")},
                  {"outsideAttachment", at(fields(at(data, "coverage")), "outsideAttachment")}};
    result.emplace_back("id", text(geometry_key(object(result))));
    return object(std::move(result));
}

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
