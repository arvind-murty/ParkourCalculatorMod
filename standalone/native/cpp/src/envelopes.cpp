// Port of reachability/intervals.reachable_envelopes. Results must equal the
// Python's, including int/float kinds (positions and planes can be ints), so
// bounds use Num with Python's max/min tie rules.
#include "pkc/envelopes.hpp"


#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "py_math.hpp"
#include "pkc/vertical.hpp"
#include "value_ops.hpp"

namespace pkc {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr std::int64_t kParentWindow = 10;  // intervals.PARENT_WINDOW

using Interval = std::array<Num, 2>;
using StateBox = std::array<Interval, 3>;  // X, Z, Y

std::int64_t integer_of(const Value& value) { return std::get<std::int64_t>(value.data); }

Num num(const Value& value) {  // a Python int or float operand
    if (auto* integer = std::get_if<std::int64_t>(&value.data)) return whole(*integer);
    if (auto* real_value = std::get_if<double>(&value.data)) return flt(*real_value);
    fail("expected a number");
}

Num clamp(const Num& value) { return py_max(flt(-kCap), py_min(flt(kCap), value)); }  // physics/vertical.clamp

Value interval(const Interval& range) { return array({value(range[0]), value(range[1])}); }

Value box(const Interval& x, const Interval& y, const Interval& z) {
    return object({{"X", interval(x)}, {"Y", interval(y)}, {"Z", interval(z)}});
}

Value reason(std::int64_t state, int axis, const char* why) {
    return object({{"state", integer_value(state)}, {"axis", integer_value(axis)}, {"reason", text(why)}});
}

}  // namespace

