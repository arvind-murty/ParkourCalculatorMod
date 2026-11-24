// Numerical requests from scheduled routes: numerics/builders.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// numerical_problem(route, plan, coefficients, parent): the prepared request for a
// scheduled plan; `parent` may be null.
Value numerical_problem(const Value& route, const Value& plan, const Value& coefficients, const Value* parent);

}  // namespace pkc
