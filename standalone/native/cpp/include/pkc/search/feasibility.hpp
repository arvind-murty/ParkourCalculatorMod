// Solve policies, node identity, choice classification and solve outcomes: search/feasibility.py.
#pragma once

#include <cstdint>
#include <string>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

std::string node_identity(const Program& program, const Value& context);  // without its repairState
bool meaningful_choice(const MovementChoice& choice);
Value outcome(const Value& result, const std::string& model, const Value& policy);
// policy_spec(value, first_witness=...): `value` null is None. Errors throw std::invalid_argument with the Python's message.
Value policy_spec(const Value& value, bool first_witness);
bool solve_due(const Value& program, std::int64_t interval);

}  // namespace pkc
