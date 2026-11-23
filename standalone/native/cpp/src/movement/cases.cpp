// Port of movement/cases.py and contact_combinations' generator helpers.
#include "pkc/movement/cases.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include "../schema.hpp"
#include "pkc/reachability/service.hpp"
#include "pkc/scene/geometry.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const double kHalfWidth = f32(0.6) / 2;  // scene/route.HALF_WIDTH

const Array& solid_of(const GenerationContext& context, const Object& block) {
    return items(items(at(fields(at(fields(context.gate->scene()), "collisionScene")), "solids"))
                     .at(as_int(at(block, "solidIndex"))));
}

std::array<Num, 6> expanded(const Array& raw) {
    Box box;
    for (std::size_t i = 0; i < 6; ++i) box[i] = num(raw[i]);
    return expanded_box(box);
}

Value faces(const GenerationContext& context, const Object& block, const std::string& axis, std::int64_t sign) {  // _faces
    return context.gate->geometry().contact_options(axis == "X" ? 0 : axis == "Y" ? 1 : 2, sign, real(0.),
                                                    {as_int(at(block, "solidIndex"))});
}

double height(const GenerationContext& context, const Object& block, double y) {  // _height
    const auto box = expanded(solid_of(context, block));
    return std::min(std::max(y, box[1].value + 1e-7), box[4].value - 1e-7);
}

double n(const Value& v) { return num(v).value; }

Array tangent_rows(const Array& box, const std::string& axis, std::int64_t state) {
    const std::size_t a = axis == "X" ? 0 : 2;
    return {object({{"mode", text(axis)}, {"t1", integer_value(state)}, {"cmp", text("GE")},
                    {"rhs", value(add(sub(num(box[a]), flt(kHalfWidth)), flt(kSolverMargin)))}, {"name", text("case:finite")}}),
            object({{"mode", text(axis)}, {"t1", integer_value(state)}, {"cmp", text("LE")},
                    {"rhs", value(sub(add(num(box[a + 3]), flt(kHalfWidth)), flt(kSolverMargin)))}, {"name", text("case:finite")}})};
}

Array side_run(const Array& box, const Array& boxes, const std::string& axis, std::int64_t sign, const std::string& along) {
    const std::size_t s = axis == "X" ? 0 : 2, a = along == "X" ? 0 : 2;
    auto plane = [&](const Array& b) { return sign > 0 ? n(b[s + 3]) : n(b[s]); };
    Value lo = box[a], hi = box[a + 3];
    for (bool grown = true; grown;) {
        grown = false;
        for (const auto& bv : boxes) {
            const Array& b = items(bv);
            if (plane(b) == plane(box) && n(b[a]) <= n(hi) && n(b[a + 3]) >= n(lo) &&
                (n(b[a]) < n(lo) || n(b[a + 3]) > n(hi))) {
                if (n(b[a]) < n(lo)) lo = b[a];  // min()/max() keep the first of equals
                if (n(b[a + 3]) > n(hi)) hi = b[a + 3];
                grown = true;
            }
        }
    }
    Array result = box;
    result[a] = lo;
    result[a + 3] = hi;
    return result;
}

Value with_name(const Value& row, const std::string& name) { return object(with(fields(row), "name", text(name))); }

}  // namespace

Value outside_row(const Array& box, const std::string& axis, std::int64_t sign, std::int64_t state,
                  const std::string& name) {
    const std::size_t a = axis == "X" ? 0 : 2;
    const Num rhs = sign > 0 ? add(add(num(box[a + 3]), flt(kHalfWidth)), flt(kSolverMargin))
                             : sub(sub(num(box[a]), flt(kHalfWidth)), flt(kSolverMargin));
    return object({{"mode", text(axis)}, {"t1", integer_value(state)}, {"cmp", text(sign > 0 ? "GE" : "LE")},
                   {"rhs", value(rhs)}, {"name", text(name)}});
}

bool exposed_side(const GenerationContext& context, const Object& block, const std::string& axis, std::int64_t sign,
                  double y) {
    y = height(context, block, y);
    const std::size_t lo = axis == "X" ? 0 : 1, hi = axis == "X" ? 2 : 3;
    const Value options = faces(context, block, axis, sign);
    for (const auto& option : items(options))
        for (const auto& r : items(items(option)[3]))
            if (n(items(r)[lo]) <= y && y < n(items(r)[hi])) return true;
    return false;
}

