// Research port of reachability/intervals.py; Python remains the production oracle.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace py = pybind11;
using namespace pybind11::literals;
using Pair = std::array<double, 2>;
using State = std::array<Pair, 3>;
constexpr double INF = std::numeric_limits<double>::infinity();

static py::object get(py::handle d, const char* key, py::object fallback = py::none()) {
    PyObject* value = PyDict_GetItemString(d.ptr(), key);
    return value ? py::reinterpret_borrow<py::object>(value) : fallback;
}
static bool truth(py::handle o) { return PyObject_IsTrue(o.ptr()) == 1; }
static double number(py::handle o) { return py::cast<double>(o); }
static int integer(py::handle o) { return py::cast<int>(o); }
static Pair pair(py::handle o) {
    auto a = py::reinterpret_borrow<py::sequence>(o);
    return {number(a[0]), number(a[1])};
}
static py::list list2(double a, double b) {
    py::list result; result.append(a); result.append(b); return result;
}
static double low(double a, double b) { return b < a ? b : a; }
static double high(double a, double b) { return b > a ? b : a; }
static bool horizontal(const std::string& axis) { return axis == "X" || axis == "Z"; }

// CPython 3.12.2 vector_norm/dl_mul/dl_fast_sum, specialized to two coordinates.
// Retain compensated rounding: libm hypot can differ by one ULP and change bounds.
// See LICENSE.cpython and https://github.com/python/cpython/blob/v3.12.2/Modules/mathmodule.c.
static double hypot2(double a, double b) {
    double values[2] = {std::fabs(a), std::fabs(b)};
    double maximum = high(values[0], values[1]);
    if (std::isinf(values[0]) || std::isinf(values[1])) return INF;
    if (std::isnan(values[0]) || std::isnan(values[1])) return std::numeric_limits<double>::quiet_NaN();
    if (maximum == 0.) return maximum;
    int exponent;
    std::frexp(maximum, &exponent);
    if (exponent < -1023) return DBL_MIN * hypot2(values[0] / DBL_MIN, values[1] / DBL_MIN);
    double scale = std::ldexp(1., -exponent), sum = 1., frac1 = 0., frac2 = 0.;
    for (double value : values) {
        double x = value * scale;
        double product = x * x, product_error = std::fma(x, x, -product);
        double combined = sum + product, error = (sum - combined) + product;
        sum = combined; frac1 += product_error; frac2 += error;
    }
    double h = std::sqrt(sum - 1. + (frac1 + frac2));
    double product = -h * h, product_error = std::fma(-h, h, -product);
    double combined = sum + product, error = (sum - combined) + product;
    sum = combined; frac1 += product_error; frac2 += error;
    double correction = sum - 1. + (frac1 + frac2);
    h += correction / (2. * h);
    return h / scale;
}

static std::vector<double> radii(double speed, const std::vector<double>& magnitudes,
                                 const std::vector<double>& frictions, double margin) {
    std::vector<double> result{0.}; result.reserve(magnitudes.size() + 1);
    for (size_t i = 0; i < std::min(magnitudes.size(), frictions.size()); ++i) {
        double move = speed + magnitudes[i] + margin;
        result.push_back(result.back() + move); speed = frictions[i] * move;
    }
    return result;
}

