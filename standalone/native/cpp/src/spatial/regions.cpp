// Port of spatial/regions.geometry_key and canonical_cover.
#include "pkc/spatial/regions.hpp"

#include <stdexcept>
#include <tuple>

#include "../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "py_order.hpp"

namespace pkc {
namespace {

const double kMinFreeWidth = .012;  // scene/geometry.MIN_FREE_WIDTH
bool thin(double width) { return width < kMinFreeWidth / 2; }

}  // namespace

std::string geometry_key(const Value& value) { return sha256_hex(dumps(value, true, true)); }

std::vector<NumBox> canonical_cover(const std::vector<NumBox>& input) {
    const std::vector<NumBox> boxes = sorted_set(input);
    if (boxes.empty()) return {};
    const Num height = boxes[0][1];
    for (const auto& b : boxes)
        if (b[1].value != height.value || b[4].value != height.value)
            fail("A support cover must lie on one horizontal plane");
    std::vector<NumBox> areas;
    for (const auto& b : boxes)
        if (b[0].value < b[3].value && b[2].value < b[5].value) areas.push_back(b);
    std::vector<Num> edge_values;
    for (const auto& b : areas) {
        edge_values.push_back(b[0]);
        edge_values.push_back(b[3]);
    }
    const std::vector<Num> edges = sorted_set(edge_values);
    using Interval = std::array<Num, 2>;
    struct Strip {
        Num x, xx;
        std::vector<Interval> coverage;
    };
    auto same = [](const std::vector<Interval>& a, const std::vector<Interval>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i][0].value != b[i][0].value || a[i][1].value != b[i][1].value) return false;
        return true;
    };
    std::vector<Strip> strips;
    for (std::size_t k = 0; k + 1 < edges.size(); ++k) {
        const Num x = edges[k], xx = edges[k + 1];
        std::vector<Interval> intervals;
        for (const auto& b : areas)
            if (b[0].value <= x.value && xx.value <= b[3].value) intervals.push_back({b[2], b[5]});
        std::stable_sort(intervals.begin(), intervals.end(), value_less<2>);
        std::vector<Interval> merged;
        for (const auto& [lo, hi] : intervals) {
            if (!merged.empty() && lo.value <= merged.back()[1].value) {
                const Num prior = merged.back()[1];
                merged.back()[1] = prior.value > hi.value ? prior : hi;  // max(hi, prior)
            } else {
                merged.push_back({lo, hi});
            }
        }
        if (!strips.empty() && strips.back().xx.value == x.value && same(strips.back().coverage, merged)) {
            strips.back().xx = xx;
        } else if (!merged.empty()) {
            strips.push_back({x, xx, merged});
        }
    }
    std::vector<NumBox> result;
    for (const auto& strip : strips)
        for (const auto& [z, zz] : strip.coverage)
            if (!thin(strip.xx.value - strip.x.value) && !thin(zz.value - z.value))
                result.push_back({strip.x, height, z, strip.xx, height, zz});
    return sorted_set(result);
}

}  // namespace pkc

// ---- support_components and build_geometry_graph (spatial/regions.py)

#include <map>
#include <set>

#include "../schema.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

Num num_of(const Value& v) {
    if (auto* i = std::get_if<std::int64_t>(&v.data)) return whole(*i);
    return flt(std::get<double>(v.data));
}

NumBox box_of(const Value& v) {
    NumBox b;
    for (int i = 0; i < 6; ++i) b[i] = num_of(items(v)[i]);
    return b;
}

Value box_value(const NumBox& b) {
    Array out;
    for (const auto& n : b) out.push_back(value(n));
    return array(std::move(out));
}

Value boxes_value(const std::vector<NumBox>& boxes) {
    Array out;
    for (const auto& b : boxes) out.push_back(box_value(b));
    return array(std::move(out));
}

Value ints(const std::vector<std::int64_t>& v) {
    Array out;
    for (auto i : v) out.push_back(integer_value(i));
    return array(std::move(out));
}

