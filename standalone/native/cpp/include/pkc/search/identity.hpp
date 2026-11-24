// Solve scope and identity: search/identity.py.
#pragma once

#include <cstdint>
#include <string>
#include <tuple>

#include "pkc/value.hpp"

namespace pkc {

// solve_scope(scene, program, supplied): `supplied` null reads the program's solveScope. Errors throw
// std::invalid_argument with the Python's message.
Value solve_scope(const Value& scene, const Value& program, const Value& supplied);

// solve_identity(scene, program, objective, scope, route, coefficients).
std::string solve_identity(const Value& scene, const Value& program, const Value& objective, const Value& scope,
                           const Value& route, const Value& coefficients);

// validation_prefix(plan, candidate, vertical, through).
std::tuple<Value, Value, Value> validation_prefix(const Value& plan, const Value& candidate, const Value& vertical,
                                                  std::int64_t through);

}  // namespace pkc