static py::dict envelopes(py::dict problem, py::object y) {
    int n = integer(problem["ticks"]);
    std::map<std::pair<int,int>, Pair> bounds;
    int constrained = 0;
    for (py::handle row : get(problem, "walls", py::list())) {
        std::string axis = py::cast<std::string>(row["mode"]);
        if (!horizontal(axis) || !get(row, "t2").is_none()) continue;
        int tick = integer(row["t1"]), a = axis == "X" ? 0 : 1;
        auto key = std::make_pair(tick, a);
        auto found = bounds.find(key);
        Pair bound = found == bounds.end() ? Pair{-INF, INF} : found->second;
        auto cmp = py::cast<std::string>(row["cmp"]); double rhs = number(row["rhs"]);
        if (cmp == "GE" || cmp == "EQ") bound[0] = high(bound[0], rhs - 1e-7);
        if (cmp == "LE" || cmp == "EQ") bound[1] = low(bound[1], rhs + 1e-7);
        bounds[key] = bound; constrained = std::max(constrained, tick);
    }
    Pair p_lo = pair(problem["position"]), p_hi = p_lo;
    Pair v_lo = pair(problem["velocity"]), v_hi = v_lo;
    if (problem.contains("initialIntervals")) {
        auto initial = py::reinterpret_borrow<py::sequence>(problem["initialIntervals"]);
        p_lo = pair(initial[0]); p_hi = pair(initial[1]); v_lo = pair(initial[2]); v_hi = pair(initial[3]);
    }
    std::map<std::pair<int,int>, py::dict> contacts;
    for (py::handle c : get(problem, "contacts", py::list())) {
        int tick = integer(c["state"]), axis = integer(c["axis"]);
        contacts[{tick, axis}] = py::reinterpret_borrow<py::dict>(c);
        constrained = std::max(constrained, tick);
    }
    std::vector<bool> ladder(n + 1, false);
    if (problem.contains("ladderTicks")) ladder = py::cast<std::vector<bool>>(problem["ladderTicks"]);
    auto magnitude = py::cast<std::vector<double>>(problem["magnitude"]);
    auto friction = py::cast<std::vector<double>>(problem["friction"]);
    std::vector<State> states; states.reserve(n + 1);
    std::vector<std::array<Pair,2>> movements;
    py::list velocities;
    py::object contradiction = py::none();
    for (int tick = 0; tick <= n; ++tick) {
        for (int axis = 0; axis < 2; ++axis) {
            auto found = bounds.find({tick, axis});
            Pair bound = found == bounds.end() ? Pair{-INF, INF} : found->second;
            p_lo[axis] = high(p_lo[axis], bound[0]); p_hi[axis] = low(p_hi[axis], bound[1]);
            if (p_lo[axis] > p_hi[axis] + 1e-9 * (1 + std::fabs(p_lo[axis]) + std::fabs(p_hi[axis])))
                contradiction = py::dict("state"_a=tick, "axis"_a=axis, "reason"_a="interval_reach_contradiction");
        }
        double height = y.is_none() ? 0. : number(y[py::int_(tick)]);
        states.push_back(State{Pair{p_lo[0], p_hi[0]}, y.is_none() ? Pair{-INF, INF} : Pair{height,height}, Pair{p_lo[1],p_hi[1]}});
        py::list velocity; velocity.append(list2(v_lo[0],v_lo[1])); velocity.append(list2(v_hi[0],v_hi[1]));
        velocities.append(velocity);
        if (!contradiction.is_none() || tick == n) break;
        std::array<Pair,2> moves;
        for (int axis = 0; axis < 2; ++axis) {
            double ml = low(v_lo[axis], 0.) - magnitude[tick];
            double mh = high(v_hi[axis], 0.) + magnitude[tick];
            if (ladder.at(tick)) {
                constexpr double cap = static_cast<double>(0.15f);
                ml = high(-cap, low(cap, ml)); mh = high(-cap, low(cap, mh));
            }
            moves[axis] = {ml - 1e-8, mh + 1e-8};
            auto contact = contacts.find({tick+1,axis});
            if (contact != contacts.end()) {
                double plane = number(contact->second["plane"]), direction = number(contact->second["direction"]);
                if ((direction > 0 && (p_lo[axis] > plane+1e-7 || p_hi[axis]+mh < plane-1e-7)) ||
                    (direction < 0 && (p_hi[axis] < plane-1e-7 || p_lo[axis]+ml > plane+1e-7)))
                    contradiction = py::dict("state"_a=tick+1,"axis"_a=axis,"reason"_a="contact_reach_contradiction");
                p_lo[axis] = p_hi[axis] = plane; v_lo[axis] = v_hi[axis] = 0.; continue;
            }
            double padding = 1e-12 * (1 + std::fabs(p_lo[axis]) + std::fabs(p_hi[axis]) + std::fabs(ml) + std::fabs(mh));
            p_lo[axis] += ml - padding; p_hi[axis] += mh + padding;
            v_lo[axis] = ml * friction[tick] - padding; v_hi[axis] = mh * friction[tick] + padding;
        }
        movements.push_back(moves);
    }
    int count = static_cast<int>(states.size());
    if (contradiction.is_none()) {
        for (int pass = 0; pass < 2; ++pass) {
            for (int t = count-2; t >= 0; --t) for (int a = 0; a < 2; ++a) {
                int axis = a*2; auto move = movements[t][a];
                states[t][axis][0] = high(states[t][axis][0], states[t+1][axis][0] - move[1]);
                states[t][axis][1] = low(states[t][axis][1], states[t+1][axis][1] - move[0]);
            }
            for (int t = 0; t < count-1; ++t) for (int a = 0; a < 2; ++a) {
                int axis = a*2; auto move = movements[t][a];
                states[t+1][axis][0] = high(states[t+1][axis][0], states[t][axis][0] + move[0]);
                states[t+1][axis][1] = low(states[t+1][axis][1], states[t][axis][1] + move[1]);
            }
            for (int t = 0; t < count; ++t) for (int a = 0; a < 2; ++a)
                if (states[t][a*2][0] > states[t][a*2][1] + 1e-7)
                    contradiction = py::dict("state"_a=t,"axis"_a=a,"reason"_a="interval_reach_contradiction");
        }
    }
    py::list state_objects;
    for (auto& state : states) state_objects.append(py::dict("X"_a=list2(state[0][0],state[0][1]),
        "Z"_a=list2(state[2][0],state[2][1]), "Y"_a=list2(state[1][0],state[1][1])));
    py::list phases;
    for (int t = 0; t < count-1; ++t) {
        auto a = py::reinterpret_borrow<py::dict>(state_objects[t]), b = py::reinterpret_borrow<py::dict>(state_objects[t+1]);
        auto sweep = [&](int axis) {return list2(low(states[t][axis][0],states[t+1][axis][0]),
                                                   high(states[t][axis][1],states[t+1][axis][1]));};
        phases.append(py::dict("Y"_a=py::dict("X"_a=a["X"],"Y"_a=sweep(1),"Z"_a=a["Z"]),
            "X"_a=py::dict("X"_a=sweep(0),"Y"_a=b["Y"],"Z"_a=a["Z"]),
            "Z"_a=py::dict("X"_a=b["X"],"Y"_a=b["Y"],"Z"_a=sweep(2))));
    }
    auto vertical = get(problem,"vertical",py::dict());
    for (py::handle contact : get(vertical,"contacts",py::list())) {
        int tick = integer(contact["state"])-1;
        if (!truth(get(contact,"case")) || tick < 0 || tick >= count-1) continue;
        auto p = py::reinterpret_borrow<py::dict>(phases[tick]);
        for (auto name : {"Y","X","Z"}) py::reinterpret_borrow<py::dict>(p[name])["Y"] = list2(-INF,INF);
        for (auto phase : {"X","Z"}) for (int a = 0; a < 2; ++a) {
            auto b = states[tick][a*2], realized = states[tick+1][a*2], move = movements[tick][a];
            py::reinterpret_borrow<py::dict>(p[phase])[a == 0 ? "X" : "Z"] =
                list2(low(low(b[0], b[0]+move[0]),realized[0]), high(high(b[1],b[1]+move[1]),realized[1]));
        }
    }
    auto initial_velocity = pair(problem["velocity"]);
    double speed = hypot2(initial_velocity[0],initial_velocity[1]);
    if (problem.contains("initialIntervals")) {
        auto initial = py::reinterpret_borrow<py::sequence>(problem["initialIntervals"]);
        auto a = pair(initial[2]), b = pair(initial[3]);
        speed = hypot2(high(std::fabs(a[0]),std::fabs(b[0])),high(std::fabs(a[1]),std::fabs(b[1])));
    }
    auto travel = radii(speed,magnitude,friction,1e-8);
    if (contradiction.is_none()) {
        for (int j = 1; j <= std::min(constrained,count-1); ++j) {
            for (int i = 0; i < j; ++i) {
                Pair gaps;
                for (int a = 0; a < 2; ++a) {
                    int axis = a*2;
                    gaps[a] = high(high(0.,states[j][axis][0]-states[i][axis][1]),states[i][axis][0]-states[j][axis][1]);
                }
                double gap = hypot2(gaps[0],gaps[1]), upper = travel[j]-travel[i];
                if (gap > upper+1e-7) {
                    contradiction = py::dict("state"_a=j,"previousState"_a=i,"reason"_a="norm_reach_contradiction",
                                             "gap"_a=gap,"travelUpperBound"_a=upper); break;
                }
            }
            if (!contradiction.is_none()) break;
        }
    }
    py::object origin = problem.contains("initialIntervals") ? py::object(py::none()) : py::object(py::list(problem["position"]));
    return py::dict("states"_a=state_objects,"phases"_a=phases,"velocities"_a=velocities,
                    "contradiction"_a=contradiction,"radii"_a=travel,"origin"_a=origin,"model"_a=problem);
}