bool contains(const NumBox& box, const Array& point) {  // cells.contains(box, point), tolerance 0
    for (int a = 0; a < 3; ++a) {
        const double p = num_of(point[a]).value;
        if (!(box[a].value <= p && p <= box[a + 3].value)) return false;
    }
    return true;
}

struct PatchRow {
    std::int64_t cell, solid;
    Value owner;
    std::string kind;
    NumBox bounds;
};

std::vector<PatchRow> patches_of(const Value& decomposition) {
    std::vector<PatchRow> out;
    for (const auto& row_value : items(at(fields(decomposition), "contacts"))) {
        const Array& r = items(row_value);
        out.push_back({as_int(r[0]), as_int(r[1]), r[2], as_text(r[5]), box_of(r[6])});
    }
    return out;
}

}  // namespace

Value support_components(const Value& decomposition) {
    const std::vector<PatchRow> patches = patches_of(decomposition);
    // grouped by (owner, height) in first-seen order, then sorted by key
    struct Group {
        Value owner;
        Num height;
        std::vector<std::pair<std::int64_t, const PatchRow*>> patches;
    };
    std::vector<Group> groups;
    for (std::size_t index = 0; index < patches.size(); ++index) {
        const PatchRow& p = patches[index];
        if (p.kind != "support") continue;
        auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& g) {
            return py_equal(g.owner, p.owner) && g.height.value == p.bounds[1].value;
        });
        if (found == groups.end()) {
            groups.push_back({p.owner, p.bounds[1], {}});
            found = groups.end() - 1;
        }
        found->patches.emplace_back(static_cast<std::int64_t>(index), &p);
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        const std::string &x = as_text(a.owner), &y = as_text(b.owner);
        if (x != y) return x < y;
        return a.height.value < b.height.value;
    });
    std::vector<std::pair<std::string, Value>> components;
    for (const auto& group : groups) {
        std::vector<NumBox> bounds;
        for (const auto& [i, p] : group.patches) bounds.push_back(p->bounds);
        const std::vector<NumBox> cover = canonical_cover(bounds);
        std::set<std::size_t> pending;
        for (std::size_t i = 0; i < cover.size(); ++i) pending.insert(i);
        while (!pending.empty()) {
            std::set<std::size_t> connected{*pending.begin()};
            std::vector<std::size_t> queue(connected.begin(), connected.end());
            pending.erase(pending.begin());
            while (!queue.empty()) {
                const std::size_t current = queue.back();
                queue.pop_back();
                std::vector<std::size_t> adjacent;
                for (std::size_t i : pending)
                    if (intersection(cover[current], cover[i])) adjacent.push_back(i);
                for (std::size_t i : adjacent) {
                    connected.insert(i);
                    pending.erase(i);
                }
                queue.insert(queue.end(), adjacent.begin(), adjacent.end());  // already sorted
            }
            std::vector<NumBox> pieces;
            for (std::size_t i : connected) pieces.push_back(cover[i]);
            std::vector<std::int64_t> incident, cells, solids;
            for (const auto& entry : group.patches) {
                const PatchRow* p = entry.second;
                if (std::any_of(pieces.begin(), pieces.end(), [&](const NumBox& b) { return intersection(p->bounds, b).has_value(); })) {
                    incident.push_back(entry.first);
                    cells.push_back(p->cell);
                    solids.push_back(p->solid);
                }
            }
            std::sort(cells.begin(), cells.end());
            cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
            std::sort(solids.begin(), solids.end());
            solids.erase(std::unique(solids.begin(), solids.end()), solids.end());
            const std::string key = "support:" + geometry_key(array({group.owner, value(group.height), boxes_value(pieces)}));
            components.emplace_back(key, array({text(key), group.owner, value(group.height), boxes_value(pieces),
                                                ints(cells), ints(solids), ints(incident)}));
        }
    }
    std::stable_sort(components.begin(), components.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Array out;
    for (auto& [key, row] : components) out.push_back(std::move(row));
    return array(std::move(out));
}

