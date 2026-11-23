// Port of search/anchors/policy.py's program readers.
#include "pkc/search/anchors/policy.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "../../py_math.hpp"
#include "../../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/search/comparison.hpp"

namespace pkc {
namespace {

std::vector<double> key_of(const Box& box) {
    std::vector<double> out;
    for (const Num& n : box) out.push_back(n.value);
    return out;
}

std::vector<double> key_of(const Value& box) {
    std::vector<double> out;
    for (const auto& n : items(box)) out.push_back(num(n).value);
    return out;
}

Value boxes_value(const std::vector<Box>& boxes) {
    Array out;
    for (const auto& box : boxes) {
        Array b;
        for (const Num& n : box) b.push_back(value(n));
        out.push_back(array(std::move(b)));
    }
    return array(std::move(out));
}

std::string joined(const std::set<std::string>& names) {
    std::string out;
    for (const auto& n : names) out += (out.empty() ? "" : "+") + n;
    return out;
}

const std::set<std::string> kFeetCases{"normal_step", "slab_boost", "blip", "blip_up", "corner_jump_cancel",
                                       "corner_blip", "corner_blip_up"};
const std::set<std::string> kAdvantageCases{"grind", "slab_boost", "corner_jump_cancel"};

}  // namespace

AnchorScene::AnchorScene(Value scene)
    : scene_(std::move(scene)),
      geometry_(at(fields(at(fields(scene_), "collisionScene")), "solids")),
      owners_(solid_owners(scene_)) {
    const Array& solids = items(at(fields(at(fields(scene_), "collisionScene")), "solids"));
    std::vector<std::int64_t> owned;  // solid_owners' keys in first-appearance order
    for (const auto& b : items(at(fields(scene_), "blocks"))) {
        const Object& block = fields(b);
        const std::string& id = as_text(at(block, "id"));
        const Value* source = get(block, "sourceBlock");
        block_owner_.emplace(id, as_text(source ? *source : at(block, "id")));
        block_solid_.emplace(id, as_int(at(block, "solidIndex")));
        const std::int64_t solid = as_int(at(block, "solidIndex"));
        if (std::find(owned.begin(), owned.end(), solid) == owned.end()) owned.push_back(solid);
    }
    for (auto i : owned) solid_index_[key_of(solids.at(i))] = i;
    for (const auto& b : items(at(fields(scene_), "blocks"))) {
        const Object& block = fields(b);
        box_owner_[key_of(solids.at(as_int(at(block, "solidIndex"))))] = owner(as_text(at(block, "id")));
    }
    for (const auto& option : scene_contact_options(scene_, geometry_, 1, 1)) {
        const std::set<std::int64_t> members(option.members.begin(), option.members.end());
        const std::string name = face_name(option, owners_);
        for (auto i : option.members) faces_[i].emplace_back(members, name);
    }
}

std::string AnchorScene::owner(const std::string& block) const {
    auto found = block_owner_.find(block);
    return found == block_owner_.end() ? block : found->second;
}

std::optional<std::string> AnchorScene::face(const std::vector<Box>& boxes) const {
    std::set<std::int64_t> found;
    for (const auto& b : boxes)
        if (auto i = solid_index_.find(key_of(b)); i != solid_index_.end()) found.insert(i->second);
    if (found.empty()) return std::nullopt;
    std::set<std::string> names;
    bool whole = false;
    if (auto entry = faces_.find(*found.begin()); entry != faces_.end())
        for (const auto& [members, name] : entry->second)
            if (std::includes(members.begin(), members.end(), found.begin(), found.end())) {
                names.insert(name);
                whole = whole || members == found;
            }
    if (names.size() == 1 && !whole) return *names.begin();
    std::set<std::string> owners;
    for (auto i : found)
        if (auto o = owners_.find(i); o != owners_.end()) owners.insert(o->second.begin(), o->second.end());
    return joined(owners);
}

std::string AnchorScene::support_face(const std::string& support) const {
    auto solid = block_solid_.find(support);
    if (solid == block_solid_.end()) return support;
    const Array& solids = items(at(fields(at(fields(scene_), "collisionScene")), "solids"));
    Box box;
    for (std::size_t k = 0; k < 6; ++k) box[k] = num(items(solids.at(solid->second))[k]);
    return *face({box});
}

std::vector<Visit> AnchorScene::visits(const Program& program) const {
    const Profile& profile = program.profile;
    std::vector<std::tuple<std::int64_t, int, std::string, const std::vector<Box>*>> events;
    static const std::vector<Box> kNone;
    for (const auto& e : profile.contact_events)
        if (e.axis == "Y" && e.direction == -1)
            events.emplace_back(e.offset, 0, digest(to_value(e)), e.boxes ? &*e.boxes : &kNone);
    for (const auto& e : profile.case_events) {
        const std::int64_t t = e.input + 1;
        if (e.lower_boxes)
            events.emplace_back(e.input, 1, digest(object({{"boxes", boxes_value(*e.lower_boxes)}})), &*e.lower_boxes);
        if (!e.boxes.empty() && profile.grounded.at(t)) {
            double top = -INFINITY;
            for (const auto& b : e.boxes) top = std::max(top, b[4].value);
            if (std::fabs(profile.position.at(t).value - top) <= 1e-7) events.emplace_back(e.input, 2, digest(to_value(e)), &e.boxes);
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return std::tie(std::get<0>(a), std::get<1>(a), std::get<2>(a)) <
               std::tie(std::get<0>(b), std::get<1>(b), std::get<2>(b));
    });
    std::vector<Visit> result{{0, support_face(as_text(at(fields(scene_), "startBlock")))}};
    for (const auto& [tick, _, __, boxes] : events)
        if (auto found = face(*boxes)) result.emplace_back(tick + 1, *found);
    if (program.terminal_support.value) result.emplace_back(profile.duration, support_face(*program.terminal_support.value));
    return result;
}

std::optional<Visit> AnchorScene::launch(const Program& program, std::optional<std::int64_t> begin,
                                         std::optional<std::int64_t> end) const {
    std::optional<Visit> last;
    for (auto& v : visits(program))
        if (!begin || v.first <= *begin) last = v;
    const auto& ladder = program.profile.ladder_ticks;
    const std::size_t stop = end ? std::min<std::size_t>(ladder.size(), *end + 1) : ladder.size();
    for (std::size_t t = last->first; t < stop; ++t)
        if (ladder[t]) return std::nullopt;
    return last;
}

std::vector<Visit> AnchorScene::feet_touches(const Program& program) const {
    std::vector<Visit> result;
    for (const auto& e : program.profile.case_events) {
        if (!kFeetCases.count(e.movement_case)) continue;
        if (auto found = face(e.boxes)) result.emplace_back(e.input + 1, *found);
        if (e.lower_boxes)
            if (auto found = face(*e.lower_boxes)) result.emplace_back(e.input + 1, *found);
    }
    std::sort(result.begin(), result.end());
    return result;
}

VisitState AnchorScene::visit_state(const Program& program) const {
    VisitState state;
    std::vector<Visit> touches = feet_touches(program);
    const std::vector<Visit> arrivals = visits(program);
    const std::set<Visit> visited(arrivals.begin(), arrivals.end());
    std::set<std::string> departed;
    std::size_t next = 0;
    auto touch_until = [&](std::optional<std::int64_t> tick) {
        while (next < touches.size() && (!tick || touches[next].first < *tick)) {
            const Visit& touch = touches[next++];
            if (departed.count(touch.second) && !visited.count(touch)) state.valid = false;
        }
    };
    for (const auto& [tick, name] : arrivals) {
        touch_until(tick);
        if (state.current == name) continue;
        if (departed.count(name)) state.valid = false;
        if (state.current) departed.insert(*state.current);
        state.current = name;
    }
    touch_until(std::nullopt);
    state.departed.assign(departed.begin(), departed.end());
    return state;
}

Audit AnchorScene::audit(const Program& program, const std::vector<std::string>& anchors,
                         std::optional<std::int64_t> reached) const {
    Audit result;
    result.visits = visit_state(program);
    std::vector<std::string> required;
    for (const auto& a : anchors) required.push_back(owner(a));
    std::size_t index = 0;
    for (const auto& [tick, name] : visits(program))
        if (std::find(required.begin() + static_cast<std::ptrdiff_t>(std::min(index, required.size())), required.end(),
                      name) != required.end()) {
            if (name != required[index]) {
                result.reason = "required_anchor_order";
                break;
            }
            result.ticks.push_back(tick);
            ++index;
        }
    if (!result.visits.valid) {
        result.reason = "departed_owner_return";
    } else if (!result.reason && reached && static_cast<std::int64_t>(index) < *reached) {
        result.reason = "missing_prior_anchor";
    }
    result.reached = static_cast<std::int64_t>(index);
    return result;
}

std::string AnchorScene::next_anchor(const Program& program, const std::vector<std::string>& anchors,
                                     const std::string& task) const {
    const std::size_t reached = static_cast<std::size_t>(audit(program, anchors).reached);
    return owner(reached < anchors.size() ? anchors[reached] : task);
}

std::set<std::pair<std::string, std::optional<std::string>>> AnchorScene::advantageous_targets(
    const Program& base, const Program& child, const std::string& next_owner) const {
    const Profile& p = child.profile;
    const std::int64_t start = base.profile.duration;
    auto owners = [&](const std::vector<Box>& boxes) {
        std::set<std::string> names;
        for (const auto& b : boxes)
            if (auto o = box_owner_.find(key_of(b)); o != box_owner_.end()) names.insert(o->second);
        return joined(names);
    };
    std::set<std::pair<std::string, std::optional<std::string>>> keys;
    if (p.grounded.back() && child.terminal_support.value && owner(*child.terminal_support.value) == next_owner)
        keys.emplace("anchor", next_owner);
    if (!p.jump.at(start)) return keys;
    for (const auto& e : p.contact_events)
        if (e.role == "ceiling" && e.offset >= start) keys.emplace("ceiling", owners(e.boxes.value()));
    for (const auto& e : p.case_events)
        if (kAdvantageCases.count(e.movement_case) && e.input >= start)
            keys.emplace(e.movement_case, e.movement_case == "grind" ? std::optional<std::string>(owners(e.boxes)) : face(e.boxes));
    return keys;
}

Value AnchorScene::transfer_structure(const Program& program, std::int64_t start, const std::string& source_block,
                                      const std::string& target_block, bool complete, bool until_arrival) const {
    const Profile& p = program.profile;
    const std::int64_t n = p.duration;
    std::vector<std::int64_t> launches;
    for (std::int64_t t = start; t < n; ++t)
        if (p.grounded[t] && p.jump[t]) launches.push_back(t);
    const std::string source = owner(source_block), target = owner(target_block);
    std::vector<Visit> contacts;
    for (auto& v : visits(program))
        if (start < v.first && v.first <= n) contacts.push_back(v);
    std::optional<std::int64_t> arrival;
    for (const auto& [t, name] : contacts)
        if (name == target && (!arrival || t < *arrival)) arrival = t;
    auto filtered = [](auto values, auto&& keep) {
        values.erase(std::remove_if(values.begin(), values.end(), [&](const auto& v) { return !keep(v); }), values.end());
        return values;
    };
    if (until_arrival && arrival) {
        contacts = filtered(contacts, [&](const Visit& v) { return v.first <= *arrival; });
        launches = filtered(launches, [&](std::int64_t t) { return t < *arrival; });
    }
    std::int64_t boundary = start;
    for (const auto& [t, name] : contacts)
        if (name == source && (!arrival || t < *arrival)) boundary = std::max(boundary, t);
    const auto preparation = filtered(launches, [&](std::int64_t t) { return t < boundary; });
    const auto transfer = filtered(launches, [&](std::int64_t t) { return t >= boundary && (!arrival || t < *arrival); });
    std::vector<std::int64_t> stepping;
    for (const auto& e : p.case_events)
        if ((e.type == "stepping" || kFeetCases.count(e.movement_case)) && boundary <= e.input && (!arrival || e.input < *arrival))
            stepping.push_back(e.input);
    std::optional<std::string> reason;
    if (std::any_of(contacts.begin(), contacts.end(), [&](const Visit& v) { return v.second != source && v.second != target; })) {
        reason = "intermediate_support_between_anchors";
    } else if (arrival && std::any_of(contacts.begin(), contacts.end(),
                                      [&](const Visit& v) { return v.first > *arrival && v.second == source; })) {
        reason = "return_after_target_arrival";
    } else if (arrival && std::any_of(launches.begin(), launches.end(), [&](std::int64_t t) { return t >= *arrival; })) {
        reason = "jump_after_target_arrival";
    } else if (transfer.size() > 1) {
        reason = "multiple_transfer_impulses";
    } else if (arrival && !stepping.empty()) {
        reason = "stepping_during_hard_transfer";
    } else if (complete && (!arrival || transfer.size() != 1)) {
        reason = "missing_transfer_jump";
    }
    auto ints = [](const std::vector<std::int64_t>& v) {
        Array out;
        for (auto x : v) out.push_back(integer_value(x));
        return array(std::move(out));
    };
    return object({{"version", integer_value(kHardJumpPolicyVersion)},
                   {"valid", boolean(!reason)},
                   {"reason", reason ? text(*reason) : Value{nullptr}},
                   {"source", text(source)},
                   {"target", text(target)},
                   {"spanStart", integer_value(start)},
                   {"preparationThrough", integer_value(boundary)},
                   {"preparationJumps", ints(preparation)},
                   {"transferJumps", ints(transfer)},
                   {"arrival", arrival ? integer_value(*arrival) : Value{nullptr}},
                   {"transferSteppingInputs", ints(stepping)},
                   {"assumption", text("hard_transfer_without_stepping")}});
}

bool AnchorScene::within_caps(const Object& request, const Program& program) const {
    const Profile& p = program.profile;
    std::int64_t jumps = 0, runs = 0;
    for (std::int64_t t = 0; t < p.duration; ++t) {
        if (p.grounded[t] && p.jump[t]) ++jumps;
        else if (p.grounded[t]) ++runs;
    }
    if (const Value* cap = get(request, "maxJumps"); cap && jumps > num(*cap).value) return false;
    if (const Value* cap = get(request, "maxTotalRunTicks"); cap && runs > num(*cap).value) return false;
    if (const Value* cap = get(request, "maxRunTicksPerJump")) {
        std::int64_t run = 0;
        for (std::int64_t t = 0; t < p.duration; ++t) {
            run = p.grounded[t] && !p.jump[t] ? run + 1 : 0;
            if (run > num(*cap).value) return false;
        }
    }
    return true;
}

Value AnchorScene::decision_point(const Program& program, const Value& guides) const {
    const std::string name = visits(program).back().second;
    const Array& names = items(at(fields(guides), "anchors"));
    for (std::size_t k = 0; k < names.size(); ++k)
        if (owner(as_text(names[k])) == name) return items(at(fields(guides), "points")).at(k);
    const std::set<std::string> owners =
        face_owners(scene_contact_options(scene_, geometry_, 1, 1), owners_, std::optional<std::string>(name));
    const Array& solids = items(at(fields(at(fields(scene_), "collisionScene")), "solids"));
    std::vector<const Array*> chosen;
    for (const auto& b : items(at(fields(scene_), "blocks")))
        if (owners.count(owner(as_text(at(fields(b), "id"))))) chosen.push_back(&items(solids.at(as_int(at(fields(b), "solidIndex")))));
    Value top = (*chosen.front())[4];
    for (const Array* s : chosen)
        if (num((*s)[4]).value > num(top).value) top = (*s)[4];
    double x0 = INFINITY, x1 = -INFINITY, z0 = INFINITY, z1 = -INFINITY;
    for (const Array* s : chosen)
        if (num((*s)[4]).value == num(top).value) {
            x0 = std::min(x0, num((*s)[0]).value);
            x1 = std::max(x1, num((*s)[3]).value);
            z0 = std::min(z0, num((*s)[2]).value);
            z1 = std::max(z1, num((*s)[5]).value);
        }
    return array({real((x0 + x1) / 2), top, real((z0 + z1) / 2)});
}

Value AnchorScene::program_objective(const Program& program, const Value& guides, const std::vector<std::string>& anchors) const {
    return decision_objective(guides, audit(program, anchors).reached, decision_point(program, guides),
                              at(fields(scene_), "objective"));
}

Value classify(const Value& guides_value, std::int64_t span, const Value& point_value) {
    const Object& guides = fields(guides_value);
    const Array& point = items(point_value);
    Array distances;
    bool turnaround = false;
    for (const auto& tv : items(at(guides, "turns"))) {
        const Object& turn = fields(tv);
        bool listed = false;
        for (const auto& s : items(at(turn, "spans"))) listed = listed || as_int(s) == span;
        if (!listed) continue;
        // sum(a*b for a,b in zip(normal, point)) - offset, keeping Python's int/float kinds.
        const Array& normal = items(at(turn, "normal"));
        const Num a = mul(num(normal[0]), num(point.at(0))), b = mul(num(normal[1]), num(point.at(1)));
        const Num total = a.integral && b.integral ? add(a, b) : flt(python_sum({a.value, b.value}));
        const Num raw = sub(total, num(at(turn, "offset"))), shifted = add(raw, num(at(turn, "margin")));
        turnaround = turnaround || shifted.value >= 0;
        distances.push_back(object({{"boundary", at(turn, "id")}, {"rawDistance", value(raw)},
                                    {"shiftedDistance", value(shifted)}, {"inside", boolean(shifted.value >= 0)}}));
    }
    return object({{"version", integer_value(1)}, {"guide", at(guides, "id")}, {"span", integer_value(span)},
                   {"point", array(point)}, {"isTurnaround", boolean(turnaround)},
                   {"distances", array(std::move(distances))}});
}

Value classify(const Value& guides, std::int64_t span, double x, double z) {
    return classify(guides, span, array({real(x), real(z)}));
}

LegClass leg_class(double dx, double dz) {
    const std::int64_t sx = std::copysign(1., dx) > 0 ? 1 : -1, sz = std::copysign(1., dz) > 0 ? 1 : -1;
    if (std::min(std::fabs(dx), std::fabs(dz)) <= std::tan(radians(kAxisLegDegrees)) * std::max(std::fabs(dx), std::fabs(dz))) {
        if (std::fabs(dx) >= std::fabs(dz)) return {{static_cast<double>(sx), 0.}, {{"X", sx}, {"Z", 1}, {"Z", -1}}};
        return {{0., static_cast<double>(sz)}, {{"Z", sz}, {"X", 1}, {"X", -1}}};
    }
    return {{std::copysign(std::sqrt(.5), dx), std::copysign(std::sqrt(.5), dz)}, {{"X", sx}, {"Z", sz}}};
}

namespace {

// direction_objective(unit(dx, dz)): the progress objective along the horizontal direction, as a custom-yaw X
// maximization; `fallback` when the direction has no length.
Value toward(double dx, double dz, const Value& fallback) {
    const double length = python_hypot(dx, dz);
    if (length <= 1e-12) return fallback;
    return object({{"axis", text("X")}, {"sense", text("MAX")},
                   {"customYaw", real(degrees(std::atan2(-(dx / length), dz / length)))}});
}

}  // namespace

Value decision_objective(const Value& guides_value, std::int64_t reached, const Value& point_value, const Value& fallback) {
    const Object& guides = fields(guides_value);
    const Array& point = items(point_value);
    const double px = num(point[0]).value, pz = num(point[2]).value;
    std::int64_t i = reached + 1;
    const Value* turn = nullptr;
    for (const auto& t : items(at(guides, "turns")))
        if (as_int(at(fields(t), "index")) == i) {
            turn = &t;
            break;
        }
    if (turn) {
        const Value inside = classify(guides_value, reached, px, pz);
        for (const auto& d : items(at(fields(inside), "distances")))
            if (py_equal(at(fields(d), "boundary"), at(fields(*turn), "id")) && py_truthy(at(fields(d), "inside"))) {
                ++i;
                break;
            }
    }
    const Array& points = items(at(guides, "points"));
    if (i >= static_cast<std::int64_t>(points.size())) return fallback;
    const Array& target = items(points[i]);
    return toward(num(target[0]).value - px, num(target[2]).value - pz, fallback);
}

Value onward_objective(const Value& guides, std::int64_t span, const Value& fallback) {
    // At E aim onward to N, rather than back toward E's landing point.
    const Array& points = items(at(fields(guides), "points"));
    const std::int64_t i = span + 1;
    if (i + 1 >= static_cast<std::int64_t>(points.size())) return fallback;
    const Array &e = items(points[i]), &n = items(points[i + 1]);
    return toward(num(n[0]).value - num(e[0]).value, num(n[2]).value - num(e[2]).value, fallback);
}

}  // namespace pkc

