#include "pkc/search/ordered_route.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/numerics/builders.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/program.hpp"
#include "pkc/scene/route.hpp"
#include "pkc/scene/static_collision.hpp"
#include "pkc/validation/numerical.hpp"
#include "pkc/validation/vertical.hpp"

namespace pkc {

namespace {

double monotonic() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double d(const Value& v) {
    double out = 0.;
    if (!numeric(v, out)) fail("expected a number");
    return out;
}

Num any_num(const Value& v) {
    if (auto* integer = std::get_if<std::int64_t>(&v.data)) return whole(*integer);
    return flt(d(v));
}

Array listed(const Object& object, const char* key) {
    const Value* value = get(object, key);
    return value ? items(*value) : Array{};
}

std::int64_t sum(const std::vector<std::int64_t>& values, std::size_t from = 0) {
    std::int64_t total = 0;
    for (std::size_t i = from; i < values.size(); ++i) total += values[i];
    return total;
}

Value ints(const std::vector<std::int64_t>& values) {
    Array out;
    for (auto v : values) out.push_back(integer_value(v));
    return array(std::move(out));
}

std::int64_t step_kinds(const Object& route) {
    std::int64_t count = 0;
    for (const auto& kind : listed(route, "kinds")) count += as_text(kind) == "step";
    return count;
}

// ranking.objective_seed(problem).
Value objective_seed(const Object& problem) {
    const Object& objective = fields(at(problem, "objective"));
    const Value& custom = get_or_null(objective, "customYaw");
    double target = !is_null(custom) ? radians(d(custom)) + M_PI / 2 : as_text(at(objective, "axis")) == "X" ? 0 : M_PI / 2;
    if (as_text(at(objective, "sense")) == "MIN") target += M_PI;
    const Array &yaws = items(at(problem, "yaw")), &bases = items(at(problem, "base"));
    Array seed;
    for (std::size_t t = 0; t < std::min(yaws.size(), bases.size()); ++t) {
        const Num yaw = any_num(yaws[t]);
        const double difference = target - d(bases[t]) - radians(yaw.value);
        const double turn = degrees(std::atan2(std::sin(difference), std::cos(difference)));
        // max(-10, min(10, turn)) keeps the int bound on ties, as Python's do.
        const Num offset = py_max(whole(-10), py_min(whole(10), flt(turn)));
        seed.push_back(value(add(yaw, std::fabs(offset.value) > 1e-3 ? offset : whole(5))));
    }
    return array(std::move(seed));
}

// scene/route.derive_footprints(route, compiler, seconds).
void derive_footprints(Object& route, const SolveHooks& hooks, double seconds) {
    const Value footprints = hooks.derive_footprints(object(route), seconds);
    Array blocks = items(at(route, "blocks"));
    if (items(footprints).size() != blocks.size()) fail("The footprint compiler returned the wrong number of supports");
    bool standing = false;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const Value& footprint = items(footprints)[i];
        if (!is_list(footprint) || items(footprint).size() != 4) fail("Invalid derived footprint");
        for (const auto& v : items(footprint)) number(v, "derived footprint");
        blocks[i] = object(with(fields(blocks[i]), "_derivedFootprint", footprint));
        standing = standing || get(fields(blocks[i]), "standingBounds");
    }
    put(route, "blocks", array(blocks));
    if (standing) {
        const Value standing_footprints = hooks.derive_standing_footprints(object(route), seconds);
        for (std::size_t i = 0; i < std::min(blocks.size(), items(standing_footprints).size()); ++i)
            blocks[i] = object(with(fields(blocks[i]), "_derivedStandingFootprint", items(standing_footprints)[i]));
        put(route, "blocks", array(blocks));
    }
}

struct Evaluation {
    Value result;
};

// search_allocations(route, evaluate, ...).
Value search_allocations(const Value& route_value, const std::function<Value(const std::vector<std::int64_t>&, const std::string&,
                                                                              const Value&, double)>& evaluate,
                         double seconds, bool improve, bool rank_equal_cost, const SolveHooks& hooks) {
    const Value prepared = prepare_route(route_value);
    const Object& route = fields(prepared);
    if (seconds <= 0) fail("Search seconds must be positive");
    const double started = monotonic(), deadline = started + seconds;
    Array attempts;
    std::vector<Value> unresolved;  // the attempt records
    std::optional<Object> best;
    std::int64_t backtracks = 0;
    std::optional<std::string> stopped;
    std::vector<std::int64_t> minima;
    for (const auto& bounds : items(at(route, "runBounds"))) minima.push_back(as_int(items(bounds)[0]));
    const std::int64_t max_total = as_int(at(route, "maxTotalRunTicks"));
    const auto legs = items(at(route, "airtimes")).size();
    const std::string sense = as_text(at(fields(at(route, "objective")), "sense"));

    auto interrupted = [&] {
        if (hooks.is_cancelled()) {
            stopped = "cancelled";
            return true;
        }
        if (monotonic() >= deadline) {
            stopped = "deadline";
            return true;
        }
        return false;
    };
    auto probe = [&](const std::vector<std::int64_t>& allocation, const std::string& stage, const Value& parent) {
        const Value result = evaluate(allocation, stage, parent, std::max(0.001, deadline - monotonic()));
        const Value& status = get_or_null(fields(result), "status");
        static const char* kStatuses[] = {"feasible", "unknown", "blocked", "error", "cancelled"};
        bool known = false;
        for (const char* s : kStatuses) known = known || py_equal(status, text(s));
        if (!known) fail("Invalid candidate evaluator result");
        Object record;
        for (const auto& field : fields(result))
            if (field.first != "candidate" && field.first != "plan") record.push_back(field);
        put(record, "allocation", ints(allocation));
        put(record, "stage", text(stage));
        put(record, "leg", integer_value(static_cast<std::int64_t>(allocation.size()) - 1));
        const Value logged = object(std::move(record));
        attempts.push_back(logged);
        if (as_text(status) == "unknown") unresolved.push_back(logged);
        return result;
    };
    std::function<void(const std::vector<std::int64_t>&, const Value&)> visit =
        [&](const std::vector<std::int64_t>& prefix, const Value& parent) {
            const std::size_t index = prefix.size();
            const Array& bounds = items(items(at(route, "runBounds"))[index]);
            const std::int64_t lo = as_int(bounds[0]), hi = as_int(bounds[1]);
            for (std::int64_t extra = lo; extra <= hi; ++extra) {
                if (interrupted()) return;
                std::vector<std::int64_t> allocation = prefix;
                allocation.push_back(extra);
                const std::int64_t cost = sum(allocation);
                const std::int64_t floor = cost + sum(minima, index + 1);
                if (floor > max_total) break;
                if (best && (floor > as_int(at(*best, "cost")) || (!rank_equal_cost && floor == as_int(at(*best, "cost")))))
                    break;
                Value seed = parent;
                if (extra) {
                    const Value run = probe(allocation, "run", parent);
                    const std::string& status = as_text(at(fields(run), "status"));
                    if (status == "error") {
                        stopped = "error";
                        return;
                    }
                    if (status == "cancelled") {
                        stopped = "cancelled";
                        return;
                    }
                    if (status == "blocked" || status == "unknown") break;
                    seed = run;
                }
                if (interrupted()) return;
                const Value result = probe(allocation, "jump", seed);
                const Object& r = fields(result);
                const std::string& status = as_text(at(r, "status"));
                if (status == "error") {
                    stopped = "error";
                    return;
                }
                if (status == "cancelled") {
                    stopped = "cancelled";
                    return;
                }
                if (status != "feasible") continue;
                if (index + 1 == legs) {
                    const bool better_objective =
                        rank_equal_cost && best && cost == as_int(at(*best, "cost")) &&
                        (sense == "MAX" ? 1 : -1) * (d(at(fields(at(r, "candidate")), "objective")) -
                                                     d(at(fields(at(*best, "candidate")), "objective"))) > 0;
                    if (!best || cost < as_int(at(*best, "cost")) || better_objective)
                        best = Object{{"allocation", ints(allocation)}, {"cost", integer_value(cost)},
                                      {"candidate", at(r, "candidate")}, {"problemHash", get_or_null(r, "problemHash")},
                                      {"plan", get_or_null(r, "plan")}};
                    if (!rank_equal_cost && (!improve || cost == sum(minima))) {
                        stopped = improve ? "lower_bound_reached" : "first_feasible";
                        return;
                    }
                } else {
                    visit(allocation, result);
                    if (stopped) return;
                }
            }
            ++backtracks;
        };
    visit({}, Value{nullptr});
    if (!stopped) interrupted();
    const std::int64_t lower = sum(minima);
    Array cheaper, equal;
    for (const auto& item : unresolved) {
        std::vector<std::int64_t> allocation;
        for (const auto& v : items(at(fields(item), "allocation"))) allocation.push_back(as_int(v));
        const std::int64_t floor = sum(allocation) + sum(minima, allocation.size());
        if (!best || floor < as_int(at(*best, "cost"))) cheaper.push_back(item);
        if (best && floor == as_int(at(*best, "cost"))) equal.push_back(item);
    }
    bool declared = false;
    for (const auto& b : items(at(route, "blocks"))) declared = declared || get(fields(b), "centerBounds");
    const std::string status = best ? "feasible" : stopped == "cancelled" ? "cancelled" : stopped == "error" ? "error" : "unknown";
    return object({{"status", text(status)},
                   {"best", best ? object(*best) : Value{nullptr}},
                   {"lowerBound", integer_value(lower)},
                   {"minimumProven", boolean(best && as_int(at(*best, "cost")) == lower)},
                   {"unresolved", array(std::move(cheaper))},
                   {"unknownAttempts", integer_value(static_cast<std::int64_t>(unresolved.size()))},
                   {"attempts", array(std::move(attempts))},
                   {"backtracks", integer_value(backtracks)},
                   {"unresolvedEqualCost", array(std::move(equal))},
                   {"termination", text(stopped ? *stopped : "search_exhausted")},
                   {"elapsedSeconds", real(monotonic() - started)},
                   {"searchPolicy", text("first-failure-increment-backtrack")},
                   {"source", get_or_null(route, "source")},
                   {"validationLevel", text(declared ? "continuous_horizontal_declared_regions_with_supplied_airtimes"
                                                     : "continuous_horizontal_support_with_supplied_airtimes")},
                   {"gameReplay", text("not_run")}});
}

}  // namespace

