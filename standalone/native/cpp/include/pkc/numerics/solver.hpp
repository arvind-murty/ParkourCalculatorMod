// numerics/solver.solve: one IPOPT solve in a pkc_numerics_worker process over its JSON-lines protocol, the
// request's own seed and every returned candidate checked by the scalar validator.
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "pkc/value.hpp"

namespace pkc {

// solve(request, seconds=..., stop_on_feasible=..., cancel_event=..., on_incumbent=...) with the native worker at
// `worker`; `cancelled` is cancel_event.is_set() (empty never cancels) and an empty on_incumbent is None. A malformed
// request throws std::invalid_argument with the Python's message.
Value solve_numerical(const Value& request, std::optional<double> seconds, std::optional<bool> stop_on_feasible,
                      const std::string& worker, const std::function<bool()>& cancelled,
                      const std::function<void(const Value& candidate)>& on_incumbent);

}  // namespace pkc
