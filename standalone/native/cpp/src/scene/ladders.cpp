// Port of scene/ladders.py. Ids keep the Python's tuple reprs; numbers keep their
// int/float kinds (columns are integer cells, planes come from solids).
#include "pkc/scene/ladders.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

#include "../schema.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const double kHalfWidth = f32(0.6) / 2;
const double kBodyHeight = f32(1.8);

Num n_of(const Value& v) {
    if (auto* i = std::get_if<std::int64_t>(&v.data)) return whole(*i);
    if (auto* d = std::get_if<double>(&v.data)) return flt(*d);
    fail("expected a number");
}

std::string tuple_repr(std::initializer_list<std::int64_t> values) {  // str((a, b, ...)) of ints
    std::string out = "(";
    bool first = true;
    for (auto v : values) {
        out += (first ? "" : ", ") + std::to_string(v);
        first = false;
    }
    return out + ")";
}

using Cell = std::array<std::int64_t, 3>;

std::vector<Cell> checked_cells(const Value& cells) {
    if (!is_list(cells)) fail("Climbable cells require a list of integer X/Y/Z cells");
    std::vector<Cell> out;
    for (const auto& c : items(cells)) {
        if (!is_list(c) || items(c).size() != 3 ||
            std::any_of(items(c).begin(), items(c).end(), [](const Value& v) { return !std::holds_alternative<std::int64_t>(v.data); }))
            fail("Climbable cells require integer X/Y/Z");
        out.push_back({as_int(items(c)[0]), as_int(items(c)[1]), as_int(items(c)[2])});
    }
    return out;
}

Object column_object(std::int64_t x, std::int64_t z, std::int64_t lo, std::int64_t hi) {
    return {{"id", text(tuple_repr({x, lo, z}))}, {"x", integer_value(x)}, {"z", integer_value(z)},
            {"minY", integer_value(lo)}, {"maxY", integer_value(hi)}};
}

std::vector<Object> columns_of(const std::vector<Cell>& cells) {
    std::map<std::pair<std::int64_t, std::int64_t>, std::set<std::int64_t>> grouped;
    for (const auto& c : cells) grouped[{c[0], c[2]}].insert(c[1]);
    std::vector<Object> columns;
    for (const auto& [xz, heights] : grouped) {
        std::optional<std::int64_t> start, previous;
        for (auto y : heights) {
            if (previous && y != *previous + 1) {
                columns.push_back(column_object(xz.first, xz.second, *start, *previous + 1));
                start.reset();
            }
            if (!start) start = y;
            previous = y;
        }
        if (start) columns.push_back(column_object(xz.first, xz.second, *start, *previous + 1));
    }
    return columns;
}

std::int64_t geti(const Object& o, std::string_view key) { return as_int(at(o, key)); }

std::vector<Object> rectangular_regions(const std::vector<Cell>& cells) {
    std::vector<Object> regions;
    for (auto c : columns_of(cells)) {
        const std::int64_t x = geti(c, "x"), z = geti(c, "z");
        c.emplace_back("maxX", integer_value(x + 1));
        c.emplace_back("maxZ", integer_value(z + 1));
        regions.push_back(std::move(c));
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < regions.size() && !changed; ++i) {
            Object& a = regions[i];
            for (std::size_t j = i + 1; j < regions.size(); ++j) {
                const Object& b = regions[j];
                if (geti(a, "minY") != geti(b, "minY") || geti(a, "maxY") != geti(b, "maxY")) continue;
                const bool along_x = geti(a, "z") == geti(b, "z") && geti(a, "maxZ") == geti(b, "maxZ") &&
                                     (geti(a, "maxX") == geti(b, "x") || geti(b, "maxX") == geti(a, "x"));
                const bool along_z = geti(a, "x") == geti(b, "x") && geti(a, "maxX") == geti(b, "maxX") &&
                                     (geti(a, "maxZ") == geti(b, "z") || geti(b, "maxZ") == geti(a, "z"));
                if (along_x || along_z) {
                    const std::int64_t x = std::min(geti(a, "x"), geti(b, "x")), mx = std::max(geti(a, "maxX"), geti(b, "maxX"));
                    const std::int64_t z = std::min(geti(a, "z"), geti(b, "z")), mz = std::max(geti(a, "maxZ"), geti(b, "maxZ"));
                    put(a, "x", integer_value(x));
                    put(a, "maxX", integer_value(mx));
                    put(a, "z", integer_value(z));
                    put(a, "maxZ", integer_value(mz));
                    put(a, "id", text(tuple_repr({x, geti(a, "minY"), z, mx, geti(a, "maxY"), mz})));
                    regions.erase(regions.begin() + j);
                    changed = true;
                    break;
                }
            }
        }
    }
    return regions;
}

