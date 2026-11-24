// Port of numerics/disk.py's spec, _solve/_cost, deficits, ipopt_seed,
// alarm_ticks and RelaxedDispatch. The constraint matrix is built in scipy's coo -> csc layout
// (column-major, rows ascending, explicit zeros kept) so Clarabel factors the
// same pattern.
#include "pkc/numerics/disk.hpp"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

extern "C" {
#include "clarabel.h"
}

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/numerics/post_alarm.hpp"
#include "pkc/validation/numerical.hpp"
#include "pkc/work.hpp"

namespace pkc {
namespace {

const int kModelVersion = 1;
const double kTol = 1e-9;
const double kInfeasibilityTol = 1e-8;
const double kSeedTilt = 1e-4;

bool is_int(const Value& value) { return std::holds_alternative<std::int64_t>(value.data); }
bool is_one(const Value& value, std::string_view expected) {
    auto* held = std::get_if<std::string>(&value.data);
    return held && *held == expected;
}

// str() of a row tuple, the Python's sort key for the deduplicated rows.
std::string py_repr(const Value& value) {
    if (auto* s = std::get_if<std::string>(&value.data)) return "'" + *s + "'";
    if (is_null(value)) return "None";
    if (auto* b = std::get_if<bool>(&value.data)) return *b ? "True" : "False";
    if (auto* i = std::get_if<std::int64_t>(&value.data)) return std::to_string(*i);
    return float_repr(std::get<double>(value.data));
}

std::string row_key(const Array& row) {
    std::string out = "(";
    for (std::size_t i = 0; i < row.size(); ++i) out += (i ? ", " : "") + py_repr(row[i]);
    return out + ")";
}

// -x with Python's kinds: -0 of an int is 0, of a float -0.0.
double negated(const Num& x) { return x.integral ? static_cast<double>(-static_cast<std::int64_t>(x.value)) : -x.value; }

struct Block {  // rows, columns, values, rhs
    std::vector<std::size_t> rows, columns;
    std::vector<double> values, rhs;

