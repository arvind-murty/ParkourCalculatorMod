// physics/coefficients.compile_request: the movement coefficients the
// in-process bridge returns for one bridge/compiler.coefficient_request message.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// Throws std::invalid_argument carrying the Python ValueError's message.
Value compile_coefficients(const Value& request);

}  // namespace pkc