std::pair<Num, Num> axis_range(const Object& column, const std::string& axis) {  // region_axis
    const char* key = axis == "X" ? "x" : "z";
    const Num lo = n_of(at(column, key));
    const Value* hi = get(column, axis == "X" ? "maxX" : "maxZ");
    return {lo, hi ? n_of(*hi) : add(lo, whole(1))};
}

std::vector<std::array<Num, 4>> rectangles(const Object& column, double y) {
    std::vector<std::array<Num, 4>> out;
    auto visit = [&](const Object& c) {
        if (!(n_of(at(c, "minY")).value <= y && y < n_of(at(c, "maxY")).value)) return;
        const Num x = n_of(at(c, "x")), z = n_of(at(c, "z"));
        const Value* mx = get(c, "maxX");
        const Value* mz = get(c, "maxZ");
        out.push_back({x, mx ? n_of(*mx) : add(x, whole(1)), z, mz ? n_of(*mz) : add(z, whole(1))});
    };
    if (const Value* volumes = get(column, "volumes")) {
        for (const auto& v : items(*volumes)) visit(fields(v));
    } else {
        visit(column);
    }
    return out;
}

Value face_value(const std::string& axis, std::int64_t direction, const Num& plane) {
    return object({{"axis", text(axis)}, {"direction", integer_value(direction)}, {"plane", value(plane)}});
}

}  // namespace

Value ladder_columns(const Value& cells) {
    Array out;
    for (auto& c : columns_of(checked_cells(cells))) out.push_back(object(std::move(c)));
    return array(std::move(out));
}

Value ladder_regions(const Value& cells_value) {
    const std::vector<Cell> cells = checked_cells(cells_value);
    std::set<Cell> remaining(cells.begin(), cells.end());
    Array regions;
    while (!remaining.empty()) {
        const Cell first = *remaining.begin();
        remaining.erase(remaining.begin());
        std::set<Cell> component{first};
        std::vector<Cell> pending{first};
        while (!pending.empty()) {
            const Cell c = pending.back();
            pending.pop_back();
            const Cell neighbours[6] = {{c[0] - 1, c[1], c[2]}, {c[0] + 1, c[1], c[2]}, {c[0], c[1] - 1, c[2]},
                                        {c[0], c[1] + 1, c[2]}, {c[0], c[1], c[2] - 1}, {c[0], c[1], c[2] + 1}};
            for (const auto& n : neighbours)
                if (remaining.erase(n)) {
                    component.insert(n);
                    pending.push_back(n);
                }
        }
        const std::vector<Cell> sorted(component.begin(), component.end());
        std::vector<Object> volumes = rectangular_regions(sorted);
        if (volumes.size() == 1) {
            regions.push_back(object(std::move(volumes[0])));
            continue;
        }
        std::string id = "region:[";
        for (std::size_t i = 0; i < sorted.size(); ++i)
            id += (i ? ", " : "") + tuple_repr({sorted[i][0], sorted[i][1], sorted[i][2]});
        id += "]";
        std::int64_t x0 = sorted[0][0], x1 = x0, y0 = sorted[0][1], y1 = y0, z0 = sorted[0][2], z1 = z0;
        for (const auto& c : sorted) {
            x0 = std::min(x0, c[0]); x1 = std::max(x1, c[0]);
            y0 = std::min(y0, c[1]); y1 = std::max(y1, c[1]);
            z0 = std::min(z0, c[2]); z1 = std::max(z1, c[2]);
        }
        Array volume_values;
        for (auto& v : volumes) volume_values.push_back(object(std::move(v)));
        regions.push_back(object({{"id", text(id)}, {"x", integer_value(x0)}, {"maxX", integer_value(x1 + 1)},
                                  {"z", integer_value(z0)}, {"maxZ", integer_value(z1 + 1)}, {"minY", integer_value(y0)},
                                  {"maxY", integer_value(y1 + 1)}, {"volumes", array(std::move(volume_values))}}));
    }
    return array(std::move(regions));
}

