// observability/turns.py: compact resume-safe observations of each decision check; they never influence decisions.
#pragma once

#include <cstdint>
#include <string>

#include "pkc/program.hpp"
#include "pkc/search/dfs_state.hpp"
#include "pkc/value.hpp"

namespace pkc {

// turns.record(state, guides, program, span=..., start=..., model=..., status=..., elapsed=..., kind=...,
// metrics=..., service=...); a null `metrics` is None.
void observe(Observations& state, const Value& guides, const Value& program, std::int64_t span, std::int64_t start,
             const Value& model, const std::string& status, const Num& elapsed, const std::string& kind,
             const Value& metrics, const QueryService& service);

// turns.replay(state, guides, ancestor, span, point, model).
void replay(Observations& state, const Value& guides, const std::string& ancestor, std::int64_t span,
            const Value& point, const Value& model);

}  // namespace pkc