Value reachable_envelopes(const Value& problem_value, const Value& y_value, std::optional<std::int64_t> parent_ticks) {
    const Object& problem = fields(problem_value);
    std::map<std::pair<std::int64_t, int>, Interval> bounds;
    if (const Value* walls = get(problem, "walls")) {
        for (const auto& row_value : items(*walls)) {
            const Object& row = fields(row_value);
            const std::string& mode = as_text(at(row, "mode"));
            if ((mode != "X" && mode != "Z") || !is_null(get_or_null(row, "t2"))) continue;
            auto& range = bounds.try_emplace({std::get<std::int64_t>(at(row, "t1").data), mode == "X" ? 0 : 1},
                                             Interval{flt(-kInf), flt(kInf)})
                              .first->second;
            const std::string& cmp = as_text(at(row, "cmp"));
            Num rhs = num(at(row, "rhs"));
            if (cmp == "GE" || cmp == "EQ") range[0] = py_max(range[0], sub(rhs, flt(1e-7)));
            if (cmp == "LE" || cmp == "EQ") range[1] = py_min(range[1], add(rhs, flt(1e-7)));
        }
    }
    const Array& position = items(at(problem, "position"));
    const Array& velocity = items(at(problem, "velocity"));
    std::array<Num, 2> p_lo{num(position[0]), num(position[1])}, p_hi = p_lo;
    std::array<Num, 2> v_lo{num(velocity[0]), num(velocity[1])}, v_hi = v_lo;
    const Value* initial = get(problem, "initialIntervals");
    if (initial) {
        std::array<std::array<Num, 2>*, 4> targets = {&p_lo, &p_hi, &v_lo, &v_hi};
        for (int k = 0; k < 4; ++k) {
            const Array& values = items(items(*initial).at(k));
            *targets[k] = {num(values.at(0)), num(values.at(1))};
        }
    }
    std::map<std::pair<std::int64_t, std::int64_t>, const Object*> contacts;  // later duplicates win, like the dict
    if (const Value* listed = get(problem, "contacts"))
        for (const auto& contact : items(*listed))
            contacts[{std::get<std::int64_t>(at(fields(contact), "state").data),
                      std::get<std::int64_t>(at(fields(contact), "axis").data)}] = &fields(contact);
    const std::int64_t ticks = std::get<std::int64_t>(at(problem, "ticks").data);
    std::vector<bool> ladder(ticks + 1, false);
    if (const Value* listed = get(problem, "ladderTicks")) {
        const Array& flags = items(*listed);
        ladder.assign(flags.size(), false);
        for (std::size_t t = 0; t < flags.size(); ++t) ladder[t] = py_truthy(flags[t]);
    }
    const Array& magnitudes = items(at(problem, "magnitude"));
    const Array& frictions = items(at(problem, "friction"));
    const bool has_y = !is_null(y_value);
    std::vector<StateBox> states;
    std::vector<std::array<std::array<Num, 2>, 2>> velocities;  // [v_lo, v_hi]
    std::vector<std::array<Interval, 2>> movements;
    Value contradiction = kNull;
    for (std::int64_t tick = 0; tick <= ticks; ++tick) {
        for (int axis = 0; axis < 2; ++axis) {
            auto found = bounds.find({tick, axis});
            Interval range = found == bounds.end() ? Interval{flt(-kInf), flt(kInf)} : found->second;
            p_lo[axis] = py_max(p_lo[axis], range[0]);
            p_hi[axis] = py_min(p_hi[axis], range[1]);
            if (p_lo[axis].value > p_hi[axis].value + 1e-9 * (1 + std::fabs(p_lo[axis].value) + std::fabs(p_hi[axis].value)))
                contradiction = reason(tick, axis, "interval_reach_contradiction");
        }
        Interval height = {flt(-kInf), flt(kInf)};
        if (has_y) height = {num(items(y_value).at(tick)), num(items(y_value).at(tick))};
        states.push_back({Interval{p_lo[0], p_hi[0]}, Interval{p_lo[1], p_hi[1]}, height});
        velocities.push_back({v_lo, v_hi});
        if (!is_null(contradiction) || tick == ticks) break;
        Num magnitude = num(magnitudes.at(tick)), friction = num(frictions.at(tick));
        std::array<Interval, 2> move;
        for (int axis = 0; axis < 2; ++axis) {
            Num movement_lo = sub(py_min(v_lo[axis], flt(0.)), magnitude);
            Num movement_hi = add(py_max(v_hi[axis], flt(0.)), magnitude);
            if (ladder.at(tick)) {
                movement_lo = clamp(movement_lo);
                movement_hi = clamp(movement_hi);
            }
            move[axis] = {sub(movement_lo, flt(1e-8)), add(movement_hi, flt(1e-8))};
            auto contact = contacts.find({tick + 1, axis});
            if (contact != contacts.end()) {
                Num plane = num(at(*contact->second, "plane"));
                double direction = num(at(*contact->second, "direction")).value;
                double lo = p_lo[axis].value, hi = p_hi[axis].value;
                if ((direction > 0 && (lo > plane.value + 1e-7 || hi + movement_hi.value < plane.value - 1e-7)) ||
                    (direction < 0 && (hi < plane.value - 1e-7 || lo + movement_lo.value > plane.value + 1e-7)))
                    contradiction = reason(tick + 1, axis, "contact_reach_contradiction");
                p_lo[axis] = p_hi[axis] = plane;
                v_lo[axis] = v_hi[axis] = flt(0.);
                continue;
            }
            Num padding = flt(1e-12 * (1 + std::fabs(p_lo[axis].value) + std::fabs(p_hi[axis].value) +
                                       std::fabs(movement_lo.value) + std::fabs(movement_hi.value)));
            p_lo[axis] = add(p_lo[axis], sub(movement_lo, padding));
            p_hi[axis] = add(p_hi[axis], add(movement_hi, padding));
            v_lo[axis] = sub(mul(movement_lo, friction), padding);
            v_hi[axis] = add(mul(movement_hi, friction), padding);
        }
        movements.push_back(move);
    }
    const std::size_t count = states.size();
    if (is_null(contradiction)) {
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t t = count - 1; t-- > 0;)
                for (int i = 0; i < 2; ++i) {
                    const auto& [lo, hi] = movements[t][i];
                    states[t][i][0] = py_max(states[t][i][0], sub(states[t + 1][i][0], hi));
                    states[t][i][1] = py_min(states[t][i][1], sub(states[t + 1][i][1], lo));
                }
            for (std::size_t t = 0; t + 1 < count; ++t)
                for (int i = 0; i < 2; ++i) {
                    const auto& [lo, hi] = movements[t][i];
                    states[t + 1][i][0] = py_max(states[t + 1][i][0], add(states[t][i][0], lo));
                    states[t + 1][i][1] = py_min(states[t + 1][i][1], add(states[t][i][1], hi));
                }
            for (std::size_t t = 0; t < count; ++t)
                for (int i = 0; i < 2; ++i)
                    if (states[t][i][0].value > states[t][i][1].value + 1e-7)
                        contradiction = reason(static_cast<std::int64_t>(t), i, "interval_reach_contradiction");
        }
    }
    // phases[t] = {Y, X, Z} boxes, each {X, Z, Y} intervals.
    std::vector<std::array<StateBox, 3>> phases;
    for (std::size_t t = 0; t + 1 < count; ++t) {
        const StateBox &a = states[t], &b = states[t + 1];
        auto sweep = [&](int axis) {
            return Interval{py_min(a[axis][0], b[axis][0]), py_max(a[axis][1], b[axis][1])};
        };
        phases.push_back({StateBox{a[0], a[1], sweep(2)}, StateBox{sweep(0), a[1], b[2]}, StateBox{b[0], sweep(1), b[2]}});
    }
    // Named step/grind events describe realized Y, not the heights of both legacy
    // step probes, so their phases keep unknown Y and widen X/Z to the realized box.
    const Value& vertical = get_or_null(problem, "vertical");
    const Value* vertical_contacts = is_null(vertical) ? nullptr : get(fields(vertical), "contacts");
    if (vertical_contacts) {
        for (const auto& contact_value : items(*vertical_contacts)) {
            const Object& contact = fields(contact_value);
            std::int64_t tick = std::get<std::int64_t>(at(contact, "state").data) - 1;
            if (!py_truthy(get_or_null(contact, "case")) || tick < 0 || tick >= static_cast<std::int64_t>(phases.size()))
                continue;
            for (auto& phase : phases[tick]) phase[2] = {flt(-kInf), flt(kInf)};
            for (int p : {1, 2}) {  // phases X and Z
                for (int i = 0; i < 2; ++i) {
                    const Interval& range = states[tick][i];
                    const Interval& move = movements[tick][i];
                    const Interval& realized = states[tick + 1][i];
                    phases[tick][p][i] = {py_min(py_min(range[0], add(range[0], move[0])), realized[0]),
                                          py_max(py_max(range[1], add(range[1], move[1])), realized[1])};
                }
            }
        }
    }
    double speed;
    if (initial) {
        const Array &lo = items(items(*initial).at(2)), &hi = items(items(*initial).at(3));
        auto widest = [&](int k) {
            Num a = num(lo.at(k)), b = num(hi.at(k));
            return py_max(flt(std::fabs(a.value)), flt(std::fabs(b.value))).value;
        };
        speed = python_hypot(widest(0), widest(1));
    } else {
        speed = python_hypot(num(velocity[0]).value, num(velocity[1]).value);
    }
    std::vector<double> radii = {0.};  // travel_radii(speed, magnitude, friction, margin=1e-8)
    for (std::size_t t = 0; t < std::min(magnitudes.size(), frictions.size()); ++t) {
        double move = speed + num(magnitudes[t]).value + 1e-8;
        radii.push_back(radii.back() + move);
        speed = num(frictions[t]).value * move;
    }
    std::int64_t constrained = 0;
    for (const auto& [key, range] : bounds) constrained = std::max(constrained, key.first);
    for (const auto& [key, contact] : contacts) constrained = std::max(constrained, key.first);
    if (is_null(contradiction)) {
        std::int64_t start = parent_ticks ? *parent_ticks + 1 : 1;
        std::int64_t earliest = parent_ticks ? std::max<std::int64_t>(0, *parent_ticks + 1 - kParentWindow) : 0;
        std::int64_t last = std::min<std::int64_t>(constrained, static_cast<std::int64_t>(count) - 1);
        for (std::int64_t j = start; j <= last && is_null(contradiction); ++j) {
            for (std::int64_t i = earliest; i < j; ++i) {
                double gaps[2];
                for (int axis = 0; axis < 2; ++axis)
                    gaps[axis] = std::max({0., states[j][axis][0].value - states[i][axis][1].value,
                                           states[i][axis][0].value - states[j][axis][1].value});
                double gap = python_hypot(gaps[0], gaps[1]);
                if (gap > radii[j] - radii[i] + 1e-7) {
                    contradiction = object({{"state", integer_value(j)},
                                            {"previousState", integer_value(i)},
                                            {"reason", text("norm_reach_contradiction")},
                                            {"gap", real(gap)},
                                            {"travelUpperBound", real(radii[j] - radii[i])}});
                    break;
                }
            }
        }
    }
    Array state_values, phase_values, velocity_values, radius_values;
    for (const StateBox& s : states)
        state_values.push_back(object({{"X", interval(s[0])}, {"Z", interval(s[1])}, {"Y", interval(s[2])}}));
    for (const auto& phase : phases) {
        Object entry;
        const char* names[3] = {"Y", "X", "Z"};
        for (int p = 0; p < 3; ++p) entry.emplace_back(names[p], box(phase[p][0], phase[p][2], phase[p][1]));
        phase_values.push_back(object(std::move(entry)));
    }
    for (const auto& [lo, hi] : velocities)
        velocity_values.push_back(array({array({value(lo[0]), value(lo[1])}), array({value(hi[0]), value(hi[1])})}));
    for (double radius : radii) radius_values.push_back(real(radius));
    return object({{"states", array(std::move(state_values))},
                   {"phases", array(std::move(phase_values))},
                   {"velocities", array(std::move(velocity_values))},
                   {"contradiction", contradiction},
                   {"radii", array(std::move(radius_values))},
                   {"origin", initial ? kNull : at(problem, "position")}});
}

