// The interaction gate's screen on a prepared program (reachability/service.py):
// coefficient request, model, envelopes and the required-contact proof, kept
// native so a screen returns only its proof.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

inline constexpr std::int64_t kProofPolicy = 5;  // service.PROOF_POLICY

// bridge/compiler.coefficient_request for a gate_prepare result.
Value coefficient_request(const Value& prepared);

// InteractionGate.analyze_model's model for a preparation ({route, plan, vertical}) and its coefficients.
Value analysis_model(const Value& prepared, const Value& coefficient, bool open_terminal);
// InteractionGate.analyze_model's build for a prepared program: `key` is the
// analysis key the gate computed from the preparation key.
struct GateAnalysis {
    std::string key;
    Value model, envelopes, coefficient, y;  // envelopes carry modelKey; Python adds their model alias
};

GateAnalysis analyze_prepared(const Value& prepared, const Value& coefficient, bool open_terminal,
                              std::optional<std::int64_t> parent_ticks, std::string key);

// InteractionGate.required for the program the analysis was prepared from: a
// rejection proof, or null.
Value required_proof(const GateAnalysis& analysis, const Program& program);

}  // namespace pkc
