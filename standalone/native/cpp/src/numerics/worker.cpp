// Port of numerics/worker.py: the IPOPT worker process, speaking the same
// JSON-lines protocol on stdin/stdout. The casadi graph is built with the
// Python's operations in the Python's operand order (`float + SX` is
// plus(SX(float), SX) in both), so IPOPT sees the same functions and
// derivatives; the Python's DM constants are SX constants here, which casadi
// folds to the same values.
#include <casadi/casadi.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/numerics/constraints.hpp"
#include "pkc/numerics/continuation.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

using casadi::DM;
using casadi::SX;
using Rows = std::vector<std::array<std::int64_t, 2>>;
const double kInf = std::numeric_limits<double>::infinity();

double monotonic() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void send(const Value& message) {
    std::string line = dumps(message, false, true) + "\n";
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

std::vector<double> doubles(const Value& values) {
    std::vector<double> out;
    for (const auto& v : items(values)) out.push_back(num(v).value);
    return out;
}

Rows rows_of(const Value& pattern) {
    Rows out;
    for (const auto& row : items(pattern)) out.push_back({as_int(items(row)[0]), as_int(items(row)[1])});
    return out;
}

Value value_of(const Rows& rows) {
    Array out;
    for (const auto& row : rows) out.push_back(array({integer_value(row[0]), integer_value(row[1])}));
    return array(std::move(out));
}

Value reals(const std::vector<double>& values) {
    Array out;
    for (double v : values) out.push_back(real(v));
    return array(std::move(out));
}

struct Contact {
    double plane;  // world coordinate
    Num direction;
};

// The prepared request's fields the worker reads.
struct Problem {
    explicit Problem(const Value& value) : value(value) {
        const Object& p = fields(value);
        n = as_int(at(p, "ticks"));
        position = doubles(at(p, "position"));
        velocity = doubles(at(p, "velocity"));
        magnitude = doubles(at(p, "magnitude"));
        friction = doubles(at(p, "friction"));
        base = doubles(at(p, "base"));
        threshold = num(at(p, "threshold")).value;
        for (const auto& c : items(at(p, "contacts"))) {
            const Object& contact = fields(c);
            contacts[{as_int(at(contact, "state")), as_int(at(contact, "axis"))}] =
                Contact{num(at(contact, "plane")).value, num(at(contact, "direction"))};
        }
        if (const Value* flags = get(p, "ladderTicks")) {
            for (const auto& f : items(*flags)) ladder.push_back(std::get<bool>(f.data));
        } else {
            ladder.assign(n + 1, false);
        }
        for (bool active : ladder) has_ladders |= active;
        max_iterations = get(p, "maxIterations") ? as_int(at(p, "maxIterations")) : 10000;
        objective_mode = get(p, "objectiveMode") ? as_text(at(p, "objectiveMode")) : "configured";
    }
    const Contact* contact(std::int64_t state, std::int64_t axis) const {
        auto found = contacts.find({state, axis});
        return found == contacts.end() ? nullptr : &found->second;
    }

    Value value;
    std::int64_t n;
    std::vector<double> position, velocity, magnitude, friction, base;
    double threshold;
    std::map<std::pair<std::int64_t, std::int64_t>, Contact> contacts;
    std::vector<bool> ladder;
    bool has_ladders = false;
    std::int64_t max_iterations;
    std::string objective_mode;
};

struct Replay {
    std::vector<std::array<double, 2>> positions, velocities, raw;
    Rows chosen, caps;
};

// forward: the numpy replay, positions relative to the request's start.
Replay forward(const Problem& problem, const std::vector<double>& yaw, double threshold, const Rows* pattern,
               const Rows* cap_pattern) {
    const std::int64_t n = problem.n;
    Replay r;
    r.positions.assign(n + 1, {0., 0.});
    r.velocities.assign(n + 1, {0., 0.});
    r.velocities[0] = {problem.velocity[0], problem.velocity[1]};
    r.chosen.assign(n, {2, 2});
    r.caps.assign(n, {0, 0});
    r.raw.assign(n, {0., 0.});
    for (std::int64_t t = 0; t < n; ++t) {
        const double angle = problem.base[t] + radians(yaw[t]);
        const double controls[2] = {problem.magnitude[t] * std::cos(angle), problem.magnitude[t] * std::sin(angle)};
        std::array<double, 2> move;
        for (int a = 0; a < 2; ++a) {
            const double v = r.velocities[t][a];
            r.chosen[t][a] = std::fabs(v) < threshold ? 0 : v >= 0 ? 1 : -1;
            const std::int64_t branch = (pattern ? *pattern : r.chosen)[t][a];
            move[a] = (branch == 0 ? 0. : v) + controls[a];
        }
        r.raw[t] = move;
        if (problem.ladder[t]) {
            for (int a = 0; a < 2; ++a) {
                r.caps[t][a] = move[a] < -kCap ? -1 : move[a] > kCap ? 1 : 0;
                if (!cap_pattern) {
                    move[a] = std::fmin(std::fmax(move[a], -kCap), kCap);  // np.clip
                } else if ((*cap_pattern)[t][a] != 0) {
                    move[a] = static_cast<double>((*cap_pattern)[t][a]) * kCap;
                }
            }
        }
        for (int a = 0; a < 2; ++a) {
            r.positions[t + 1][a] = r.positions[t][a] + move[a];
            r.velocities[t + 1][a] = problem.friction[t] * move[a];
            if (const Contact* contact = problem.contact(t + 1, a)) {
                r.positions[t + 1][a] = contact->plane - problem.position[a];
                r.velocities[t + 1][a] = 0;
            }
        }
    }
    return r;
}

std::vector<double> dense(const DM& value) { return casadi::DM::densify(value).nonzeros(); }

class Monitor : public casadi::Callback {
public:
    Monitor(casadi_int nx, casadi_int ng, casadi::Function decode, casadi::Function constraints,
            std::vector<double> lower, std::vector<double> upper, bool stop,
            const std::function<bool(const Value&)>& on_candidate)
        : nx_(nx), ng_(ng), decode_(std::move(decode)), constraints_(std::move(constraints)), lower_(std::move(lower)),
          upper_(std::move(upper)), stop_(stop), on_candidate_(on_candidate) {
        construct("monitor", casadi::Dict());
    }

    casadi_int get_n_in() override { return casadi::nlpsol_n_out(); }
    casadi_int get_n_out() override { return 1; }
    std::string get_name_in(casadi_int i) override { return casadi::nlpsol_out(i); }
    std::string get_name_out(casadi_int) override { return "stop"; }
    casadi::Sparsity get_sparsity_in(casadi_int i) override {
        const std::string name = get_name_in(i);
        if (name == "x" || name == "lam_x") return casadi::Sparsity::dense(nx_, 1);
        if (name == "g" || name == "lam_g") return casadi::Sparsity::dense(ng_, 1);
        return name == "f" ? casadi::Sparsity::dense(1, 1) : casadi::Sparsity(0, 1);
    }

    // With bound extraction, returned/callback g need not carry the original
    // expressions. Check the unextracted NLP at x on callbacks AND final
    // output. The parent still replays every original requested wall.
    bool record(const DM& x, double f) const {
        const std::vector<double> values = dense(constraints_(std::vector<DM>{x}).at(0));
        for (double v : dense(x))
            if (!std::isfinite(v)) return false;
        for (double v : values)
            if (!std::isfinite(v)) return false;
        if (!std::isfinite(f)) return false;
        double below = 0, above = 0;  // np.max(..., initial=0)
        for (std::size_t i = 0; i < values.size(); ++i) {
            below = std::fmax(below, lower_[i] - values[i]);
            above = std::fmax(above, values[i] - upper_[i]);
        }
        if (std::fmax(below, above) <= 1e-8) {
            Value yaw = reals(dense(decode_(std::vector<DM>{x}).at(0)));
            const bool accepted = on_candidate_(yaw);
            if (f < best || (accepted && stop_)) {
                best = f;
                best_yaw = yaw;
            }
            return accepted;
        }
        return false;
    }

    std::vector<DM> eval(const std::vector<DM>& arguments) const override {
        const auto& names = casadi::nlpsol_out();
        auto index = [&](const char* name) { return std::find(names.begin(), names.end(), name) - names.begin(); };
        const bool accepted = record(arguments.at(index("x")), static_cast<double>(arguments.at(index("f"))));
        return {DM(accepted && stop_ ? 1 : 0)};
    }

    mutable double best = kInf;
    mutable std::optional<Value> best_yaw;

private:
    casadi_int nx_, ng_;
    casadi::Function decode_, constraints_;
    std::vector<double> lower_, upper_;
    bool stop_;
    const std::function<bool(const Value&)>& on_candidate_;
};

struct PhaseOutcome {
    Value yaw;
    std::string status;
    double cost;
};

PhaseOutcome fixed_phase(const Problem& problem, const Value& yaw_value, const Rows& pattern, double seconds, bool stop,
                         const std::function<bool(const Value&)>& on_candidate, const std::string& kind,
                         const std::string& seed_name, const Rows& caps, std::int64_t attempt) {
    const double started = monotonic();
    const std::int64_t n = problem.n;
    const std::vector<double> yaw = doubles(yaw_value);
    const Replay seeds = forward(problem, yaw, problem.threshold, &pattern, &caps);
    std::vector<SX> variables, rows;
    std::vector<double> initial, lower, upper;
    auto variable = [&](const std::string& name, const std::vector<double>& seed) {
        SX value = SX::sym(name, static_cast<casadi_int>(seed.size()));
        variables.push_back(value);
        initial.insert(initial.end(), seed.begin(), seed.end());
        return value;
    };
    auto row = [&](const SX& value, double lo, double hi) {
        rows.push_back(value);
        lower.insert(lower.end(), value.numel(), lo);
        upper.insert(upper.end(), value.numel(), hi);
    };

    std::vector<SX> p{SX(DM::zeros(2))}, v{SX(DM(problem.velocity))}, facings;
    const double threshold = problem.threshold;
    for (std::int64_t t = 0; t < n; ++t) {
        for (int a = 0; a < 2; ++a) {
            const std::int64_t branch = pattern[t][a];
            if (branch == 0) {
                row(v[t](a), -threshold + 1e-8, threshold - 1e-8);
            } else if (branch == -1 || branch == 1) {
                row(SX(static_cast<double>(branch)) * v[t](a), threshold + 1e-8, kInf);
            }
        }
        SX theta = variable("theta" + std::to_string(t), {radians(yaw[t])});
        facings.push_back(theta * (180 / M_PI));
        SX angle = SX(problem.base[t]) + theta;
        SX control = SX(problem.magnitude[t]) * SX::vertcat({cos(angle), sin(angle)});
        SX carry = SX::vertcat({pattern[t][0] == 0 ? SX(0.) : v[t](0), pattern[t][1] == 0 ? SX(0.) : v[t](1)});
        SX pn = variable("p" + std::to_string(t), {seeds.positions[t + 1][0], seeds.positions[t + 1][1]});
        SX vn = variable("v" + std::to_string(t), {seeds.velocities[t + 1][0], seeds.velocities[t + 1][1]});
        SX raw = carry + control;
        std::vector<SX> movement;
        for (int a = 0; a < 2; ++a) {
            const std::int64_t b = problem.ladder[t] ? caps[t][a] : 0;
            if (problem.ladder[t]) {
                if (b) {
                    row(SX(static_cast<double>(b)) * raw(a), kCap, kInf);
                } else {
                    row(raw(a), -kCap, kCap);
                }
            }
            movement.push_back(b ? SX(static_cast<double>(b) * kCap) : raw(a));
        }
        SX displacement = SX::vertcat(movement);
        SX proposed = p[t] + displacement;
        for (int a = 0; a < 2; ++a) {
            if (const Contact* contact = problem.contact(t + 1, a)) {
                const double plane = contact->plane - problem.position[a];
                const SX direction(contact->direction.value);
                row(pn(a) - plane, 0, 0);
                row(vn(a), 0, 0);
                row(direction * (proposed(a) - plane), kSolverMargin, kInf);
                row(direction * (p[t](a) - plane), -kInf, 0);
            } else {
                row(pn(a) - proposed(a), 0, 0);
                row(vn(a) - SX(problem.friction[t]) * displacement(a), 0, 0);
            }
        }
        p.push_back(pn);
        v.push_back(vn);
    }

    const Value numerical = reduce_walls(at(fields(problem.value), "walls"));
    for (const auto& wall_value : items(at(fields(numerical), "walls"))) {
        const Object& wall = fields(wall_value);
        const std::string& mode = as_text(at(wall, "mode"));
        const std::int64_t t1 = as_int(at(wall, "t1"));
        const Value& t2_value = get_or_null(wall, "t2");
        const bool relative = !is_null(t2_value);
        const std::int64_t t2 = relative ? as_int(t2_value) : 0;
        const double sign = relative && as_text(at(wall, "op")) == "PLUS" ? 1 : -1;
        const double rhs = num(at(wall, "rhs")).value;
        SX value;
        if (mode == "DXZ" || mode == "DZX") {
            SX dx = fabs(p[t1](0) - p[t2](0)), dz = fabs(p[t1](1) - p[t2](1));
            value = mode == "DXZ" ? dx - dz : dz - dx;
        } else if (mode == "F") {
            value = facings[t1];
            if (relative) value = value + SX(sign) * facings[t2];
        } else if (mode == "X" || mode == "Z") {
            const int a = mode == "X" ? 0 : 1;
            value = p[t1](a) + problem.position[a];
            if (relative) value = value + SX(sign) * (p[t2](a) + problem.position[a]);
        } else {
            fail("Unsupported constraint mode: " + mode);
        }
        SX residual = value - rhs;
        const std::string& cmp = as_text(at(wall, "cmp"));
        if (mode == "F") {
            double seed = yaw[t1] - rhs;
            if (relative) seed += sign * yaw[t2];
            residual = residual - SX(360 * std::floor((seed + 180) / 360));
            row(residual, cmp == "GE" || cmp == "EQ" ? 0 : -180, cmp == "LE" || cmp == "EQ" ? 0 : 180);
        } else {
            row(residual, cmp == "GE" || cmp == "EQ" ? 0 : -kInf, cmp == "LE" || cmp == "EQ" ? 0 : kInf);
        }
    }

    const Object& objective = fields(at(fields(problem.value), "objective"));
    const std::int64_t tick = as_int(at(objective, "tick"));
    SX point = p[tick] + SX(DM(problem.position));
    SX cost;
    const Value& custom = get_or_null(objective, "customYaw");
    if (!is_null(custom)) {
        const double angle = radians(num(custom).value);
        SX direction = SX(DM(std::vector<double>{-std::sin(angle), std::cos(angle)}));
        if (as_text(at(objective, "type")) == "MOTION" && tick > 0) point = p[tick] - p[tick - 1];
        cost = dot(direction, point);
    } else {
        cost = point(as_text(at(objective, "axis")) == "X" ? 0 : 1);
    }
    cost = cost * SX(as_text(at(objective, "sense")) == "MAX" ? -1. : 1.);
    if (problem.objective_mode == "constant_zero") cost = SX(0.);
    SX x = SX::vertcat(variables), g = SX::vertcat(rows);
    casadi::Function decode("decode", {x}, {SX::vertcat(facings)});
    casadi::Function constraints("original_constraints", {x}, {g});
    Monitor monitor(x.numel(), g.numel(), decode, constraints, lower, upper, stop, on_candidate);
    casadi::Dict options{{"print_time", false}, {"error_on_fail", false}, {"iteration_callback", monitor},
                         {"detect_simple_bounds", true}, {"ipopt.print_level", 0}, {"ipopt.sb", "yes"},
                         {"ipopt.max_iter", problem.max_iterations},
                         {"ipopt.tol", 1e-9}, {"ipopt.constr_viol_tol", 1e-9}, {"ipopt.acceptable_tol", 1e-8},
                         {"ipopt.acceptable_constr_viol_tol", 1e-9}, {"ipopt.bound_relax_factor", 0.0}};
    // A request without a wall limit stops on max_iter only.
    if (std::isfinite(seconds)) options["ipopt.max_wall_time"] = std::fmax(0.01, seconds);
    casadi::Function solver =
        casadi::nlpsol("local_ipopt", "ipopt", casadi::SXDict{{"x", x}, {"g", g}, {"f", cost}}, options);
    casadi::DMDict solution = solver(casadi::DMDict{{"x0", DM(initial)}, {"lbg", DM(lower)}, {"ubg", DM(upper)}});
    monitor.record(solution.at("x"), static_cast<double>(solution.at("f")));
    const casadi::Dict stats = solver.stats();
    const std::string status = stats.at("return_status").to_string();
    std::int64_t zeros = 0, survivors = 0, saturated = 0;
    for (const auto& r : pattern)
        for (auto b : r) {
            zeros += b == 0;
            survivors += b == 1 || b == -1;
        }
    for (const auto& r : caps)
        for (auto b : r) saturated += b != 0;
    send(object({{"type", text("phase")},
                 {"status", text(status)},
                 {"iterations", integer_value(stats.at("iter_count").to_int())},
                 {"seconds", real(monotonic() - started)},
                 {"zeros", integer_value(zeros)},
                 {"survivors", integer_value(survivors)},
                 {"kind", text(kind)},
                 {"seed", text(seed_name)},
                 {"ladderSaturated", integer_value(saturated)},
                 {"capAttempt", integer_value(attempt)},
                 {"simpleBoundExtraction", boolean(true)},
                 {"constraintRowsBeforeExtraction", integer_value(g.numel())},
                 {"wallReduction", at(fields(numerical), "audit")},
                 {"objectiveMode", text(problem.objective_mode)},
                 {"stopOnFeasible", boolean(stop)}}));
    return {monitor.best_yaw ? *monitor.best_yaw : yaw_value, status, monitor.best};
}

std::pair<Value, std::string> phase(const Problem& problem, const Value& yaw, const Value& pattern_value,
                                    double seconds, bool stop, const std::function<bool(const Value&)>& on_candidate,
                                    const std::string& kind, const std::string& seed_name) {
    const double end = monotonic() + seconds;
    const Rows pattern = rows_of(pattern_value);
    const Value* explicit_caps = get(fields(problem.value), "ladderPattern");
    const bool searching = problem.has_ladders && !explicit_caps;
    const Rows caps = explicit_caps ? rows_of(*explicit_caps)
                                    : forward(problem, doubles(yaw), problem.threshold, &pattern, nullptr).caps;
    std::deque<std::pair<Value, Rows>> queue{{yaw, caps}};
    std::set<Rows> seen;
    Value best = yaw;
    double best_cost = kInf;
    std::string best_status = "Not_Started";
    std::int64_t attempt = 0;
    const std::int64_t limit = searching ? 4 : 1;
    while (!queue.empty() && attempt < limit) {
        if (end - monotonic() < 0.05) break;
        auto [seed_yaw, chosen_caps] = queue.front();
        queue.pop_front();
        if (!seen.insert(chosen_caps).second) continue;
        const double remaining = end - monotonic();
        const double budget = searching && attempt < 3 ? remaining * 0.65 : remaining;
        PhaseOutcome result = fixed_phase(problem, seed_yaw, pattern, budget, stop, on_candidate, kind, seed_name,
                                          chosen_caps, attempt);
        attempt += 1;
        if (result.cost < best_cost) {
            best = result.yaw;
            best_cost = result.cost;
            best_status = result.status;
        } else if (best_status == "Not_Started") {
            best_status = result.status;
        }
        if (explicit_caps || !problem.has_ladders || result.status == "User_Requested_Stop") break;
        const Replay replay = forward(problem, doubles(result.yaw), problem.threshold, &pattern, &chosen_caps);
        if (replay.caps != chosen_caps) queue.emplace_back(result.yaw, replay.caps);
        Rows adjacent = chosen_caps;
        bool changed = false;
        for (std::int64_t t = 0; t < problem.n; ++t) {
            if (!problem.ladder[t]) continue;
            for (int a = 0; a < 2; ++a) {
                const double raw = replay.raw[t][a];
                if (std::fabs(std::fabs(raw) - kCap) <= 2e-6) {
                    adjacent[t][a] = chosen_caps[t][a] ? 0 : raw >= 0 ? 1 : -1;
                    changed = true;
                }
            }
        }
        if (changed) queue.emplace_back(result.yaw, adjacent);
        if (!std::isfinite(result.cost)) queue.emplace_back(seed_yaw, Rows(problem.n, {0, 0}));
    }
    return {best, best_status};
}

int main_worker() {
    std::string line;
    std::getline(std::cin, line);
    const Value request = loads(line);
    if (!py_equal(at(fields(request), "version"), integer_value(1))) fail("Unsupported protocol version");
    {
        // casadi writes to std::cout, which carries the protocol.
        std::streambuf* protocol = std::cout.rdbuf(std::cerr.rdbuf());
        casadi::nlpsol("check_ipopt", "ipopt", casadi::SXDict{{"x", SX::sym("x")}, {"f", SX(0.)}},
                       casadi::Dict{{"print_time", false}, {"ipopt.print_level", 0}, {"ipopt.sb", "yes"}});
        std::cout.rdbuf(protocol);
    }
    const Problem problem(request);
    auto run_phase = [&](const Value&, const Value& yaw, const Value& pattern, double seconds, bool stop,
                         const std::function<bool(const Value&)>& on_candidate, const std::string& kind,
                         const std::string& seed) {
        return phase(problem, yaw, pattern, seconds, stop, on_candidate, kind, seed);
    };
    auto infer = [&](const Value& yaw) {
        return value_of(forward(problem, doubles(yaw), problem.threshold, nullptr, nullptr).chosen);
    };
    const std::string status = run(request, run_phase, infer, send, monotonic);
    send(object({{"type", text("done")}, {"status", text(status)}}));
    return 0;
}

}  // namespace
}  // namespace pkc

int main() {
    try {
        return pkc::main_worker();
    } catch (const std::exception& error) {
        pkc::send(pkc::object({{"type", pkc::text("error")}, {"message", pkc::text(error.what())}}));
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