Value build_geometry_graph(const Value& scene_value, const Value& decomposition, const Value& supports_value,
                           const Value& task_goal) {
    const Object& scene = fields(scene_value);
    const Object& d = fields(decomposition);
    std::vector<NumBox> cells;
    for (const auto& c : items(at(d, "cells"))) cells.push_back(box_of(items(c)[0]));
    struct PortalRow {
        std::int64_t a, b;
        NumBox bounds;
    };
    std::vector<PortalRow> portals;
    for (const auto& p_value : items(at(d, "portals"))) {
        const Array& p = items(p_value);
        portals.push_back({as_int(items(p[0])[0]), as_int(items(p[0])[1]), box_of(p[1])});
    }
    const std::vector<PatchRow> patches = patches_of(decomposition);
    Array regions, transitions;
    std::vector<std::string> air_keys;
    for (const auto& box : cells) {  // transit = every raw cell's bounds
        const std::string key = "air:" + geometry_key(box_value(box));
        std::vector<std::int64_t> incident;
        for (std::size_t c = 0; c < cells.size(); ++c)
            if (intersection(cells[c], box)) incident.push_back(static_cast<std::int64_t>(c));
        air_keys.push_back(key);
        regions.push_back(array({text(key), text("air_volume"), array({box_value(box)}), ints(incident), array({}), array({}),
                                 text("transit:" + geometry_key(box_value(box)))}));
    }
    const Array& supports = items(supports_value);
    for (const auto& s_value : supports) {
        const Array& s = items(s_value);
        regions.push_back(array({s[0], text("support_surface"), s[3], s[4], array({s[1]}), s[5], text("")}));
    }
    auto link = [&](const std::string& source, const std::string& target, const char* kind, const Value& cover,
                    const Value& portal_ids, const Value& patch_ids) {
        const std::string key = "transition:" + geometry_key(array({text(source), text(target), text(kind), cover}));
        const Value families = std::string(kind) == "open_portal" ? array({})
                                                                  : array({text("normal"), text("stepping"), text("ceiling")});
        transitions.push_back(array({text(key), text(source), text(target), text(kind), cover, portal_ids, patch_ids, families}));
    };
    for (const auto& p : portals) {
        const std::string& a = air_keys.at(p.a);
        const std::string& b = air_keys.at(p.b);
        const Value cover = array({box_value(p.bounds)});
        std::vector<std::int64_t> touching;
        for (std::size_t r = 0; r < portals.size(); ++r)
            if (intersection(portals[r].bounds, p.bounds)) touching.push_back(static_cast<std::int64_t>(r));
        link(a, b, "open_portal", cover, ints(touching), array({}));
        link(b, a, "open_portal", cover, ints(touching), array({}));
    }
    const Array& solids = items(at(fields(at(scene, "collisionScene")), "solids"));
    const double half = f32(0.6) / 2;
    std::vector<std::size_t> air_order(cells.size());
    for (std::size_t i = 0; i < air_order.size(); ++i) air_order[i] = i;
    std::stable_sort(air_order.begin(), air_order.end(), [&](std::size_t x, std::size_t y) { return air_keys[x] < air_keys[y]; });
    for (const auto& s_value : supports) {
        const Array& s = items(s_value);
        const std::string support_key = as_text(s[0]);
        for (std::size_t r : air_order) {
            std::vector<std::int64_t> incident_ids;
            std::vector<NumBox> incident_boxes;
            for (const auto& i_value : items(s[6])) {
                const PatchRow& patch = patches.at(as_int(i_value));
                const auto overlap = intersection(cells[r], patch.bounds);
                if (!overlap) continue;
                const Array& solid = items(solids.at(patch.solid));
                const double box[6] = {num_of(solid[0]).value - half, 0, num_of(solid[2]).value - half,
                                       num_of(solid[3]).value + half, 0, num_of(solid[5]).value + half};
                bool inside = true;
                for (int a : {0, 2}) inside = inside && (*overlap)[a + 3].value > box[a] && (*overlap)[a].value < box[a + 3];
                if (inside) {
                    incident_ids.push_back(as_int(i_value));
                    incident_boxes.push_back(*overlap);
                }
            }
            if (incident_ids.empty()) continue;
            const Value cover = boxes_value(canonical_cover(incident_boxes));
            link(air_keys[r], support_key, "support_arrival", cover, array({}), ints(incident_ids));
            link(support_key, air_keys[r], "support_departure", cover, array({}), ints(incident_ids));
        }
    }
    const Object& initial = fields(at(scene, "initial"));
    const Array& start = items(at(initial, "position"));
    Value start_owner{nullptr};
    for (const auto& b_value : items(at(scene, "blocks"))) {
        const Object& b = fields(b_value);
        if (py_equal(at(b, "id"), at(scene, "startBlock"))) {
            const Value* source = get(b, "sourceBlock");
            start_owner = source ? *source : at(b, "id");
            break;
        }
    }
    const Value* on_ground = get(initial, "onGround");
    const bool grounded = !on_ground || py_truthy(*on_ground);
    Array starts;
    for (const auto& s_value : supports) {
        const Array& s = items(s_value);
        if (!grounded || !py_equal(s[1], start_owner)) continue;
        if (std::any_of(items(s[3]).begin(), items(s[3]).end(), [&](const Value& b) { return contains(box_of(b), start); }))
            starts.push_back(s[0]);
    }
    if (starts.empty()) {
        std::vector<std::string> keys;
        for (std::size_t c = 0; c < cells.size(); ++c)
            if (contains(cells[c], start)) keys.push_back(air_keys[c]);
        std::sort(keys.begin(), keys.end());
        for (const auto& k : keys) starts.push_back(text(k));
    }
    if (!is_null(task_goal) && !py_equal(get_or_null(fields(task_goal), "kind"), text("landing")))
        fail("Route graph requires a physical landing task goal");
    const Value goal_id = is_null(task_goal) ? at(scene, "goalBlock")
                                             : (get(fields(task_goal), "block") ? at(fields(task_goal), "block") : at(scene, "goalBlock"));
    const Object* goal_block = nullptr;
    for (const auto& b_value : items(at(scene, "blocks")))
        if (py_equal(at(fields(b_value), "id"), goal_id)) {
            goal_block = &fields(b_value);
            break;
        }
    if (!goal_block) fail("StopIteration");
    const Value* goal_source = get(*goal_block, "sourceBlock");
    const Value goal_owner = goal_source ? *goal_source : goal_id;
    Array goals;
    for (const auto& s_value : supports) {
        const Array& s = items(s_value);
        if (py_equal(s[1], goal_owner) && py_equal(s[2], at(*goal_block, "top")) && contains(items(s[5]), at(*goal_block, "solidIndex")))
            goals.push_back(s[0]);
    }
    // geometry_key(dict(schema, bounds, regions=[asdict(r)], transitions=[asdict(t)], starts, goals, taskGoal))
    Array region_dicts, transition_dicts;
    for (const auto& r_value : regions) {
        const Array& r = items(r_value);
        region_dicts.push_back(object({{"key", r[0]}, {"kind", r[1]}, {"cover", r[2]}, {"cells", r[3]}, {"owners", r[4]},
                                       {"solids", r[5]}, {"predicate", Value{nullptr}}, {"transit_key", r[6]}}));
    }
    for (const auto& t_value : transitions) {
        const Array& t = items(t_value);
        transition_dicts.push_back(object({{"key", t[0]}, {"source", t[1]}, {"target", t[2]}, {"kind", t[3]}, {"cover", t[4]},
                                           {"portals", t[5]}, {"patches", t[6]}, {"movement_families", t[7]},
                                           {"capability", text("not_ruled_out")}, {"evidence", text("geometry_only")}}));
    }
    const std::string context = geometry_key(object({{"schema", integer_value(1)}, {"bounds", at(d, "bounds")},
                                                     {"regions", array(region_dicts)}, {"transitions", array(transition_dicts)},
                                                     {"starts", array(starts)}, {"goals", array(goals)}, {"taskGoal", task_goal}}));
    return object({{"regions", array(std::move(regions))},
                   {"transitions", array(std::move(transitions))},
                   {"starts", array(std::move(starts))},
                   {"goals", array(std::move(goals))},
                   {"context", text(context)}});
}

}  // namespace pkc
