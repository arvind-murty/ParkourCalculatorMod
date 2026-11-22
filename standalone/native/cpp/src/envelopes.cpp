// Port of reachability/intervals.reachable_envelopes. Results must equal the
// Python's, including int/float kinds (positions and planes can be ints), so
// bounds use Num with Python's max/min tie rules.
#include "pkc/envelopes.hpp"

#include <array>
#include <cfloat>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "pkc/vertical.hpp"
#include "value_ops.hpp"

namespace pkc {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr std::int64_t kParentWindow = 10;  // intervals.PARENT_WINDOW

using Interval = std::array<Num, 2>;
using StateBox = std::array<Interval, 3>;  // X, Z, Y

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

// CPython 3.12.2 math.hypot (vector_norm with compensated rounding) for two
// coordinates; libm hypot can differ by one ULP and change a travel decision.
// See native/cpp/LICENSE.cpython and Modules/mathmodule.c at v3.12.2.
double python_hypot(double a, double b) {
    double values[2] = {std::fabs(a), std::fabs(b)};
    double maximum = values[1] > values[0] ? values[1] : values[0];
    if (std::isinf(values[0]) || std::isinf(values[1])) return kInf;
    if (std::isnan(values[0]) || std::isnan(values[1])) return std::numeric_limits<double>::quiet_NaN();
    if (maximum == 0.) return maximum;
    int exponent;
    std::frexp(maximum, &exponent);
    if (exponent < -1023) return DBL_MIN * python_hypot(values[0] / DBL_MIN, values[1] / DBL_MIN);
    double scale = std::ldexp(1., -exponent), sum = 1., frac1 = 0., frac2 = 0.;
    for (double value : values) {
        double x = value * scale;
        double product = x * x, product_error = std::fma(x, x, -product);
        double combined = sum + product, error = (sum - combined) + product;
        sum = combined;
        frac1 += product_error;
        frac2 += error;
    }
    double h = std::sqrt(sum - 1. + (frac1 + frac2));
    double product = -h * h, product_error = std::fma(-h, h, -product);
    double combined = sum + product, error = (sum - combined) + product;
    sum = combined;
    frac1 += product_error;
    frac2 += error;
    double correction = sum - 1. + (frac1 + frac2);
    h += correction / (2. * h);
    return h / scale;
}

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

}  // namespace pkc
