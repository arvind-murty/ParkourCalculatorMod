// Port of scene/geometry.py. normalize_geometry rebuilds the dicts the Python
// mutates in place, keeping their key order.
#include "pkc/scene/geometry.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <set>
#include <string>

#include "../value_ops.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const double kHalfWidth = f32(0.6) / 2;  // scene/route.HALF_WIDTH

double finite_number(const Value& value, const std::string& name) {  // schema.number
    if (auto* i = std::get_if<std::int64_t>(&value.data)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&value.data))
        if (std::isfinite(*d)) return *d;
    fail(name + " must be a finite number");
}

const Array& box_bounds(const Value* values, const std::string& name) {
    if (!values || !is_list(*values) || items(*values).size() != 6)
        fail(name + " requires minX,minY,minZ,maxX,maxY,maxZ");
    const Array& box = items(*values);
    double b[6];
    for (int i = 0; i < 6; ++i) b[i] = finite_number(box[i], name);
    if (b[0] >= b[3] || b[1] >= b[4] || b[2] >= b[5]) fail(name + " must have positive volume");
    return box;
}

std::int64_t int_of(const Value& value) {  // type(value) is int
    auto* i = std::get_if<std::int64_t>(&value.data);
    if (!i) fail("Climbable cells require integer X/Y/Z");
    return *i;
}

// scene/ladders.ladder_columns, for its checks: normalize_geometry ignores the columns.
std::set<std::array<std::int64_t, 3>> ladder_cells(const Value& cells) {
    if (!is_list(cells)) fail("Climbable cells require a list of integer X/Y/Z cells");
    std::set<std::array<std::int64_t, 3>> found;
    for (const auto& cell : items(cells)) {
        if (!is_list(cell) || items(cell).size() != 3) fail("Climbable cells require integer X/Y/Z");
        found.insert({int_of(items(cell)[0]), int_of(items(cell)[1]), int_of(items(cell)[2])});
    }
    return found;
}

bool has_only(const Object& object, std::initializer_list<const char*> keys) {
    return std::all_of(object.begin(), object.end(), [&](const auto& field) {
        return std::any_of(keys.begin(), keys.end(), [&](const char* key) { return field.first == key; });
    });
}

Value support_bounds(const Array& b) { return array({b[0], b[3], b[2], b[5]}); }

std::int64_t ordered(double value) {
    std::int64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits >= 0 ? bits : -(bits & 0x7fffffffffffffff);
}

double unordered(std::int64_t key) {
    std::int64_t bits = key >= 0 ? key : (-key | INT64_MIN);
    double value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// _first: least finite double satisfying a predicate monotone in its argument.
std::optional<double> first(const std::function<bool(double)>& predicate, double guess) {
    if (predicate(guess) && !predicate(std::nextafter(guess, -INFINITY))) return guess;
    if (!predicate(guess) && predicate(std::nextafter(guess, INFINITY))) return std::nextafter(guess, INFINITY);
    std::int64_t lo = ordered(-DBL_MAX), hi = ordered(DBL_MAX);
    if (!predicate(unordered(hi))) return std::nullopt;
    while (lo < hi) {
        __int128 sum = static_cast<__int128>(lo) + hi;  // Python's (lo + hi) // 2: floored, no overflow
        auto middle = static_cast<std::int64_t>(sum >= 0 ? sum / 2 : -((-sum + 1) / 2));
        if (predicate(unordered(middle))) {
            hi = middle;
        } else {
            lo = middle + 1;
        }
    }
    return unordered(lo);
}

double as_float(const Value& value) {  // float(value)
    double out;
    if (!numeric(value, out)) fail("could not convert to float");
    return out;
}

}  // namespace

