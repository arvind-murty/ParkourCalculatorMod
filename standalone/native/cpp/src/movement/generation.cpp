// Port of the open generators: movement/normal.normal_choices, movement/stepping.open_stepping_choices and
// movement/ceiling_generation.open_ceiling_choices, with generation.generate_choices.
#include "pkc/movement/generation.hpp"

#include <algorithm>
#include <set>

#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/movement/cases.hpp"
#include "pkc/movement/normal.hpp"
#include "pkc/reachability/service.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

Object label(const std::string& owner, const std::string& role) { return {{"owner", text(owner)}, {"role", text(role)}}; }

double n(const Value& v) { return num(v).value; }

// named_choice on a dict event, rows, finite events and region choices.
std::optional<MovementChoice> named(const GenerationContext& context, const std::string& kind, const Object& event,
                                    bool jumping, const Array& rows, const Array& finite,
                                    std::optional<std::string> support, const Array& regions) {
    return named_choice(context, kind, context.state,
                        case_events_from_value(array({object(with(event, "type", text(kind)))})).front(), jumping,
                        position_constraints_from_value(array(rows)), contact_events_from_value(array(finite)),
                        std::move(support), region_choices_from_value(array(regions)));
}

bool fresh(std::set<std::string>& made, const Array& key) {  // _fresh
    return made.insert(dumps(array(key), true, false)).second;
}

