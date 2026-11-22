// Port of spatial/cells.subtract_open.
#include "pkc/spatial/cells.hpp"

namespace pkc {

std::vector<NumBox> subtract_open(const NumBox& box, const NumBox& obstacle, bool boundaries) {
    for (int a = 0; a < 3; ++a)
        if (box[a].value == box[a + 3].value &&
            !(obstacle[a].value < box[a].value && box[a].value < obstacle[a + 3].value))
            return {box};
    for (int a = 0; a < 3; ++a)
        if (box[a].value < box[a + 3].value &&
            (box[a + 3].value <= obstacle[a].value || box[a].value >= obstacle[a + 3].value))
            return {box};
    NumBox middle = box;
    std::vector<NumBox> pieces;
    for (int axis = 0; axis < 3; ++axis) {
        if (middle[axis].value == middle[axis + 3].value) continue;
        const Num lo = obstacle[axis].value > middle[axis].value ? obstacle[axis] : middle[axis];   // max(m, o)
        const Num hi = obstacle[axis + 3].value < middle[axis + 3].value ? obstacle[axis + 3] : middle[axis + 3];  // min
        if (middle[axis].value < lo.value ||
            (boundaries && middle[axis].value == lo.value && lo.value == obstacle[axis].value)) {
            NumBox piece = middle;
            piece[axis + 3] = lo;
            pieces.push_back(piece);
        }
        if (hi.value < middle[axis + 3].value ||
            (boundaries && hi.value == middle[axis + 3].value && middle[axis + 3].value == obstacle[axis + 3].value)) {
            NumBox piece = middle;
            piece[axis] = hi;
            pieces.push_back(piece);
        }
        middle[axis] = lo;
        middle[axis + 3] = hi;
    }
    return pieces;
}

}  // namespace pkc

// ---- decompose and the cell helpers it uses (spatial/cells.py)

#include <chrono>
#include <map>

#include "../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "pkc/scene/static_collision.hpp"
#include "pkc/spatial/regions.hpp"
#include "pkc/vertical.hpp"
#include "py_order.hpp"

namespace pkc {
namespace {

const double kMinFreeWidth = .012;  // scene/geometry.MIN_FREE_WIDTH
bool thin(double width) { return width < kMinFreeWidth / 2; }
Num max_of(const Num& a, const Num& b) { return b.value > a.value ? b : a; }
Num min_of(const Num& a, const Num& b) { return b.value < a.value ? b : a; }

Num number_of(const Value& v) {
    if (auto* i = std::get_if<std::int64_t>(&v.data)) return whole(*i);
    if (auto* d = std::get_if<double>(&v.data)) return flt(*d);
    fail("expected a number");
}

class Deadline {
public:
    explicit Deadline(std::optional<double> seconds) {
        if (seconds) end_ = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                                std::chrono::duration<double>(*seconds));
    }
    void check(const char* message) const {
        if (end_ && std::chrono::steady_clock::now() >= *end_) throw Timeout(message);
    }

private:
    std::optional<std::chrono::steady_clock::time_point> end_;
};

// merge_boxes: merge touching boxes along each axis until nothing changes.
std::vector<NumBox> merge_boxes(const std::vector<NumBox>& boxes) {
    std::vector<NumBox> values = sorted_set(boxes);
    bool changed = true;
    while (changed) {
        changed = false;
        for (int axis = 0; axis < 3; ++axis) {
            // groups keyed by the other four coordinates, in first-seen order
            std::vector<std::pair<std::array<Num, 4>, std::vector<NumBox>>> groups;
            for (const auto& b : values) {
                std::array<Num, 4> key;
                int k = 0;
                for (int i = 0; i < 6; ++i)
                    if (i != axis && i != axis + 3) key[k++] = b[i];
                auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return value_equal(g.first, key); });
                if (found == groups.end()) {
                    groups.push_back({key, {b}});
                } else {
                    found->second.push_back(b);
                }
            }
            std::vector<NumBox> merged;
            for (auto& [key, group] : groups) {
                std::stable_sort(group.begin(), group.end(), [&](const NumBox& a, const NumBox& b) {
                    if (a[axis].value != b[axis].value) return a[axis].value < b[axis].value;
                    return a[axis + 3].value < b[axis + 3].value;
                });
                NumBox current = group[0];
                for (std::size_t i = 1; i < group.size(); ++i) {
                    const NumBox& other = group[i];
                    if (current[axis + 3].value >= other[axis].value) {
                        current[axis + 3] = max_of(current[axis + 3], other[axis + 3]);
                        changed = true;
                    } else {
                        merged.push_back(current);
                        current = other;
                    }
                }
                merged.push_back(current);
            }
            values = sorted_set(merged);
        }
    }
    return values;
}

