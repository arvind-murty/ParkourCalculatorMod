// Port of search/feasibility.node_identity, meaningful_choice and outcome.
#include "pkc/search/feasibility.hpp"

#include <algorithm>

#include "pkc/canonical.hpp"
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