Value normalize_geometry(const Value& scene_value) {
    Object scene = fields(scene_value);
    Object geometry;
    Array solids;
    Value bounds;
    if (const Value* collision = get(scene, "collisionScene")) {
        if (!is_object(*collision) || !has_only(fields(*collision), {"version", "profile", "bounds", "solids",
                                                                      "stepHeight", "ladders"}))
            fail("Unsupported collision scene");
        geometry = fields(*collision);
        const Value& version = get_or_null(geometry, "version");
        if (!std::holds_alternative<std::int64_t>(version.data) || std::get<std::int64_t>(version.data) != 1 ||
            !py_equal(get_or_null(geometry, "profile"), text("minecraft-1.8.9")))
            fail("Expected a minecraft-1.8.9 collision scene");
        const Value& solids_value = get_or_null(geometry, "solids");
        bounds = get_or_null(geometry, "bounds");
        if (!is_list(solids_value) || items(solids_value).empty()) fail("Supply collision solids");
        solids = items(solids_value);
        Array blocks;
        std::vector<std::int64_t> indices;
        for (const auto& block_value : items(at(scene, "blocks"))) {
            if (!is_object(block_value) ||
                !has_only(fields(block_value), {"id", "bounds", "top", "solidIndex", "sourceBlock"}))
                fail("Invalid derived collision support");
            Object block = fields(block_value);
            const Value index = get_or_null(block, "solidIndex");
            setdefault(block, "sourceBlock", get_or_null(block, "id"));
            const Value& source = at(block, "sourceBlock");
            if (!std::holds_alternative<std::string>(source.data) || std::get<std::string>(source.data).empty())
                fail("Collision supports require physical block provenance");
            auto* i = std::get_if<std::int64_t>(&index.data);
            if (!i || *i < 0 || *i >= static_cast<std::int64_t>(solids.size())) fail("Invalid support solid index");
            const Array& b = box_bounds(&solids[*i], "collision solid");
            if (!py_equal(get_or_null(block, "bounds"), support_bounds(b)) || !py_equal(get_or_null(block, "top"), b[4]))
                fail("Support must match its actual collision box");
            indices.push_back(*i);
            blocks.push_back(object(std::move(block)));
        }
        std::sort(indices.begin(), indices.end());
        bool every = indices.size() == solids.size();
        for (std::size_t k = 0; every && k < indices.size(); ++k) every = indices[k] == static_cast<std::int64_t>(k);
        if (!every) fail("Every collision solid must have its derived support entry");
        put(scene, "blocks", array(std::move(blocks)));
    } else {
        bounds = get_or_null(scene, "geometryBounds");
        Array supports;
        for (const auto& block_value : items(at(scene, "blocks"))) {
            if (!is_object(block_value) || fields(block_value).size() != 2 || !get(fields(block_value), "id") ||
                !get(fields(block_value), "boxes"))
                fail("Collision blocks require id and actual boxes");
            const Object& block = fields(block_value);
            const Value& id = at(block, "id");
            const Value& boxes = at(block, "boxes");
            if (!std::holds_alternative<std::string>(id.data) || std::get<std::string>(id.data).empty() ||
                !is_list(boxes) || items(boxes).empty())
                fail("Collision blocks require an ID and nonempty boxes");
            const std::string& name = std::get<std::string>(id.data);
            for (std::size_t index = 0; index < items(boxes).size(); ++index) {
                const Array& b = box_bounds(&items(boxes)[index], "collision box");
                std::string support = items(boxes).size() == 1 ? name : name + "#" + std::to_string(index);
                supports.push_back(object({{"id", text(support)},
                                           {"sourceBlock", id},
                                           {"solidIndex", integer_value(static_cast<std::int64_t>(solids.size()))},
                                           {"bounds", support_bounds(b)},
                                           {"top", b[4]}}));
                solids.push_back(items(boxes)[index]);
            }
        }
        put(scene, "blocks", array(std::move(supports)));
        geometry = {{"version", integer_value(1)}, {"profile", text("minecraft-1.8.9")}, {"bounds", bounds},
                    {"solids", array(solids)}, {"stepHeight", real(.6)}};
    }
    put(scene, "collisionScene", object(geometry));  // where the Python's dict sits; ladders update it below
    const Array& box = box_bounds(&bounds, "geometryBounds");
    double limits[6];
    for (int i = 0; i < 6; ++i) limits[i] = finite_number(box[i], "geometryBounds");
    const Value* step = get(geometry, "stepHeight");
    if (step && !py_equal(*step, real(.6))) fail("This route family uses normal step height .6");
    if (const Value* declared = get(scene, "geometryBounds"); declared && !py_equal(*declared, bounds))
        fail("Inconsistent geometry coverage bounds");
    for (const auto& solid : solids) {
        const Array& b = box_bounds(&solid, "collision solid");
        for (int i = 0; i < 3; ++i)
            if (finite_number(b[i], "") < limits[i] || finite_number(b[i + 3], "") > limits[i + 3])
                fail("Collision solid outside supplied geometry bounds");
    }
    const Value* climbable = get(scene, "climbableCells");
    const Value* ladders = get(geometry, "ladders");
    Value cells = climbable ? *climbable : ladders ? *ladders : array({});
    ladder_cells(cells);
    if (ladders) ladder_cells(*ladders);
    if (climbable && !is_null(*climbable) && ladders && !is_null(*ladders) &&
        ladder_cells(*climbable) != ladder_cells(*ladders))
        fail("Conflicting climbable cells and scene ladder declarations");
    if (py_truthy(cells)) {
        put(geometry, "ladders", cells);
        put(scene, "collisionScene", object(std::move(geometry)));
        put(scene, "climbableCells", cells);
    }
    return object(std::move(scene));
}

std::pair<double, double> support_center_interval(double lo, double hi) {
    auto start = first([&](double center) { return center + kHalfWidth > lo; }, lo - kHalfWidth);
    auto after = first([&](double center) { return !(center - kHalfWidth < hi); }, hi + kHalfWidth);
    double last = after ? std::nextafter(*after, -INFINITY) : DBL_MAX;
    if (!start || *start > last) fail("No player center overlaps the support interval");
    return {*start, last};
}

Value route_footprints(const Value& blocks_value, bool standing) {
    if (!is_list(blocks_value) || items(blocks_value).size() < 2) fail("Supply a route of support blocks");
    Array footprints;
    for (const auto& block_value : items(blocks_value)) {
        const Object& block = fields(block_value);
        const Value* bounds = standing ? get(block, "standingBounds") : nullptr;
        if (bounds && is_null(*bounds)) bounds = nullptr;
        if (!bounds) {
            const Value* center = get(block, "centerBounds");
            if (center && !is_null(*center)) {
                Array values;
                for (const auto& v : items(*center)) values.push_back(real(as_float(v)));
                footprints.push_back(array(std::move(values)));
                continue;
            }
        }
        const Array& b = items(bounds ? *bounds : at(block, "bounds"));
        if (b.size() != 4) fail("ValueError: wrong number of support bounds");
        auto x = support_center_interval(as_float(b[0]), as_float(b[1]));
        auto z = support_center_interval(as_float(b[2]), as_float(b[3]));
        footprints.push_back(array({real(x.first), real(x.second), real(z.first), real(z.second)}));
    }
    return array(std::move(footprints));
}

}  // namespace pkc