    // `entries` in the Python dict's insertion order; the csc build sorts them anyway.
    void row(const std::vector<std::pair<std::size_t, double>>& entries, double value) {
        const std::size_t index = rhs.size();
        for (const auto& [column, coefficient] : entries) {
            rows.push_back(index);
            columns.push_back(column);
            values.push_back(coefficient);
        }
        rhs.push_back(value);
    }
};

std::string status_name(ClarabelSolverStatus status) {
    static const char* names[] = {"Unsolved",     "Solved",           "PrimalInfeasible",
                                  "DualInfeasible", "AlmostSolved",   "AlmostPrimalInfeasible",
                                  "AlmostDualInfeasible", "MaxIterations", "MaxTime",
                                  "NumericalError", "InsufficientProgress", "CallbackTerminated"};
    return names[status];
}

}  // namespace

double seconds_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct DiskProblem::Solver {
    ClarabelDefaultSolver_f64* solver = nullptr;
    ~Solver() {
        if (solver) clarabel_DefaultSolver_f64_free(solver);
    }
};

namespace {

// _cost: the minimized cost vector, the world offset and the sign recovering the score (cost@x is sign*(score-offset)).
struct Cost {
    std::vector<double> cost;
    double offset = 0., sign = 0.;
};

Cost cost_of(const Value* objective_value, std::int64_t n, std::size_t columns,
             const std::function<std::size_t(int, std::int64_t)>& pos, const std::vector<Num>& position) {
    Cost out{std::vector<double>(columns, 0.)};
    if (!objective_value || is_null(*objective_value)) return out;
    const Object& objective = fields(*objective_value);
    const std::int64_t tick = as_int(at(objective, "tick"));
    if (tick < 0 || tick > n) return out;
    out.sign = as_text(at(objective, "sense")) == "MAX" ? -1. : 1.;
    const Value& custom = get_or_null(objective, "customYaw");
    if (!is_null(custom)) {
        const double angle = radians(num(custom).value);
        const double weights[2] = {-std::sin(angle), std::cos(angle)};
        const bool motion = is_one(get_or_null(objective, "type"), "MOTION") && tick > 0;
        out.offset = motion ? 0. : weights[0] * position[0].value + weights[1] * position[1].value;
        for (int axis = 0; axis < 2; ++axis) {
            out.cost[pos(axis, tick)] += out.sign * weights[axis];
            if (motion) out.cost[pos(axis, tick - 1)] -= out.sign * weights[axis];
        }
    } else {
        const int axis = as_text(at(objective, "axis")) == "X" ? 0 : 1;
        out.cost[pos(axis, tick)] = out.sign;
        out.offset = position[axis].value;
    }
    return out;
}

}  // namespace

DiskProblem::DiskProblem(const Value& content_value, double time_limit, unsigned max_iter,
                         const std::vector<DiskCut>& cuts, const std::optional<DiskLevel>& level)
    : solver_(std::make_unique<Solver>()) {
    const double started = seconds_now();
    const Object& content = fields(content_value);
    const std::int64_t n = as_int(at(content, "ticks"));
    const std::size_t width = 3 * n + 2;
    std::function<std::size_t(int, std::int64_t)> pos = [&](int axis, std::int64_t t) { return axis * width + t; };
    auto vel = [&](int axis, std::int64_t t) { return axis * width + n + 1 + t; };
    auto control = [&](int axis, std::int64_t t) { return axis * width + 2 * (n + 1) + t; };
    Block equalities, inequalities;
    const std::vector<Num> position = [&] {
        std::vector<Num> out;
        for (const auto& v : items(at(content, "position"))) out.push_back(num(v));
        return out;
    }();
    std::map<std::pair<std::int64_t, std::int64_t>, const Object*> contacts;
    for (const auto& c : items(at(content, "contacts")))
        contacts[{as_int(at(fields(c), "state")), as_int(at(fields(c), "axis"))}] = &fields(c);
    const Value& initial = at(content, "initialIntervals");
    const Array& velocity = items(at(content, "velocity"));
    const Array& friction = items(at(content, "friction"));
    for (int axis = 0; axis < 2; ++axis) {
        if (is_null(initial)) {
            equalities.row({{pos(axis, 0), 1.}}, 0.);
            equalities.row({{vel(axis, 0), 1.}}, num(velocity[axis]).value);
        } else {
            // Interval starts are absolute; positions are offsets from `position`.
            const Array& bounds = items(initial);
            auto bound = [&](int which) { return num(items(bounds[which])[axis]); };
            const Num origin = position[axis];
            inequalities.row({{pos(axis, 0), -1.}}, sub(origin, bound(0)).value);
            inequalities.row({{pos(axis, 0), 1.}}, sub(bound(1), origin).value);
            inequalities.row({{vel(axis, 0), -1.}}, negated(bound(2)));
            inequalities.row({{vel(axis, 0), 1.}}, bound(3).value);
        }
        for (std::int64_t t = 0; t < n; ++t) {
            auto found = contacts.find({t + 1, axis});
            if (found == contacts.end()) {
                equalities.row({{pos(axis, t + 1), 1.}, {pos(axis, t), -1.}, {vel(axis, t), -1.}, {control(axis, t), -1.}},
                               0.);
                const Num f = num(friction[t]);
                equalities.row({{vel(axis, t + 1), 1.}, {vel(axis, t), negated(f)}, {control(axis, t), negated(f)}}, 0.);
            } else {
                const Object& contact = *found->second;
                const double plane = sub(num(at(contact, "plane")), position[axis]).value;
                const double d = num(at(contact, "direction")).value;
                equalities.row({{pos(axis, t + 1), 1.}}, plane);
                equalities.row({{vel(axis, t + 1), 1.}}, 0.);
                inequalities.row({{pos(axis, t), d}}, d * plane);
                inequalities.row({{pos(axis, t), -d}, {vel(axis, t), -d}, {control(axis, t), -d}}, -d * plane - kSolverMargin);
            }
        }
    }
    for (const auto& row_value : items(at(content, "rows"))) {
        const Array& row = items(row_value);
        const int axis = as_text(row[0]) == "X" ? 0 : 1;
        const std::int64_t t = as_int(row[1]);
        std::vector<std::pair<std::size_t, double>> entries{{pos(axis, t), 1.}};
        Num offset = position[axis];
        if (!is_null(row[2])) {
            const double sign = as_text(row[3]) == "MINUS" ? -1. : 1.;
            const std::size_t column = pos(axis, as_int(row[2]));
            auto same = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.first == column; });
            if (same != entries.end()) {
                same->second = same->second + sign;
            } else {
                entries.emplace_back(column, 0. + sign);
            }
            offset = add(offset, flt(sign * position[axis].value));
        }
        const std::string& cmp = as_text(row[4]);
        const Num rhs = num(row[5]);
        if (cmp == "EQ") {
            equalities.row(entries, sub(rhs, offset).value);
        } else if (cmp == "LE") {
            inequalities.row(entries, sub(rhs, offset).value);
        } else {
            for (auto& entry : entries) entry.second = -entry.second;
            inequalities.row(entries, sub(offset, rhs).value);
        }
    }
    for (const DiskCut& cut : cuts)
        inequalities.row({{control(0, cut.tick), -cut.nx}, {control(1, cut.tick), -cut.nz}}, -cut.rhs);
    const std::size_t columns = 2 * width;
    if (level) {
        const Cost weights = cost_of(&level->objective, n, columns, pos, position);
        const bool maximized = as_text(at(fields(level->objective), "sense")) == "MAX";
        std::vector<std::pair<std::size_t, double>> entries;
        for (std::size_t i = 0; i < columns; ++i)
            if (weights.cost[i] != 0.) entries.emplace_back(i, maximized ? -weights.sign * weights.cost[i]
                                                                          : weights.sign * weights.cost[i]);
        inequalities.row(entries, maximized ? weights.offset - level->value : level->value - weights.offset);
    }
    Block disks;
    const Array& magnitudes = items(at(content, "magnitude"));
    for (std::int64_t t = 0; t < n; ++t) {
        disks.rhs.insert(disks.rhs.end(), {num(magnitudes[t]).value + kDiskAllowance, 0., 0.});
        for (int axis = 0; axis < 2; ++axis) {
            disks.rows.push_back(3 * t + 1 + axis);
            disks.columns.push_back(control(axis, t));
            disks.values.push_back(-1.);
        }
    }

    // coo -> csc: column-major, rows ascending within a column (the blocks stack in row order).
    std::vector<std::size_t> colptr(columns + 1, 0), rowval;
    std::vector<double> nzval, rhs;
    std::vector<std::vector<std::pair<std::size_t, double>>> by_column(columns);
    std::size_t offset_rows = 0;
    for (const Block* block : {&equalities, &inequalities, &disks}) {
        for (std::size_t k = 0; k < block->values.size(); ++k)
            by_column[block->columns[k]].emplace_back(block->rows[k] + offset_rows, block->values[k]);
        rhs.insert(rhs.end(), block->rhs.begin(), block->rhs.end());
        offset_rows += block->rhs.size();
    }
    for (std::size_t c = 0; c < columns; ++c) {
        std::stable_sort(by_column[c].begin(), by_column[c].end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [r, v] : by_column[c]) {
            rowval.push_back(r);
            nzval.push_back(v);
        }
        colptr[c + 1] = rowval.size();
    }
    Cost objective = cost_of(get(content, "objective"), n, columns, pos, position);
    cost_ = std::move(objective.cost);
    offset_ = objective.offset;
    sense_ = objective.sign;
    for (std::int64_t t = 0; t < n; ++t) columns_.push_back({control(0, t), control(1, t)});

    std::vector<ClarabelSupportedConeT_f64> cones;
    auto cone = [&](ClarabelSupportedConeT_Tag tag, std::size_t dimension) {
        ClarabelSupportedConeT_f64 c{};
        c.tag = tag;
        c.zero_cone_t = dimension;  // the union's dimension field for zero/nonnegative/second-order cones
        cones.push_back(c);
    };
    if (!equalities.rhs.empty()) cone(ClarabelZeroConeT_Tag, equalities.rhs.size());
    if (!inequalities.rhs.empty()) cone(ClarabelNonnegativeConeT_Tag, inequalities.rhs.size());
    for (std::int64_t t = 0; t < n; ++t) cone(ClarabelSecondOrderConeT_Tag, 3);
    ClarabelDefaultSettings_f64 settings = clarabel_DefaultSettings_f64_default();
    settings.verbose = false;
    settings.max_threads = 1;
    settings.direct_solve_method = QDLDL;
    settings.time_limit = work_only() ? INFINITY : time_limit;  // a work-only run stops on max_iter only
    settings.max_iter = max_iter;
    settings.tol_feas = settings.tol_gap_abs = settings.tol_gap_rel = kTol;
    settings.tol_infeas_abs = settings.tol_infeas_rel = kInfeasibilityTol;
    // Same verdicts and search decisions, faster solves (research/clarabel-refinement/README.md).
    settings.iterative_refinement_enable = false;
    const double assembled = seconds_now();
    std::vector<std::uintptr_t> p_colptr(columns + 1, 0);
    ClarabelCscMatrix_f64 P, A;
    clarabel_CscMatrix_f64_init(&P, columns, columns, p_colptr.data(), nullptr, nullptr);
    std::vector<std::uintptr_t> a_colptr(colptr.begin(), colptr.end()), a_rowval(rowval.begin(), rowval.end());
    clarabel_CscMatrix_f64_init(&A, rhs.size(), columns, a_colptr.data(), a_rowval.data(), nzval.data());
    solver_->solver = clarabel_DefaultSolver_f64_new(&P, cost_.data(), &A, rhs.data(), cones.size(), cones.data(),
                                                     &settings);
    assembly_seconds = assembled - started;
    setup_seconds = seconds_now() - assembled;
}