static py::dict interaction(py::dict envelope, py::object request, bool refine, py::object objective) {
    if (py::isinstance<py::list>(request)) request = py::dict("rows"_a=request);
    py::dict selected = py::cast<py::dict>(request);
    auto contradiction = get(envelope,"contradiction");
    if (truth(contradiction)) {
        py::dict result("ruledOut"_a=true); result.attr("update")(contradiction); return result;
    }
    auto retained = []() {return py::dict("ruledOut"_a=false,"reason"_a="not_ruled_out");};
    if (selected.contains("alternatives")) {
        py::list results; bool all = true;
        for (py::handle item : selected["alternatives"]) {
            py::dict result = interaction(envelope,py::reinterpret_borrow<py::object>(item),refine,objective);
            all = all && truth(result["ruledOut"]); results.append(result);
        }
        if (py::len(results) && all) return py::dict("ruledOut"_a=true,"reason"_a="all_interaction_alternatives_excluded","alternatives"_a=results);
        return retained();
    }
    auto states = py::reinterpret_borrow<py::list>(envelope["states"]);
    std::map<std::pair<int,std::string>,Pair> tightened;
    for (py::handle row : get(selected,"rows",py::list())) {
        auto axis_object = get(row,"mode");
        if (!py::isinstance<py::str>(axis_object)) continue;
        auto axis = py::cast<std::string>(axis_object);
        auto t_object = get(row,"t1",get(row,"offset"));
        if (!horizontal(axis) || t_object.is_none()) continue;
        int tick = integer(t_object);
        if (tick < 0 || tick >= py::len(states)) continue;
        auto key = std::make_pair(tick,axis);
        auto existing = tightened.find(key);
        Pair bound = existing == tightened.end() ? pair(states[tick][py::str(axis)]) : existing->second;
        auto other = get(row,"t2");
        if (!other.is_none()) {
            int t2 = integer(other); if (t2 < 0 || t2 >= py::len(states)) continue;
            auto b = pair(states[t2][py::str(axis)]);
            if (py::cast<std::string>(get(row,"op",py::str(""))) == "MINUS")
                bound = {bound[0]-b[1],bound[1]-b[0]};
            else bound = {bound[0]+b[0],bound[1]+b[1]};
        }
        double rhs = number(row["rhs"]); auto cmp = py::cast<std::string>(row["cmp"]);
        if (((cmp == "GE" || cmp == "EQ") && bound[1] < rhs-1e-7) ||
            ((cmp == "LE" || cmp == "EQ") && bound[0] > rhs+1e-7))
            return py::dict("ruledOut"_a=true,"reason"_a="interaction_row_unreachable","tick"_a=tick,
                "phase"_a="state","axis"_a=axis,"reachable"_a=list2(bound[0],bound[1]),
                "required"_a=py::dict("cmp"_a=row["cmp"],"rhs"_a=row["rhs"]));
        if (other.is_none()) tightened[key] = {(cmp == "GE" || cmp == "EQ") ? high(bound[0],rhs-1e-7) : bound[0],
                                              (cmp == "LE" || cmp == "EQ") ? low(bound[1],rhs+1e-7) : bound[1]};
    }
    for (py::handle region : get(selected,"regions",py::list())) {
        int tick = integer(region["tick"]); auto phase = py::cast<std::string>(get(region,"phase",py::str("state")));
        auto domains = py::reinterpret_borrow<py::list>(envelope[phase == "state" ? "states" : "phases"]);
        if (tick < 0 || tick >= py::len(domains)) continue;
        py::object domain = phase == "state" ? py::reinterpret_borrow<py::object>(domains[tick]) : domains[tick][py::str(phase)];
        auto box = py::reinterpret_borrow<py::sequence>(region["bounds"]);
        auto origin = get(envelope,"origin");
        if (!origin.is_none() && envelope.contains("radii")) {
            double radius = number(envelope["radii"][py::int_((phase == "state" || phase == "Y") ? tick : tick+1)]);
            auto o = pair(origin); Pair gap;
            for (int a = 0; a < 2; ++a) gap[a] = high(high(number(box[a*2])-o[a],0.),o[a]-number(box[a*2+3]));
            double distance = hypot2(gap[0],gap[1]);
            if (distance > radius+1e-7) return py::dict("ruledOut"_a=true,"reason"_a="interaction_distance_unreachable",
                "tick"_a=tick,"phase"_a=phase,"gap"_a=distance,"travelUpperBound"_a=radius);
        }
        for (int a = 0; a < 3; ++a) {
            std::string axis(1,"XYZ"[a]); auto bound = pair(domain[py::str(axis)]);
            bool excluded = truth(get(region,"interior")) ? (bound[1] <= number(box[a]) || bound[0] >= number(box[a+3]))
                : (bound[1] < number(box[a])-1e-7 || bound[0] > number(box[a+3])+1e-7);
            if (excluded) {
                py::list required; required.append(box[a]); required.append(box[a+3]);
                return py::dict("ruledOut"_a=true,"reason"_a="interaction_region_unreachable","tick"_a=tick,
                    "phase"_a=phase,"axis"_a=axis,"reachable"_a=list2(bound[0],bound[1]),"required"_a=required);
            }
        }
    }
    if (refine && !get(envelope,"model").is_none()) {
        auto checked = py::module_::import("parkour_solver.numerics.disk").attr("screen")(
            envelope["model"],get(selected,"rows",py::list()),objective);
        if (!checked.is_none()) {
            auto outcome = py::cast<std::string>(checked["outcome"]);
            if (outcome == "infeasible") {
                py::dict result("ruledOut"_a=true); result.attr("update")(checked["proof"]); return result;
            }
            if (outcome == "undecided") return py::dict("ruledOut"_a=false,"reason"_a="relaxation_undecided",
                "status"_a=checked["status"],"message"_a=checked["message"]);
        }
    }
    return retained();
}

PYBIND11_MODULE(_pruning_native, module) {
    module.def("reachable_envelopes", &envelopes, "problem"_a, "y"_a=py::none());
    module.def("interaction_reachability", &interaction, "envelopes"_a,"interaction"_a,
               py::kw_only(),"refine"_a=false,"objective"_a=py::none());
    module.def("hypot2", &hypot2);
}
