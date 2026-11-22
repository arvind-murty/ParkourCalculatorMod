// Port of spatial/surfaces.py's free_rectangles and SceneGeometry computations.
// Coordinates keep Python's int/float kinds (a solid's top passes through
// expanded_box unchanged), since contact options are named by a key over them.
#include "pkc/spatial/surfaces.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>

#include "../value_ops.hpp"
#include "pkc/spatial/regions.hpp"
#include "pkc/vertical.hpp"
#include "py_order.hpp"

namespace pkc {
namespace {

const double kMinFreeWidth = .012;  // scene/geometry.MIN_FREE_WIDTH
const double kHalfWidth = f32(0.6) / 2;
const double kBodyHeight = f32(1.8);
const int kTangentAxes[3][2] = {{1, 2}, {0, 2}, {0, 1}};

bool thin(double width) { return width < kMinFreeWidth / 2; }

Num number_of(const Value& v) {
    if (auto* i = std::get_if<std::int64_t>(&v.data)) return whole(*i);
    return flt(std::get<double>(v.data));
}

Num max_of(const Num& a, const Num& b) { return b.value > a.value ? b : a; }  // Python max(a, b)
Num min_of(const Num& a, const Num& b) { return b.value < a.value ? b : a; }  // Python min(a, b)

NumBox embed(const Rect& r) { return {r[0], flt(0.0), r[1], r[2], flt(0.0), r[3]}; }
Rect project(const NumBox& b) { return {b[0], b[2], b[3], b[5]}; }

std::vector<Rect> cover(const std::vector<Rect>& rectangles) {  // _cover
    std::vector<NumBox> embedded;
    for (const auto& r : rectangles) embedded.push_back(embed(r));
    std::vector<Rect> out;
    for (const auto& b : canonical_cover(embedded)) out.push_back(project(b));
    return out;
}

std::optional<Rect> overlap(const Rect& a, const Rect& b, bool positive_area = true) {
    Rect r{max_of(a[0], b[0]), max_of(a[1], b[1]), min_of(a[2], b[2]), min_of(a[3], b[3])};
    if (positive_area) {
        if (r[0].value < r[2].value && r[1].value < r[3].value) return r;
    } else if (r[0].value <= r[2].value && r[1].value <= r[3].value) {
        return r;
    }
    return std::nullopt;
}

bool adjacent(const Rect& a, const Rect& b) {
    auto o = overlap(a, b, false);
    return o && ((*o)[0].value < (*o)[2].value || (*o)[1].value < (*o)[3].value);
}

using Fragment = std::pair<std::int64_t, Rect>;  // (owner solid, rect)

// _components: edge-connected fragment groups as (cover, owners in first-seen order).
std::vector<std::pair<std::vector<Rect>, std::vector<std::int64_t>>> components(const std::vector<Fragment>& fragments) {
    std::set<std::size_t> pending;
    for (std::size_t i = 0; i < fragments.size(); ++i) pending.insert(i);
    std::vector<std::pair<std::vector<Rect>, std::vector<std::int64_t>>> result;
    while (!pending.empty()) {
        const std::size_t first = *pending.begin();
        pending.erase(pending.begin());
        std::vector<std::size_t> work{first}, component;
        while (!work.empty()) {
            const std::size_t at = work.back();
            work.pop_back();
            component.push_back(at);
            const std::vector<std::size_t> snapshot(pending.begin(), pending.end());
            for (std::size_t other : snapshot)
                if (adjacent(fragments[at].second, fragments[other].second)) {
                    pending.erase(other);
                    work.push_back(other);
                }
        }
        std::vector<Rect> rects;
        std::vector<std::int64_t> owners;
        for (std::size_t index : component) {
            rects.push_back(fragments[index].second);
            if (std::find(owners.begin(), owners.end(), fragments[index].first) == owners.end())
                owners.push_back(fragments[index].first);
        }
        result.emplace_back(cover(rects), owners);
    }
    return result;
}

std::vector<Rect> maximal_rectangles(const std::vector<Rect>& cover_rects) {  // _maximal_rectangles
    std::vector<Num> u_values, v_values;
    for (const auto& r : cover_rects) u_values.push_back(r[0]);
    for (const auto& r : cover_rects) u_values.push_back(r[2]);
    for (const auto& r : cover_rects) v_values.push_back(r[1]);
    for (const auto& r : cover_rects) v_values.push_back(r[3]);
    const std::vector<Num> us = sorted_set(u_values), vs = sorted_set(v_values);
    const std::size_t nu = us.size(), nv = vs.size();
    std::vector<std::vector<bool>> filled(nu ? nu - 1 : 0, std::vector<bool>(nv ? nv - 1 : 0, false));
    for (std::size_t i = 0; i + 1 < nu; ++i)
        for (std::size_t j = 0; j + 1 < nv; ++j) {
            const double mu = (us[i].value + us[i + 1].value) / 2, mv = (vs[j].value + vs[j + 1].value) / 2;
            filled[i][j] = std::any_of(cover_rects.begin(), cover_rects.end(), [&](const Rect& r) {
                return r[0].value <= mu && mu <= r[2].value && r[1].value <= mv && mv <= r[3].value;
            });
        }
    auto runs = [&](std::size_t i0, std::size_t i1) {
        std::vector<std::pair<std::size_t, std::size_t>> rows;
        std::optional<std::size_t> start;
        for (std::size_t j = 0; j + 1 < nv; ++j) {
            bool all = true;
            for (std::size_t i = i0; i < i1; ++i) all = all && filled[i][j];
            if (all) {
                if (!start) start = j;
            } else if (start) {
                rows.emplace_back(*start, j);
                start.reset();
            }
        }
        if (start) rows.emplace_back(*start, nv - 1);
        return rows;
    };
    std::vector<Rect> result;
    for (std::size_t i0 = 0; i0 < nu; ++i0)
        for (std::size_t i1 = i0 + 1; i1 < nu; ++i1)
            for (const auto& [j0, j1] : runs(i0, i1)) {
                bool wider = false;
                for (long i : {static_cast<long>(i0) - 1, static_cast<long>(i1)}) {
                    if (i < 0 || i >= static_cast<long>(nu) - 1) continue;
                    bool all = true;
                    for (std::size_t j = j0; j < j1; ++j) all = all && filled[i][j];
                    wider = wider || all;
                }
                if (!wider) result.push_back({us[i0], vs[j0], us[i1], vs[j1]});
            }
    return result;
}

std::vector<Rect> erode(const std::vector<Rect>& cover_rects, const std::array<Num, 2>& shrink) {  // _erode
    std::vector<Rect> pieces;
    for (const auto& r : maximal_rectangles(cover_rects)) {
        Rect piece{add(r[0], shrink[0]), add(r[1], shrink[1]), sub(r[2], shrink[0]), sub(r[3], shrink[1])};
        if (piece[0].value < piece[2].value && piece[1].value < piece[3].value) pieces.push_back(piece);
    }
    return cover(pieces);
}

Value rect_value(const Rect& r) { return array({value(r[0]), value(r[1]), value(r[2]), value(r[3])}); }

Value ints(const std::vector<std::int64_t>& values) {
    Array out;
    for (auto v : values) out.push_back(integer_value(v));
    return array(std::move(out));
}

}  // namespace

std::vector<Rect> free_rectangles(const std::vector<NumBox>& boxes) {
    std::vector<Num> edge_values;
    for (const auto& b : boxes) {
        edge_values.push_back(b[0]);
        edge_values.push_back(b[3]);
    }
    std::vector<Num> xs{flt(-INFINITY)};
    for (const Num& x : sorted_set(edge_values)) xs.push_back(x);
    xs.push_back(flt(INFINITY));
    auto empty = [&](double x0, double x1, double z0, double z1) {
        return std::none_of(boxes.begin(), boxes.end(), [&](const NumBox& b) {
            return b[0].value < x1 && x0 < b[3].value && b[2].value < z1 && z0 < b[5].value;
        });
    };
    std::vector<Rect> result;
    for (std::size_t i = 0; i < xs.size(); ++i)
        for (std::size_t j = i + 1; j < xs.size(); ++j) {
            const Num xa = xs[i], xb = xs[j];
            if (thin(xb.value - xa.value)) continue;
            std::vector<std::array<Num, 2>> spans;
            for (const auto& b : boxes)
                if (b[0].value < xb.value && xa.value < b[3].value) spans.push_back({b[2], b[5]});
            std::stable_sort(spans.begin(), spans.end(), value_less<2>);
            Num start = flt(-INFINITY);
            std::vector<std::array<Num, 2>> free;
            for (const auto& [lo, hi] : spans) {
                if (lo.value > start.value) free.push_back({start, lo});
                start = max_of(start, hi);
            }
            free.push_back({start, flt(INFINITY)});
            for (const auto& [za, zb] : free) {
                if (thin(zb.value - za.value)) continue;
                if ((i > 0 && empty(xs[i - 1].value, xb.value, za.value, zb.value)) ||
                    (j < xs.size() - 1 && empty(xa.value, xs[j + 1].value, za.value, zb.value)))
                    continue;
                result.push_back({xa, za, xb, zb});
            }
        }
    return result;
}

Value free_rectangles(const Value& boxes_value) {
    std::vector<NumBox> boxes;
    for (const auto& b : items(boxes_value)) {
        NumBox box;
        for (int i = 0; i < 6; ++i) box[i] = number_of(items(b)[i]);
        boxes.push_back(box);
    }
    Array out;
    for (const auto& r : free_rectangles(boxes)) out.push_back(rect_value(r));
    return array(std::move(out));
}

SceneGeometry::SceneGeometry(const Value& solids) {
    for (const auto& solid : items(solids)) {
        const Array& s = items(solid);
        boxes_.push_back({sub(number_of(s[0]), flt(kHalfWidth)), sub(number_of(s[1]), flt(kBodyHeight)),
                          sub(number_of(s[2]), flt(kHalfWidth)), add(number_of(s[3]), flt(kHalfWidth)),
                          number_of(s[4]), add(number_of(s[5]), flt(kHalfWidth))});
    }
}

std::vector<std::int64_t> SceneGeometry::touching(std::int64_t index, const std::vector<std::int64_t>& indices) const {
    // near(): every box overlapping the reach rectangle; the Python grid only narrows the search.
    const NumBox& box = boxes_[index];
    const double reach = kMinFreeWidth / 2;
    const double x0 = box[0].value - reach, z0 = box[2].value - reach, x1 = box[3].value + reach,
                 z1 = box[5].value + reach;
    std::vector<std::int64_t> result;
    for (std::int64_t b = 0; b < static_cast<std::int64_t>(boxes_.size()); ++b) {
        const NumBox& other = boxes_[b];
        if (!(other[0].value <= x1 && x0 <= other[3].value && other[2].value <= z1 && z0 <= other[5].value)) continue;
        if (b == index || !std::binary_search(indices.begin(), indices.end(), b)) continue;
        bool touches = true;
        for (int k = 0; k < 3; ++k)
            touches = touches && thin(std::max(box[k].value, other[k].value) - std::min(box[k + 3].value, other[k + 3].value));
        if (touches) result.push_back(b);
    }
    return result;
}

std::vector<std::vector<std::int64_t>> SceneGeometry::cluster_groups(std::vector<std::int64_t> indices) const {
    std::sort(indices.begin(), indices.end());  // tuple(sorted(indices)); repeats stay, as in the Python
    std::map<std::int64_t, std::int64_t> parent;
    for (auto i : indices) parent[i] = i;
    auto root = [&](std::int64_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };
    for (auto a : indices)
        for (auto b : touching(a, indices))
            if (b > a) parent[root(b)] = root(a);
    std::map<std::int64_t, std::vector<std::int64_t>> groups;
    std::vector<std::int64_t> order;
    for (auto i : indices) {
        const auto r = root(i);
        if (!groups.count(r)) order.push_back(r);
        groups[r].push_back(i);
    }
    std::vector<std::vector<std::int64_t>> result;
    for (auto r : order) result.push_back(groups[r]);
    std::sort(result.begin(), result.end());
    return result;
}

Value SceneGeometry::sides(const std::vector<std::int64_t>& members, const std::vector<std::int64_t>& active) const {
    std::vector<NumBox> boxes;
    for (auto k : active) boxes.push_back(boxes_[members[k]]);
    struct Bound {
        std::string axis;
        std::int64_t direction;
        Num plane;
        std::vector<std::int64_t> owners;
    };
    auto bound_less = [](const Bound& a, const Bound& b) {
        if (a.axis != b.axis) return a.axis < b.axis;
        if (a.direction != b.direction) return a.direction < b.direction;
        if (a.plane.value != b.plane.value) return a.plane.value < b.plane.value;
        return a.owners < b.owners;
    };
    struct SideRow {
        std::vector<Bound> bounds;
        Rect region;
    };
    std::vector<SideRow> sides;
    for (const Rect& region : free_rectangles(boxes)) {
        std::vector<Bound> bounds;
        const std::tuple<const char*, int, std::int64_t, Num> edges[4] = {
            {"X", 3, 1, region[0]}, {"X", 0, -1, region[2]}, {"Z", 5, 1, region[1]}, {"Z", 2, -1, region[3]}};
        for (const auto& [axis, edge, direction, plane] : edges) {
            if (std::isinf(plane.value)) continue;
            const bool x = std::string(axis) == "X";
            const int other[4] = {x ? 2 : 0, x ? 5 : 3, x ? 1 : 0, x ? 3 : 2};
            std::vector<std::int64_t> owners;
            for (std::size_t n = 0; n < active.size(); ++n) {
                const NumBox& b = boxes[n];
                if (b[edge].value == plane.value && b[other[0]].value <= region[other[3]].value &&
                    region[other[2]].value <= b[other[1]].value)
                    owners.push_back(members[active[n]]);
            }
            bounds.push_back({axis, direction, plane, owners});
        }
        std::stable_sort(bounds.begin(), bounds.end(), bound_less);
        sides.push_back({bounds, region});
    }
    std::stable_sort(sides.begin(), sides.end(), [](const SideRow& a, const SideRow& b) {
        // sorted(sides, key=lambda s: s.bounds): tuples of (axis, direction, plane) compared in order
        for (std::size_t i = 0; i < std::min(a.bounds.size(), b.bounds.size()); ++i) {
            const Bound &x = a.bounds[i], &y = b.bounds[i];
            if (x.axis != y.axis) return x.axis < y.axis;
            if (x.direction != y.direction) return x.direction < y.direction;
            if (x.plane.value != y.plane.value) return x.plane.value < y.plane.value;
        }
        return a.bounds.size() < b.bounds.size();
    });
    Array out;
    for (const auto& side : sides) {
        Array bounds, owners;
        for (const auto& b : side.bounds) {
            bounds.push_back(array({text(b.axis), integer_value(b.direction), value(b.plane)}));
            owners.push_back(ints(b.owners));
        }
        out.push_back(array({array(std::move(bounds)), array(std::move(owners)), rect_value(side.region)}));
    }
    return array(std::move(out));
}

Value SceneGeometry::contact_options(int axis, std::int64_t sign, const Value& clearance_value,
                                     std::vector<std::int64_t> members) const {
    const Num clearance = number_of(clearance_value);
    std::sort(members.begin(), members.end());
    const int u = kTangentAxes[axis][0], v = kTangentAxes[axis][1];
    // dicts keyed by plane value, keeping the first key object seen
    std::vector<Num> planes;
    std::vector<std::vector<Fragment>> buckets;
    std::vector<std::vector<Rect>> faces;
    auto slot = [&](const Num& plane) {
        for (std::size_t k = 0; k < planes.size(); ++k)
            if (planes[k].value == plane.value) return k;
        planes.push_back(plane);
        buckets.emplace_back();
        faces.emplace_back();
        return planes.size() - 1;
    };
    for (auto i : members) {
        const NumBox& box = boxes_[i];
        const Rect rect{box[u], box[v], box[u + 3], box[v + 3]};
        const Num plane = box[sign < 0 ? axis : axis + 3];
        const std::size_t k = slot(plane);
        faces[k].push_back(rect);
        std::vector<NumBox> pieces{embed(rect)};
        for (const NumBox& other : boxes_) {
            const Rect cut{other[u], other[v], other[u + 3], other[v + 3]};
            if (!(other[axis].value < plane.value && plane.value < other[axis + 3].value) || !overlap(rect, cut))
                continue;
            const NumBox cutter{cut[0], flt(-1.0), cut[1], cut[2], flt(1.0), cut[3]};
            std::vector<NumBox> next;
            for (const auto& piece : pieces)
                for (const auto& b : subtract_open(piece, cutter))
                    if (b[0].value < b[3].value && b[2].value < b[5].value) next.push_back(b);
            pieces = std::move(next);
        }
        std::vector<Rect> projected;
        for (const auto& b : pieces) projected.push_back(project(b));
        for (const auto& r : cover(projected)) buckets[k].emplace_back(i, r);
    }
    const std::array<Num, 2> shrink{u == 1 ? whole(0) : clearance, v == 1 ? whole(0) : clearance};
    std::vector<std::size_t> order(planes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return planes[a].value < planes[b].value; });
    Array result;
    for (std::size_t k : order) {
        const Num& plane = planes[k];
        const std::vector<Rect> inner = erode(cover(faces[k]), shrink);
        for (const auto& [component_cover, contributors] : components(buckets[k])) {
            std::vector<Rect> overlaps;
            for (const auto& a : inner)
                for (const auto& b : component_cover)
                    if (auto o = overlap(a, b)) overlaps.push_back(*o);
            const std::vector<Rect> regions = cover(overlaps);
            if (regions.empty()) continue;
            std::vector<std::int64_t> owners = contributors;
            std::sort(owners.begin(), owners.end());
            Array cover_values, region_values;
            for (const auto& r : component_cover) cover_values.push_back(rect_value(r));
            for (const auto& r : regions) region_values.push_back(rect_value(r));
            const std::string key = geometry_key(array({integer_value(axis), integer_value(sign), value(plane),
                                                        array(cover_values)}));
            result.push_back(array({text("surface:" + key), value(plane), ints(owners), array(std::move(region_values))}));
        }
    }
    return array(std::move(result));
}

}  // namespace pkc
