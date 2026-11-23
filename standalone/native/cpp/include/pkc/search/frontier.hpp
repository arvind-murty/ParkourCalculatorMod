// The movement frontier's persistent continuation: search/frontier.py.
#pragma once

#include <vector>

#include "pkc/value.hpp"

namespace pkc {

// restore_alternatives(state, extra_live): queues each held decision-node alternative whose branch died, into the
// continuation `state`; whether anything was queued.
bool restore_alternatives(Value& state, const std::vector<Value>& extra_live);

}  // namespace pkc
