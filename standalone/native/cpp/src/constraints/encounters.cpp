// Lazy phase covers for one obstacle-cluster encounter in a selected landing
// segment: constraints/encounters.py (see its module docstring).
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "pkc/canonical.hpp"
#include "pkc/constraints/avoidance.hpp"
#include "pkc/envelopes.hpp"
#include "../py_math.hpp"
#include "../value_ops.hpp"
#include "variant_state.hpp"

namespace pkc {

namespace {

constexpr const char* kPhases[3] = {"Y", "X", "Z"};
constexpr double kInf = std::numeric_limits<double>::infinity();

double d(const Value& v) {
    double out = 0.;
    if (!numeric(v, out)) fail("expected a number");
    return out;
}

Num any_num(const Value& v) {  // a number of either kind, infinities included
    if (auto* integer = std::get_if<std::int64_t>(&v.data)) return {static_cast<double>(*integer), true};
    return {d(v), false};
}

std::int64_t i64(const Value& v) {
    auto* held = std::get_if<std::int64_t>(&v.data);
    if (!held) fail("expected an int");
    return *held;
}

Value ints(const std::vector<std::int64_t>& values) {
    Array out;
    for (auto v : values) out.push_back(integer_value(v));
    return array(std::move(out));
}

std::vector<std::int64_t> int_list(const Value& v) {
    std::vector<std::int64_t> out;
    for (const auto& x : items(v)) out.push_back(i64(x));
    return out;
}

using Region = std::array<double, 4>;

Region region(const Object& side) {
    const Array& values = items(at(side, "region"));
    Region out;
    for (int i = 0; i < 4; ++i) out[i] = is_null(values[i]) ? (i < 2 ? -kInf : kInf) : d(values[i]);
    return out;
}

bool nested(const Region& a, const Region& b) {
    auto contains = [](const Region& outer, const Region& inner) {
        return outer[0] <= inner[0] && outer[1] <= inner[1] && inner[2] <= outer[2] && inner[3] <= outer[3];
    };
    return contains(a, b) || contains(b, a);
}

bool intersects(const Region& a, const Region& b) {
    for (int i : {0, 1})
        if (!(std::max(a[i], b[i]) <= std::min(a[i + 2], b[i + 2]))) return false;
    return true;
}

const Object& side_at(const Object& item, std::int64_t i) { return fields(items(at(item, "sides")).at(i)); }

// Sides of different active sets describe one place when their regions nest.
bool same(const Object& item, std::int64_t a, std::int64_t b) {
    if (a == b) return true;
    const Object &first = side_at(item, a), &second = side_at(item, b);
    return as_text(at(first, "axis")) != "Y" && as_text(at(second, "axis")) != "Y" && nested(region(first), region(second));
}

// The axis bounds [lo, hi] of an envelope state or phase box.
std::pair<double, double> range_of(const Object& bounds, const std::string& axis) {
    const Array& r = items(at(bounds, axis));
    return {d(r[0]), d(r[1])};
}

bool satisfied(const Object& side, const Object& bounds) {
    for (const auto& b : items(at(side, "bounds"))) {
        const Object& bound = fields(b);
        const auto [lo, hi] = range_of(bounds, as_text(at(bound, "axis")));
        const double plane = d(at(bound, "plane"));
        if (!(d(at(bound, "direction")) > 0 ? lo >= plane : hi <= plane)) return false;
    }
    return true;
}

bool reaches(const Object& side, const Object& bounds) {
    for (const auto& b : items(at(side, "bounds"))) {
        const Object& bound = fields(b);
        const auto [lo, hi] = range_of(bounds, as_text(at(bound, "axis")));
        const double plane = d(at(bound, "plane"));
        if (!(d(at(bound, "direction")) > 0 ? hi >= plane : lo <= plane)) return false;
    }
    return true;
}

// side_dict for a native SceneGeometry.sides row [bounds, owners, region].
Object side_dict(const Value& side) {
    const Array& row = items(side);
    const Array &bounds = items(row[0]), &owners = items(row[1]), &region_values = items(row[2]);
    Array key, bound_values;
    std::set<std::string> axes;
    std::set<std::int64_t> all;
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        const Array& b = items(bounds[i]);
        key.push_back(array({b[0], b[1], b[2]}));
        bound_values.push_back(object({{"axis", b[0]}, {"direction", b[1]}, {"plane", b[2]}, {"owners", owners[i]}}));
        axes.insert(as_text(b[0]));
        for (const auto& o : items(owners[i])) all.insert(i64(o));
    }
    Array region_out;
    for (const auto& v : region_values) region_out.push_back(std::isinf(d(v)) ? Value{nullptr} : v);
    const bool single = bounds.size() == 1;
    return {{"key", array(std::move(key))},
            {"bounds", array(bound_values)},
            {"owners", ints(std::vector<std::int64_t>(all.begin(), all.end()))},
            {"region", array(std::move(region_out))},
            {"axis", text(axes.size() == 1 ? *axes.begin() : "XZ")},
            {"direction", single ? at(fields(bound_values[0]), "direction") : integer_value(0)},
            {"plane", single ? at(fields(bound_values[0]), "plane") : Value{nullptr}}};
}

struct Cluster {
    std::vector<std::int64_t> members;
    std::vector<NumBox> boxes;
};

Cluster cluster_of(const SceneGeometry& geometry, std::int64_t id, const Value& basis) {
    for (auto& members : geometry.cluster_groups(int_list(basis))) {
        if (members.front() != id) continue;
        Cluster cluster{members, {}};
        for (auto m : members) cluster.boxes.push_back(geometry.boxes().at(m));
        return cluster;
    }
    fail("KeyError: cluster " + std::to_string(id));
}

std::vector<std::int64_t> active(const Cluster& cluster, double y_lo, double y_hi) {
    std::vector<std::int64_t> out;
    for (std::size_t k = 0; k < cluster.boxes.size(); ++k)
        if (cluster.boxes[k][1].value < y_hi && y_lo < cluster.boxes[k][4].value) out.push_back(k);
    return out;
}

Object vertical_side(const Cluster& cluster, double y_lo) {
    std::optional<Num> below;
    for (const auto& b : cluster.boxes)
        if (b[4].value <= y_lo) below = below ? py_max(*below, b[4]) : b[4];
    std::int64_t direction = 1;
    Num plane;
    if (below) {
        plane = *below;
    } else {
        direction = -1;
        plane = cluster.boxes.front()[1];
        for (const auto& b : cluster.boxes) plane = py_min(plane, b[1]);
    }
    return {{"key", array({array({text("Y"), integer_value(direction), value(plane)})})},
            {"bounds", array({})},
            {"owners", array({})},
            {"region", array({kNull, kNull, kNull, kNull})},
            {"axis", text("Y")},
            {"direction", integer_value(direction)},
            {"plane", value(plane)}};
}

// The finite contact region precedes that input's horizontal movement: entry/exit geometry, never a new position
// pin at tick+1 (post-contact travel envelopes can extend beyond the support's footprint).
Value support_region(const Object& profile, const Value& envelopes, std::int64_t tick) {
    if (const Value* events = get(profile, "contactEvents")) {
        for (const auto& e_value : items(*events)) {
            const Object& e = fields(e_value);
            if (!py_equal(get_or_null(e, "axis"), text("Y")) || !py_equal(get_or_null(e, "direction"), integer_value(-1)) ||
                i64(at(e, "offset")) + 1 != tick || !get(e, "regions"))
                continue;
            const Array& regions = items(at(e, "regions"));
            Num x0 = any_num(items(regions[0])[0]), z0 = any_num(items(regions[0])[1]);
            Num x1 = any_num(items(regions[0])[2]), z1 = any_num(items(regions[0])[3]);
            for (const auto& r : regions) {
                const Array& rect = items(r);
                x0 = py_min(x0, any_num(rect[0]));
                z0 = py_min(z0, any_num(rect[1]));
                x1 = py_max(x1, any_num(rect[2]));
                z1 = py_max(z1, any_num(rect[3]));
            }
            const Array& boxes = items(at(e, "boxes"));
            Num top = any_num(items(boxes.at(0))[4]);
            for (const auto& b : boxes) top = py_max(top, any_num(items(b)[4]));
            return object({{"X", array({value(x0), value(x1)})},
                           {"Z", array({value(z0), value(z1)})},
                           {"Y", array({value(top), value(top)})}});
        }
    }
    return items(at(fields(envelopes), "states")).at(tick);
}

Value region_query(std::int64_t tick, const char* phase, const NumBox& box) {
    Array bounds;
    for (const auto& v : box) bounds.push_back(value(v));
    return object({{"alternatives", array({object({{"regions", array({object({{"tick", integer_value(tick)},
                                                                               {"phase", text(phase)},
                                                                               {"bounds", array(std::move(bounds))},
                                                                               {"interior", boolean(true)}})})}})})}});
}

bool ruled_out(const Value& proof) { return py_truthy(at(fields(proof), "ruledOut")); }

Value describe(const Object& item, const Array& spans);

// One encounter follows a simple side walk, with its entry allowed as exit (encounters._continuation).
std::optional<std::vector<std::int64_t>> continuation(const Object& item, const std::vector<std::int64_t>& history,
                                                      std::int64_t face) {
    if (history.empty()) return std::vector<std::int64_t>{face};
    if (same(item, history.back(), face)) {
        auto out = history;
        out.back() = face;
        return out;
    }
    if (history.size() > 1 && same(item, history.back(), history.front())) return std::nullopt;
    if (std::any_of(history.begin(), history.end(), [&](std::int64_t f) { return same(item, f, face); })) {
        if (!same(item, face, history.front())) return std::nullopt;
    }
    auto out = history;
    out.push_back(face);
    return out;
}

}  // namespace