DiskProblem::~DiskProblem() = default;

void DiskProblem::update_q(const std::vector<double>& q) {
    clarabel_DefaultSolver_f64_update_q(solver_->solver, q.data(), q.size());
}

std::optional<Controls> DiskProblem::solve_controls() {
    clarabel_DefaultSolver_f64_solve(solver_->solver);
    const ClarabelDefaultSolution_f64 solution = clarabel_DefaultSolver_f64_solution(solver_->solver);
    Controls controls;
    for (const auto& tick : columns_) {
        const double a[2] = {solution.x[tick[0]], solution.x[tick[1]]};
        if (!std::isfinite(a[0]) || !std::isfinite(a[1])) return std::nullopt;
        controls.push_back({a[0], a[1]});
    }
    return controls;
}

Value DiskProblem::run() {
    const double started = seconds_now();
    clarabel_DefaultSolver_f64_solve(solver_->solver);
    const double finished = seconds_now();
    const ClarabelDefaultSolution_f64 solution = clarabel_DefaultSolver_f64_solution(solver_->solver);
    const std::string status = status_name(solution.status);
    const bool finite = std::all_of(solution.x, solution.x + solution.x_length, [](double v) { return std::isfinite(v); });
    Value controls{nullptr}, score{nullptr};
    std::string outcome;
    if ((status == "Solved" || status == "AlmostSolved") && finite) {
        outcome = "feasible";
        Array rows;
        for (const auto& tick : columns_) rows.push_back(array({real(solution.x[tick[0]]), real(solution.x[tick[1]])}));
        controls = array(std::move(rows));
        if (sense_ != 0) {
            double dot = 0.;
            for (std::size_t i = 0; i < cost_.size(); ++i) dot += cost_[i] * solution.x[i];
            score = real(offset_ + sense_ * dot);
        }
    } else {
        // Every status but a limit is a verdict: no undecided result reaches IPOPT.
        outcome = status == "MaxIterations" || status == "MaxTime" ? "limit" : "infeasible";
    }
    return object({{"status", text(status)},
                   {"outcome", text(outcome)},
                   {"controls", controls},
                   {"objective", score},
                   {"iterations", integer_value(static_cast<std::int64_t>(solution.iterations))},
                   {"solveSeconds", real(finished - started)}});
}