Value interaction_reachability(const Value& envelopes_value, const Value& interaction_value, bool refine) {
    const Object& envelopes = fields(envelopes_value);
    const Object interaction = is_list(interaction_value) ? Object{{"rows", interaction_value}} : fields(interaction_value);
    auto ruled_out = [](Object fields_after) {
        Object out{{"ruledOut", boolean(true)}};
        for (auto& field : fields_after) out.push_back(std::move(field));
        return object(std::move(out));
    };
    const Value not_ruled_out = object({{"ruledOut", boolean(false)}, {"reason", text("not_ruled_out")}});
    if (const Value* contradiction = get(envelopes, "contradiction"); contradiction && py_truthy(*contradiction))
        return ruled_out(fields(*contradiction));
    if (const Value* alternatives = get(interaction, "alternatives")) {
        Array results;
        for (const auto& item : items(*alternatives))
            results.push_back(interaction_reachability(envelopes_value, item, refine));
        const bool all = std::all_of(results.begin(), results.end(),
                                     [](const Value& r) { return py_truthy(at(fields(r), "ruledOut")); });
        if (!results.empty() && all)
            return ruled_out({{"reason", text("all_interaction_alternatives_excluded")}, {"alternatives", array(results)}});
        return not_ruled_out;
    }
    const Array& states = items(at(envelopes, "states"));
    const auto count = static_cast<std::int64_t>(states.size());
    auto in_states = [&](const Value& t) {
        return std::holds_alternative<std::int64_t>(t.data) && integer_of(t) >= 0 && integer_of(t) < count;
    };
    std::map<std::pair<std::int64_t, std::string>, std::array<Num, 2>> tightened;
    if (const Value* rows = get(interaction, "rows")) {
        for (const auto& row_value : items(*rows)) {
            const Object& row = fields(row_value);
            const Value& axis_value = get_or_null(row, "mode");
            const Value* t1 = get(row, "t1");
            const Value& t_value = t1 ? *t1 : get_or_null(row, "offset");
            const bool horizontal = std::holds_alternative<std::string>(axis_value.data) &&
                                    (as_text(axis_value) == "X" || as_text(axis_value) == "Z");
            if (!horizontal || is_null(t_value) || !in_states(t_value)) continue;
            const std::string& axis = as_text(axis_value);
            const std::int64_t t = integer_of(t_value);
            auto found = tightened.find({t, axis});
            std::array<Num, 2> range;
            if (found != tightened.end()) {
                range = found->second;
            } else {
                const Array& state = items(at(fields(states[t]), axis));
                range = {num(state[0]), num(state[1])};
            }
            Num lo = range[0], hi = range[1];
            const Value& t2 = get_or_null(row, "t2");
            if (!is_null(t2)) {
                if (!in_states(t2)) continue;
                const Array& other = items(at(fields(states[integer_of(t2)]), axis));
                const Num a = num(other[0]), b = num(other[1]);
                const Value* op = get(row, "op");
                if (op && std::holds_alternative<std::string>(op->data) && as_text(*op) == "MINUS") {
                    std::tie(lo, hi) = std::make_pair(sub(lo, b), sub(hi, a));
                } else {
                    std::tie(lo, hi) = std::make_pair(add(lo, a), add(hi, b));
                }
            }
            const Num rhs = num(at(row, "rhs"));
            const std::string& cmp = as_text(at(row, "cmp"));
            const bool lower = cmp == "GE" || cmp == "EQ", upper = cmp == "LE" || cmp == "EQ";
            if ((lower && hi.value < rhs.value - 1e-7) || (upper && lo.value > rhs.value + 1e-7))
                return ruled_out({{"reason", text("interaction_row_unreachable")}, {"tick", t_value},
                                  {"phase", text("state")}, {"axis", axis_value},
                                  {"reachable", array({value(lo), value(hi)})},
                                  {"required", object({{"cmp", at(row, "cmp")}, {"rhs", at(row, "rhs")}})}});
            if (is_null(t2))
                tightened[{t, axis}] = {lower ? py_max(lo, flt(rhs.value - 1e-7)) : lo,
                                        upper ? py_min(hi, flt(rhs.value + 1e-7)) : hi};
        }
    }
    if (const Value* regions = get(interaction, "regions")) {
        const Value& origin = get_or_null(envelopes, "origin");
        const Value* radii = get(envelopes, "radii");
        for (const auto& region_value : items(*regions)) {
            const Object& region = fields(region_value);
            const Value& t_value = at(region, "tick");
            const Value* phase_value = get(region, "phase");
            const std::string phase = phase_value ? as_text(*phase_value) : "state";
            const Array& domains = phase == "state" ? states : items(at(envelopes, "phases"));
            const std::int64_t t = integer_of(t_value);
            if (t < 0 || t >= static_cast<std::int64_t>(domains.size())) continue;
            const Object& domain = phase == "state" ? fields(domains[t]) : fields(at(fields(domains[t]), phase));
            const Array& box = items(at(region, "bounds"));
            if (!is_null(origin) && radii) {
                const Value& radius = items(*radii).at(phase == "state" || phase == "Y" ? t : t + 1);
                double gaps[2];
                for (int k = 0; k < 2; ++k) {
                    const int i = k * 2;
                    const Num c = num(items(origin)[k]);
                    gaps[k] = py_max(py_max(sub(num(box[i]), c), flt(0.)), sub(c, num(box[i + 3]))).value;
                }
                const double gap = python_hypot(gaps[0], gaps[1]);
                if (gap > num(radius).value + 1e-7)
                    return ruled_out({{"reason", text("interaction_distance_unreachable")}, {"tick", t_value},
                                      {"phase", text(phase)}, {"gap", real(gap)}, {"travelUpperBound", radius}});
            }
            const Value* interior = get(region, "interior");
            const bool inside = interior && py_truthy(*interior);
            for (int i = 0; i < 3; ++i) {
                const char* axis = i == 0 ? "X" : i == 1 ? "Y" : "Z";
                const Array& range = items(at(domain, axis));
                const double lo = num(range[0]).value, hi = num(range[1]).value;
                const double low = num(box[i]).value, high = num(box[i + 3]).value;
                const bool excluded = inside ? (hi <= low || lo >= high) : (hi < low - 1e-7 || lo > high + 1e-7);
                if (excluded)
                    return ruled_out({{"reason", text("interaction_region_unreachable")}, {"tick", t_value},
                                      {"phase", text(phase)}, {"axis", text(axis)},
                                      {"reachable", array({range[0], range[1]})},
                                      {"required", array({box[i], box[i + 3]})}});
            }
        }
    }
    if (refine) {
        const Value* model = get(envelopes, "model");
        if (model && !is_null(*model)) throw Unsupported("refined interaction screens run disk.screen in Python");
    }
    return not_ruled_out;
}

Value interaction_reachability_many(const Value& envelopes, const Value& interactions) {
    Array out;
    for (const auto& query : items(interactions)) out.push_back(interaction_reachability(envelopes, query));
    return array(std::move(out));
}

}  // namespace pkc