bool convex_corner(const GenerationContext& context, const Object& block, std::int64_t x_direction,
                   std::int64_t z_escape, double y) {
    const auto box = expanded(solid_of(context, block));
    y = height(context, block, y);
    const double x = x_direction > 0 ? box[0].value : box[3].value, z = z_escape > 0 ? box[5].value : box[2].value;
    bool x_side = false, z_side = false;
    const Value x_faces = faces(context, block, "X", -x_direction), z_faces = faces(context, block, "Z", z_escape);
    for (const auto& o : items(x_faces))
        for (const auto& rv : items(items(o)[3])) {
            const Array& r = items(rv);
            x_side = x_side || (n(r[0]) <= y && y < n(r[2]) && n(r[1]) <= z && z <= n(r[3]));
        }
    if (!x_side) return false;
    for (const auto& o : items(z_faces))
        for (const auto& rv : items(items(o)[3])) {
            const Array& r = items(rv);
            z_side = z_side || (n(r[1]) <= y && y < n(r[3]) && n(r[0]) <= x && x <= n(r[2]));
        }
    return z_side;
}

Array corner_rows(const Array& box, std::int64_t first, std::int64_t second, std::int64_t x_direction,
                  std::int64_t z_escape, const Array& boxes_given) {
    const Array boxes = boxes_given.empty() ? Array{array(box)} : boxes_given;
    Array result{outside_row(box, "X", -x_direction, first), outside_row(box, "Z", z_escape, second)};
    for (auto& r : tangent_rows(side_run(box, boxes, "X", -x_direction, "Z"), "Z", first)) result.push_back(r);
    Object cross = fields(outside_row(box, "X", -x_direction, second));
    put(cross, "cmp", text(x_direction > 0 ? "GE" : "LE"));
    // Flipped from the outside side (face - margin) to the margin past the face.
    put(cross, "rhs", value(add(num(at(cross, "rhs")), flt(x_direction * 2 * kSolverMargin))));
    put(cross, "name", text("case:cross_x"));
    result.push_back(object(std::move(cross)));
    for (auto& r : tangent_rows(side_run(box, boxes, "Z", z_escape, "X"), "X", second)) result.push_back(r);
    return result;
}

std::vector<std::pair<Array, Array>> top_choices(const GenerationContext& context, const FaceOption& face,
                                                 const std::string& owner, std::int64_t state,
                                                 const std::string& name, bool jumping) {
    auto option = reachable_option(context, face, state, jumping,
                                   {{"owner", text(owner)},
                                    {"role", text(name == "case:lower_landing" ? "lower landing" : "top")}});
    if (!option) return {};
    Array rows;
    for (const auto& r : finite_rows(1, items(bounding_region(option->regions)), {{"t1", integer_value(state)}}))
        rows.push_back(with_name(r, name));
    Array choices;
    if (option->regions.size() > 1 && context.region_items) {
        Array options;
        for (const auto& region : option->regions) {
            Array named;
            for (const auto& r : finite_rows(1, items(region), {{"leg", integer_value(0)}, {"offset", integer_value(state)}}))
                named.push_back(with_name(r, name));
            options.push_back(array(std::move(named)));
        }
        choices.push_back(object({{"offset", integer_value(state)}, {"surface", text(option->surface)},
                                  {"owner", text(owner)}, {"options", array(std::move(options))}}));
    }
    return {{std::move(rows), std::move(choices)}};
}

std::vector<Array> ceiling_clearances(const GenerationContext& context, double feet, double top, bool jumping) {
    const Value& scene = context.gate->scene();
    std::map<std::int64_t, const Object*> blocks;  // {solidIndex: block}: the last block of a solid
    for (const auto& b : items(at(fields(scene), "blocks"))) blocks[as_int(at(fields(b), "solidIndex"))] = &fields(b);
    const Array& solids = items(at(fields(at(fields(scene), "collisionScene")), "solids"));
    std::vector<Array> choices;
    for (const auto& option : scene_contact_options(scene, context.gate->geometry(), 1, -1)) {
        if (!(feet <= n(option.plane) && n(option.plane) < top) || !candidate_option(context, option, "ceilingSolids"))
            continue;
        std::vector<std::string> ids;
        for (std::int64_t i : option.members) ids.push_back(as_text(at(*blocks.at(i), "id")));
        std::sort(ids.begin(), ids.end());
        std::string owner;
        for (const auto& id : ids) owner += (owner.empty() ? "" : "+") + id;
        const Object label{{"owner", text(owner)}, {"role", text("take-off ceiling")}};
        bool reachable = false;  // any(): the first reachable region stops the queries
        for (const auto& rv : option.regions) {
            const Array& r = items(rv);
            if (bounds_allowed_many(context, {Array{r[0], integer_value(0), r[1], r[2], integer_value(0), r[3]}}, 0,
                                    label, jumping, {"Y"})[0]) {
                reachable = true;
                break;
            }
        }
        if (!reachable) continue;
        Array rows;
        std::set<std::tuple<std::string, std::int64_t, double>> sides;
        for (std::int64_t index : option.members)
            for (const std::string axis : {"X", "Z"})
                for (std::int64_t sign : {-1, 1})
                    if (exposed_side(context, *blocks.at(index), axis, sign, n(option.plane))) {
                        Value row = outside_row(items(solids.at(index)), axis, sign, 0, "ceiling_clear:" + owner);
                        if (sides.insert({axis, sign, n(at(fields(row), "rhs"))}).second) rows.push_back(std::move(row));
                    }
        choices.push_back(std::move(rows));
    }
    // itertools.product(*choices): one row from each ceiling's set, the last ceiling varying fastest.
    std::vector<Array> product{Array{}};
    for (const Array& rows : choices) {
        std::vector<Array> next;
        for (const Array& prefix : product)
            for (const auto& row : rows) {
                Array extended = prefix;
                extended.push_back(row);
                next.push_back(std::move(extended));
            }
        product = std::move(next);
    }
    return product;
}