Value disk_spec(const Value& problem_value, const Value& extra_rows, const Value* objective_value, bool strict) {
    const Object& problem = fields(problem_value);
    const Value* threshold = get(problem, "threshold");
    if (threshold && !py_equal(*threshold, integer_value(0))) return {nullptr};
    if (const Value* ladder = get(problem, "ladderTicks"))
        for (const auto& flag : items(*ladder))
            if (py_truthy(flag)) return {nullptr};
    if (get(problem, "inertiaPattern")) return {nullptr};
    const std::int64_t n = as_int(at(problem, "ticks"));
    auto in_range = [&](const Value& t) { return is_int(t) && as_int(t) >= 0 && as_int(t) <= n; };
    std::vector<std::pair<std::string, Array>> rows;  // (str(row), row)
    std::set<std::string> seen;  // value identities: set(rows) drops a row equal to an earlier one (1 == 1.0)
    auto identity = [](const Array& row) {
        std::string out;
        for (const auto& v : row) {
            double number;
            out += numeric(v, number) && !std::holds_alternative<bool>(v.data) ? float_repr(number + 0.0) : py_repr(v);
            out += '\x1f';
        }
        return out;
    };
    auto collect = [&](const Value& source) -> bool {
        for (const auto& r_value : items(source)) {
            const Object& r = fields(r_value);
            const Value* t1 = get(r, "t1");
            const Value& t = t1 ? *t1 : get_or_null(r, "offset");
            const Value& t2 = get_or_null(r, "t2");
            const Value& mode = get_or_null(r, "mode");
            if (is_one(mode, "X") || is_one(mode, "Z")) {
                if (in_range(t) && (is_null(t2) || in_range(t2))) {
                    const Value* op = get(r, "op");
                    Array row{mode, t, t2, op ? *op : text("PLUS"), at(r, "cmp"), at(r, "rhs")};
                    if (seen.insert(identity(row)).second) rows.emplace_back(row_key(row), std::move(row));
                    continue;
                }
            }
            if (strict) return false;
        }
        return true;
    };
    const Value* walls = get(problem, "walls");
    if ((walls && !collect(*walls)) || !collect(extra_rows)) return {nullptr};
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Array sorted;
    for (auto& row : rows) sorted.push_back(array(std::move(row.second)));
    Object result;
    for (const char* key : {"ticks", "position", "velocity", "magnitude", "friction"})
        result.emplace_back(key, at(problem, key));
    const Value* contacts = get(problem, "contacts");
    const Value* intervals = get(problem, "initialIntervals");
    result.emplace_back("version", integer_value(kModelVersion));
    result.emplace_back("rows", array(std::move(sorted)));
    result.emplace_back("contacts", contacts ? *contacts : array({}));
    result.emplace_back("initialIntervals", intervals ? *intervals : Value{nullptr});
    const Value& own = get_or_null(problem, "objective");
    std::optional<Object> objective;
    if (!is_null(own) && !is_one(get_or_null(problem, "objectiveMode"), "constant_zero")) {
        objective = fields(own);
    } else if (objective_value && !is_null(*objective_value)) {
        objective = with(fields(*objective_value), "tick", integer_value(n));
    }
    if (objective) {
        Object selected;
        for (const char* key : {"tick", "axis", "sense", "customYaw", "type"})
            selected.emplace_back(key, get_or_null(*objective, key));
        result.emplace_back("objective", object(std::move(selected)));
    }
    return object(std::move(result));
}

