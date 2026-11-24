// The worker's phase schedule: numerics/continuation.py.
#pragma once

#include <functional>
#include <string>
#include <utility>

#include "pkc/value.hpp"

namespace pkc {

// phase(problem, yaw, pattern, seconds, stop, on_candidate, kind, seed) -> (yaw, IPOPT status).
using Phase = std::function<std::pair<Value, std::string>(const Value& problem, const Value& yaw, const Value& pattern,
                                                          double seconds, bool stop,
                                                          const std::function<bool(const Value&)>& on_candidate,
                                                          const std::string& kind, const std::string& seed)>;

// run(problem, phase, infer, emit, clock): `infer(yaw)` is the inertia pattern the
// yaw's forward replay chooses; `emit` receives each improving candidate message.
// Returns the last phase status.
std::string run(const Value& problem, const Phase& phase, const std::function<Value(const Value&)>& infer,
                const std::function<void(const Value&)>& emit, const std::function<double()>& clock);

}  // namespace pkc