std::vector<NumBox> union_subtract(const std::vector<NumBox>& boxes, const std::vector<NumBox>& solids, std::int64_t limit,
                                   bool boundaries, const Deadline& deadline) {
    std::vector<NumBox> result = sorted_set(boxes);
    for (const auto& obstacle : sorted_set(solids)) {
        deadline.check("Cell decomposition deadline");
        std::vector<NumBox> pieces;
        for (const auto& box : result)
            for (const auto& piece : subtract_open(box, obstacle, boundaries)) pieces.push_back(piece);
        result = sorted_set(pieces);
        if (static_cast<std::int64_t>(result.size()) > limit)
            fail("Cell decomposition exceeded max_cells; geometry was not truncated");
    }
    return result;
}

struct Patch {
    std::int64_t cell, solid;
    Value owner;
    int axis, side;
    std::string kind;
    NumBox bounds;
};

std::vector<Patch> contact_patches(const std::vector<NumBox>& cells, const std::vector<NumBox>& solids,
                                   const std::map<std::int64_t, Value>& owners) {
    std::vector<Patch> contacts;
    for (std::size_t c = 0; c < cells.size(); ++c)
        for (std::size_t i = 0; i < solids.size(); ++i)
            for (int axis = 0; axis < 3; ++axis)
                for (int side : {-1, 1}) {
                    NumBox face = solids[i];
                    face[axis] = face[axis + 3] = solids[i][side < 0 ? axis : axis + 3];
                    const auto overlap = intersection(cells[c], face);
                    if (!overlap) continue;
                    bool slim = false;
                    for (int a = 0; a < 3; ++a)
                        if (a != axis && thin((*overlap)[a + 3].value - (*overlap)[a].value)) slim = true;
                    if (slim) continue;
                    auto owner = owners.find(static_cast<std::int64_t>(i));
                    if (owner == owners.end()) fail("KeyError: " + std::to_string(i));
                    const std::string kind = axis == 1 && side == 1 ? "support" : axis == 1 ? "ceiling" : "wall";
                    contacts.push_back({static_cast<std::int64_t>(c), static_cast<std::int64_t>(i), owner->second, axis,
                                        side, kind, *overlap});
                }
    return contacts;
}

Value box_value(const NumBox& b) {
    Array out;
    for (const auto& n : b) out.push_back(value(n));
    return array(std::move(out));
}

}  // namespace

std::optional<NumBox> intersection(const NumBox& a, const NumBox& b) {
    NumBox r{max_of(a[0], b[0]), max_of(a[1], b[1]), max_of(a[2], b[2]),
             min_of(a[3], b[3]), min_of(a[4], b[4]), min_of(a[5], b[5])};
    for (int i = 0; i < 3; ++i)
        if (r[i].value > r[i + 3].value) return std::nullopt;
    return r;
}

int dimension(const NumBox& box) {
    int d = 0;
    for (int a = 0; a < 3; ++a) d += box[a].value < box[a + 3].value;
    return d;
}

