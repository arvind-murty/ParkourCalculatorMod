// reachability/intervals: conservative X/Z state boxes, interaction phases and
// travel radii for one movement model, and the interaction screens over them.
#pragma once

#include <cstdint>
#include <optional>

#include "pkc/value.hpp"

namespace pkc {

// The Python result without its `model` entry, which aliases the input problem.
Value reachable_envelopes(const Value& problem, const Value& y, std::optional<std::int64_t> parent_ticks);

// interaction_reachability(envelopes, interaction, refine=refine, objective=...).
// A refined screen of envelopes with a model throws Unsupported: disk.screen's
// decided-result cache stays in Python.
Value interaction_reachability(const Value& envelopes, const Value& interaction, bool refine = false);

// interaction_reachability_many(envelopes, interactions).
Value interaction_reachability_many(const Value& envelopes, const Value& interactions);

}  // namespace pkc