Value disk_deficits(const Value& problem, const Value& controls) {
    const Array& magnitudes = items(at(fields(problem), "magnitude"));
    const Array& rows = items(controls);
    Array values;
    double peak = 0.;
    std::size_t count = std::min(magnitudes.size(), rows.size());
    std::vector<double> shortfall;
    for (std::size_t t = 0; t < count; ++t) {
        const double m = num(magnitudes[t]).value;
        const Array& c = items(rows[t]);
        double value = 0.;
        if (m > 0) {
            const double gap = m - python_hypot(num(c[0]).value, num(c[1]).value);
            value = (gap > 0. ? gap : 0.) / m;  // max(0., gap) keeps 0. unless gap is larger
        }
        shortfall.push_back(value);
        values.push_back(real(value));
    }
    Value tick{nullptr};
    if (!shortfall.empty()) {
        peak = shortfall[0];
        for (double v : shortfall)
            if (v > peak) peak = v;
        tick = integer_value(std::find(shortfall.begin(), shortfall.end(), peak) - shortfall.begin());
    }
    return array({real(peak), array(std::move(values)), tick});
}

Value ipopt_seed(const Value& yaw) {
    Array out;
    for (const auto& y : items(yaw)) out.push_back(real(num(y).value + degrees(kSeedTilt)));
    return array(std::move(out));
}

Value alarm_ticks(const Value& problem, const Value& shortfall, double fraction) {
    const Object& p = fields(problem);
    const Value* vertical = get(p, "vertical");
    const Value* jumps = vertical && py_truthy(*vertical) ? get(fields(*vertical), "jumpVelocity") : nullptr;
    const bool known = jumps && py_truthy(*jumps);
    Array out;
    const Array& values = items(shortfall);
    for (std::size_t t = 0; t < values.size(); ++t)
        if (num(values[t]).value > fraction && known && !is_null(items(*jumps)[t]))
            out.push_back(integer_value(static_cast<std::int64_t>(t)));
    return array(std::move(out));
}

namespace {

struct DecidedCache {
    std::list<std::pair<std::string, Value>> order;  // insertion order, at most 1024
    std::unordered_map<std::string, std::list<std::pair<std::string, Value>>::iterator> index;
    std::map<std::string, Object> stats{{"screen", {}}, {"candidate", {}}};
};

DecidedCache& decided() {
    static DecidedCache cache;
    return cache;
}

void bump(Object& counter, const std::string& name) {  // collections.Counter()[name] += 1
    for (auto& [k, v] : counter)
        if (k == name) {
            v = integer_value(as_int(v) + 1);
            return;
        }
    counter.emplace_back(name, integer_value(1));
}

// _attempt: one run, its verdict and its built problem (null after an exception).
std::pair<Value, std::shared_ptr<DiskProblem>> attempt(const Value& content, double time_limit, unsigned max_iter,
                                                       const std::vector<DiskCut>& cuts,
                                                       const std::optional<DiskLevel>& level) {
    try {
        auto built = std::make_shared<DiskProblem>(content, time_limit, max_iter, cuts, level);
        Object result = fields(built->run());
        result.emplace_back("assemblySeconds", real(built->assembly_seconds));
        result.emplace_back("setupSeconds", real(built->setup_seconds));
        return {object(std::move(result)), built};
    } catch (const std::exception& error) {
        return {object({{"status", text("exception")}, {"outcome", text("infeasible")}, {"controls", Value{nullptr}},
                        {"objective", Value{nullptr}}, {"iterations", integer_value(0)},
                        {"message", text(std::string("ValueError('") + error.what() + "')")}}),
                nullptr};
    }
}

}  // namespace

std::pair<Value, std::shared_ptr<DiskProblem>> disk_decide(const Value& content, double seconds,
                                                           const DiskLimits& limits, const std::vector<DiskCut>& cuts,
                                                           const std::optional<DiskLevel>& level) {
    auto result = attempt(content, std::min(limits.cap, seconds), limits.max_iter, cuts, level);
    if (as_text(at(fields(result.first), "outcome")) == "limit") {
        result = attempt(content, std::min(limits.retry_cap, seconds), limits.retry_max_iter, cuts, level);
        Object retry = with(fields(result.first), "retried", boolean(true));
        if (as_text(at(retry, "outcome")) == "limit") put(retry, "outcome", text("infeasible"));
        result.first = object(std::move(retry));
    }
    return result;
}

