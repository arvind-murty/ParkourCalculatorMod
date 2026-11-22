// Normal landings and the traces every open generator builds on: movement/normal.py.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pkc/movement/generation.hpp"

namespace pkc {

// replay(initial, ticks, jumping=..., contacts=vertical, finite=..., threshold=...) without ladder or sneak.
Trace replay(const ScalarState& initial, std::int64_t ticks, bool jumping, const std::vector<VerticalContact>& vertical,
             const std::vector<ContactEvent>& finite, const Num& threshold);

std::optional<std::int64_t> first_descent(const Trace& trace, double top);

double jump_rise();                  // JUMP_RISE
std::int64_t flat_jump_ticks();      // FLAT_JUMP_TICKS

struct Approach {
    bool jumping;
    Trace trace;
};
std::vector<Approach> approaches(const GenerationContext& context);
std::vector<Approach> choice_approaches(const GenerationContext& context);

struct ChoiceParts {  // event_choice's keyword arguments
    std::vector<VerticalContact> vertical;
    std::vector<ContactEvent> finite;
    std::vector<CaseEvent> cases;
    std::vector<PositionConstraint> constraints;
    std::optional<std::string> support;
    std::string landing_phase = "post";
    std::vector<RegionChoice> region_choices;
};

std::optional<MovementChoice> event_choice(const GenerationContext& context, const std::string& kind,
                                           const ScalarState& initial, std::int64_t duration, bool jumping,
                                           ChoiceParts parts);

// named_choice: `event` becomes the choice's one case event (its type set to `kind`), contacted at input + 1.
std::optional<MovementChoice> named_choice(const GenerationContext& context, const std::string& kind,
                                           const ScalarState& initial, CaseEvent event, bool jumping,
                                           std::vector<PositionConstraint> constraints,
                                           std::vector<ContactEvent> finite, std::optional<std::string> support,
                                           std::vector<RegionChoice> region_choices);

}  // namespace pkc
