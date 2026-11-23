// Intermediate movement ordering and the comparison policy: search/comparison.py
// and search/deferrals.assess, on label dicts.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

inline constexpr std::int64_t kComparisonPolicyVersion = 1;  // comparison.POLICY_VERSION

// comparison_label(program, start=..., through=..., identity=..., mode=..., context=..., evidence=...);
// `mode` is 'ordinary' or 'turnaround'.
Value comparison_label(const Program& program, std::int64_t start, std::optional<std::int64_t> through,
                       const std::string& identity, const std::string& mode, const Object& context,
                       const Object& evidence);
Value priority_key(const Value& label);  // [T, -J, tickCost, id]
// compare_candidates(candidate, peer, context=context, incumbent=incumbent); a null peer or incumbent is None.
Value compare_candidates(const Value& candidate, const Value& peer, const Object& context, const Value& incumbent);
// deferrals.assess(label, peers, context): `peers` is the peer index dict.
Value assess(const Value& label, const Value& peers, const Object& context);
// deferrals.remember_peer(peers, label): keeps the label in the peer index; whether its entry changed.
bool remember_peer(Value& peers, const Value& label);

}  // namespace pkc