Value disk_cached_solve(const Value& content, double seconds, const std::string& kind, const DiskLimits& limits) {
    DecidedCache& cache = decided();
    const std::string encoded = dumps(content, true, false);
    Object& stats = cache.stats[kind];
    if (auto hit = cache.index.find(encoded); hit != cache.index.end()) {
        bump(stats, "cacheHits");
        return object(with(fields(hit->second->second), "cached", boolean(true)));
    }
    bump(stats, "solved");
    const Value result = disk_decide(content, seconds, limits).first;
    if (get(fields(result), "retried")) bump(stats, "retried");
    bump(stats, as_text(at(fields(result), "outcome")));
    cache.order.emplace_back(encoded, result);
    cache.index[encoded] = std::prev(cache.order.end());
    while (cache.order.size() > 1024) {
        cache.index.erase(cache.order.front().first);
        cache.order.pop_front();
    }
    return object(with(fields(result), "cached", boolean(false)));
}

void disk_clear_decided() {
    DecidedCache& cache = decided();
    cache.order.clear();
    cache.index.clear();
}

Value disk_stats() {
    Object out;
    for (const auto& [kind, counter] : decided().stats) out.emplace_back(kind, object(counter));
    return object(std::move(out));
}

Value disk_screen(const Value& model, const Value& extra_rows, const Value* objective, const DiskLimits& limits) {
    const Value content = disk_spec(model, extra_rows, objective, false);
    if (is_null(content) ||
        (items(at(fields(content), "rows")).empty() && !py_truthy(get_or_null(fields(content), "contacts"))))
        return Value{nullptr};
    const Value result = disk_cached_solve(content, limits.retry_cap, "screen", limits);
    const Object& r = fields(result);
    Object checked{{"outcome", at(r, "outcome")}, {"status", at(r, "status")}};
    if (as_text(at(r, "outcome")) == "infeasible") {
        checked.emplace_back("proof", object({{"reason", text("disk_control_relaxation_infeasible")},
                                              {"relaxation", text("acceleration_disk")}, {"status", at(r, "status")}}));
    }
    return object(std::move(checked));
}

Value decided_result(const Value& problem, double started, const char* status, const Value& candidate,
                     const std::string& termination, const Value& solver_status, const Value& relaxed) {
    const Object& p = fields(problem);
    const double elapsed = seconds_now() - started;
    return object({{"version", integer_value(1)}, {"status", text(status)}, {"feasible", boolean(!is_null(candidate))},
                   {"candidate", candidate}, {"objectiveMode", at(p, "objectiveMode")},
                   {"problemHash", text(fingerprint(problem))}, {"elapsedSeconds", real(elapsed)},
                   {"firstFeasibleSeconds", is_null(candidate) ? kNull : real(seconds_now() - started)},
                   {"problem", object({{"ticks", at(p, "ticks")},
                                       {"constraints", integer_value(static_cast<std::int64_t>(items(at(p, "walls")).size()))},
                                       {"contacts", integer_value(static_cast<std::int64_t>(items(at(p, "contacts")).size()))},
                                       {"threshold", at(p, "threshold")}, {"objective", at(p, "objective")},
                                       {"source", get_or_null(p, "source")}})},
                   {"termination", text(termination)}, {"workerStatus", solver_status},
                   {"workerLaunched", boolean(false)}, {"error", kNull}, {"tolerance", real(kReplayTolerance)},
                   {"phases", array({})}, {"backend", text("clarabel")}, {"disk", relaxed},
                   {"diagnostics", object({{"rejectedCandidates", integer_value(0)}, {"lastRejection", kNull},
                                           {"stderr", array({})}})}});
}

