// Port of search/anchors/policy.py's program readers.
#include "pkc/search/anchors/policy.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "../../py_math.hpp"
#include "../../schema.hpp"
#include "pkc/canonical.hpp"

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
