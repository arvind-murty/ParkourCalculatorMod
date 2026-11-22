// Port of numerics/disk.py's spec, _solve/_cost, deficits, ipopt_seed and
// alarm_ticks. The constraint matrix is built in scipy's coo -> csc layout
// (column-major, rows ascending, explicit zeros kept) so Clarabel factors the
// same pattern.
#include "pkc/numerics/disk.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
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

namespace pkc {
namespace {

const int kModelVersion = 1;
const double kAllowance = 1e-9;
const double kPenetration = 1e-7;
const double kTol = 1e-9;
const double kInfeasibilityTol = 1e-8;
const double kSeedTilt = 1e-4;

double seconds_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

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

Value disk_solve(const Value& content_value, double time_limit, unsigned max_iter) {
    const double started = seconds_now();
    const Object& content = fields(content_value);
    const std::int64_t n = as_int(at(content, "ticks"));
    const std::size_t width = 3 * n + 2;
    auto pos = [&](int axis, std::int64_t t) { return axis * width + t; };
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
                inequalities.row({{pos(axis, t), -d}, {vel(axis, t), -d}, {control(axis, t), -d}}, -d * plane - kPenetration);
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
    Block disks;
    const Array& magnitudes = items(at(content, "magnitude"));
    for (std::int64_t t = 0; t < n; ++t) {
        disks.rhs.insert(disks.rhs.end(), {num(magnitudes[t]).value + kAllowance, 0., 0.});
        for (int axis = 0; axis < 2; ++axis) {
            disks.rows.push_back(3 * t + 1 + axis);
            disks.columns.push_back(control(axis, t));
            disks.values.push_back(-1.);
        }
    }

    // coo -> csc: column-major, rows ascending within a column (the blocks stack in row order).
    const std::size_t columns = 2 * width;
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

    // _cost: the minimized cost vector, the world offset and the sign recovering the score.
    std::vector<double> cost(columns, 0.);
    double score_offset = 0., sign = 0.;
    const Value* objective_value = get(content, "objective");
    if (objective_value && !is_null(*objective_value)) {
        const Object& objective = fields(*objective_value);
        const std::int64_t tick = as_int(at(objective, "tick"));
        if (0 <= tick && tick <= n) {
            sign = as_text(at(objective, "sense")) == "MAX" ? -1. : 1.;
            const Value& custom = get_or_null(objective, "customYaw");
            if (!is_null(custom)) {
                const double angle = radians(num(custom).value);
                const double weights[2] = {-std::sin(angle), std::cos(angle)};
                const bool motion = is_one(get_or_null(objective, "type"), "MOTION") && tick > 0;
                score_offset = motion ? 0. : weights[0] * position[0].value + weights[1] * position[1].value;
                for (int axis = 0; axis < 2; ++axis) {
                    cost[pos(axis, tick)] += sign * weights[axis];
                    if (motion) cost[pos(axis, tick - 1)] -= sign * weights[axis];
                }
            } else {
                const int axis = as_text(at(objective, "axis")) == "X" ? 0 : 1;
                cost[pos(axis, tick)] = sign;
                score_offset = position[axis].value;
            }
        }
    }

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
    settings.time_limit = time_limit;
    settings.max_iter = max_iter;
    settings.tol_feas = settings.tol_gap_abs = settings.tol_gap_rel = kTol;
    settings.tol_infeas_abs = settings.tol_infeas_rel = kInfeasibilityTol;
    const double assembled = seconds_now();
    std::vector<std::uintptr_t> p_colptr(columns + 1, 0);
    ClarabelCscMatrix_f64 P, A;
    clarabel_CscMatrix_f64_init(&P, columns, columns, p_colptr.data(), nullptr, nullptr);
    std::vector<std::uintptr_t> a_colptr(colptr.begin(), colptr.end()), a_rowval(rowval.begin(), rowval.end());
    clarabel_CscMatrix_f64_init(&A, rhs.size(), columns, a_colptr.data(), a_rowval.data(), nzval.data());
    ClarabelDefaultSolver_f64* solver = clarabel_DefaultSolver_f64_new(&P, cost.data(), &A, rhs.data(), cones.size(),
                                                                       cones.data(), &settings);
    const double ready = seconds_now();
    clarabel_DefaultSolver_f64_solve(solver);
    const double finished = seconds_now();
    ClarabelDefaultSolution_f64 solution = clarabel_DefaultSolver_f64_solution(solver);
    const std::string status = status_name(solution.status);
    std::vector<double> x(solution.x, solution.x + solution.x_length);
    const auto iterations = static_cast<std::int64_t>(solution.iterations);
    clarabel_DefaultSolver_f64_free(solver);
    Value controls{nullptr}, score{nullptr};
    std::string outcome;
    const bool finite = std::all_of(x.begin(), x.end(), [](double v) { return std::isfinite(v); });
    if ((status == "Solved" || status == "AlmostSolved") && finite) {
        outcome = "feasible";
        Array rows;
        for (std::int64_t t = 0; t < n; ++t) rows.push_back(array({real(x[control(0, t)]), real(x[control(1, t)])}));
        controls = array(std::move(rows));
        if (sign != 0) {
            double dot = 0.;
            for (std::size_t i = 0; i < columns; ++i) dot += cost[i] * x[i];
            score = real(score_offset + sign * dot);
        }
    } else {
        outcome = status == "PrimalInfeasible" || status == "AlmostPrimalInfeasible" ? "infeasible" : "undecided";
    }
    return object({{"status", text(status)},
                   {"outcome", text(outcome)},
                   {"controls", controls},
                   {"objective", score},
                   {"iterations", integer_value(iterations)},
                   {"assemblySeconds", real(assembled - started)},
                   {"setupSeconds", real(ready - assembled)},
                   {"solveSeconds", real(finished - ready)}});
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
    std::vector<Array> rows;
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
                    // set(rows): a row equal to an earlier one (1 == 1.0) is dropped.
                    if (std::none_of(rows.begin(), rows.end(), [&](const Array& seen) { return py_equal(seen, row); }))
                        rows.push_back(std::move(row));
                    continue;
                }
            }
            if (strict) return false;
        }
        return true;
    };
    const Value* walls = get(problem, "walls");
    if ((walls && !collect(*walls)) || !collect(extra_rows)) return {nullptr};
    std::stable_sort(rows.begin(), rows.end(), [](const Array& a, const Array& b) { return row_key(a) < row_key(b); });
    Array sorted;
    for (auto& row : rows) sorted.push_back(array(std::move(row)));
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

}  // namespace pkc