namespace {

const std::string& termination_of(const Value& result) { return as_text(at(fields(result), "termination")); }
bool feasible(const Value& result) { return as_text(at(fields(result), "status")) == "feasible"; }

// RelaxedDispatch.confirm: answer an alarmed request with the post-alarm pipeline (no IPOPT), or re-solve it with
// IPOPT; the outcome governs it.
Value confirm(const Value& problem, const Value& relaxed_candidate, const Object& relaxed, double seconds,
              const std::function<void(const Value&)>& on_incumbent, const RelaxedServices& services) {
    const std::string key = fingerprint(problem);
    const Value* stored = services.answer("confirmation", key);
    const bool cached = stored != nullptr;
    if (services.pipeline) {
        Value result;
        if (cached) {
            Object hit = fields(*stored);
            put(hit, "confirmation", object(with(with(fields(at(hit, "confirmation")), "cached", boolean(true)),
                                                 "clarabelSolves", integer_value(0))));
            result = object(std::move(hit));
        } else {
            result = post_alarm_run(problem, relaxed_candidate, object(relaxed), seconds, services);
            // Decided: an answer, a sector certificate, or no answer after every step; a deadline is not.
            if (feasible(result) || termination_of(result) == "quadrant_infeasible" ||
                termination_of(result) == "post_alarm_none")
                services.remember("confirmation", key, result);
        }
        if (feasible(result) && on_incumbent) on_incumbent(at(fields(result), "candidate"));
        return result;
    }
    Value result;
    if (cached) {
        result = object(with(fields(*stored), "workerLaunched", boolean(false)));
    } else {
        // Assumption (measured, research/disk-relaxation/README.md): the disk directions are the most accurate
        // single-attempt seed. The request's fallback stays.
        result = services.full(object(with(fields(problem), "yaw", ipopt_seed(at(fields(relaxed_candidate), "yaw")))),
                               seconds, false, {});
        const Object& r = fields(result);
        const std::string& status = as_text(at(r, "status"));
        if (status == "feasible" || (status == "unknown" && as_text(at(r, "termination")) == "worker_finished"))
            services.remember("confirmation", key, result);
    }
    const Value& disk_objective = at(fields(relaxed_candidate), "objective");
    Object confirmation = {{"peak", at(relaxed, "peakRelativeDeficit")}, {"tick", at(relaxed, "peakTick")},
                           {"cached", boolean(cached)}, {"diskObjective", disk_objective},
                           {"ipoptObjective", kNull}, {"gap", kNull}, {"material", boolean(false)}};
    const Object& r = fields(result);
    if (as_text(at(r, "status")) == "feasible") {
        const Value& ipopt_objective = at(fields(at(r, "candidate")), "objective");
        const double sense = as_text(at(fields(at(fields(problem), "objective")), "sense")) == "MAX" ? 1 : -1;
        const double gap = sense * (num(disk_objective).value - num(ipopt_objective).value);
        put(confirmation, "ipoptObjective", ipopt_objective);
        put(confirmation, "gap", real(gap));
        put(confirmation, "material", boolean(gap > services.material_gap));
        if (on_incumbent) on_incumbent(at(r, "candidate"));
    }
    return object(with(with(with(r, "backend", text("ipopt")), "disk", object(relaxed)), "confirmation",
                       object(std::move(confirmation))));
}

// _full_strength: the relaxed point itself when it is full strength, else the post-alarm pipeline's answer, then one
// IPOPT polish from the answer's exact facings (the SEED_TILT turn moves validated points out of IPOPT's tolerance),
// keeping the better; without an answer or a sector certificate, IPOPT from the turned disk facings
// (research/post-alarm/README.md#full-strength-acceptance-and-frame-end-re-solves).
Value full_strength(const Value& problem, const Value& relaxed_candidate, const Object& relaxed, double started,
                    double seconds, const RelaxedServices& services) {
    const Object& relaxed_point = fields(relaxed_candidate);
    Object record{{"method", text("direct")}, {"pushObjective", kNull}, {"ipoptObjective", kNull},
                  {"ipoptCalls", integer_value(0)}, {"clarabelSolves", integer_value(0)}, {"cached", boolean(false)}};
    Value answer{nullptr};
    std::string termination = "disk_finished";
    if (num(items(disk_deficits(problem, at(relaxed_point, "controls")))[0]).value < kPostAlarmFull) {
        Value checked = validate(problem, post_alarm_facings(problem, at(relaxed_point, "controls")));
        if (py_truthy(at(fields(checked), "feasible"))) answer = checked;
    }
    if (is_null(answer)) {
        const Value pushed = post_alarm_run(problem, relaxed_candidate, object(relaxed),
                                            std::max(.001, started + seconds - seconds_now()), services);
        const Object& p = fields(pushed);
        const Object& confirmation = fields(at(p, "confirmation"));
        put(record, "method", at(confirmation, "method"));
        put(record, "clarabelSolves", at(confirmation, "clarabelSolves"));
        if (as_text(at(p, "status")) == "cancelled" || termination_of(pushed) == "quadrant_infeasible")
            return object(with(p, "frameEnd", object(std::move(record))));
        answer = at(p, "candidate");
        termination = termination_of(pushed);
    }
    Object request;
    if (!is_null(answer)) {
        put(record, "pushObjective", at(fields(answer), "objective"));
        for (const auto& [k, v] : with(fields(problem), "yaw", at(fields(answer), "yaw")))
            if (k != "fallbackYaw") request.emplace_back(k, v);
    } else {
        request = with(fields(problem), "yaw", ipopt_seed(at(relaxed_point, "yaw")));
    }
    const Value ipopt_value =
        services.full(object(std::move(request)), std::max(.001, started + seconds - seconds_now()), false, {});
    const Object& ipopt = fields(ipopt_value);
    const Value& launched = get_or_null(ipopt, "workerLaunched");
    put(record, "ipoptCalls", integer_value(std::holds_alternative<bool>(launched.data) && std::get<bool>(launched.data)));
    const std::string& status = as_text(at(ipopt, "status"));
    if (status == "feasible") put(record, "ipoptObjective", at(fields(at(ipopt, "candidate")), "objective"));
    const double sense = as_text(at(fields(at(fields(problem), "objective")), "sense")) == "MAX" ? 1 : -1;
    if (is_null(answer) || status == "cancelled" ||
        (status == "feasible" && sense * num(at(fields(at(ipopt, "candidate")), "objective")).value >
                                     sense * num(at(fields(answer), "objective")).value))
        return object(with(with(with(ipopt, "backend", text("ipopt")), "disk", object(relaxed)), "frameEnd",
                           object(std::move(record))));
    const Value result = decided_result(problem, started, "feasible", answer, termination, at(relaxed, "status"),
                                        object(relaxed));
    return object(with(with(fields(result), "workerLaunched", launched), "frameEnd", object(std::move(record))));
}

// RelaxedDispatch._frame_end: full_strength's answer for a validated relaxed point, cached when decided like a
// confirmation.
Value frame_end(const Value& problem, const Value& relaxed_candidate, const Object& relaxed, double started,
                double seconds, const std::function<void(const Value&)>& on_incumbent,
                const RelaxedServices& services) {
    const std::string key = fingerprint(problem);
    Value result;
    if (const Value* stored = services.answer("frameEnd", key)) {
        Object hit = with(fields(*stored), "workerLaunched", boolean(false));
        put(hit, "frameEnd", object(with(with(with(fields(at(hit, "frameEnd")), "cached", boolean(true)), "ipoptCalls",
                                              integer_value(0)),
                                         "clarabelSolves", integer_value(0))));
        result = object(std::move(hit));
    } else {
        result = full_strength(problem, relaxed_candidate, relaxed, started, seconds, services);
        if (feasible(result) || termination_of(result) == "quadrant_infeasible" ||
            termination_of(result) == "worker_finished")
            services.remember("frameEnd", key, result);
    }
    if (feasible(result) && on_incumbent) on_incumbent(at(fields(result), "candidate"));
    return result;
}

}  // namespace