namespace pkc {
namespace {

double dist(const Array& p, const Array& point) {  // math.dist(p, point)
    const double d[3] = {num(p[0]).value - num(point[0]).value, num(p[1]).value - num(point[1]).value,
                         num(p[2]).value - num(point[2]).value};
    return python_norm(d, 3);
}

Value nearest(const Array& cover, const Value& point_value) {
    if (cover.empty()) fail("Anchor has no exposed landing region");
    const Array& point = items(point_value);
    Value best;
    double best_distance = 0.;
    bool found = false;
    for (const auto& b_value : cover) {
        const Array& b = items(b_value);
        Array p;
        for (std::size_t a = 0; a < 3; ++a) {
            const Num clamped = py_min(py_max(num(point[a]), num(b[a])), num(b[a + 3]));
            p.push_back(value(clamped));
        }
        const double distance = dist(p, point);
        // min by (distance, p): a tie keeps the smaller point, then the first.
        bool better = !found || distance < best_distance;
        if (!better && distance == best_distance) better = py_compare(array(p), best) < 0;
        if (better) {
            best = array(std::move(p));
            best_distance = distance;
            found = true;
        }
    }
    return best;
}

const Object& block_named(const Object& scene, const Value& id) {
    for (const auto& b : items(at(scene, "blocks")))
        if (py_equal(at(fields(b), "id"), id)) return fields(b);
    throw std::runtime_error("StopIteration");
}

Value landing_cover(const Object& data, const Value& block) {
    const Object& b = block_named(fields(at(data, "scene")), block);
    Array out;
    for (const auto& s : items(at(fields(at(data, "geometry")), "supports")))
        if (contains(items(at(fields(s), "solids")), at(b, "solidIndex")) &&
            py_equal(at(fields(s), "height"), at(b, "top")))
            for (const auto& box : items(at(fields(s), "cover"))) out.push_back(box);
    return array(std::move(out));
}

Value terminal_point(const Object& data) {
    const Object& scene = fields(at(data, "scene"));
    const Object& task = fields(at(data, "task"));
    if (as_text(at(task, "kind")) != "landing") fail("Anchor search requires a finite physical landing task");
    const Object& b = block_named(scene, at(task, "block"));
    const Array& box = items(items(at(fields(at(scene, "collisionScene")), "solids")).at(
        static_cast<std::size_t>(as_int(at(b, "solidIndex")))));
    const Object& objective = fields(at(scene, "objective"));
    const std::size_t axis = as_text(at(objective, "axis")) == "X" ? 0 : 2, other = 2 - axis;
    const bool high = as_text(at(objective, "sense")) == "MAX";
    Array point = {real(0.), at(b, "top"), real(0.)};
    point[axis] = box[high ? axis + 3 : axis];
    point[other] = real((num(box[other]).value + num(box[other + 3]).value) / 2);
    return array(std::move(point));
}

// anchor_guides.unit(x, z), or nullopt for no length.
std::optional<std::array<double, 2>> unit(double x, double z) {
    const double length = python_hypot(x, z);
    if (length <= 1e-12) return std::nullopt;
    return std::array<double, 2>{x / length, z / length};
}

}  // namespace

Value build_guides(const Value& data_value, const Value& policy_value) {
    const Object& data = fields(data_value);
    const Object& scene = fields(at(data, "scene"));
    const Object& policy = fields(policy_value);
    Array names = {at(scene, "startBlock")};
    for (const auto& a : items(at(policy, "anchors"))) names.push_back(a);
    names.push_back(at(fields(at(data, "task")), "block"));
    const std::size_t n = names.size();
    std::vector<Value> points(n);
    points[n - 1] = terminal_point(data);
    Object covers;
    for (const auto& name : names) put(covers, as_text(name), landing_cover(data, name));
    for (std::size_t i = n - 2; i > 0; --i) points[i] = nearest(items(at(covers, as_text(names[i]))), points[i + 1]);
    points[0] = at(fields(at(scene, "initial")), "position");
    const Array& solids = items(at(fields(at(scene, "collisionScene")), "solids"));
    Object footprints;
    for (const auto& name : names) {
        Array boxes;
        for (const auto& b : items(at(scene, "blocks")))
            if (py_equal(at(fields(b), "id"), name)) boxes.push_back(solids.at(static_cast<std::size_t>(as_int(at(fields(b), "solidIndex")))));
        put(footprints, as_text(name), array(std::move(boxes)));
    }
    const Object& turnaround = fields(at(policy, "turnaround"));
    Array turns;
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const Array &s = items(points[i - 1]), &e = items(points[i]), &nx = items(points[i + 1]);
        const auto incoming = unit(num(e[0]).value - num(s[0]).value, num(e[2]).value - num(s[2]).value);
        const auto away = unit(num(e[0]).value - num(nx[0]).value, num(e[2]).value - num(nx[2]).value);
        if (!incoming || !away) continue;
        const double dot = python_sum({(*incoming)[0] * (*away)[0], (*incoming)[1] * (*away)[1]});
        const double angle = degrees(std::acos(std::max(-1., std::min(1., -dot))));
        if (angle <= num(at(turnaround, "degrees")).value) continue;
        const auto normal = unit((*incoming)[0] + (*away)[0], (*incoming)[1] + (*away)[1]);
        if (!normal) continue;
        const Object& b = block_named(scene, names[i]);
        const Array& box = items(solids.at(static_cast<std::size_t>(as_int(at(b, "solidIndex")))));
        Array footprint;
        double offset = 0.;
        bool first = true;
        for (std::size_t x : {0u, 3u})
            for (std::size_t z : {2u, 5u}) {
                footprint.push_back(array({box[x], box[z]}));
                const double c = (*normal)[0] * num(box[x]).value + (*normal)[1] * num(box[z]).value;
                if (first || c < offset) offset = c;
                first = false;
            }
        Object turn{{"version", integer_value(1)},
                    {"index", integer_value(static_cast<std::int64_t>(i))},
                    {"anchor", names[i]},
                    {"S", points[i - 1]},
                    {"E", points[i]},
                    {"N", points[i + 1]},
                    {"angleDegrees", real(angle)},
                    {"normal", array({real((*normal)[0]), real((*normal)[1])})},
                    {"offset", real(offset)},
                    {"margin", at(turnaround, "spatialMargin")},
                    {"units", text("blocks")},
                    {"footprint", array(std::move(footprint))},
                    {"spans", array({integer_value(static_cast<std::int64_t>(i - 1)),
                                     integer_value(static_cast<std::int64_t>(i))})}};
        turn.emplace_back("id", text("turn:" + digest(object(turn))));
        turns.push_back(object(std::move(turn)));
    }
    Object result{{"version", integer_value(2)},
                  {"anchors", array(names)},
                  {"points", array(Array(points.begin(), points.end()))},
                  {"covers", object(std::move(covers))},
                  {"coverPhase", text("pre_or_post")},
                  {"physicalFootprints", object(std::move(footprints))},
                  {"turns", array(std::move(turns))}};
    result.emplace_back("id", text("guides:" + digest(object(result))));
    return object(std::move(result));
}

