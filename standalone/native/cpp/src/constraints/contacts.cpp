// Port of constraints/contacts.py's run-off faces.
#include "pkc/constraints/contacts.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../schema.hpp"
#include "pkc/envelopes.hpp"
#include "pkc/vertical.hpp"
#include "pkc/scene/geometry.hpp"
#include "pkc/scene/route.hpp"

namespace pkc {
namespace {

Box box_of(const Value& value) {
    const Array& b = items(value);
    Box box;
    for (std::size_t i = 0; i < 6; ++i) box[i] = num(b.at(i));
    return box;
}

// block_interaction(envelopes, box, tick, phases)['ruledOut'] for a collision box.
bool ruled_out(const Value& envelopes, const Value& solid, std::int64_t tick, const std::vector<const char*>& phases) {
    Array bounds;
    for (const Num& n : expanded_box(box_of(solid))) bounds.push_back(value(n));
    Array alternatives;
    for (const char* phase : phases)
        alternatives.push_back(object({{"regions", array({object({{"tick", integer_value(tick)},
                                                                    {"phase", text(phase)},
                                                                    {"bounds", array(bounds)},
                                                                    {"interior", boolean(false)}})})}}));
    return py_truthy(at(fields(interaction_reachability(envelopes, object({{"alternatives", array(alternatives)}}))),
                        "ruledOut"));
}

double n(const Value& v) { return num(v).value; }

Array bounding(const std::vector<Array>& rs) {
    Num x0 = num(rs[0][0]), z0 = num(rs[0][1]), x1 = num(rs[0][2]), z1 = num(rs[0][3]);
    for (const auto& r : rs) {  // min()/max() keep the first of equal values
        if (n(r[0]) < x0.value) x0 = num(r[0]);
        if (n(r[1]) < z0.value) z0 = num(r[1]);
        if (n(r[2]) > x1.value) x1 = num(r[2]);
        if (n(r[3]) > z1.value) z1 = num(r[3]);
    }
    return {value(x0), value(z0), value(x1), value(z1)};
}

}  // namespace

Array exposed_tops(const SceneGeometry& geometry, const Object& block) {
    Array out;
    const Value options = geometry.contact_options(1, 1, real(0.), {as_int(at(block, "solidIndex"))});
    for (const auto& row : items(options))
        for (const auto& r : items(items(row)[3])) out.push_back(r);
    return out;
}

Array avoid_contact_rows(std::int64_t leg, std::int64_t offset, const std::string& reason, const std::string& block_id,
                         const Array& region, double x, double z) {
    const double clearance = kSolverMargin;
    std::vector<std::pair<double, Value>> rows;
    const std::tuple<const char*, double, double, double> axes[] = {{"X", x, n(region[0]), n(region[2])},
                                                                    {"Z", z, n(region[1]), n(region[3])}};
    for (const auto& [axis, coordinate, lo, hi] : axes)
        for (const auto& [cmp, edge] : {std::pair<const char*, double>{"LE", lo - clearance}, {"GE", hi + clearance}})
            rows.emplace_back(std::fabs(coordinate - edge),
                              object({{"leg", integer_value(leg)}, {"offset", integer_value(offset)},
                                      {"mode", text(axis)}, {"cmp", text(cmp)}, {"rhs", real(edge)},
                                      {"name", text(reason + ":" + block_id)}}));
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Array out;
    for (auto& [_, row] : rows) out.push_back(std::move(row));
    return out;
}

Array floor_checks(const Value& scene_value, const std::vector<std::string>& block_ids, const Array& profiles,
                   const Value* envelopes) {
    const Object& scene = fields(scene_value);
    const Array& blocks = items(at(scene, "blocks"));
    const Value& solids_value = at(fields(at(scene, "collisionScene")), "solids");
    const Array& solids = items(solids_value);
    std::optional<SceneGeometry> geometry;
    auto tops = [&](const Object& block) {
        if (!geometry) geometry.emplace(solids_value);
        return exposed_tops(*geometry, block);
    };
    auto block_by_id = [&](const std::string& id) -> const Object& {
        const Object* found = nullptr;  // {b['id']: b}: the last block with the id
        for (const auto& b : blocks)
            if (as_text(at(fields(b), "id")) == id) found = &fields(b);
        if (!found) fail("KeyError: '" + id + "'");
        return *found;
    };
    // block_interaction is pure, so the cheap height and step tests run before it; the checks are the same.
    Array checks;
    for (std::size_t leg = 0; leg < profiles.size(); ++leg) {
        const Object& profile = fields(profiles[leg]);
        const std::string& target_id = block_ids.at(leg + 1);
        const double target_top = n(at(block_by_id(target_id), "top"));
        const std::int64_t duration = as_int(at(profile, "duration"));
        auto listed_or = [&](const char* key, std::size_t size, bool first) {
            if (const Value* list = get(profile, key)) return items(*list);
            Array out(size, boolean(false));
            if (first && size) out[0] = boolean(true);
            return out;
        };
        const Array jump = listed_or("jump", duration, true);
        const Array sneak = listed_or("sneak", duration, false);
        const Array ladder = listed_or("ladderTicks", duration + 1, false);
        const Value* threshold_value = get(profile, "threshold");
        const Value threshold = threshold_value ? *threshold_value : real(.005);
        Array jump_velocity;
        std::vector<std::optional<Num>> jumps;
        std::vector<bool> sneaking;
        for (const auto& j : jump) {
            jump_velocity.push_back(py_truthy(j) ? real(kJumpVelocity) : kNull);
            jumps.push_back(py_truthy(j) ? std::optional<Num>(flt(kJumpVelocity)) : std::nullopt);
        }
        for (const auto& s : sneak) sneaking.push_back(py_truthy(s));
        const Object config{{"threshold", threshold}, {"jumpVelocity", array(jump_velocity)}, {"sneak", array(sneak)}};
        const Array& position = items(at(profile, "position"));
        const Array& velocity = items(at(profile, "velocity"));
        const Value* contact_events = get(profile, "contactEvents");
        const Value* case_events = get(profile, "caseEvents");
        auto emit = [&](std::int64_t offset, const Value& block, const char* reason) {
            for (const auto& region : tops(fields(block)))
                checks.push_back(object({{"leg", integer_value(static_cast<std::int64_t>(leg))},
                                         {"offset", integer_value(offset)}, {"block", block}, {"region", region},
                                         {"reason", text(reason)}}));
        };
        for (std::int64_t offset = 0; offset < duration; ++offset) {
            const double y = n(position.at(offset)), next_y = n(position.at(offset + 1));
            if (n(vertical_move(config, offset, velocity.at(offset), py_truthy(ladder.at(offset)))) >= 0) continue;
            for (const auto& b : blocks) {
                const Object& block = fields(b);
                const Value& top = at(block, "top");
                const Value& solid = solids.at(as_int(at(block, "solidIndex")));
                if (!(next_y <= n(top) && n(top) <= y) || (offset + 1 == duration && n(top) == target_top)) continue;
                bool planned_floor = false;
                if (contact_events)
                    for (const auto& e : items(*contact_events)) {
                        const Object& event = fields(e);
                        const Value& role = get_or_null(event, "role");
                        planned_floor |= as_int(at(event, "offset")) == offset &&
                                         (py_equal(role, text("landing")) || py_equal(role, text("step"))) &&
                                         py_equal(at(event, "plane"), top) && contains(items(at(event, "boxes")), solid);
                    }
                if (planned_floor || (envelopes && ruled_out(*envelopes, solid, offset, {"Y"}))) continue;
                emit(offset, b, "earlier_floor");
            }
        }
        const Value& kind = get_or_null(profile, "profileKind");
        for (const auto& b : blocks) {
            const Object& block = fields(b);
            const Value& solid = solids.at(as_int(at(block, "solidIndex")));
            bool selected_step = (py_equal(kind, text("stair")) || py_equal(kind, text("step"))) &&
                                 as_text(at(block, "id")) == target_id;
            if (case_events)
                for (const auto& e : items(*case_events)) {
                    const Object& event = fields(e);
                    const Value& movement_case = at(event, "case");
                    selected_step |= py_equal(at(event, "input"), integer_value(0)) &&
                                     (py_equal(movement_case, text("normal_step")) ||
                                      py_equal(movement_case, text("slab_boost"))) &&
                                     contains(items(at(event, "boxes")), solid);
                }
            if (selected_step) continue;
            VerticalContactRow row{1, -1, num(at(block, "top"))};
            row.step = true;
            Array failures;
            vertical_step({row}, jumps, sneaking, num(threshold), 0, num(position.at(0)), num(velocity.at(0)), false,
                          true, 0, &failures);
            if (!failures.empty() || (envelopes && ruled_out(*envelopes, solid, 0, {"Y", "X", "Z"}))) continue;
            emit(1, b, "unplanned_stair");
        }
    }
    return checks;
}

std::optional<Value> walk_off_faces(const Value& scene_value, const SceneGeometry& geometry, const Program& program,
                                    std::int64_t cut, const Value& envelopes, const Value& landing_boxes) {
    const Profile& profile = program.profile;
    if (!profile.grounded.at(cut) || profile.ladder_ticks.at(cut)) return std::nullopt;
    const Object& scene = fields(scene_value);
    const double y0 = profile.position.at(cut).value;
    const Array& solids = items(at(fields(at(scene, "collisionScene")), "solids"));
    std::vector<const Object*> feet;
    for (const auto& b : items(at(scene, "blocks"))) {
        const Object& block = fields(b);
        if (std::fabs(n(at(block, "top")) - y0) < 1e-7 &&
            !ruled_out(envelopes, solids.at(as_int(at(block, "solidIndex"))), cut, {"Y"}))
            feet.push_back(&block);
    }
    std::vector<std::pair<const Object*, Array>> regions;
    for (const Object* b : feet)
        for (const auto& r : exposed_tops(geometry, *b)) regions.emplace_back(b, items(r));
    std::vector<std::size_t> parent(regions.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    auto root = [&](std::size_t i) {
        while (parent[i] != i) i = parent[i];
        return i;
    };
    for (std::size_t i = 0; i < regions.size(); ++i)
        for (std::size_t j = 0; j < i; ++j) {
            const Array &a = regions[i].second, &b = regions[j].second;
            const double dx = std::min(n(a[2]), n(b[2])) - std::max(n(a[0]), n(b[0]));
            const double dz = std::min(n(a[3]), n(b[3])) - std::max(n(a[1]), n(b[1]));
            if (dx >= -1e-9 && dz >= -1e-9 && std::max(dx, dz) > 1e-9) parent[root(i)] = root(j);
        }
    std::vector<std::pair<std::size_t, std::vector<std::size_t>>> groups;  // dict insertion order
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const std::size_t r = root(i);
        auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == r; });
        if (found == groups.end()) {
            groups.push_back({r, {i}});
        } else {
            found->second.push_back(i);
        }
    }
    std::set<std::int64_t> launch;
    if (cut) {
        for (const auto& e : profile.contact_events)
            if (e.offset == cut - 1 && (e.role == "landing" || e.role == "step") && e.boxes)
                for (std::size_t i = 0; i < solids.size(); ++i) {
                    const Array& solid = items(solids[i]);
                    for (const auto& box : *e.boxes) {
                        bool equal = true;
                        for (std::size_t k = 0; k < 6; ++k) equal = equal && n(solid[k]) == box[k].value;
                        if (equal) launch.insert(static_cast<std::int64_t>(i));
                    }
                }
    } else {
        const Array& position = items(at(fields(at(scene, "initial")), "position"));
        for (const Object* b : feet)
            if (supports(*b, n(position[0]), n(position[2]))) launch.insert(as_int(at(*b, "solidIndex")));
        std::vector<Array> start;
        for (const auto& [b, r] : regions)
            if (launch.count(as_int(at(*b, "solidIndex")))) start.push_back(r);
        if (!start.empty()) {
            Array sorted;
            for (auto s : launch) sorted.push_back(integer_value(s));
            return object({{"solids", array(sorted)}, {"region", array(bounding(start))},
                           {"diagonal", boolean(false)}, {"options", array({})}});
        }
    }
    std::vector<const std::vector<std::size_t>*> held;
    for (const auto& [_, g] : groups) {
        bool holds = launch.empty();
        for (auto i : g) holds = holds || launch.count(as_int(at(*regions[i].first, "solidIndex")));
        if (holds) held.push_back(&g);
    }
    if (held.size() != 1) return std::nullopt;
    std::vector<Array> pieces;
    std::map<std::string, const Object*> blocks;  // sorted ids, as '+'.join(sorted(blocks)) reads them
    for (auto i : *held[0]) {
        pieces.push_back(regions[i].second);
        blocks.emplace(as_text(at(*regions[i].first, "id")), regions[i].first);
    }
    const Array region = bounding(pieces);
    const double r0 = n(region[0]), r1 = n(region[1]), r2 = n(region[2]), r3 = n(region[3]);
    std::vector<NumBox> free_input;
    for (const auto& p : pieces) free_input.push_back({num(p[0]), flt(0.), num(p[1]), num(p[2]), flt(1.), num(p[3])});
    for (auto i : geometry.near(r0, r1, r2, r3)) {
        const NumBox& b = geometry.boxes()[i];
        if (b[1].value < y0 && y0 < b[4].value + 1e-6) free_input.push_back(b);
    }
    for (const Rect& f : free_rectangles(free_input))
        if (std::min(f[2].value, r2) - std::max(f[0].value, r0) > 1e-9 &&
            std::min(f[3].value, r3) - std::max(f[1].value, r1) > 1e-9)
            return std::nullopt;
    std::set<std::int64_t> members;
    for (const auto& [_, b] : blocks) members.insert(as_int(at(*b, "solidIndex")));
    const Array& states = items(at(fields(envelopes), "states"));
    const Object& state = fields(states.at(cut));
    const Array &sx = items(at(state, "X")), &sz = items(at(state, "Z"));
    std::string id;
    for (const auto& [name, _] : blocks) id += (id.empty() ? "" : "+") + name;
    const Array rows = avoid_contact_rows(0, cut, "walk_off", id, region, (0. + n(sx[0]) + n(sx[1])) / 2,
                                          (0. + n(sz[0]) + n(sz[1])) / 2);
    std::map<std::pair<std::string, int>, Value> face;
    for (const auto& row : rows) {
        const Object& r = fields(row);
        face[{as_text(at(r, "mode")), as_text(at(r, "cmp")) == "GE" ? 1 : -1}] = row;
    }
    double low[2] = {INFINITY, INFINITY}, high[2] = {-INFINITY, -INFINITY};
    for (auto i : members) {
        const Array& box = items(solids.at(i));
        low[0] = std::min(low[0], n(box[0]));
        low[1] = std::min(low[1], n(box[2]));
        high[0] = std::max(high[0], n(box[3]));
        high[1] = std::max(high[1], n(box[5]));
    }
    int beyond[2] = {0, 0};
    double displacement[2] = {0., 0.};
    if (!is_null(landing_boxes) && !items(landing_boxes).empty())
        for (int k = 0; k < 2; ++k) {
            double lo = INFINITY, hi = -INFINITY;
            for (const auto& b : items(landing_boxes)) {
                lo = std::min(lo, n(items(b)[2 * k]));
                hi = std::max(hi, n(items(b)[2 * k + 3]));
            }
            beyond[k] = lo >= high[k] ? 1 : hi <= low[k] ? -1 : 0;
            displacement[k] = (lo + hi) / 2 - (low[k] + high[k]) / 2;
        }
    using Face = std::pair<std::string, int>;
    const char* xz[2] = {"X", "Z"};
    std::vector<std::vector<Face>> sets;
    if (beyond[0] == 0 && beyond[1] == 0) {
        std::vector<Face> all;
        for (const auto& row : rows) {  // list(face): the rows' order
            const Object& r = fields(row);
            Face f{as_text(at(r, "mode")), as_text(at(r, "cmp")) == "GE" ? 1 : -1};
            if (std::find(all.begin(), all.end(), f) == all.end()) all.push_back(f);
        }
        sets = {all};
    } else if (beyond[0] == 0 || beyond[1] == 0) {
        const int k = beyond[0] ? 0 : 1;
        const int toward = displacement[1 - k] > 0 ? 1 : -1;
        sets = {{{xz[k], beyond[k]}, {xz[1 - k], toward}, {xz[1 - k], -toward}}, {{xz[k], -beyond[k]}}};
    } else {
        const int order[2] = {std::fabs(displacement[0]) >= std::fabs(displacement[1]) ? 0 : 1,
                              std::fabs(displacement[0]) >= std::fabs(displacement[1]) ? 1 : 0};
        sets = {{{xz[order[0]], beyond[order[0]]}, {xz[order[1]], beyond[order[1]]}},
                {{xz[order[0]], -beyond[order[0]]}, {xz[order[1]], -beyond[order[1]]}}};
    }
    std::vector<std::int64_t> all_solids(solids.size());
    for (std::size_t i = 0; i < solids.size(); ++i) all_solids[i] = static_cast<std::int64_t>(i);
    std::vector<std::int64_t> cluster;
    for (auto& group : geometry.cluster_groups(all_solids))
        if (std::any_of(group.begin(), group.end(), [&](std::int64_t m) { return members.count(m); })) {
            cluster = std::move(group);
            break;
        }
    std::vector<std::int64_t> active;  // geometry.active(cluster, y0 - 1e-6, y0)
    for (std::size_t k = 0; k < cluster.size(); ++k) {
        const NumBox& b = geometry.boxes()[cluster[k]];
        if (b[1].value < y0 && y0 - 1e-6 < b[4].value) active.push_back(static_cast<std::int64_t>(k));
    }
    std::map<Face, double> edge{{{"X", -1}, r0}, {{"X", 1}, r2}, {{"Z", -1}, r1}, {{"Z", 1}, r3}};
    std::set<Face> open_faces;
    const Value sides = geometry.sides(cluster, active);
    for (const auto& side : items(sides)) {
        const Array& bounds = items(items(side)[0]);
        const Array& owners = items(items(side)[1]);
        for (std::size_t b = 0; b < bounds.size() && b < owners.size(); ++b) {
            const Array& bound = items(bounds[b]);
            Face f{as_text(bound[0]), static_cast<int>(as_int(bound[1]))};
            auto e = edge.find(f);
            if (e == edge.end() || std::fabs(n(bound[2]) - e->second) >= 1e-9) continue;
            bool owned = false;
            for (const auto& o : items(owners[b])) owned = owned || members.count(as_int(o));
            if (owned) open_faces.insert(f);
        }
    }
    std::vector<Face> chosen;
    for (const auto& faces : sets) {
        std::vector<Face> open;
        for (const auto& f : faces)
            if (open_faces.count(f)) open.push_back(f);
        if (!open.empty()) {
            chosen = std::move(open);
            break;
        }
    }
    Array options;
    for (const auto& f : chosen)
        if (!py_truthy(at(fields(interaction_reachability(envelopes, array({face.at(f)}))), "ruledOut")))
            options.push_back(face.at(f));
    Array sorted;
    for (auto s : members) sorted.push_back(integer_value(s));
    return object({{"solids", array(sorted)}, {"region", array(region)},
                   {"diagonal", boolean(beyond[0] != 0 && beyond[1] != 0)}, {"options", array(options)}});
}

}  // namespace pkc