Value region_rectangles(const Value& column, const Value& y) {
    Array out;
    for (const auto& r : rectangles(fields(column), n_of(y).value))
        out.push_back(array({value(r[0]), value(r[1]), value(r[2]), value(r[3])}));
    return array(std::move(out));
}

bool region_contains(const Value& column, const Value& position) {
    const Array& p = items(position);
    const double x = n_of(p[0]).value, z = n_of(p[2]).value;
    for (const auto& b : rectangles(fields(column), n_of(p[1]).value))
        if (b[0].value <= x && x < b[1].value && b[2].value <= z && z < b[3].value) return true;
    return false;
}

Value region_axis(const Value& column, const std::string& axis) {
    const auto [lo, hi] = axis_range(fields(column), axis);
    return array({value(lo), value(hi)});
}

Value climb_walls(const Value& column_value, const Value& solids) {
    const Object& column = fields(column_value);
    std::vector<std::pair<std::tuple<std::string, std::int64_t, Num>, Array>> groups;  // first-seen order
    const Num max_y = n_of(at(column, "maxY")), min_y = n_of(at(column, "minY"));
    const Array& boxes = items(solids);
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        const Array& box = items(boxes[index]);
        if (n_of(box[1]).value >= max_y.value + kBodyHeight || n_of(box[4]).value <= min_y.value) continue;
        for (const auto& entry : {std::tuple<std::string, int, int, Num>{"X", 0, 2, n_of(at(column, "x"))},
                                  std::tuple<std::string, int, int, Num>{"Z", 2, 0, n_of(at(column, "z"))}}) {
            const std::string& axis = std::get<0>(entry);
            const int a = std::get<1>(entry), tangent = std::get<2>(entry);
            const Num& c = std::get<3>(entry);
            const Num cell_tangent = n_of(at(column, axis == "X" ? "z" : "x"));
            const Num tangent_hi = axis_range(column, axis == "X" ? "Z" : "X").second;
            if (n_of(box[tangent]).value >= tangent_hi.value + kHalfWidth ||
                n_of(box[tangent + 3]).value <= cell_tangent.value - kHalfWidth)
                continue;
            for (std::int64_t direction : {-1, 1}) {
                const Num plane = direction > 0 ? sub(n_of(box[a]), flt(kHalfWidth)) : add(n_of(box[a + 3]), flt(kHalfWidth));
                if (c.value <= plane.value && plane.value < axis_range(column, axis).second.value) {
                    auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& g) {
                        return std::get<0>(g.first) == axis && std::get<1>(g.first) == direction &&
                               std::get<2>(g.first).value == plane.value;
                    });
                    if (found == groups.end()) {
                        groups.push_back({{axis, direction, plane}, {}});
                        found = groups.end() - 1;
                    }
                    found->second.push_back(object({{"solidIndex", integer_value(static_cast<std::int64_t>(index))},
                                                    {"box", boxes[index]}}));
                }
            }
        }
    }
    Array out;
    for (auto& [key, parts] : groups)
        out.push_back(object({{"axis", text(std::get<0>(key))}, {"direction", integer_value(std::get<1>(key))},
                              {"plane", value(std::get<2>(key))}, {"parts", array(std::move(parts))}}));
    return array(std::move(out));
}

Value movement_contexts(const Value& column, const Value& solids, bool include_fall) {
    Array contexts = items(climb_walls(column, solids));
    if (include_fall) {
        const auto [lo, hi] = axis_range(fields(column), "X");
        contexts.push_back(object({{"mode", text("fall_only")}, {"axis", text("X")}, {"direction", integer_value(1)},
                                   {"plane", real(add(lo, hi).value / 2)}, {"parts", array({})}}));
    }
    return array(std::move(contexts));
}