Value anchor_settings(const Value& value, const Value& scene_value) {
    Object result{{"version", integer_value(1)},
                  {"anchors", array({})},
                  {"turnaround", object({{"degrees", real(110.)}, {"spatialMargin", real(.3)}})},
                  {"solveEveryDecisionLevels", integer_value(1)},
                  {"feasibilityPolicy", text("numerical_oracle")},
                  {"observability", boolean(true)}};
    if (!is_object(value)) fail("Unsupported anchorSearch fields");
    for (const auto& [k, v] : fields(value))
        if (!get(result, k)) fail("Unsupported anchorSearch fields");
    for (const auto& [k, v] : fields(value)) put(result, k, v);
    const Object& scene = fields(scene_value);
    std::set<std::string> names;
    std::map<std::string, std::int64_t> solid;  // the last block of an id
    for (const auto& b : items(at(scene, "blocks"))) {
        names.insert(as_text(at(fields(b), "id")));
        solid[as_text(at(fields(b), "id"))] = as_int(at(fields(b), "solidIndex"));
    }
    const Value& anchors_value = at(result, "anchors");
    bool valid = is_list(anchors_value);
    std::vector<std::string> anchors;
    if (valid) {
        std::set<std::string> seen;
        for (const auto& a : items(anchors_value)) {
            const auto* name = std::get_if<std::string>(&a.data);
            valid = valid && name && seen.insert(*name).second && names.count(*name);
            if (name) anchors.push_back(*name);
        }
    }
    if (!valid) fail("Anchors must be ordered unique scene blocks");
    const std::string& start = as_text(at(scene, "startBlock"));
    const std::string& goal = as_text(at(scene, "goalBlock"));
    if (std::find(anchors.begin(), anchors.end(), start) != anchors.end() ||
        std::find(anchors.begin(), anchors.end(), goal) != anchors.end())
        fail("Initial and terminal supports are implicit endpoints");
    // Visits name merged faces, so an anchor is only reached if its top is a whole face of its own (user, 2026-11-22).
    const SceneGeometry geometry(at(fields(at(scene, "collisionScene")), "solids"));
    const std::vector<FaceOption> tops = scene_contact_options(scene_value, geometry, 1, 1);
    const SolidOwners owners = solid_owners(scene_value);
    const AnchorScene named(scene_value);
    std::vector<std::string> checked = anchors;
    checked.push_back(start);
    checked.push_back(goal);
    for (const auto& a : checked) {
        std::set<std::string> shared;
        for (const auto& f : tops)
            if (std::find(f.members.begin(), f.members.end(), solid.at(a)) != f.members.end())
                for (std::int64_t i : f.members)
                    if (auto o = owners.find(i); o != owners.end()) shared.insert(o->second.begin(), o->second.end());
        shared.erase(named.owner(a));
        if (!shared.empty()) {
            std::string listed;
            for (const auto& o : shared) listed += (listed.empty() ? "" : ", ") + o;
            fail(a + " shares its top face with " + listed + "; declare them as one block");
        }
    }
    if (!py_equal(at(result, "version"), integer_value(1)) ||
        !py_equal(at(result, "feasibilityPolicy"), text("numerical_oracle")))
        fail("Unsupported anchor policy: version must be 1 and feasibilityPolicy 'numerical_oracle'");
    const Value& levels = at(result, "solveEveryDecisionLevels");
    if (!std::holds_alternative<std::int64_t>(levels.data) || std::get<std::int64_t>(levels.data) < 1)
        fail("Decision solve interval must be positive");
    if (!std::holds_alternative<bool>(at(result, "observability").data)) fail("observability must be boolean");
    const Value& turn = at(result, "turnaround");
    if (!is_object(turn)) fail("Unsupported turnaround settings");
    for (const auto& [k, v] : fields(turn))
        if (k != "degrees" && k != "spatialMargin") fail("Unsupported turnaround settings");
    Object merged{{"degrees", real(110.)}, {"spatialMargin", real(.3)}};
    for (const auto& [k, v] : fields(turn)) put(merged, k, v);
    for (const auto& [k, v] : merged) {
        const bool finite = std::holds_alternative<std::int64_t>(v.data) ||
                            (std::holds_alternative<double>(v.data) && std::isfinite(std::get<double>(v.data)));
        if (!finite) fail("Turn settings must be finite numbers");
    }
    const double degrees_value = num(at(merged, "degrees")).value;
    if (!(0 < degrees_value && degrees_value < 180) || num(at(merged, "spatialMargin")).value < 0)
        fail("Invalid turn angle or margin");
    put(result, "turnaround", object(std::move(merged)));
    return object(std::move(result));
}