Array concat(Array a, const Array& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

bool enabled(const GenerationContext& context, const std::string& c) { return !context.cases || context.cases->count(c); }

struct Face {
    FaceOption face;
    std::string name;
    std::vector<const Object*> members;
};

std::vector<MovementChoice> normal_choices(const GenerationContext& context) {
    std::vector<MovementChoice> out;
    const SolidOwners& owners = context.gate->owners();
    for (const auto& [jumping, trace] : choice_approaches(context)) {
        for (const auto& face : candidate_faces(context, "landing")) {
            context.budget();
            const std::string name = face_name(face, owners);
            if (!context.feet_allowed(name)) continue;
            const auto t = first_descent(trace, n(face.plane));
            if (!t) continue;
            const auto option = reachable_option(context, face, *t, jumping, label(name, "landing"));
            if (!option) continue;
            const Value event = contact_event(*option, "Y", -1, *t, "landing");
            if (context.state.on_ground && !jumping && n(face.plane) < context.state.position.value &&
                !walk_off_allowed(context, at(fields(event), "boxes"), label(name, "landing")))
                continue;
            ChoiceParts parts;
            VerticalContact landing;
            landing.state = *t + 1;
            landing.direction = -1;
            landing.plane = num(face.plane);
            parts.vertical = {landing};
            parts.finite = contact_events_from_value(array({event}));
            parts.support = name;
            parts.landing_phase = "pre";
            parts.region_choices = region_choices_from_value(array(region_choices(context, event, name)));
            if (auto choice = event_choice(context, "normal", context.state, *t + 1, jumping, std::move(parts)))
                out.push_back(std::move(*choice));
        }
    }
    return out;
}

void flight_cases(const GenerationContext& context, bool jumping, const std::vector<Face>& faces, const Face& lower,
                  const Array& lower_boxes, const Array& lower_rows, const Array& lower_regions, std::int64_t landing,
                  const Value& y_value, std::vector<MovementChoice>& out) {  // _open_flight_cases
    const double y = n(y_value);
    for (const auto& [higher, name, members] : faces) {
        if (n(higher.plane) <= n(lower.face.plane) || !context.feet_allowed(name)) continue;
        const Array boxes = higher.boxes;
        std::set<std::string> made;
        std::map<std::string, bool> sequenced;
        auto sequence_allowed = [&](const Object& event, const Array& rows, std::optional<std::string> support) {
            // The common outcome is the same for every member of the face.
            const std::string key = dumps(array({at(event, "case"), array(rows)}), true, false);
            auto found = sequenced.find(key);
            if (found == sequenced.end()) {
                auto common = named(context, "stepping", event, jumping, rows, {}, support, {});
                found = sequenced.emplace(key, common && is_null(context.gate->sequence(context, *common))).first;
            }
            return found->second;
        };
        auto event_of = [&](const std::string& c, const Value& plane) {
            return Object{{"case", text(c)}, {"input", integer_value(landing)}, {"plane", plane},
                          {"lowerHeight", lower.face.plane}, {"higherHeight", higher.plane}, {"boxes", array(boxes)},
                          {"lowerBoxes", array(lower_boxes)},
                          {"anchors", object({{"landing", integer_value(landing)}, {"hit", integer_value(landing + 1)}})}};
        };
        for (const Object* block : members) {
            const Value& box_value = items(at(fields(at(fields(context.gate->scene()), "collisionScene")), "solids"))
                                         .at(as_int(at(*block, "solidIndex")));
            const Array& box = items(box_value);
            if (!interaction_allowed(context, box_value, landing, label(as_text(at(*block, "id")), "flight case"), jumping))
                continue;
            if (enabled(context, "blip") && 0 <= n(higher.plane) - y && n(higher.plane) - y <= f32(.6)) {
                for (const auto& [top, regions] : top_choices(context, higher, name, landing + 1, "case:region", jumping)) {
                    const Object shared = event_of("blip", higher.plane);
                    std::vector<std::pair<Array, Array>> options;
                    if (sequence_allowed(shared, concat(lower_rows, top), name)) {
                        if (landing) {
                            options = landing_wall_options(context, *block, landing, y);
                        } else {
                            for (const std::string axis : {"X", "Z"})
                                for (std::int64_t sign : {-1, 1})
                                    if (exposed_side(context, *block, axis, sign, y))
                                        options.push_back({{outside_row(box, axis, sign, 0, "blip:wall_avoid")}, {}});
                        }
                    }
                    for (const auto& [rows, finite] : options) {
                        if (!fresh(made, {text("blip"), array(rows), array(finite), array(top)})) continue;
                        if (auto choice = named(context, "stepping", event_of("blip", higher.plane), jumping,
                                                concat(concat(lower_rows, rows), top), finite, name,
                                                concat(lower_regions, regions)))
                            out.push_back(std::move(*choice));
                    }
                }
            }
            for (const std::string c : {"blip_up", "corner_blip_up"}) {
                const double gap = n(higher.plane) - y;
                if (!enabled(context, c) || !(c == "blip_up" ? y > n(higher.plane) : 0 <= gap && gap <= f32(.6))) continue;
                const Object shared = event_of(c, y_value);
                const auto tops = c == "blip_up" ? top_choices(context, higher, name, landing + 1, "case:region", jumping)
                                                 : std::vector<std::pair<Array, Array>>{{{}, {}}};
                bool any = false;
                for (const auto& [top, _] : tops)
                    if (sequence_allowed(shared, concat(lower_rows, top), std::nullopt)) {
                        any = true;
                        break;
                    }
                if (!any) continue;
                std::vector<std::pair<Array, Array>> options;
                if (c == "blip_up") {
                    for (const std::string axis : {"X", "Z"})
                        for (std::int64_t sign : {-1, 1})
                            if (exposed_side(context, *block, axis, sign, y))
                                for (const auto& [top, regions] : tops)
                                    options.push_back(
                                        {concat({outside_row(box, axis, sign, landing, "blip_up:outside")}, top), regions});
                } else {
                    for (std::int64_t xdir : {-1, 1})
                        for (std::int64_t zdir : {-1, 1})
                            if (convex_corner(context, *block, xdir, zdir, y))
                                options.push_back({corner_rows(box, landing, landing + 1, xdir, zdir, higher.boxes), {}});
                }
                for (const auto& [constraint, regions] : options) {
                    if (!fresh(made, {text(c), array(constraint)})) continue;
                    if (auto choice = named(context, "stepping", event_of(c, y_value), jumping,
                                            concat(lower_rows, constraint), {}, std::nullopt,
                                            concat(lower_regions, regions)))
                        out.push_back(std::move(*choice));
                }
            }
        }
    }
}

std::vector<MovementChoice> stepping_choices(const GenerationContext& context) {  // open_stepping_choices
    std::vector<MovementChoice> out;
    const Value& scene = context.gate->scene();
    const Array& solids = items(at(fields(at(fields(scene), "collisionScene")), "solids"));
    const double y0 = context.state.position.value;
    std::vector<Face> faces;
    for (auto& face : candidate_faces(context, "stepping")) {
        Face f{face, face_name(face, context.gate->owners()), {}};
        for (const auto& b : items(at(fields(scene), "blocks")))
            if (std::count(face.members.begin(), face.members.end(), as_int(at(fields(b), "solidIndex"))))
                f.members.push_back(&fields(b));
        faces.push_back(std::move(f));
    }
    if (context.state.on_ground) {
        for (const auto& [face, name, members] : faces) {
            if (!context.feet_allowed(name)) continue;
            const double rise = n(face.plane) - y0;
            std::set<std::string> made;
            for (const Object* block : members) {
                const Value& box_value = solids.at(as_int(at(*block, "solidIndex")));
                const Array& box = items(box_value);
                const std::string id = as_text(at(*block, "id"));
                for (const auto& [c, jumping] : {std::pair<std::string, bool>{"normal_step", false}, {"slab_boost", true}}) {
                    if (!enabled(context, c) || !(0 < rise && rise <= f32(.6)) || (jumping && rise <= f32(.42)) ||
                        (!jumping && !context.run_tick_choices))
                        continue;
                    if (!interaction_allowed(context, box_value, 0, label(id, c), jumping)) continue;
                    for (const std::string axis : {"X", "Z"})
                        for (std::int64_t direction : {-1, 1}) {
                            if (!exposed_side(context, *block, axis, -direction, y0)) continue;
                            for (const auto& [top, regions] : top_choices(context, face, name, 1, "case:region", jumping)) {
                                const Array rows = concat({outside_row(box, axis, -direction, 0)}, top);
                                if (!fresh(made, {text(c), array(rows)})) continue;
                                const Object event{
                                    {"case", text(c)}, {"input", integer_value(0)}, {"plane", face.plane},
                                    {"boxes", array(face.boxes)},
                                    {"anchors", object({{jumping ? "beforeJump" : "beforeStep", integer_value(0)},
                                                        {jumping ? "jump" : "hit", integer_value(1)}})}};
                                if (auto choice = named(context, "stepping", event, jumping, rows, {},
                                                        jumping ? std::nullopt : std::optional<std::string>(name), regions))
                                    out.push_back(std::move(*choice));
                            }
                        }
                }
                if (enabled(context, "corner_jump_cancel") && f32(.42) < rise && rise <= f32(.6) &&
                    interaction_allowed(context, box_value, 0, label(id, "corner_jump_cancel"), true)) {
                    for (std::int64_t xdir : {-1, 1})
                        for (std::int64_t zdir : {-1, 1}) {
                            if (!convex_corner(context, *block, xdir, zdir, y0)) continue;
                            const Array rows = corner_rows(box, 0, 1, xdir, zdir, face.boxes);
                            if (!fresh(made, {text("corner_jump_cancel"), array(rows)})) continue;
                            const Object event{{"case", text("corner_jump_cancel")}, {"input", integer_value(0)},
                                               {"plane", value(context.state.position)}, {"higherHeight", face.plane},
                                               {"boxes", array(face.boxes)},
                                               {"anchors", object({{"beforeJump", integer_value(0)}, {"jump", integer_value(1)}})}};
                            if (auto choice = named(context, "stepping", event, true, rows, {}, std::nullopt, {}))
                                out.push_back(std::move(*choice));
                        }
                }
            }
        }
    }
    for (const auto& [jumping, trace] : choice_approaches(context)) {
        for (const auto& lower : faces) {
            context.budget();
            if (!context.feet_allowed(lower.name)) continue;
            const auto landing = first_descent(trace, n(lower.face.plane));
            if (!landing) continue;
            bool reachable = false;
            for (const Object* b : lower.members)
                if (interaction_allowed(context, solids.at(as_int(at(*b, "solidIndex"))), *landing,
                                        label(as_text(at(*b, "id")), "lower landing"), jumping)) {
                    reachable = true;
                    break;
                }
            if (!reachable) continue;
            const Array lower_boxes = lower.face.boxes;
            if (context.state.on_ground && !jumping && n(lower.face.plane) < y0 &&
                !walk_off_allowed(context, array(lower_boxes), label(lower.name, "lower landing")))
                continue;
            const Value y = value(trace.position[*landing]);
            for (const auto& [lower_rows, lower_regions] :
                 top_choices(context, lower.face, lower.name, *landing, "case:lower_landing", jumping))
                flight_cases(context, jumping, faces, lower, lower_boxes, lower_rows, lower_regions, *landing, y, out);
        }
    }
    return out;
}

std::vector<MovementChoice> ceiling_choices(const GenerationContext& context) {  // open_ceiling_choices
    std::vector<MovementChoice> out;
    const Value& scene = context.gate->scene();
    const Object& s = fields(scene);
    if (const Value* hits = get(s, "ceilingHits"); hits && !py_truthy(*hits)) return out;
    std::map<std::int64_t, std::string> names;  // {solidIndex: id}: the last block on a solid names it
    std::map<std::int64_t, const Object*> blocks;
    for (const auto& b : items(at(s, "blocks"))) {
        names[as_int(at(fields(b), "solidIndex"))] = as_text(at(fields(b), "id"));
        blocks[as_int(at(fields(b), "solidIndex"))] = &fields(b);
    }
    auto owner_of = [&](const FaceOption& option) {
        std::vector<std::string> ids;
        for (auto i : option.members) ids.push_back(names.at(i));
        std::sort(ids.begin(), ids.end());
        std::string joined;
        for (const auto& id : ids) joined += (joined.empty() ? "" : "+") + id;
        return joined;
    };
    const auto undersides = scene_contact_options(scene, context.gate->geometry(), 1, -1);
    for (const auto& option : undersides) {
        context.budget();
        if (!candidate_option(context, option, "ceilingSolids")) continue;
        const double plane = n(option.plane);
        const std::string owner = owner_of(option);
        for (const auto& [jumping, trace] : choice_approaches(context)) {
            std::optional<std::int64_t> hit;
            for (std::size_t t = 0; t < trace.movement.size(); ++t)
                if (trace.movement[t].value > 0 && trace.position[t].value <= plane && plane < trace.position[t + 1].value) {
                    hit = static_cast<std::int64_t>(t);
                    break;
                }
            if (!hit) continue;
            const auto reached = reachable_option(context, option, *hit, jumping, label(owner, "ceiling"));
            if (!reached || behind_decision(context, reached->regions, *hit, jumping, label(owner, "ceiling"))) continue;
            const Value event = contact_event(*reached, "Y", 1, *hit, "ceiling", {{"blockId", text(owner)}});
            ChoiceParts parts;
            VerticalContact contact;
            contact.state = *hit + 1;
            contact.plane = num(option.plane);
            contact.direction = 1;
            parts.vertical = {contact};
            parts.finite = contact_events_from_value(array({event}));
            parts.region_choices = region_choices_from_value(array(region_choices(context, event, owner)));
            if (auto choice = event_choice(context, "ceiling", context.state, *hit + 1, jumping, std::move(parts)))
                out.push_back(std::move(*choice));
        }
    }
    if (!context.state.on_ground) return out;
    // grind_options(context, feet, allowed)
    const Num feet = context.state.position;
    for (const auto& underside : undersides) {
        context.budget();
        if (!candidate_option(context, underside, "ceilingSolids")) continue;
        const Num clearance = sub(num(underside.plane), feet);
        const std::string owner = owner_of(underside);
        if (!(0 <= clearance.value && clearance.value < f32(.42))) continue;
        bool allowed = false;
        for (const auto& box : underside.boxes)
            if (interaction_allowed(context, box, 0, label(owner, "grind"), true)) {
                allowed = true;
                break;
            }
        if (!allowed) continue;
        const auto option = reachable_option(context, underside, 1, true, label(owner, "grind"));
        if (!option || behind_decision(context, option->regions, 1, true, label(owner, "grind"))) continue;
        const Array bounding = items(bounding_region(option->regions));
        Array under;
        for (const auto& r : finite_rows(1, bounding, {{"t1", integer_value(1)}}))
            under.push_back(object(with(fields(r), "name", text("case:finite"))));
        Array regions;
        if (option->regions.size() > 1 && context.region_items) {
            Array options;
            for (const auto& region : option->regions) {
                Array rows;
                for (const auto& r : finite_rows(1, items(region), {{"leg", integer_value(0)}, {"offset", integer_value(1)}}))
                    rows.push_back(object(with(fields(r), "name", text("case:finite"))));
                options.push_back(array(std::move(rows)));
            }
            regions.push_back(object({{"offset", integer_value(1)}, {"surface", text(option->surface)},
                                      {"owner", text(owner)}, {"options", array(std::move(options))}}));
        }
        std::set<std::tuple<std::string, std::int64_t, double>> sides;
        for (auto index : option->members) {
            const Array& box = items(items(at(fields(at(s, "collisionScene")), "solids")).at(index));
            for (const std::string axis : {"X", "Z"})
                for (std::int64_t sign : {-1, 1}) {
                    if (!exposed_side(context, *blocks.at(index), axis, sign, n(option->plane)) ||
                        entry_backward(context, axis, sign, bounding, label(owner, "grind")))
                        continue;
                    const Value row = outside_row(box, axis, sign, 0, "grind:outside");
                    if (!sides.insert({axis, sign, n(at(fields(row), "rhs"))}).second) continue;
                    const Object event{{"case", text("grind")}, {"input", integer_value(0)}, {"plane", value(feet)},
                                       {"clearance", value(clearance)}, {"boxes", array(option->boxes)},
                                       {"anchors", object({{"beforeJump", integer_value(0)}, {"jump", integer_value(1)}})}};
                    if (auto choice = named(context, "ceiling", event, true, concat({row}, under), {}, std::nullopt, regions))
                        out.push_back(std::move(*choice));
                }
        }
    }
    return out;
}

}  // namespace

std::vector<MovementChoice> generate_choices(const std::string& kind, const GenerationContext& context) {
    if (kind == "normal") return normal_choices(context);
    if (kind == "stepping") return stepping_choices(context);
    if (kind == "ceiling") return ceiling_choices(context);
    fail("Unsupported movement generator: " + kind);
}

}  // namespace pkc