Value entry_channels(const Value& column_value, const Value& wall_value) {
    const Object& column = fields(column_value);
    const Object& wall = fields(wall_value);
    Array result;
    auto channel = [&](const char* family, const std::string& axis, std::int64_t direction) {
        const auto [lo, hi] = axis_range(column, axis);
        result.push_back(object({{"family", text(family)},
                                 {"channel", text(std::string(family) + ":" + axis + ":" + std::to_string(direction))},
                                 {"axis", text(axis)}, {"direction", integer_value(direction)},
                                 {"plane", value(direction > 0 ? hi : lo)}}));
    };
    const Value* mode = get(wall, "mode");
    if (mode && std::holds_alternative<std::string>(mode->data) && as_text(*mode) == "fall_only") {
        for (const char* axis : {"X", "Z"})
            for (std::int64_t direction : {-1, 1}) channel("side", axis, direction);
        return array(std::move(result));
    }
    const std::string axis = as_text(at(wall, "axis"));
    const std::string side = axis == "X" ? "Z" : "X";
    channel("front", axis, -as_int(at(wall, "direction")));
    channel("side", side, -1);
    channel("side", side, 1);
    return array(std::move(result));
}

Value outside_row(const Value& face_value, const Value& state, const Value& name) {
    const Object& face = fields(face_value);
    const std::int64_t direction = as_int(at(face, "direction"));
    return object({{"mode", at(face, "axis")}, {"t1", state}, {"cmp", text(direction > 0 ? "GE" : "LE")},
                   {"rhs", value(add(n_of(at(face, "plane")), mul(whole(direction), flt(kSolverMargin))))}, {"name", name}});
}