std::vector<double> corner_ledges(const GenerationContext& context, const Array& box, std::int64_t x_direction,
                                  std::int64_t z_escape, double feet, const Array& boxes) {
    double lo = -INFINITY, hi = INFINITY;
    std::optional<double> z;
    for (const auto& rv : corner_rows(box, 0, 1, x_direction, z_escape, boxes)) {
        const Object& r = fields(rv);
        if (as_int(at(r, "t1")) != 1) continue;
        const std::string& mode = as_text(at(r, "mode"));
        const double rhs = n(at(r, "rhs"));
        if (mode == "X" && as_text(at(r, "cmp")) == "GE") lo = std::max(lo, rhs);
        if (mode == "X" && as_text(at(r, "cmp")) == "LE") hi = std::min(hi, rhs);
        if (mode == "Z" && !z) z = rhs;
    }
    std::set<double> planes;
    for (const auto& option : context.gate->top_faces())
        if (n(option.plane) <= feet)
            for (const auto& rv : option.regions) {
                const Array& r = items(rv);
                if (n(r[0]) <= hi && n(r[2]) >= lo && n(r[1]) <= *z && *z <= n(r[3])) planes.insert(n(option.plane));
            }
    return {planes.begin(), planes.end()};
}

Value contact_event(const FaceOption& option, const std::string& axis, std::int64_t direction, std::int64_t offset,
                    const std::string& role, Object extra) {
    Object event{{"leg", integer_value(0)}, {"offset", integer_value(offset)}, {"axis", text(axis)},
                 {"direction", integer_value(direction)}, {"plane", option.plane}, {"boxes", array(option.boxes)},
                 {"surface", text(option.surface)}, {"regions", array(option.regions)}, {"role", text(role)}};
    for (auto& f : extra) event.push_back(std::move(f));
    return object(std::move(event));
}

Array region_choices(const GenerationContext& context, const Value& event_value, const std::string& owner) {
    const Object& event = fields(event_value);
    const Array& regions = items(at(event, "regions"));
    if (regions.size() == 1 || !context.region_items) return {};
    const std::string& axis = as_text(at(event, "axis"));
    const std::int64_t offset = as_int(at(event, "offset")) +
                                (axis == "Z" || (axis == "Y" && py_equal(get_or_null(event, "role"), text("step"))));
    Array options;
    for (const auto& r : regions)
        options.push_back(array(finite_rows(axis == "X" ? 0 : axis == "Y" ? 1 : 2, items(r),
                                            {{"leg", integer_value(0)}, {"offset", integer_value(offset)}})));
    return {object({{"offset", integer_value(offset)}, {"surface", at(event, "surface")}, {"owner", text(owner)},
                    {"options", array(std::move(options))}})};
}

std::optional<FaceOption> reachable_option(const GenerationContext& context, const FaceOption& option,
                                           std::int64_t tick, bool jumping, const Object& label) {
    FaceOption kept = option;
    kept.regions.clear();
    for (const auto& rv : option.regions) {  // each region's reachability, then its O10 check, as the Python
        const Array& r = items(rv);
        const Array bounds{r[0], integer_value(0), r[1], r[2], integer_value(0), r[3]};
        if (bounds_allowed_many(context, {bounds}, tick, label, jumping, {"Y"})[0] &&
            !behind_decision(context, r, tick, jumping, label))
            kept.regions.push_back(rv);
    }
    if (kept.regions.empty()) return std::nullopt;
    return kept;
}

}  // namespace pkc