Value decompose(const Value& scene_value, std::int64_t max_cells, std::optional<double> seconds, bool refine_support) {
    const Deadline deadline(seconds);
    const Object& scene = fields(scene_value);
    const Object& geometry = fields(at(scene, "collisionScene"));
    const Value& bounds_value = get_or_null(geometry, "bounds");
    if (!is_list(bounds_value) || items(bounds_value).size() != 6)
        fail("decomposition bounds requires minX,minY,minZ,maxX,maxY,maxZ");
    NumBox source;
    for (int i = 0; i < 6; ++i) {
        const Value& v = items(bounds_value)[i];
        if (!(std::holds_alternative<std::int64_t>(v.data) || (std::holds_alternative<double>(v.data) && std::isfinite(std::get<double>(v.data)))))
            fail("decomposition bounds must be a finite number");
        source[i] = number_of(v);
    }
    if (source[0].value >= source[3].value || source[1].value >= source[4].value || source[2].value >= source[5].value)
        fail("decomposition bounds must have positive volume");
    const double half = f32(0.6) / 2, body = f32(1.8);
    const NumBox bounds{add(source[0], flt(half)), source[1], add(source[2], flt(half)),
                        sub(source[3], flt(half)), sub(source[4], flt(body)), sub(source[5], flt(half))};
    for (int a = 0; a < 3; ++a)
        if (bounds[a].value > bounds[a + 3].value) fail("Scene coverage is smaller than the player body");
    std::vector<NumBox> solids;
    for (const auto& s_value : items(at(geometry, "solids"))) {
        const Array& s = items(s_value);
        solids.push_back({sub(number_of(s[0]), flt(half)), sub(number_of(s[1]), flt(body)), sub(number_of(s[2]), flt(half)),
                          add(number_of(s[3]), flt(half)), number_of(s[4]), add(number_of(s[5]), flt(half))});
    }
    const std::vector<NumBox> raw = union_subtract({bounds}, merge_boxes(solids), max_cells, false, deadline);
    const std::vector<NumBox> volumes = merge_boxes(raw);
    std::vector<NumBox> faces;
    for (const auto& solid : solids)
        for (int axis = 0; axis < 3; ++axis)
            for (int side : {-1, 1}) {
                NumBox b = solid;
                b[axis] = b[axis + 3] = solid[side < 0 ? axis : axis + 3];
                if (auto clipped = intersection(bounds, b)) faces.push_back(*clipped);
            }
    std::vector<NumBox> strata = merge_boxes(union_subtract(faces, solids, max_cells, true, deadline));
    std::vector<NumBox> kept;
    for (const auto& b : strata) {
        const bool inside = std::any_of(volumes.begin(), volumes.end(), [&](const NumBox& v) {
            auto o = intersection(b, v);
            return o && value_equal(*o, b);
        });
        std::array<double, 3> extents{b[3].value - b[0].value, b[4].value - b[1].value, b[5].value - b[2].value};
        std::sort(extents.begin(), extents.end());
        if (!inside && !thin(extents[1]) && !thin(extents[2])) kept.push_back(b);
    }
    std::vector<NumBox> all = volumes;
    all.insert(all.end(), kept.begin(), kept.end());
    std::vector<NumBox> cells = sorted_set(all);
    if (static_cast<std::int64_t>(cells.size()) > max_cells) fail("Cell decomposition exceeded max_cells");
    std::map<std::int64_t, Value> owners;  // {solidIndex: sourceBlock or id}; the last block wins
    for (const auto& b_value : items(at(scene, "blocks"))) {
        const Object& b = fields(b_value);
        const Value* source_block = get(b, "sourceBlock");
        owners[std::get<std::int64_t>(at(b, "solidIndex").data)] = source_block ? *source_block : at(b, "id");
    }
    std::vector<Patch> contacts = contact_patches(cells, solids, owners);
    const std::size_t base_count = cells.size();
    std::vector<std::vector<std::int64_t>> parents;
    for (std::size_t c = 0; c < cells.size(); ++c) parents.push_back({static_cast<std::int64_t>(c)});
    if (refine_support) {
        // by_owner: (cell, owner) -> support patch bounds at the cell's floor, first seen kept
        std::vector<std::pair<std::pair<std::int64_t, std::string>, std::vector<NumBox>>> by_owner;
        for (const auto& patch : contacts) {
            if (patch.kind != "support" || cells[patch.cell][1].value != patch.bounds[1].value) continue;
            const std::pair<std::int64_t, std::string> key{patch.cell, dumps(patch.owner, false, true)};
            auto found = std::find_if(by_owner.begin(), by_owner.end(), [&](const auto& e) { return e.first == key; });
            if (found == by_owner.end()) {
                by_owner.push_back({key, {}});
                found = by_owner.end() - 1;
            }
            if (std::none_of(found->second.begin(), found->second.end(), [&](const NumBox& b) { return value_equal(b, patch.bounds); }))
                found->second.push_back(patch.bounds);
        }
        std::map<std::int64_t, std::vector<NumBox>> by_cell;
        for (const auto& [key, patches] : by_owner)
            for (const auto& b : canonical_cover(patches)) {
                auto& set = by_cell[key.first];
                if (std::none_of(set.begin(), set.end(), [&](const NumBox& x) { return value_equal(x, b); })) set.push_back(b);
            }
        std::vector<std::pair<NumBox, std::vector<std::int64_t>>> refined;
        for (std::size_t c = 0; c < cells.size(); ++c) {
            deadline.check("Support refinement deadline");
            std::vector<NumBox> pieces{cells[c]};
            std::vector<NumBox> cuts = by_cell.count(c) ? sorted_set(by_cell[c]) : std::vector<NumBox>{};
            for (const auto& patch : cuts)
                for (int axis : {0, 2})
                    for (const Num& edge : {patch[axis], patch[axis + 3]}) {
                        std::vector<NumBox> next;
                        for (const auto& box : pieces) {
                            if (box[axis].value < edge.value && edge.value < box[axis + 3].value) {
                                NumBox lo = box, hi = box;
                                lo[axis + 3] = edge;
                                hi[axis] = edge;
                                next.push_back(lo);
                                next.push_back(hi);
                            } else {
                                next.push_back(box);
                            }
                        }
                        pieces = std::move(next);
                        if (static_cast<std::int64_t>(pieces.size() + refined.size()) > max_cells)
                            fail("Support refinement exceeded max_cells; geometry was not truncated");
                    }
            for (const auto& box : pieces) {
                auto found = std::find_if(refined.begin(), refined.end(), [&](const auto& e) { return value_equal(e.first, box); });
                if (found == refined.end()) {
                    refined.push_back({box, {static_cast<std::int64_t>(c)}});
                } else {
                    found->second.push_back(static_cast<std::int64_t>(c));
                }
            }
        }
        std::stable_sort(refined.begin(), refined.end(), [](const auto& a, const auto& b) { return value_less(a.first, b.first); });
        cells.clear();
        parents.clear();
        for (auto& [box, owners_of] : refined) {
            cells.push_back(box);
            parents.push_back(owners_of);
        }
        contacts = contact_patches(cells, solids, owners);
    }
    Array cell_rows, portal_rows, contact_rows, climbable_rows, parent_rows;
    for (const auto& c : cells) cell_rows.push_back(array({box_value(c), integer_value(dimension(c))}));
    for (std::size_t i = 0; i < cells.size(); ++i) {
        deadline.check("Cell adjacency deadline");
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            const auto overlap = intersection(cells[i], cells[j]);
            if (overlap && dimension(*overlap) >= std::min(dimension(cells[i]), dimension(cells[j])) - 1)
                portal_rows.push_back(array({array({integer_value(static_cast<std::int64_t>(i)), integer_value(static_cast<std::int64_t>(j))}),
                                             box_value(*overlap), integer_value(dimension(*overlap))}));
        }
    }
    for (const auto& p : contacts)
        contact_rows.push_back(array({integer_value(p.cell), integer_value(p.solid), p.owner, integer_value(p.axis),
                                      integer_value(p.side), text(p.kind), box_value(p.bounds)}));
    const Value* ladders = get(geometry, "ladders");
    for (std::size_t c = 0; c < cells.size(); ++c) {
        Array memberships;
        if (ladders)
            for (const auto& ladder_value : items(*ladders)) {
                const Array& l = items(ladder_value);
                const NumBox region{number_of(l[0]), number_of(l[1]), number_of(l[2]), add(number_of(l[0]), whole(1)),
                                    add(number_of(l[1]), whole(1)), add(number_of(l[2]), whole(1))};
                if (intersection(cells[c], region)) memberships.push_back(ladder_value);
            }
        if (!memberships.empty())
            climbable_rows.push_back(array({integer_value(static_cast<std::int64_t>(c)), array(std::move(memberships))}));
    }
    for (std::size_t c = 0; c < parents.size(); ++c) {
        Array ids;
        for (auto id : parents[c]) ids.push_back(integer_value(id));
        parent_rows.push_back(array({integer_value(static_cast<std::int64_t>(c)), array(std::move(ids))}));
    }
    std::int64_t volume_cells = 0, boundary_cells = 0;
    for (const auto& c : cells) (dimension(c) == 3 ? volume_cells : boundary_cells) += 1;
    const Object statistics{{"rawCells", integer_value(static_cast<std::int64_t>(raw.size()))},
                            {"baseCells", integer_value(static_cast<std::int64_t>(base_count))},
                            {"volumeCells", integer_value(volume_cells)},
                            {"boundaryCells", integer_value(boundary_cells)},
                            {"cells", integer_value(static_cast<std::int64_t>(cells.size()))},
                            {"supportRefinement", boolean(refine_support)},
                            {"portals", integer_value(static_cast<std::int64_t>(portal_rows.size()))},
                            {"contactPatches", integer_value(static_cast<std::int64_t>(contact_rows.size()))}};
    return object({{"bounds", box_value(bounds)},
                   {"cells", array(std::move(cell_rows))},
                   {"portals", array(std::move(portal_rows))},
                   {"contacts", array(std::move(contact_rows))},
                   {"climbable", array(std::move(climbable_rows))},
                   {"statistics", object(statistics)},
                   {"parents", array(std::move(parent_rows))}});
}

}  // namespace pkc