Value relaxed_dispatch(const Value& problem, double seconds, bool stop_on_feasible, DispatchMode mode,
                       const std::function<void(const Value& candidate)>& on_incumbent,
                       const RelaxedServices& services) {
    auto full = [&](const char* reason) {
        return object(with(with(fields(services.full(problem, seconds, stop_on_feasible, on_incumbent)), "backend",
                                text("ipopt")),
                           "fallback", text(reason)));
    };
    const Value content = disk_spec(problem, array({}), nullptr, true);
    if (is_null(content)) return full("unsupported");
    const double started = seconds_now();
    if (services.cancelled && services.cancelled())
        return decided_result(problem, started, "cancelled", kNull, "cancelled", kNull, kNull);
    const Value solved_value = disk_cached_solve(content, seconds, "candidate", services.limits);
    const Object& solved = fields(solved_value);
    Object relaxed;
    for (const char* key : {"status", "outcome", "iterations", "cached"}) relaxed.emplace_back(key, at(solved, key));
    const std::string& outcome = as_text(at(solved, "outcome"));
    if (outcome == "infeasible")
        return decided_result(problem, started, "unknown", kNull, "disk_finished", at(solved, "status"),
                              object(std::move(relaxed)));
    Object candidate = fields(validate_controls(problem, at(solved, "controls")));
    // The replay only fails a solved point by numerical error; it is not worth an IPOPT call (user, 2026-11-23).
    if (!py_truthy(at(candidate, "feasible")))
        return decided_result(problem, started, "unknown", kNull, "disk_rejected", at(solved, "status"),
                              object(std::move(relaxed)));
    const Value deficit_value = disk_deficits(problem, at(solved, "controls"));
    const Array& deficit = items(deficit_value);
    const Value alarmed = alarm_ticks(problem, deficit.at(1), services.alarm_fraction);
    put(relaxed, "peakRelativeDeficit", deficit.at(0));
    put(relaxed, "peakTick", deficit.at(2));
    put(relaxed, "alarmTicks", alarmed);
    put(relaxed, "objective", at(solved, "objective"));
    put(candidate, "model", text("relaxed_disk"));
    const Value candidate_value = object(std::move(candidate));
    if (mode == DispatchMode::full_strength)
        return frame_end(problem, candidate_value, relaxed, started, seconds, on_incumbent, services);
    if (mode == DispatchMode::confirmed && !items(alarmed).empty() && !stop_on_feasible)
        return confirm(problem, candidate_value, relaxed, seconds, on_incumbent, services);
    if (on_incumbent) on_incumbent(candidate_value);
    return decided_result(problem, started, "feasible", candidate_value, "disk_finished", at(solved, "status"),
                          object(std::move(relaxed)));
}

}  // namespace pkc
