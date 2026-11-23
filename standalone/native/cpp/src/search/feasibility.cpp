// Port of search/feasibility.policy_spec, node_identity, meaningful_choice, outcome, solve_due and unresolved_only.
#include "pkc/search/feasibility.hpp"

#include <algorithm>

#include "pkc/canonical.hpp"
#include "../schema.hpp"
#include "../value_ops.hpp"

namespace pkc {

std::string node_identity(const Program& program, const Value& context) {
    // Repair cursors, warm yaws, observations and mutable queue state do not identify a physical node.
    Program physical = program;
    physical.repair_state.reset();
    return digest(object({{"version", integer_value(1)}, {"program", text(program_search_key(physical))}, {"context", context}}));
}

bool meaningful_choice(const MovementChoice& choice) {
    const Profile& p = choice.profile;
    if (std::find(p.jump.begin(), p.jump.end(), true) != p.jump.end() || !p.case_events.empty()) return true;
    return std::any_of(p.contact_events.begin(), p.contact_events.end(),
                       [&](const ContactEvent& e) { return e.axis != "Y" || !p.grounded.at(e.offset); });
}

Value policy_spec(const Value& value, bool first_witness) {
    Object defaults{{"version", integer_value(1)}, {"purpose", text(first_witness ? "anchor" : "terminal")},
                    {"stopOnFeasible", boolean(first_witness)}, {"rankEqualCost", boolean(!first_witness)},
                    {"oracleRejection", boolean(false)}, {"numerical", text("ipopt")}};
    if (!is_null(value)) {
        for (const auto& [key, unused] : fields(value))
            if (!get(defaults, key)) fail("Unknown solve policy field");
        for (const auto& [key, field] : fields(value)) put(defaults, key, field);
    }
    auto one_of = [&](const char* key, std::initializer_list<const char*> names) {
        return std::any_of(names.begin(), names.end(), [&](const char* n) { return py_equal(at(defaults, key), text(n)); });
    };
    if (!py_equal(at(defaults, "version"), integer_value(1)) || !one_of("purpose", {"node", "anchor", "terminal"}) ||
        !one_of("numerical", {"ipopt", "relaxed"}))
        fail("Unsupported solve policy");
    for (const char* key : {"stopOnFeasible", "rankEqualCost", "oracleRejection"})
        if (!std::holds_alternative<bool>(at(defaults, key).data)) fail("Solve policy flags must be boolean");
    if (py_equal(at(defaults, "purpose"), text("terminal")) && std::get<bool>(at(defaults, "stopOnFeasible").data))
        fail("Terminal optimization cannot stop at the first feasible seed");
    return object(std::move(defaults));
}

bool solve_due(const Value& program_value, std::int64_t interval) {
    const Object& program = fields(program_value);
    const Value* depth = get(program, "decisionDepth");
    const Value* solved = get(program, "lastSolvedDepth");
    return (depth ? as_int(*depth) : 0) - (solved ? as_int(*solved) : 0) >= interval;
}

bool unresolved_only(const Value& repair_value) {
    // No runnable repair remains unless the caller explicitly retries unknowns.
    const Object& repair = fields(repair_value);
    const Value& generation = get_or_null(repair, "constraintGeneration");
    return py_truthy(get_or_null(repair, "unresolved")) && !py_truthy(get_or_null(repair, "pending")) &&
           !py_truthy(get_or_null(repair, "repairSources")) && !py_truthy(get_or_null(repair, "inFlight")) &&
           (is_null(generation) || py_truthy(get_or_null(fields(generation), "exhausted")));
}

Value outcome(const Value& result_value, const std::string& model, const Value& policy) {
    const Object& result = fields(result_value);
    const Value* repair_value = get(result, "repairState");
    const Object& repair = repair_value ? fields(*repair_value) : Object{};
    const Value* completed = get(repair, "completed");
    const Array complete = completed ? items(*completed) : Array{};
    const Value& generation = get_or_null(repair, "constraintGeneration");
    const bool settled = !py_truthy(get_or_null(repair, "pending")) && !py_truthy(get_or_null(repair, "repairSources")) &&
                         !py_truthy(get_or_null(repair, "inFlight")) && !py_truthy(get_or_null(repair, "unresolved"));
    const bool generated = is_null(generation) || py_truthy(get_or_null(fields(generation), "exhausted"));
    auto rejected = [](const Value& c) {
        return py_truthy(get_or_null(fields(c), "oracleRejected")) || py_truthy(get_or_null(fields(c), "proven"));
    };
    std::string status;
    if (!is_null(get_or_null(result, "best"))) {
        status = "feasible";
    } else if (py_equal(get_or_null(result, "status"), text("error"))) {
        status = "error";
    } else if (py_truthy(at(fields(policy), "oracleRejection")) && !complete.empty() && settled && generated &&
               std::all_of(complete.begin(), complete.end(), rejected)) {
        status = "oracle_rejected";
    } else if ((!complete.empty() || (!is_null(generation) && py_truthy(get_or_null(fields(generation), "exhausted")))) &&
               settled && generated) {
        // Exhausting repair/physical alternatives is a backtrack, not a numerical infeasibility label for this node or
        // its ancestors. A fully screened-out generator can exhaust before emitting any solve.
        status = "exhausted";
    } else {
        status = "suspended";
    }
    Array alternatives;
    for (const auto& c : complete)
        if (rejected(c)) alternatives.push_back(at(fields(c), "branch"));
    return object({{"version", integer_value(1)}, {"status", text(status)}, {"model", text(model)}, {"policy", policy},
                   {"termination", get_or_null(result, "termination")}, {"rejectedAlternatives", array(std::move(alternatives))}});
}

}  // namespace pkc
