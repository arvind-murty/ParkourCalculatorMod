// Continuous request checks: numerics/model.py.
#pragma once

#include <string>

#include "pkc/value.hpp"

namespace pkc {

// prepare(request): the checked request with the Python's defaults filled in,
// in its key order. Errors throw std::invalid_argument with the Python's message.
Value prepare_problem(const Value& request);

// fingerprint(problem): SHA-256 of the problem without its run settings.
std::string fingerprint(const Value& problem);

}  // namespace pkc