Value schedule_constraints(const Value& schedule_value, const Value& column_value, const Value& wall_value,
                           const Value& entry_face, const Value& exit_face, const Value& start_value) {
    const Object& schedule = fields(schedule_value);
    const Object& column = fields(column_value);
    const Object& wall = fields(wall_value);
    const std::int64_t start = as_int(start_value);
    const Num margin = flt(kSolverMargin);
    Array rows, contacts, scene_contacts;
    const Array& ys = items(at(fields(at(schedule, "trace")), "position"));
    const Array& ladder = items(at(schedule, "ladderTicks"));
    const Value* constraint_start = get(schedule, "constraintStart");
    const Value* constraint_end = get(schedule, "constraintEnd");
    const std::int64_t first_state = constraint_start ? as_int(*constraint_start) : 0;
    const std::int64_t end_state = constraint_end ? as_int(*constraint_end) : static_cast<std::int64_t>(ys.size());
    const Array& parts = items(at(wall, "parts"));
    auto part_box = [](const Value& part) -> const Array& { return items(at(fields(part), "box")); };
    auto row = [&](const std::string& mode, std::int64_t t1, const char* cmp, const Num& rhs, const char* name) {
        rows.push_back(object({{"mode", text(mode)}, {"t1", integer_value(t1)}, {"cmp", text(cmp)}, {"rhs", value(rhs)},
                               {"name", text(name)}}));
    };
    const std::size_t pairs = std::min(ys.size(), ladder.size());
    const Value* corridor_choices = get(schedule, "corridorChoices");
    for (std::size_t state = 0; state < pairs; ++state) {
        const auto s = static_cast<std::int64_t>(state);
        if (s < first_state || s >= end_state) continue;
        const Num y = n_of(ys[state]);
        const std::int64_t at_state = start + s;
        if (py_truthy(ladder[state])) {
            const auto rects = rectangles(column, y.value);
            if (rects.empty()) fail("Ladder occupancy has no cells at this height");
            Value choice = integer_value(0);
            if (corridor_choices)
                if (const Value* c = get(fields(*corridor_choices), std::to_string(state))) choice = *c;
            if (!std::holds_alternative<std::int64_t>(choice.data) || as_int(choice) < 0 ||
                as_int(choice) >= static_cast<std::int64_t>(rects.size()))
                fail("Invalid exact ladder corridor choice");
            const auto& b = rects[as_int(choice)];
            for (const auto& [axis, lo, hi] : {std::tuple<std::string, Num, Num>{"X", b[0], b[1]},
                                               std::tuple<std::string, Num, Num>{"Z", b[2], b[3]}}) {
                row(axis, at_state, "GE", add(lo, margin), "ladder:inside");
                row(axis, at_state, "LE", sub(hi, margin), "ladder:inside");
            }
        } else if (s < as_int(at(schedule, "entryState"))) {
            rows.push_back(outside_row(entry_face, integer_value(at_state), text("ladder:before_entry")));
        } else if (n_of(at(column, "minY")).value <= y.value && y.value < n_of(at(column, "maxY")).value && !is_null(exit_face)) {
            rows.push_back(outside_row(exit_face, integer_value(at_state), text("ladder:after_exit")));
        }
        if (std::any_of(parts.begin(), parts.end(), [&](const Value& part) {
                const Array& box = part_box(part);
                return y.value < n_of(box[4]).value && y.value + kBodyHeight > n_of(box[1]).value;
            }))
            row(as_text(at(wall, "axis")), at_state, as_int(at(wall, "direction")) > 0 ? "LE" : "GE", n_of(at(wall, "plane")),
                "ladder:wall_avoid");
    }
    const Array& hits = items(at(schedule, "hit"));
    const Value* face_choices = get(schedule, "faceCorridorChoices");
    for (std::size_t tick = 0; tick < hits.size(); ++tick) {
        if (!py_truthy(hits[tick])) continue;
        const Num y = n_of(ys.at(tick + 1));
        std::vector<const Array*> touching;
        for (const auto& part : parts) {
            const Array& box = part_box(part);
            if (y.value < n_of(box[4]).value && y.value + kBodyHeight > n_of(box[1]).value) touching.push_back(&box);
        }
        if (touching.empty()) fail("Selected climb has no finite wall at its Y stage");
        const std::int64_t axis = as_text(at(wall, "axis")) == "X" ? 0 : 1;
        const Value contact = object({{"state", integer_value(start + static_cast<std::int64_t>(tick) + 1)},
                                      {"axis", integer_value(axis)}, {"plane", at(wall, "plane")},
                                      {"direction", at(wall, "direction")}});
        contacts.push_back(contact);
        const std::string tangent = axis == 0 ? "Z" : "X";
        const int a = axis == 0 ? 2 : 0;
        const std::int64_t stage = axis == 0 ? static_cast<std::int64_t>(tick) : static_cast<std::int64_t>(tick) + 1;
        std::stable_sort(touching.begin(), touching.end(),
                         [&](const Array* x, const Array* y2) { return n_of((*x)[a]).value < n_of((*y2)[a]).value; });
        struct Corridor {
            Num lo, hi;
            Array boxes;
        };
        std::vector<Corridor> corridors;
        for (const Array* box : touching) {
            const Num lo = sub(n_of((*box)[a]), flt(kHalfWidth)), hi = add(n_of((*box)[a + 3]), flt(kHalfWidth));
            if (!corridors.empty() && lo.value < corridors.back().hi.value) {
                if (hi.value > corridors.back().hi.value) corridors.back().hi = hi;  // max(prior, hi)
                corridors.back().boxes.push_back(array(*box));
            } else {
                corridors.push_back({lo, hi, {array(*box)}});
            }
        }
        Value choice = integer_value(0);
        if (face_choices)
            if (const Value* c = get(fields(*face_choices), std::to_string(tick))) choice = *c;
        if (!std::holds_alternative<std::int64_t>(choice.data) || as_int(choice) < 0 ||
            as_int(choice) >= static_cast<std::int64_t>(corridors.size()))
            fail("Invalid finite wall corridor");
        const Corridor& chosen = corridors[as_int(choice)];
        Object scene_contact = with(fields(contact), "axis", integer_value(axis == 0 ? 0 : 2));
        scene_contact.emplace_back("boxes", array(chosen.boxes));
        scene_contacts.push_back(object(std::move(scene_contact)));
        row(tangent, start + stage, "GE", add(chosen.lo, margin), "ladder:finite_face");
        row(tangent, start + stage, "LE", sub(chosen.hi, margin), "ladder:finite_face");
    }
    return array({array(std::move(rows)), array(std::move(contacts)), array(std::move(scene_contacts))});
}

Value exit_faces(const Value& column_value, const Value& target_value) {
    const Object& column = fields(column_value);
    const Array& target = items(target_value);
    std::vector<std::pair<double, Value>> faces;
    for (const char* axis : {"X", "Z"})
        for (std::int64_t sign : {-1, 1}) {
            const auto [lo, hi] = axis_range(column, axis);
            const Num plane = sign > 0 ? hi : lo;
            const double t = n_of(target[std::string(axis) == "X" ? 0 : 1]).value;
            const double key = std::abs(t - plane.value) - static_cast<double>(sign) * (t - plane.value);
            faces.emplace_back(key, face_value(axis, sign, plane));
        }
    std::stable_sort(faces.begin(), faces.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Array out;
    for (auto& f : faces) out.push_back(f.second);
    return array(std::move(out));
}

}  // namespace pkc