Value semantic_settings(const Value& policy) {
    Object out;
    for (const auto& [k, v] : fields(policy))
        if (k != "observability") out.emplace_back(k, v);
    out.emplace_back("hardJumpPolicyVersion", integer_value(kHardJumpPolicyVersion));
    out.emplace_back("searchPolicyVersion", integer_value(kSearchPolicyVersion));
    out.emplace_back("comparisonPolicyVersion", integer_value(kComparisonPolicyVersion));
    return object(std::move(out));
}

Value encounter_groups(const Value& scene_value, const std::vector<std::string>& names, InteractionGate& gate,
                       const AnchorScene& anchors) {
    const Object& scene = fields(scene_value);
    std::map<std::string, std::int64_t> solid;
    std::map<std::int64_t, std::vector<std::string>> by_solid;
    for (const auto& b : items(at(scene, "blocks"))) {
        const std::string& id = as_text(at(fields(b), "id"));
        solid[id] = as_int(at(fields(b), "solidIndex"));
        by_solid[solid[id]].push_back(id);
    }
    std::vector<std::int64_t> indices;
    for (const auto& [i, ids] : by_solid) indices.push_back(i);
    const Array& solids = items(at(fields(at(scene, "collisionScene")), "solids"));
    const auto table = [&](const std::string& name) -> const FaceTable& {
        // face_table(scene, support_face(scene, name), ...): a block id names the face its solid's top is on.
        std::optional<std::string> key = name;
        if (auto s = solid.find(name); s != solid.end()) {
            Box box;
            for (std::size_t k = 0; k < 6; ++k) box[k] = num(items(solids.at(static_cast<std::size_t>(s->second)))[k]);
            key = anchors.face({box});
        }
        return gate.reachable_faces(key);
    };
    const auto neighbours = [&](const std::string& name) {
        std::set<std::string> out;
        for (std::int64_t i : gate.geometry().touching(solid.at(name), indices))
            for (const auto& n : by_solid.at(i)) out.insert(n);
        return out;
    };
    Array groups;
    std::size_t first = 0;
    while (first + 1 < names.size()) {
        std::set<std::string> members{names[first + 1]}, landings{names[first + 1]};
        while (true) {
            std::set<std::string> frontier;
            for (const auto& m : members)
                for (const auto& c : neighbours(m))
                    if (!members.count(c) && std::any_of(landings.begin(), landings.end(),
                                                         [&](const std::string& l) { return table(l).blocks.count(c) > 0; }))
                        frontier.insert(c);
            if (frontier.empty()) break;
            members.insert(frontier.begin(), frontier.end());
            for (bool grown = true; grown;) {
                std::set<std::string> added;
                for (const auto& c : members)
                    if (!landings.count(c) && std::any_of(landings.begin(), landings.end(), [&](const std::string& l) {
                            return table(l).landing.count(c) > 0;
                        }))
                        added.insert(c);
                landings.insert(added.begin(), added.end());
                grown = !added.empty();
            }
        }
        std::set<std::string> owners;
        for (const auto& l : landings) owners.insert(anchors.owner(l));
        std::optional<std::size_t> reached;
        for (std::size_t m = first + 1; m < names.size(); ++m)
            if (owners.count(anchors.owner(names[m]))) reached = m;
        if (!reached) fail("max() arg is an empty sequence");
        const std::size_t last = *reached - 1;
        Array group_anchors, member_list, landing_list;
        for (std::size_t k = first + 1; k < std::min(last + 2, names.size()); ++k) group_anchors.push_back(text(names[k]));
        for (const auto& m : members) member_list.push_back(text(m));
        for (const auto& l : landings) landing_list.push_back(text(l));
        groups.push_back(object({{"first", integer_value(static_cast<std::int64_t>(first))},
                                 {"last", integer_value(static_cast<std::int64_t>(last))},
                                 {"anchors", array(std::move(group_anchors))}, {"members", array(std::move(member_list))},
                                 {"landings", array(std::move(landing_list))}}));
        first = last + 1;
    }
    return array(std::move(groups));
}

}  // namespace pkc
