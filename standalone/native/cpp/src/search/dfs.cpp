// Port of search/dfs.evaluate_node, commit_arrival and candidate_key, with spatial/checkpoints.retain_prefix.
#include "pkc/search/dfs.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include "../schema.hpp"
#include "../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "pkc/constraints/variants.hpp"
#include "pkc/program.hpp"
#include "pkc/search/anchors/policy.hpp"
#include "pkc/search/feasibility.hpp"
#include "pkc/search/repair.hpp"

namespace pkc {
namespace {

double monotonic() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

const Value& get_or(const Object& object, const char* key, const Value& fallback) {
    const Value* found = get(object, key);
    return found ? *found : fallback;
}

Array zeros(std::size_t n) { return Array(n, integer_value(0)); }

// The (mode, t1, t2, op, cmp, rhs) tuple commit_arrival compares rows by.
Array row_key(const Object& row) {
    Array key;
    for (const char* k : {"mode", "t1", "t2", "op", "cmp", "rhs"}) key.push_back(get_or_null(row, k));
    return key;
}

bool holds(const std::vector<Array>& keys, const Array& key) {
    return std::any_of(keys.begin(), keys.end(), [&](const Array& k) { return py_equal(k, key); });
}

// spatial/checkpoints.retain_prefix(program, best). Rows are identified as repr(sorted(row.items())) is: their
// canonical JSON keeps the same distinctions (int and float, -0.0).
Object retain_prefix(const Value& program, const Value& best_value) {
    Object result = fields(program);
    erase(result, "repairState");
    erase(result, "prefixChain");
    Object profile = fields(at(result, "profile"));
    Array retained = items(at(profile, "positionConstraints"));
    std::vector<std::string> identities;
    for (const auto& r : retained) identities.push_back(canonical_json(r));
    const Object& best = fields(best_value);
    for (const auto& row_value : items(at(fields(at(best, "plan")), "walls"))) {
        const Object& row = fields(row_value);
        if (py_equal(get_or_null(row, "mode"), text("F"))) fail("Checkpoint retention must not fix a facing");
        const Value& name = get_or(row, "name", text(""));
        if (as_text(name).rfind("checkpoint_probe:", 0) == 0) continue;
        Object chosen;
        for (const auto& [k, v] : row)
            if (k == "mode" || k == "t1" || k == "t2" || k == "op" || k == "cmp" || k == "rhs" || k == "name")
                chosen.emplace_back(k, v);
        const Value chosen_value = object(std::move(chosen));
        std::string identity = canonical_json(chosen_value);
        if (std::find(identities.begin(), identities.end(), identity) == identities.end()) {
            retained.push_back(chosen_value);
            identities.push_back(std::move(identity));
        }
    }
    put(profile, "positionConstraints", array(std::move(retained)));
    put(result, "profile", object(std::move(profile)));
    put(result, "guideSeedYaw", at(fields(at(best, "candidate")), "yaw"));
    return result;
}

// scene/route.row_tick(event, row).
Num row_tick(const Object& event, const Object& row) {
    const bool run = py_equal(get_or(row, "phase", text("departure")), text("run"));
    return add(num(at(event, run ? "landed" : "jump")), num(at(row, "offset")));
}

}  // namespace

std::string candidate_key(const Value& program) {
    Object physical = fields(program);
    erase(physical, "repairState");
    return digest(text(program_search_key(program_from_value(object(std::move(physical))))));
}

Value commit_arrival(const Value& program_value, const Value& best_value, const Value& source_program,
                     bool keep_region_choices) {
    const Object& program = fields(program_value);
    const Object& source = fields(is_null(source_program) ? program_value : source_program);
    const std::int64_t n = as_int(at(fields(at(program, "profile")), "duration"));
    const Object& source_profile = fields(at(source, "profile"));
    const Array& source_rows = items(at(source_profile, "positionConstraints"));
    const Value source_owners = get_or(source_profile, "constraintOwners", array(zeros(source_rows.size())));
    const std::int64_t committed = as_int(get_or(source, "committedThrough", integer_value(0)));
    std::vector<Array> dropped;
    for (std::size_t i = 0; i < source_rows.size() && i < items(source_owners).size(); ++i) {
        const std::int64_t owner = as_int(items(source_owners)[i]);
        if (owner > n && committed < owner) dropped.push_back(row_key(fields(source_rows[i])));
    }
    Object best = fields(best_value);
    Object plan = fields(at(best, "plan"));
    Array walls;
    for (const auto& r : items(at(plan, "walls"))) {
        const Object& row = fields(r);
        const Value& t2 = get_or_null(row, "t2");
        if (as_int(at(row, "t1")) <= n && (is_null(t2) || as_int(t2) <= n) && !holds(dropped, row_key(row)))
            walls.push_back(r);
    }
    put(plan, "walls", array(std::move(walls)));
    put(best, "plan", object(std::move(plan)));
    Object result = retain_prefix(program_value, object(std::move(best)));
    Object profile = fields(at(result, "profile"));
    const Object& old = fields(at(program, "profile"));
    const std::size_t old_rows = items(at(old, "positionConstraints")).size();
    Array owners = items(get_or(old, "constraintOwners", array(zeros(old_rows))));
    const std::size_t added = items(at(profile, "positionConstraints")).size() - old_rows;
    owners.insert(owners.end(), added, integer_value(0));
    put(profile, "constraintOwners", array(std::move(owners)));
    erase(result, "repairState");
    const Array& yaw = items(at(result, "guideSeedYaw"));
    put(result, "guideSeedYaw", array(Array(yaw.begin(), yaw.begin() + std::min<std::size_t>(n, yaw.size()))));
    if (!keep_region_choices) {
        // The solved rectangle's rows are in the plan's walls, now retained position constraints.
        const Value choices = get_or(profile, "regionChoices", array({}));
        erase(profile, "regionChoices");
        Array open;
        for (const auto& c : items(choices))
            if (as_int(at(fields(c), "offset")) > n) open.push_back(c);
        if (!open.empty()) put(profile, "regionChoices", array(std::move(open)));
    }
    put(result, "profile", object(std::move(profile)));
    return object(std::move(result));
}

std::optional<NodeEvaluation> evaluate_node(const Value& scene, const Value& program_in, const NodeHooks& hooks,
                                            const NodeOptions& options) {
    Object program = fields(program_in);
    const std::string identity = as_text(at(program, "decisionNode"));
    const Value depth = at(program, "decisionDepth");
    auto unchanged = [&](const char* status) {
        return NodeEvaluation{object({{"status", text(status)}, {"program", object(program)}}), true};
    };
    if (!solve_due(program_in, options.solve_interval)) return unchanged("untested");
    if (py_truthy(options.saved)) {
        const Object& saved = fields(options.saved);
        const Value& status = get_or_null(saved, "status");
        if (py_equal(status, text("oracle_rejected"))) return unchanged("oracle_rejected");
        if (py_equal(status, text("feasible")))
            // The saved program carries physical rows and a free-yaw seed.
            return NodeEvaluation{object({{"status", text("feasible")}, {"program", at(saved, "program")},
                                          {"alternatives", get_or(saved, "alternatives", array({}))}}),
                                  false};
        const Value& repair = get_or_null(saved, "repairState");
        if (!is_null(repair)) {
            put(program, "repairState", repair);
            hooks.assign(repair);
        }
    }
    // Climb every side family and keep the best, as at a group end: families are compared here.
    const Value policy = policy_spec(object({{"purpose", text("node")}, {"stopOnFeasible", boolean(false)},
                                             {"rankEqualCost", boolean(true)}, {"oracleRejection", boolean(true)},
                                             {"numerical", text(options.numerical)}}),
                                     true);
    const double t = monotonic();
    const Value prepared = hooks.prepare(object(program));
    const double prepared_at = monotonic();
    if (!is_null(prepared)) {
        const Object& route = fields(at(fields(prepared), "route"));
        for (const char* key : {"passages", "hitEvents", "ladderEvents"})
            if (py_truthy(get_or_null(route, key))) return std::nullopt;
    }
    hooks.begin();
    NodeSolve solve = hooks.begin_solve();
    Value result_value;
    if (is_null(prepared)) {
        result_value = object({{"status", text("unknown")}, {"termination", text("vertical_schedule")}, {"best", kNull},
                               {"attempts", array({})}, {"contactAttempts", array({})},
                               {"work", object({{"solverCalls", integer_value(0)}})},
                               {"fullSceneValidated", boolean(false)}});
    } else {
        Object route = fields(at(fields(prepared), "route"));
        if (!is_null(solve.objective)) {
            put(route, "objective", object({{"axis", text("X")}, {"sense", text("MAX")}}));
            put(route, "guideObjective", solve.objective);
        }
        EvaluateHooks evaluate = solve.hooks;
        if (hooks.progress)
            evaluate.on_progress = [&](const Value& repair) {
                put(program, "repairState", repair);
                hooks.progress(repair);
            };
        EvaluateOptions eo;
        eo.seconds = solve.seconds;
        eo.candidate_seconds = options.candidate_seconds;
        eo.policy = policy;
        eo.adapter_identity = solve.adapter_identity;
        eo.node_query = true;
        eo.retry_unresolved = options.retry_unresolved;
        eo.climb_families = true;
        eo.timings = {{"programCompilation", real(prepared_at - t)}};
        result_value = evaluate_prepared(scene, object(program), object(std::move(route)),
                                         at(fields(prepared), "profiles"), at(fields(prepared), "vertical"), evaluate, eo);
    }
    const Object& result = fields(result_value);
    const Value& result_repair = get_or_null(result, "repairState");
    if (!hooks.progress && !is_null(result_repair)) {
        // Nothing saves progress, so the repair published nothing: leave its final state where progress would.
        put(program, "repairState", result_repair);
        hooks.settle(result_repair);
    }
    const Value& best = get_or_null(result, "best");
    Object query = fields(get_or(result, "nodeFeasibility",
                                 object({{"status", text(py_truthy(best) ? "feasible" : "suspended")},
                                         {"model", text(identity)}})));
    const Value repair_value = py_truthy(result_repair) ? result_repair : object({});
    const Object& repair = fields(repair_value);
    const Value& generation = get_or_null(repair, "constraintGeneration");
    const Value* exhausted = py_truthy(generation) ? get(fields(generation), "exhausted") : nullptr;
    if (py_equal(at(query, "status"), text("feasible")) &&
        (py_truthy(get_or_null(repair, "pending")) || py_truthy(get_or_null(repair, "inFlight")) ||
         py_truthy(get_or_null(repair, "repairSources")) ||
         (exhausted && std::holds_alternative<bool>(exhausted->data) && !std::get<bool>(exhausted->data))))
        // An interrupted climb has a best but has not decided the node: resume it exactly first.
        put(query, "status", text("suspended"));
    const std::string status = as_text(at(query, "status"));
    hooks.observe(get_or_null(query, "model"), status, monotonic() - t, at(result, "work"));
    Array alternatives;
    bool input_program = true;
    Value record_repair;
    if (status == "feasible") {
        const Object& profile = fields(at(program, "profile"));
        const std::int64_t duration = as_int(at(profile, "duration"));
        const std::int64_t settled = std::max(settled_through(profile, duration),
                                              as_int(get_or(program, "settledThrough", integer_value(0))));
        const Value source = object(program);
        auto branch_program = [&](const Value& chosen) {
            // Retain selected physical walls; never endpoint/velocity/yaw equalities. A decision check makes an
            // intermediate choice for this branch: the exact rows of every stretch settled (closed) at the node stay,
            // so later checks never re-decide them; the open stretch's rows go.
            Object witness = fields(chosen);
            Object plan = fields(at(witness, "plan"));
            const Array& events = items(at(plan, "events"));
            std::vector<Array> unsettled;
            for (const auto& r : items(get_or(witness, "alternativeRows", array({})))) {
                const Object& row = fields(r);
                const std::int64_t leg = as_int(at(row, "leg"));
                if (leg >= static_cast<std::int64_t>(events.size())) continue;
                const Num tick = row_tick(fields(events[leg]), row);
                if (tick.value >= settled)
                    unsettled.push_back({at(row, "mode"), value(tick), at(row, "cmp"), at(row, "rhs")});
            }
            Array walls;
            for (const auto& w : items(at(plan, "walls"))) {
                const Object& wall = fields(w);
                if (!holds(unsettled, {at(wall, "mode"), at(wall, "t1"), at(wall, "cmp"), at(wall, "rhs")}))
                    walls.push_back(w);
            }
            put(plan, "walls", array(std::move(walls)));
            put(witness, "plan", object(std::move(plan)));
            Object kept = fields(commit_arrival(source, object(std::move(witness)), kNull, true));
            Object kept_profile = fields(at(kept, "profile"));
            const Value choices = get_or(kept_profile, "regionChoices", array({}));
            erase(kept_profile, "regionChoices");
            Array regions;
            for (const auto& c : items(choices))
                if (as_int(at(fields(c), "offset")) >= settled) regions.push_back(c);
            if (!regions.empty()) put(kept_profile, "regionChoices", array(std::move(regions)));
            put(kept, "profile", object(std::move(kept_profile)));
            put(kept, "settledThrough", integer_value(settled));
            for (const char* k : {"decisionNode", "decisionParent", "decisionDepth", "priorityTurnaround", "restoredRun"})
                if (const Value* v = get(program, k)) put(kept, k, *v);
            put(kept, "lastSolvedDepth", depth);
            const Object& candidate = fields(at(fields(chosen), "candidate"));
            const Value& point = items(at(candidate, "positions")).back();
            put(kept, "decisionPoint", point);
            put(kept, "decisionVelocity", items(at(candidate, "velocity")).back());
            put(kept, "decisionPointTick", integer_value(duration));
            put(kept, "decisionPointSource", text("validated_node"));
            put(kept, "decisionTurnaround",
                at(fields(classify(options.guide_plan, options.span, num(items(point).at(0)).value,
                                   num(items(point).at(1)).value)),
                   "isTurnaround"));
            return object(std::move(kept));
        };
        const Value updated = branch_program(best);
        // Other branch choices and close switch timings wait, best first, until this branch dies.
        record_repair = get_or(result, "repairState", object({}));
        const Object& held_repair = fields(record_repair);
        std::vector<std::string> known{candidate_key(updated)};
        const Value objective = at(fields(at(fields(best), "route")), "objective");
        for (const auto& variant : node_alternatives(items(get_or(held_repair, "acceptedVariants", array({}))),
                                                     get_or_null(held_repair, "incumbent"), objective)) {
            Object held = fields(branch_program(at(fields(variant), "best")));
            std::string key = candidate_key(object(held));
            if (std::find(known.begin(), known.end(), key) != known.end()) continue;
            known.push_back(std::move(key));
            put(held, "decisionNode", text(digest(array({text(identity), text("alternative"),
                                                         integer_value(static_cast<std::int64_t>(alternatives.size()))}))));
            put(held, "alternativeOf", text(identity));
            alternatives.push_back(object(std::move(held)));
        }
        hooks.replay(at(fields(at(fields(best), "candidate")), "positions"), get_or_null(query, "model"));
        program = fields(updated);
        input_program = false;
    } else if (!is_null(result_repair)) {
        put(program, "repairState", result_repair);
        hooks.assign(result_repair);
    }
    Value recovery{nullptr};
    const Value& restored = get_or_null(program, "restoredRun");
    if (py_truthy(restored) && py_equal(get_or_null(fields(restored), "frame"), options.frame) &&
        (status == "oracle_rejected" || status == "exhausted"))
        recovery = object({{"reason", text("restored_internal_preparation_failed")}, {"candidate", text(identity)},
                           {"model", get_or_null(query, "model")}});
    const Value parent = get_or_null(program, "decisionParent");
    Object record{{"status", text(status)}, {"parent", parent}, {"query", object(query)}, {"depth", depth},
                  {"lastSolvedDepth", get_or(program, "lastSolvedDepth", integer_value(0))}, {"program", object(program)}};
    if (!alternatives.empty()) record.emplace_back("alternatives", array(alternatives));
    // The committed program drops its repair; the solve report reads the node's alternatives from it.
    if (status == "feasible") record.emplace_back("repairState", record_repair);
    hooks.store(identity, object(std::move(record)), object(with(with(query, "parent", parent), "depth", depth)), recovery);
    const bool defer = status == "suspended" && unresolved_only(get_or(program, "repairState", object({})));
    return NodeEvaluation{object({{"status", text(status)}, {"program", object(program)},
                                  {"alternatives", array(std::move(alternatives))}, {"deferUnknown", boolean(defer)}}),
                          input_program};
}

}  // namespace pkc