Value candidate_rank(const Value& best_value, const Value& objective) {
    const Object& best = fields(best_value);
    const std::int64_t ticks = integer(at(fields(at(best, "plan")), "ticks"), "total ticks", 1, 100000);
    const Num v = number(at(fields(at(best, "candidate")), "objective"), "candidate objective");
    const bool max = as_text(at(fields(objective), "sense")) == "MAX";
    return array({integer_value(ticks), value(max ? Num{-v.value, v.integral} : v)});
}

bool rank_less(const Value& a, const Value& b) {
    const Array &x = items(a), &y = items(b);
    for (std::size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
        if (d(x[i]) < d(y[i])) return true;
        if (d(y[i]) < d(x[i])) return false;
    }
    return x.size() < y.size();
}

Value find_route(const Value& request, const SolveHooks& hooks, const RouteSearch& search) {
    const double started = monotonic();
    Object route = fields(prepare_route(request));
    if (search.seconds <= 0) fail("Search seconds must be positive");
    if (search.candidate_seconds <= 0) fail("Candidate seconds must be positive");
    if (search.retries < 0 || search.retries > 10) fail("Retry count must be between zero and ten");
    if (!listed(route, "passages").empty() || !listed(route, "hitEvents").empty() || !listed(route, "ladderEvents").empty())
        throw Unsupported("routes with passages, wall hits or ladder events solve in Python");
    if (hooks.derive_footprints && !hooks.is_cancelled())
        derive_footprints(route, hooks, std::min(search.seconds, search.candidate_seconds));
    const Value route_value = object(route);
    const auto legs = items(at(route, "airtimes")).size();
    const NumericalSolve& numerical = search.numerical ? search.numerical : hooks.solve;

    auto evaluate = [&](const std::vector<std::int64_t>& allocation, const std::string& stage, const Value& parent,
                        double available) -> Value {
        const double start = monotonic();
        Object plan = fields(schedule(route_value, allocation, stage));
        if (py_truthy(get_or_null(plan, "passageFailures")))
            return object({{"status", text("blocked")}, {"plan", object(plan)},
                           {"certificate", object({{"reason", text("passage_timing")}})}});
        if (!py_truthy(at(plan, "ticks"))) {
            const Array& position = items(at(fields(at(route, "initial")), "position"));
            return object({{"status", text("feasible")},
                           {"candidate", object({{"yaw", array({})}, {"positions", array({array({position[0], position[2]})})}})},
                           {"plan", object(plan)}});
        }
        try {
            if (py_truthy(get_or_null(route, "verticalProfiles"))) {
                const Value vertical = validate_vertical_schedule(route_value, at(route, "verticalProfiles"), object(plan));
                const Object& v = fields(vertical);
                if (!py_truthy(at(v, "feasible")))
                    return object({{"status", text("blocked")}, {"plan", object(plan)},
                                   {"certificate", object({{"reason", text("vertical_schedule")}})}});
                Object reference = fields(at(v, "vertical"));
                put(reference, "referencePosition", at(v, "y"));
                put(reference, "referenceVelocity", at(v, "vy"));
                put(plan, "vertical", object(std::move(reference)));
            }
            const Value plan_value = object(plan);
            const Value coefficients =
                hooks.compile(route_value, plan_value, std::max(0.001, std::min(available, search.candidate_seconds)));
            const Value parent_candidate = parent;
            Object problem = fields(numerical_problem(route_value, plan_value, coefficients,
                                                      is_null(parent) ? nullptr : &parent_candidate));
            if (hooks.work_proposal) hooks.work_proposal(object(problem));
            const bool polish = search.optimize_goal && stage == "jump" && allocation.size() == legs;
            const bool early_stop = search.stop_on_feasible ? *search.stop_on_feasible : !polish;
            const Value fallback = polish ? objective_seed(problem) : initial_guess(route_value, plan_value, nullptr);
            if (!py_equal(fallback, at(problem, "yaw"))) put(problem, "fallbackYaw", fallback);

            const Array& blocks = items(at(route, "blocks"));
            auto checked_candidate = [&](const Object& problem_now, const Value& returned_value,
                                         Array* failures) -> std::optional<Object> {
                const Object& returned = fields(returned_value);
                const bool relaxed = get(returned, "controls") != nullptr;
                const Value problem_v = object(problem_now);
                Object candidate = fields(relaxed ? validate_controls(problem_v, at(returned, "controls"))
                                                  : validate(problem_v, at(returned, "yaw")));
                const Array& positions = items(at(candidate, "positions"));
                Array failed;
                for (const auto& c : listed(plan, "supportChecks")) {
                    const Array& check = items(c);
                    const auto tick = as_int(check[0]);
                    const Array& p = items(positions.at(tick));
                    if (!supports(fields(blocks.at(as_int(check[1]))), d(p[0]), d(p[1]), as_text(check[2])))
                        failed.push_back(object({{"state", check[0]}, {"block", check[1]}, {"kind", check[2]}}));
                }
                for (const auto& c : listed(plan, "exitChecks")) {
                    const Array& check = items(c);
                    const Array& p = items(positions.at(as_int(check[0])));
                    if (supports(fields(blocks.at(as_int(check[1]))), d(p[0]), d(p[1]), "run"))
                        failed.push_back(object({{"state", check[0]}, {"block", check[1]},
                                                 {"kind", text("run_off_still_supported")}}));
                }
                if (failures) failures->insert(failures->end(), failed.begin(), failed.end());
                if (!py_truthy(at(candidate, "feasible")) || !failed.empty()) return std::nullopt;
                Array raw;
                if (relaxed) {
                    put(candidate, "model", at(returned, "model"));
                    const Array &velocity = items(at(candidate, "velocity")), &controls = items(at(candidate, "controls"));
                    for (std::size_t t = 0; t < std::min(velocity.size(), controls.size()); ++t) {
                        Array row;
                        const Array &v = items(velocity[t]), &c = items(controls[t]);
                        for (std::size_t a = 0; a < std::min(v.size(), c.size()); ++a)
                            row.push_back(value(add(any_num(v[a]), any_num(c[a]))));
                        raw.push_back(array(std::move(row)));
                    }
                } else {
                    const Array &velocity = items(at(candidate, "velocity")), &yaw = items(at(candidate, "yaw"));
                    const Array &base = items(at(problem_now, "base")), &magnitude = items(at(problem_now, "magnitude"));
                    const double threshold = d(at(problem_now, "threshold"));
                    for (std::size_t t = 0; t < yaw.size(); ++t) {
                        const double angle = d(base[t]) + radians(d(yaw[t]));
                        Array row;
                        const Array& v = items(velocity[t]);
                        for (std::size_t a = 0; a < v.size(); ++a) {
                            const Num kept = std::fabs(d(v[a])) >= threshold ? any_num(v[a]) : whole(0);
                            row.push_back(value(add(kept, mul(any_num(magnitude[t]),
                                                              flt(a == 0 ? std::cos(angle) : std::sin(angle))))));
                        }
                        raw.push_back(array(std::move(row)));
                    }
                }
                put(candidate, "rawMovement", array(std::move(raw)));
                if (const Value* movement = get(problem_now, "movement")) put(candidate, "movement", *movement);
                put(candidate, "friction", at(problem_now, "friction"));
                return candidate;
            };
            auto publish = [&](const Value& candidate) {
                if (!search.on_candidate || stage != "jump" || allocation.size() != legs) return;
                auto checked = checked_candidate(problem, candidate, nullptr);
                if (!checked) return;
                search.on_candidate(object({{"allocation", ints(allocation)},
                                            {"cost", integer_value(sum(allocation))},
                                            {"candidate", object(std::move(*checked))},
                                            {"plan", plan_value},
                                            {"problemHash", text(fingerprint(object(problem)))},
                                            {"route", route_value},
                                            {"groundRunTicks", integer_value(sum(allocation) + step_kinds(route))}}));
            };
            Array diagnostics;
            for (std::int64_t attempt = 0; attempt <= search.retries; ++attempt) {
                if (hooks.is_cancelled()) return object({{"status", text("cancelled")}});
                const double remaining = available - (monotonic() - start);
                if (remaining <= 0) break;
                const double budget = std::min(search.candidate_seconds * static_cast<double>(attempt + 1), remaining);
                if (attempt) put(problem, "yaw", fallback);
                if (hooks.work_before_solve) hooks.work_before_solve(object(problem));
                const Value result_value = numerical(object(problem), budget, early_stop, publish);
                const Object& result = fields(result_value);
                Object diagnostic{{"status", at(result, "status")},
                                  {"termination", get_or_null(result, "termination")},
                                  {"elapsedSeconds", get_or_null(result, "elapsedSeconds")},
                                  {"error", get_or_null(result, "error")},
                                  {"problemHash", text(fingerprint(object(problem)))},
                                  {"request", object(problem)},
                                  {"callOptions", object({{"seconds", real(budget)}, {"stopOnFeasible", boolean(early_stop)}})},
                                  {"objectiveMode", text("configured")},
                                  {"workerLaunched", get_or_null(result, "workerLaunched")},
                                  {"workerStatus", get_or_null(result, "workerStatus")}};
                for (const char* key : {"backend", "disk", "confirmation", "fallback"})
                    if (const Value* v = get(result, key)) diagnostic.emplace_back(key, *v);
                diagnostics.push_back(object(std::move(diagnostic)));
                if (as_text(at(result, "status")) == "cancelled")
                    return object({{"status", text("cancelled")}, {"diagnostics", array(diagnostics)}});
                if (py_truthy(at(result, "feasible"))) {
                    Array failed;
                    auto candidate = checked_candidate(problem, at(result, "candidate"), &failed);
                    diagnostics.back() = object(with(fields(diagnostics.back()), "supportFailures", array(failed)));
                    if (candidate)
                        return object({{"status", text("feasible")}, {"candidate", object(std::move(*candidate))},
                                       {"plan", plan_value}, {"problemHash", text(fingerprint(object(problem)))},
                                       {"diagnostics", array(diagnostics)}});
                }
                if (as_text(at(result, "status")) == "error")
                    return object({{"status", text("error")}, {"diagnostics", array(diagnostics)},
                                   {"error", get_or_null(result, "error")}});
            }
            return object({{"status", text("unknown")}, {"plan", plan_value}, {"diagnostics", array(diagnostics)},
                           {"problemHash", text(fingerprint(object(problem)))}});
        } catch (const Timeout& error) {
            return object({{"status", text("unknown")}, {"error", text(error.what())}});
        } catch (const Unsupported&) {
            throw;
        } catch (const std::invalid_argument& error) {
            return object({{"status", text("error")}, {"error", text(error.what())}});
        } catch (const std::runtime_error& error) {
            return object({{"status", text("error")}, {"error", text(error.what())}});
        }
    };
    const double remaining = std::max(1e-9, search.seconds - (monotonic() - started));
    Object result = fields(search_allocations(route_value, evaluate, remaining, search.improve, search.rank_equal_cost, hooks));
    put(result, "elapsedSeconds", real(monotonic() - started));
    const std::int64_t mandatory = step_kinds(route);
    put(result, "mandatoryGroundTicks", integer_value(mandatory));
    if (py_truthy(at(result, "best"))) {
        Object best = fields(at(result, "best"));
        put(best, "groundRunTicks", integer_value(as_int(at(best, "cost")) + mandatory));
        put(best, "route", route_value);
        put(result, "best", object(std::move(best)));
    }
    return object(std::move(result));
}

}  // namespace pkc
