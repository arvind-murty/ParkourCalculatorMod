// Port of numerics/continuation.py.
#include "pkc/numerics/continuation.hpp"

#include <optional>
#include <set>
#include <vector>

#include "../schema.hpp"
#include "pkc/validation/numerical.hpp"

namespace pkc {
namespace {

class Incumbent {
public:
    Incumbent(const Value& problem, const std::function<void(const Value&)>& emit) : problem_(problem), emit_(emit) {}

    bool consider(const Value& yaw) {
        Value candidate;
        try {
            candidate = validate(problem_, yaw);
        } catch (const std::invalid_argument&) {
            return false;
        }
        const Object& checked = fields(candidate);
        if (!std::get<bool>(at(checked, "feasible").data)) return false;
        const double direction = as_text(at(fields(at(fields(problem_), "objective")), "sense")) == "MAX" ? 1 : -1;
        if (!best || direction * num(at(checked, "objective")).value >
                         direction * num(at(fields(*best), "objective")).value) {
            best = candidate;
            emit_(object({{"type", text("candidate")}, {"yaw", at(checked, "yaw")}}));
        }
        return true;
    }

    Value pattern() const {
        const double threshold = num(at(fields(problem_), "threshold")).value;
        const Array& velocity = items(at(fields(*best), "velocity"));
        Array rows;
        for (std::size_t t = 0; t + 1 < velocity.size(); ++t) {
            Array row;
            for (const auto& v : items(velocity[t])) {
                double value = num(v).value;
                row.push_back(integer_value(std::fabs(value) < threshold ? 0 : value > 0 ? 1 : -1));
            }
            rows.push_back(array(std::move(row)));
        }
        return array(std::move(rows));
    }

    std::optional<Value> best;

private:
    const Value& problem_;
    const std::function<void(const Value&)>& emit_;
};

// tuple(yaw) as a set key: Python compares the floats by value.
std::vector<double> key(const Value& yaw) {
    std::vector<double> out;
    for (const auto& v : items(yaw)) out.push_back(num(v).value);
    return out;
}

bool succeeded(const std::string& status) {
    return status == "Solve_Succeeded" || status == "Solved_To_Acceptable_Level";
}

}  // namespace

std::string run(const Value& problem_value, const Phase& phase, const std::function<Value(const Value&)>& infer,
                const std::function<void(const Value&)>& emit, const std::function<double()>& clock) {
    const Object& problem = fields(problem_value);
    const double end = clock() + num(at(problem, "seconds")).value;
    const bool stop = std::get<bool>(at(problem, "stopOnFeasible").data);
    Incumbent incumbent(problem_value, emit);
    incumbent.consider(at(problem, "yaw"));
    if (incumbent.best && stop) return "User_Requested_Stop";
    Array free_rows(static_cast<std::size_t>(as_int(at(problem, "ticks"))),
                    array({integer_value(2), integer_value(2)}));
    const Value free = array(std::move(free_rows));
    std::string status = "Not_Started";
    std::set<std::vector<double>> polished;
    auto on_candidate = [&](const Value& yaw) { return incumbent.consider(yaw); };

    auto solve_phase = [&](const Value& yaw, const Value& pattern, double deadline, double fraction,
                           const std::string& kind, const std::string& seed) -> std::pair<Value, bool> {
        const double remaining = deadline - clock();
        if (remaining <= 0.05) return {yaw, false};
        auto [result, phase_status] = phase(problem_value, yaw, pattern, remaining * fraction, stop, on_candidate,
                                            kind, seed);
        status = phase_status;
        incumbent.consider(result);
        return {result, true};
    };

    if (const Value* pattern = get(problem, "inertiaPattern")) {
        solve_phase(at(problem, "yaw"), *pattern, end, 1, "fixed_pattern", "initial");
        return status;
    }

    std::vector<std::pair<std::string, Value>> seeds{{"initial", at(problem, "yaw")}};
    const Value* fallback = get(problem, "fallbackYaw");
    if (fallback && !is_null(*fallback) && !py_equal(*fallback, at(problem, "yaw")))
        seeds.emplace_back("fallback", *fallback);
    if (num(at(problem, "threshold")).value == 0) {
        for (std::size_t index = 0; index < seeds.size(); ++index) {
            const auto& [name, seed] = seeds[index];
            incumbent.consider(seed);
            if (incumbent.best && stop) return "User_Requested_Stop";
            const double remaining = end - clock();
            const double deadline = index + 1 < seeds.size() ? clock() + remaining * .4 : end;
            solve_phase(seed, free, deadline, 1, "carry", name);
        }
        return status;
    }
    for (std::size_t index = 0; index < seeds.size(); ++index) {
        const auto& [name, seed] = seeds[index];
        const double remaining = end - clock();
        const double deadline = index + 1 < seeds.size() ? clock() + remaining * 0.4 : end;
        incumbent.consider(seed);
        Value yaw;
        if (!incumbent.best || index > 0) {
            auto [carried, ran] = solve_phase(seed, free, deadline, 0.3, "carry", name);
            yaw = carried;
            if (ran) {
                if (incumbent.best && stop) return "User_Requested_Stop";
                Array zeros;
                const Value inferred = infer(yaw);
                for (const auto& row : items(inferred)) {
                    Array marked;
                    for (const auto& b : items(row)) marked.push_back(integer_value(as_int(b) == 0 ? 0 : 2));
                    zeros.push_back(array(std::move(marked)));
                }
                yaw = solve_phase(yaw, array(std::move(zeros)), deadline, 0.35, "zero_events", name).first;
            }
        } else {
            yaw = seed;
        }
        for (int round = 0; round < 3; ++round) {
            Value pattern;
            std::string kind;
            if (incumbent.best) {
                if (stop) return "User_Requested_Stop";
                if (polished.count(key(at(fields(*incumbent.best), "yaw")))) break;
                yaw = at(fields(*incumbent.best), "yaw");
                pattern = incumbent.pattern();
                kind = "incumbent_polish";
            } else {
                pattern = infer(yaw);
                kind = "fixed_pattern";
            }
            auto [next, ran] = solve_phase(yaw, pattern, deadline, 1, kind, name);
            yaw = next;
            if (ran && succeeded(status) && incumbent.best && py_equal(incumbent.pattern(), pattern))
                polished.insert(key(at(fields(*incumbent.best), "yaw")));
            if (!ran || succeeded(status) || status == "User_Requested_Stop") break;
        }
    }
    return status;
}

}  // namespace pkc
