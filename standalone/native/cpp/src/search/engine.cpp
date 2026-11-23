// Port of search/engine.evaluate_program after the gate's preparation, segment_solids and numerical_counts.
#include "pkc/search/engine.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "../schema.hpp"
#include "../value_ops.hpp"
#include "pkc/constraints/contacts.hpp"
#include "pkc/constraints/variants.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/scene/route.hpp"
#include "pkc/search/anchors/policy.hpp"
#include "pkc/search/feasibility.hpp"
#include "pkc/search/goals.hpp"
#include "pkc/search/identity.hpp"
#include "pkc/search/repair.hpp"
#include "pkc/validation/numerical.hpp"

namespace pkc {
namespace {

double monotonic() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

const Value& get_or(const Object& object, const char* key, const Value& fallback) {
    const Value* found = get(object, key);
    return found ? *found : fallback;
}

// segment_solids(scene, program, profile, through, gate): the solids each landing stretch's launch face reaches.
std::vector<std::pair<std::int64_t, std::vector<std::int64_t>>> segment_solids(
    const Value& scene, const Value& program, const Object& profile, std::int64_t through,
    const std::function<Value(const std::string&)>& reachable_faces) {
    std::vector<std::pair<std::int64_t, std::vector<std::int64_t>>> result;
    const auto segments = landing_segments(profile, through);
    if (segments.empty()) return result;
    const AnchorScene anchors(scene);
    const Program typed = program_from_value(program);
    for (const auto& [begin, end] : segments) {
        const auto found = anchors.launch(typed, begin, end);
        if (!found) continue;
        const Value table = reachable_faces(found->second);
        if (end - found->first > as_int(at(fields(table), "ticks"))) continue;
        std::vector<std::int64_t> solids;
        for (const auto& s : items(at(fields(table), "solids"))) solids.push_back(as_int(s));
        std::sort(solids.begin(), solids.end());
        result.emplace_back(begin, std::move(solids));  // landing_segments ascend, so the list stays sorted by begin
    }
    return result;
}

}  // namespace

Value numerical_counts(const Array& diagnostics) {
    Object counts;  // a Counter: a key exists once incremented, even by zero
    auto bump = [&](const std::string& key, std::int64_t by) {
        const Value* held = get(counts, key);
        put(counts, key, integer_value((held ? as_int(*held) : 0) + by));
    };
    for (const auto& d_value : diagnostics) {
        const Object& d = fields(d_value);
        const bool ipopt = py_equal(get_or(d, "backend", text("ipopt")), text("ipopt"));
        const Value& launched = get_or_null(d, "workerLaunched");
        const auto* launched_flag = std::get_if<bool>(&launched.data);
        bump("workerLaunches", launched_flag && *launched_flag);
        const Value& confirmation = get_or_null(d, "confirmation");
        const bool feasible = py_equal(get_or_null(d, "status"), text("feasible"));
        bump("feasibleSeedReturns", ipopt && is_null(confirmation) && launched_flag && !*launched_flag && feasible);
        bump("conicCandidates", !ipopt && feasible);
        if (!is_null(confirmation)) {
            const Object& c = fields(confirmation);
            bump("confirmations", 1);
            bump("confirmationIpoptCalls", !py_truthy(at(c, "cached")));
            double material = 0;
            if (!numeric(at(c, "material"), material)) fail("material must be a number");
            bump("materialGaps", static_cast<std::int64_t>(material));
        }
        const Value& fallback = get_or_null(d, "fallback");
        if (py_truthy(fallback)) {
            bump("relaxedFallbacks", 1);
            bump("relaxedFallback:" + as_text(fallback), 1);
        }
    }
    return object(std::move(counts));
}

Value evaluate_prepared(const Value& scene_value, const Value& program_value, const Value& route_value,
                        const Value& profiles, const Value& vertical_value, const EvaluateHooks& hooks,
                        const EvaluateOptions& options) {
    Object timings = options.timings;
    double marked = monotonic();
    auto mark = [&](const char* name) {
        const double now = monotonic();
        put(timings, name, real(now - marked));
        marked = now;
    };
    const Object& scene = fields(scene_value);
    const Object& program = fields(program_value);
    const Object& route = fields(route_value);
    const Object& vertical = fields(vertical_value);
    const Value& policy = options.policy;
    const Value scope_value = solve_scope(scene_value, program_value, options.supplied_scope);
    const Object& scope = fields(scope_value);
    const std::int64_t through = as_int(at(scope, "through"));
    Array owners;
    for (const auto& b : items(at(scene, "blocks"))) owners.push_back(at(fields(b), "sourceBlock"));

    Object plan = fields(schedule(route_value, {0}));
    if (!py_truthy(at(vertical, "feasible")))
        return object({{"status", text("unknown")}, {"termination", text("vertical_schedule")}, {"best", kNull},
                       {"attempts", array({})}, {"contactAttempts", array({})}, {"work", hooks.work_summary()},
                       {"solveScope", scope_value}, {"fullSceneValidated", boolean(false)}});
    put(plan, "vertical", object(with(with(fields(at(vertical, "vertical")), "referencePosition", at(vertical, "y")),
                                      "referenceVelocity", at(vertical, "vy"))));
    const Value plan_value = object(plan);
    mark("scopeAndVerticalPreparation");
    const Value analysis = hooks.analyze_model(route_value, plan_value, vertical_value, scope_value);
    const Value& coefficients = at(fields(analysis), "coefficient");
    mark("coefficientCompilation");
    const Value& objective = get_or(route, "guideObjective", at(route, "objective"));
    const Object& initial = fields(at(scene, "initial"));
    auto planar = [](const Value& v) { return array({items(v).at(0), items(v).at(2)}); };
    Object model = fields(coefficients);
    put(model, "version", integer_value(1));
    put(model, "ticks", at(plan, "ticks"));
    put(model, "walls", at(plan, "walls"));
    put(model, "contacts", at(plan, "contacts"));
    put(model, "position", planar(at(initial, "position")));
    put(model, "velocity", planar(at(initial, "velocity")));
    put(model, "threshold", get_or(route, "physicsThreshold", real(.005)));
    put(model, "vertical", at(vertical, "vertical"));
    put(model, "objective", object(with(fields(objective), "tick", at(plan, "ticks"))));
    const Value& envelopes = at(fields(analysis), "envelopes");
    mark("reachability");
    const Value reference_yaw = initial_guess(route_value, plan_value, nullptr);
    const Value reference =
        at(fields(validate(prepare_problem(object(with(model, "yaw", reference_yaw))), reference_yaw)), "positions");
    const Value walk_offs_value = get_or(program, "walkOffs", array({}));
    const Array& walk_offs = items(walk_offs_value);
    // A carried walk-off face selection replaces the generic floor checks on its support at its tick.
    Array checks;
    const Array& solid_indices = items(at(scope, "solidIndices"));
    for (const auto& c_value : floor_checks(scene_value, {as_text(at(scene, "startBlock")), as_text(at(scene, "goalBlock"))},
                                            items(profiles), &envelopes)) {
        const Object& c = fields(c_value);
        const Value& solid = at(fields(at(c, "block")), "solidIndex");
        if (!contains(solid_indices, solid) || as_int(at(c, "offset")) >= through) continue;
        const bool replaced = std::any_of(walk_offs.begin(), walk_offs.end(), [&](const Value& w) {
            return py_equal(at(c, "offset"), at(fields(w), "offset")) && contains(items(at(fields(w), "solids")), solid);
        });
        if (!replaced) checks.push_back(c_value);
    }
    const std::string identity = solve_identity(
        scene_value, program_value, objective, scope_value,
        object({{"adapter", options.adapter_identity}, {"policy", policy}, {"nodeQuery", boolean(options.node_query)}}),
        coefficients);
    const Value saved_repair = get_or(program, "repairState", object({}));
    const Value saved_context = get_or(fields(saved_repair), "context", object({}));
    const Value& saved_scope = get_or_null(fields(saved_context), "scope");
    const Value saved_inner = py_truthy(saved_scope) ? get_or(fields(saved_scope), "scope", object({})) : object({});
    const bool same_policy = py_equal(get_or_null(fields(saved_inner), "solvePolicy"), policy);
    const Value& repair_route = is_null(options.final_route) ? route_value : options.final_route;

    VariantInputs inputs;
    inputs.route = route_value;
    inputs.profile = items(profiles).at(0);
    inputs.geometry = at(scene, "collisionScene");
    inputs.envelopes = envelopes;
    inputs.scope = scope_value;
    inputs.checks = checks;
    inputs.walk_offs = walk_offs;
    inputs.reference = reference;
    inputs.owners = array(owners);
    inputs.objective = objective;
    inputs.settled = as_int(get_or(program, "settledThrough", integer_value(0)));
    inputs.segment_solids = segment_solids(scene_value, program_value, fields(inputs.profile), through,
                                           hooks.reachable_faces);
    inputs.check = hooks.check_generation;
    // Conflict-driven: obstacles join the generator only once a solve hits them.
    ConstraintVariants variants(std::move(inputs), Obstacles{},
                                same_policy ? get_or_null(fields(saved_repair), "constraintGeneration") : kNull);
    mark("eligibilityAndVariantConstruction");

    RepairOptions repair;
    repair.seconds = options.seconds;
    repair.candidate_seconds = options.candidate_seconds;
    repair.retries = 0;
    repair.improve = true;
    repair.rank_equal_cost = py_truthy(at(fields(policy), "rankEqualCost"));
    repair.optimize_goal = true;
    repair.stop_on_feasible = py_truthy(at(fields(policy), "stopOnFeasible"));
    repair.oracle_rejection = py_truthy(at(fields(policy), "oracleRejection"));
    repair.climb_families = options.climb_families;
    repair.retry_unresolved = options.retry_unresolved;
    repair.geometry = at(scene, "collisionScene");
    repair.solid_blocks = array(owners);
    repair.resume_state = get_or_null(program, "repairState");
    repair.repair_scope = object({{"solveIdentity", text(identity)}, {"scope", scope_value}, {"solvePolicy", policy},
                                  {"assemblyVersion", integer_value(13)}});
    repair.validation_scope = scope_value;
    repair.numerical = hooks.numerical;
    repair.confirm = hooks.confirm;
    repair.on_progress = hooks.on_progress;
    repair.candidate_filter = [&](const Value& plan_in, const Value& candidate, const Value& full_vertical) {
        const auto [p, c, v] = validation_prefix(plan_in, candidate, full_vertical, through);
        // Anchor search always validates a partial corridor: its frames see only their own goal.
        const Value verdict = hooks.candidate(p, c, v, scope_value);
        if (py_truthy(at(fields(verdict), "accepted")) && !is_null(options.required_landing) &&
            !landing_satisfied(scene_value, as_text(options.required_landing), full_vertical))
            return object(with(with(fields(verdict), "accepted", boolean(false)), "reason",
                               text("spatial_goal_not_satisfied")));
        return verdict;
    };
    // Finalization re-solves a relaxed witness's own branch route with IPOPT.
    Object result = fields(solve_contact_branches(repair_route, profiles, array(checks), hooks.solve, repair,
                                                  is_null(options.final_route) ? &variants : nullptr));
    mark("physicalSearchAndValidation");
    const Value generation_value = variants.state();
    const Object& generation = fields(generation_value);
    Array diagnostics;
    const Value attempts = get_or(result, "attempts", array({}));
    for (const auto& a : items(attempts)) {
        const Value listed = get_or(fields(a), "diagnostics", array({}));
        for (const auto& d : items(listed)) diagnostics.push_back(d);
    }
    hooks.record_counts(numerical_counts(diagnostics));
    for (const auto& [key, seconds] : fields(at(generation, "timings"))) put(timings, key, seconds);
    put(result, "timings", object(std::move(timings)));
    const Value best = at(result, "best");
    const bool complete = as_text(at(scope, "purpose")) == "complete";
    if (!is_null(best) && complete) hooks.accepted(best);
    put(result, "work", hooks.work_summary());
    const Value feasibility = outcome(object(result), identity, policy);
    put(result, "solveScope", scope_value);
    put(result, "solveIdentity", text(identity));
    put(result, "solvePolicy", policy);
    put(result, "objectiveMode", text("configured"));
    put(result, "nodeFeasibility", feasibility);
    put(result, "fullSceneValidated", boolean(!is_null(best) && complete));
    put(result, "reachability", object({{"excludedSolids", at(generation, "exclusions")},
                                        {"interactionGroups", integer_value(static_cast<std::int64_t>(
                                                                  items(at(generation, "items")).size()))}}));
    if (!is_null(best)) put(result, "best", object(with(fields(best), "validationScope", scope_value)));
    return object(std::move(result));
}

}  // namespace pkc