std::vector<std::pair<std::int64_t, std::int64_t>> landing_segments(const Object& profile, std::int64_t through) {
    // A query ending at an airborne head hit does not create a landing boundary.
    std::set<std::int64_t> cuts{0, through};
    if (const Value* events = get(profile, "contactEvents")) {
        for (const auto& e_value : items(*events)) {
            const Object& e = fields(e_value);
            const Value& role = get_or_null(e, "role");
            if (!py_equal(get_or_null(e, "axis"), text("Y")) || !py_equal(get_or_null(e, "direction"), integer_value(-1)) ||
                !(py_equal(role, text("landing")) || py_equal(role, text("step"))))
                continue;
            const std::int64_t cut = i64(at(e, "offset")) + 1;
            if (0 < cut && cut < through) cuts.insert(cut);
        }
    }
    if (const Value* ground_value = get(profile, "grounded")) {
        const Array& ground = items(*ground_value);
        const auto end = std::min<std::int64_t>(through, static_cast<std::int64_t>(ground.size()));
        for (std::int64_t t = 1; t < end; ++t)
            if (py_truthy(ground[t]) && !py_truthy(ground[t - 1])) cuts.insert(t);
    }
    std::vector<std::int64_t> sorted(cuts.begin(), cuts.end());
    std::vector<std::pair<std::int64_t, std::int64_t>> out;
    for (std::size_t i = 0; i + 1 < sorted.size(); ++i) out.emplace_back(sorted[i], sorted[i + 1]);
    return out;
}

