// Node identity, choice classification and solve outcomes: search/feasibility.py.
#pragma once

#include <string>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

std::string node_identity(const Program& program, const Value& context);  // without its repairState
bool meaningful_choice(const MovementChoice& choice);
Value outcome(const Value& result, const std::string& model, const Value& policy);

}  // namespace pkc