Array canonical_rows(const Array& rows) {
    using Key = std::tuple<std::int64_t, std::int64_t, std::string, std::string>;
    std::vector<std::pair<Key, Object>> bounds;
    for (const auto& r : rows) {
        const Object& row = fields(r);
        const Value* t1 = get(row, "t1");
        const Value t = t1 ? *t1 : get_or_null(row, "offset");
        const Value* leg = get(row, "leg");
        const std::string& cmp = as_text(at(row, "cmp"));
        const Key key{leg ? i64(*leg) : 0, i64(t), as_text(at(row, "mode")), cmp};
        auto found = std::find_if(bounds.begin(), bounds.end(), [&](const auto& b) { return b.first == key; });
        const double rhs = d(at(row, "rhs"));
        if (found == bounds.end() ||
            (cmp == "GE" ? rhs > d(at(found->second, "rhs")) : rhs < d(at(found->second, "rhs")))) {
            Object entry = with(with(with(row, "leg", integer_value(std::get<0>(key))), "offset", t), "t1", t);
            if (found == bounds.end())
                bounds.emplace_back(key, std::move(entry));
            else
                found->second = std::move(entry);
        }
    }
    std::stable_sort(bounds.begin(), bounds.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Array out;
    for (auto& [_, row] : bounds) out.push_back(object(std::move(row)));
    return out;
}

bool rows_entailed(const Value& envelopes, const Array& selected, const Array& required) {
    const Array& states = items(at(fields(envelopes), "states"));
    std::vector<std::pair<std::pair<std::int64_t, std::string>, std::pair<double, double>>> bounds;
    auto lookup = [&](std::int64_t t, const std::string& axis) -> std::pair<double, double>& {
        for (auto& [key, value] : bounds)
            if (key.first == t && key.second == axis) return value;
        bounds.push_back({{t, axis}, range_of(fields(states.at(t)), axis)});
        return bounds.back().second;
    };
    auto tick = [](const Object& row) {
        const Value* t1 = get(row, "t1");
        return i64(t1 ? *t1 : at(row, "offset"));
    };
    for (const auto& r : selected) {
        const Object& row = fields(r);
        auto& [lo, hi] = lookup(tick(row), as_text(at(row, "mode")));
        const double rhs = d(at(row, "rhs"));
        const std::string& cmp = as_text(at(row, "cmp"));
        if (cmp == "GE") lo = std::max(lo, rhs);
        if (cmp == "LE") hi = std::min(hi, rhs);
    }
    for (const auto& r : required) {
        const Object& row = fields(r);
        const auto [lo, hi] = lookup(tick(row), as_text(at(row, "mode")));
        const double rhs = d(at(row, "rhs"));
        const std::string& cmp = as_text(at(row, "cmp"));
        if ((cmp == "GE" && lo < rhs) || (cmp == "LE" && hi > rhs)) return false;
    }
    return true;
}

Value prepare_encounter(ConstraintVariants& owner, const Value& item_value, const Value& envelopes) {
    // Prepared atomically: a raised check leaves the unprocessed item resumable.
    const Object& item = fields(item_value);
    if (get(item, "phases")) return item_value;
    const SceneGeometry& geometry = owner.geometry();
    const Cluster cluster = cluster_of(geometry, i64(at(item, "clusterId")), at(item, "basis"));
    const Object& profile = owner.profile();
    std::vector<Object> sides;
    Array phases, vertical_clear, covered, uncertain_at, excluded_faces;

    auto index = [&](Object side) -> std::int64_t {
        const Value& key = at(side, "key");
        for (std::size_t i = 0; i < sides.size(); ++i) {
            if (!py_equal(at(sides[i], "key"), key)) continue;
            Object& known = sides[i];
            auto merged = [](const Value& a, const Value& b) {
                std::set<std::int64_t> all;
                for (const auto& v : items(a)) all.insert(i64(v));
                for (const auto& v : items(b)) all.insert(i64(v));
                return ints(std::vector<std::int64_t>(all.begin(), all.end()));
            };
            put(known, "owners", merged(at(known, "owners"), at(side, "owners")));
            Array bounds = items(at(known, "bounds"));
            const Array& other = items(at(side, "bounds"));
            for (std::size_t b = 0; b < std::min(bounds.size(), other.size()); ++b)
                bounds[b] = object(with(fields(bounds[b]), "owners",
                                        merged(at(fields(bounds[b]), "owners"), at(fields(other[b]), "owners"))));
            put(known, "bounds", array(std::move(bounds)));
            return static_cast<std::int64_t>(i);
        }
        sides.push_back(std::move(side));
        return static_cast<std::int64_t>(sides.size()) - 1;
    };
    auto sides_of = [&](const std::vector<std::int64_t>& subset) {
        std::vector<std::int64_t> out;
        const Value found = geometry.sides(cluster.members, subset);
        for (const auto& s : items(found)) out.push_back(index(side_dict(s)));
        return out;
    };
    auto boundary = [&](std::int64_t tick) {
        const Value bounds_value = support_region(profile, envelopes, tick);
        const Object& bounds = fields(bounds_value);
        const auto [y_lo, y_hi] = range_of(bounds, "Y");
        const auto members = active(cluster, y_lo, y_hi);
        if (members.empty()) return std::vector<std::int64_t>{index(vertical_side(cluster, y_lo))};
        std::vector<std::int64_t> out;
        for (auto i : sides_of(members))
            if (reaches(sides[i], bounds)) out.push_back(i);
        return out;
    };
    const auto entry = boundary(i64(at(item, "begin")));
    const auto exit = boundary(i64(at(item, "end")));
    std::set<std::int64_t> uncertain;
    if (const Value* cases = get(profile, "caseEvents"))
        for (const auto& e : items(*cases)) {
            const Value* input = get(fields(e), "input");
            uncertain.insert(i64(input ? *input : at(fields(e), "offset")));
        }
    if (const Value* model = get(fields(envelopes), "model"); model && !is_null(*model))
        if (const Value* vertical = get(fields(*model), "vertical"))
            if (const Value* contacts = get(fields(*vertical), "contacts"))
                for (const auto& c : items(*contacts))
                    if (py_truthy(get_or_null(fields(c), "case"))) uncertain.insert(i64(at(fields(c), "state")) - 1);
    std::set<std::int64_t> required_x, required_z;
    const Array& envelope_phases = items(at(fields(envelopes), "phases"));
    for (std::int64_t tick = i64(at(item, "begin")); tick < i64(at(item, "end")); ++tick) {
        for (int p = 0; p < 3; ++p) {
            const std::string phase = kPhases[p];
            owner.check();
            owner.metric("phaseChecks");
            const std::int64_t at_phase = 3 * tick + p;
            const Object& bounds = fields(at(fields(envelope_phases.at(tick)), phase));
            const auto [y_lo, y_hi] = range_of(bounds, "Y");
            const auto members = active(cluster, y_lo, y_hi);
            if (members.empty()) {
                const std::int64_t vertical = index(vertical_side(cluster, y_lo));
                vertical_clear.push_back(array({integer_value(at_phase), integer_value(vertical)}));
                covered.push_back(array({integer_value(at_phase), ints({vertical})}));
                owner.metric("excludedPhases");
                continue;
            }
            // Members the envelope rules out at this phase cannot be touched then; the sides are built around the rest.
            Array queries;
            for (auto k : members) queries.push_back(region_query(tick, kPhases[p], cluster.boxes[k]));
            const Value checks = interaction_reachability_many(envelopes, array(std::move(queries)));
            std::vector<std::int64_t> reachable;
            for (std::size_t n = 0; n < members.size(); ++n)
                if (!ruled_out(items(checks)[n])) reachable.push_back(members[n]);
            if (reachable.empty()) {
                std::vector<std::int64_t> certain;
                for (auto i : sides_of(members))
                    if (satisfied(sides[i], bounds)) certain.push_back(i);
                covered.push_back(array({integer_value(at_phase), ints(certain)}));
                owner.metric("excludedPhases");
                continue;
            }
            if (uncertain.count(tick)) {
                // Named cases have extra internal probes that neither realized endpoints nor sweep rows specify:
                // keep the unknown for the named-case scene replay to judge.
                uncertain_at.push_back(integer_value(at_phase));
                owner.metric("uncertainPhases");
                continue;
            }
            const auto states_x = phase_states(tick, phase, "X"), states_z = phase_states(tick, phase, "Z");
            std::vector<std::pair<std::int64_t, Array>> faces;
            for (auto i : sides_of(reachable)) {
                Array rows;
                for (const auto& b : items(at(sides[i], "bounds"))) {
                    const Object& bound = fields(b);
                    for (auto t : as_text(at(bound, "axis")) == "X" ? states_x : states_z)
                        rows.push_back(owner.face_row(bound, t));
                }
                faces.emplace_back(i, std::move(rows));
            }
            Array face_queries;
            for (const auto& [_, rows] : faces) face_queries.push_back(array(rows));
            const Value face_checks = interaction_reachability_many(envelopes, array(std::move(face_queries)));
            Object options, costs;
            for (std::size_t n = 0; n < faces.size(); ++n) {
                const auto& [i, rows] = faces[n];
                if (ruled_out(items(face_checks)[n])) {
                    owner.metric("excludedFaces");
                    excluded_faces.push_back(array({integer_value(at_phase), integer_value(i)}));
                    continue;
                }
                options.emplace_back(std::to_string(i), array(rows));
                std::vector<double> terms;
                for (const auto& r : rows) {
                    const Object& row = fields(r);
                    const double sign = as_text(at(row, "cmp")) == "GE" ? 1 : -1;
                    const double gap = sign * (d(at(row, "rhs")) -
                                               owner.coordinate(as_text(at(row, "mode")), i64(at(row, "t1")), envelopes).value);
                    const double positive = std::max(0., gap);
                    terms.push_back(positive * positive);
                }
                costs.emplace_back(std::to_string(i), terms.empty() ? integer_value(0) : real(python_sum(terms)));
            }
            required_x.insert(states_x.begin(), states_x.end());
            required_z.insert(states_z.begin(), states_z.end());
            phases.push_back(object({{"at", integer_value(at_phase)}, {"options", object(std::move(options))},
                                     {"costs", object(std::move(costs))}}));
        }
    }
    Array possible;
    for (const auto& p : phases) {
        const std::int64_t a = i64(at(fields(p), "at"));
        possible.push_back(array({integer_value(a / 3), text(kPhases[a % 3])}));
    }
    // A ranking lower bound only: it cannot exclude a side or timing.
    std::vector<double> suffix(phases.size() + 1, 0.);
    for (auto i = static_cast<std::int64_t>(phases.size()) - 1; i >= 0; --i) {
        const Object& costs = fields(at(fields(phases[i]), "costs"));
        double least = 0.;
        for (std::size_t c = 0; c < costs.size(); ++c) least = c == 0 ? d(costs[c].second) : std::min(least, d(costs[c].second));
        suffix[i] = suffix[i + 1] + least;
    }
    Array suffix_values, side_values;
    for (double s : suffix) suffix_values.push_back(real(s));
    for (auto& s : sides) side_values.push_back(object(std::move(s)));
    const Value side_list = array(std::move(side_values));
    Object result = item;
    put(result, "sides", side_list);
    put(result, "phases", array(phases));
    put(result, "verticalClear", array(vertical_clear));
    put(result, "coveredPhases", array(covered));
    put(result, "uncertain", array(uncertain_at));
    put(result, "excludedFaces", array(excluded_faces));
    put(result, "entryFaces", ints(entry));
    put(result, "exitFaces", ints(exit));
    put(result, "required", object({{"X", ints({required_x.begin(), required_x.end()})},
                                    {"Z", ints({required_z.begin(), required_z.end()})}}));
    put(result, "possiblePhases", array(possible));
    put(result, "suffixCost", array(std::move(suffix_values)));
    auto& state = owner.mutable_state();
    const Value segment = array({at(item, "begin"), at(item, "end")});
    if (!uncertain_at.empty()) {
        const Value record = object({{"clusterId", at(item, "clusterId")}, {"members", at(item, "members")},
                                     {"segment", segment}, {"phases", array(uncertain_at)}});
        if (!contains(state.uncertain_interactions, record)) state.uncertain_interactions.push_back(record);
    }
    state.encounter_log.push_back(object({{"clusterId", at(item, "clusterId")}, {"members", at(item, "members")},
                                          {"names", at(item, "names")}, {"owner", at(item, "owner")},
                                          {"segment", segment}, {"sides", side_list},
                                          {"possiblePhases", array(possible)}, {"coveredPhases", array(covered)},
                                          {"excludedFaces", array(excluded_faces)},
                                          {"verticalClear", array(vertical_clear)}, {"uncertain", array(uncertain_at)}}));
    owner.metric("preparedEncounters");
    return object(std::move(result));
}

namespace {

Value describe(const Object& item, const Array& spans) {
    // Exact phase spans are diagnostic truth; integer gates remain convenient labels for consumers that display a
    // tick. Generated rows are never rebuilt from these labels.
    std::vector<std::pair<std::int64_t, std::int64_t>> timeline;
    const Array& phases = items(at(item, "phases"));
    for (const auto& s : spans) {
        const Array& span = items(s);
        const std::int64_t a = i64(span[0]), b = i64(span[1]), f = i64(span[2]);
        for (const auto& p : phases) {
            const std::int64_t at_phase = i64(at(fields(p), "at"));
            if (a <= at_phase && at_phase <= b) timeline.emplace_back(at_phase, f);
        }
    }
    const Array& entry = items(at(item, "entryFaces"));
    if (!spans.empty() && entry.size() == 1 && !same(item, i64(entry[0]), i64(items(spans[0])[2])))
        // Geometric entry is already covered before the first emitted row; its phase is the selected landing.
        timeline.insert(timeline.begin(), {std::max<std::int64_t>(0, 3 * i64(at(item, "begin")) - 1), i64(entry[0])});
    const std::int64_t last = spans.empty() ? 0 : i64(items(spans.back())[1]);
    for (const auto& c : items(at(item, "coveredPhases"))) {
        const Array& entry_c = items(c);
        const std::int64_t at_phase = i64(entry_c[0]);
        const Array& certain = items(entry_c[1]);
        if (!spans.empty() && timeline[0].first < at_phase && at_phase < last && certain.size() == 1)
            timeline.emplace_back(at_phase, i64(certain[0]));
    }
    for (const auto& v : items(at(item, "verticalClear"))) {
        const std::int64_t a = i64(items(v)[0]), f = i64(items(v)[1]);
        if (!spans.empty() && timeline[0].first < a && a <= last) timeline.emplace_back(a, f);
    }
    for (const auto& v : items(at(item, "verticalClear"))) {
        const std::int64_t a = i64(items(v)[0]), f = i64(items(v)[1]);
        if (!spans.empty() && a > last) {
            timeline.emplace_back(a, f);
            break;
        }
    }
    std::sort(timeline.begin(), timeline.end());
    std::vector<std::int64_t> chosen, switches;
    for (const auto& [a, face] : timeline) {
        if (chosen.empty() || !same(item, chosen.back(), face)) {
            chosen.push_back(face);
            switches.push_back(a);
        }
    }
    const Array& sides = items(at(item, "sides"));
    Array fs, gates, switch_phases, phase_spans, entry_faces, exit_faces;
    for (auto i : chosen) fs.push_back(sides.at(i));
    for (std::size_t n = 1; n < switches.size(); ++n) {
        gates.push_back(integer_value(switches[n] / 3));
        switch_phases.push_back(integer_value(switches[n]));
    }
    for (const auto& s : spans) {
        const Array& span = items(s);
        phase_spans.push_back(array({span[0], span[1], at(fields(sides.at(i64(span[2]))), "key")}));
    }
    for (const auto& i : entry) entry_faces.push_back(sides.at(i64(i)));
    for (const auto& i : items(at(item, "exitFaces"))) exit_faces.push_back(sides.at(i64(i)));
    const auto count = static_cast<std::int64_t>(fs.size());
    return object({{"leg", integer_value(0)},
                   {"clusterId", at(item, "clusterId")},
                   {"members", at(item, "members")},
                   {"blockId", at(item, "owner")},
                   {"segment", array({at(item, "begin"), at(item, "end")})},
                   {"faces", array(fs)},
                   {"gates", array(std::move(gates))},
                   {"switchPhases", array(std::move(switch_phases))},
                   {"phaseSpans", array(std::move(phase_spans))},
                   {"entryFaces", array(std::move(entry_faces))},
                   {"exitFaces", array(std::move(exit_faces))},
                   {"kind", text(passage_kind(fs))},
                   {"blockageCounts", blockage_counts(fs)},
                   {"blockageCount", integer_value(std::max<std::int64_t>(0, count - 1))}});
}

Value history_value(const std::vector<std::int64_t>& history) { return ints(history); }

}  // namespace

std::optional<Value> encounter_option(ConstraintVariants& owner, const Value& item_value, VariantCursor& cursor,
                                      const Value& envelopes) {
    const Value prepared = prepare_encounter(owner, item_value, envelopes);
    const Object& item = fields(prepared);
    const Array& entry = items(at(item, "entryFaces"));
    std::vector<std::int64_t> history;
    if (entry.size() == 1 && as_text(at(side_at(item, i64(entry[0])), "axis")) != "Y") history = {i64(entry[0])};
    if (!cursor.pending) cursor.pending = std::vector<EncounterWalk>{EncounterWalk{0, {}, history, {}}};
    if (!cursor.seen) cursor.seen.emplace();
    if (!cursor.completed) cursor.completed.emplace();
    std::set<std::string> seen(cursor.seen->begin(), cursor.seen->end());
    const Array& phases = items(at(item, "phases"));
    const Array& suffix = items(at(item, "suffixCost"));
    const auto phase_count = static_cast<std::int64_t>(phases.size());
    auto& state = owner.mutable_state();
    auto dropped = [&](const char* reason, const Array& rows) {
        state.dropped_options.push_back(object({{"clusterId", at(item, "clusterId")}, {"members", at(item, "members")},
                                                {"segment", array({at(item, "begin"), at(item, "end")})},
                                                {"reason", text(reason)}, {"rows", array(rows)}}));
    };
    while (!cursor.pending->empty()) {
        owner.check();
        const EncounterWalk node = cursor.pending->back();
        std::int64_t at_index = node.at;
        // Existing rows can already cover later sweeps, including the terminal one: no optional third face for them.
        while (at_index < phase_count) {
            bool covered = false;
            for (const auto& [_, rows] : fields(at(fields(phases[at_index]), "options")))
                if (rows_entailed(envelopes, node.rows, items(rows))) {
                    covered = true;
                    break;
                }
            if (!covered) break;
            ++at_index;
        }
        if (at_index == phase_count) {
            const Array rows = canonical_rows(node.rows);
            const std::string key = owner.effective_key(rows);
            cursor.pending->pop_back();
            owner.metric("optionLeaves");
            if (std::find(cursor.completed->begin(), cursor.completed->end(), key) != cursor.completed->end()) {
                owner.metric("duplicateOptions");
                dropped("duplicate", rows);
                continue;
            }
            // Same remaining capabilities: a completed weaker cover subsumes a stricter description. Never used on
            // unfinished walks.
            bool dominated = false;
            if (cursor.covers)
                for (const auto& weaker : *cursor.covers)
                    if (rows_entailed(envelopes, rows, items(weaker))) {
                        dominated = true;
                        break;
                    }
            if (dominated) {
                owner.metric("dominatedOptions");
                dropped("dominated", rows);
                continue;
            }
            cursor.completed->push_back(key);
            if (!cursor.covers) cursor.covers.emplace();
            cursor.covers->push_back(array(rows));
            Object option{{"rows", array(rows)}};
            if (!node.spans.empty()) option.emplace_back("passage", describe(item, node.spans));
            return object(std::move(option));
        }
        struct Child {
            double cost;
            std::string key;
            EncounterWalk walk;
        };
        std::vector<Child> children;
        const Object& phase_at = fields(phases[at_index]);
        const std::int64_t phase_at_value = i64(at(phase_at, "at"));
        for (const auto& [face_string, _] : fields(at(phase_at, "options"))) {
            const std::int64_t face = std::stoll(face_string);
            std::vector<std::int64_t> walk_history = node.history;
            const std::int64_t previous_at = node.spans.empty() ? 3 * i64(at(item, "begin")) - 1 : i64(items(node.spans.back())[1]);
            bool gap_unknown = false;
            for (const auto& u : items(at(item, "uncertain")))
                if (previous_at < i64(u) && i64(u) < phase_at_value) gap_unknown = true;
            for (const auto& c : items(at(item, "coveredPhases"))) {
                const Array& entry_c = items(c);
                const std::int64_t p = i64(entry_c[0]);
                if (!(previous_at < p && p < phase_at_value)) continue;
                const Array& certain = items(entry_c[1]);
                if (certain.empty()) gap_unknown = true;
                for (const auto& f : certain)
                    if (walk_history.empty() || !same(item, i64(f), walk_history.back())) gap_unknown = true;
            }
            // A proved-clear gap may contain the intervening side (or unknown named probes), whose rows this branch
            // already owns: do not reject the next face because that implicit bridge is not in the catalogue.
            if (gap_unknown) walk_history.clear();
            if (!walk_history.empty() && !same(item, walk_history.back(), face)) {
                bool gap = false;
                for (const auto& v : items(at(item, "verticalClear"))) {
                    const std::int64_t a = i64(items(v)[0]);
                    if (previous_at < a && a < phase_at_value) gap = true;
                }
                if (!intersects(region(side_at(item, walk_history.back())), region(side_at(item, face))) && !gap) continue;
            }
            const auto following = continuation(item, walk_history, face);
            if (!following) continue;
            Array rows = node.rows;
            double paid = 0.;
            for (std::int64_t stop = at_index; stop < phase_count; ++stop) {
                owner.check();
                const Object& phase = fields(phases[stop]);
                const Value* face_rows = get(fields(at(phase, "options")), face_string);
                if (!face_rows) break;
                Array joined = rows;
                for (const auto& r : items(*face_rows)) joined.push_back(r);
                rows = canonical_rows(joined);
                if (ruled_out(interaction_reachability(envelopes, array(rows)))) break;
                paid += d(at(fields(at(phase, "costs")), face_string));
                Array spans = node.spans;
                spans.push_back(array({integer_value(phase_at_value), at(phase, "at"), integer_value(face)}));
                EncounterWalk child{stop + 1, rows, *following, std::move(spans)};
                const std::string key =
                    digest(array({integer_value(child.at), text(owner.effective_key(rows)), history_value(*following)}));
                if (!seen.count(key))
                    children.push_back({paid + d(suffix.at(stop + 1)) + static_cast<double>(following->size()) * 1e-9,
                                        key, std::move(child)});
            }
        }
        std::stable_sort(children.begin(), children.end(), [](const Child& a, const Child& b) {
            if (a.cost != b.cost) return a.cost < b.cost;
            if (a.walk.at != b.walk.at) return a.walk.at > b.walk.at;
            return a.key < b.key;
        });
        // Publish the whole expansion atomically: an interrupted build leaves its parent pending.
        cursor.pending->pop_back();
        owner.metric("optionNodes");
        for (auto c = children.rbegin(); c != children.rend(); ++c) {
            if (seen.count(c->key)) continue;
            seen.insert(c->key);
            cursor.seen->push_back(c->key);
            cursor.pending->push_back(c->walk);
            owner.metric("generatedChildren");
        }
    }
    return std::nullopt;
}

}  // namespace pkc
