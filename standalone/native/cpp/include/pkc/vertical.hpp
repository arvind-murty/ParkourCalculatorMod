// Vertical movement: physics/vertical.py and movement/vertical_profiles.py.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pkc/program.hpp"

namespace pkc {

inline double f32(double value) { return static_cast<double>(static_cast<float>(value)); }

inline const double kStepHeight = f32(0.6);
inline const double kGravity = 0.08;
inline const double kYDrag = f32(0.98);
inline const double kClimbCarry = (0.2 - kGravity) * kYDrag;
inline const double kCap = f32(0.15);
inline const double kJumpVelocity = f32(.42);

struct VerticalContactRow {
    std::int64_t state, direction;
    Num plane;
    bool step = false;
    std::optional<std::string> movement_case;
    std::optional<Num> lower_height, higher_height, clearance;
};

struct StepResult {
    Num proposed, carry;
    bool ground;
    bool failed;
};

// vertical_step for one tick; a jump input moves at kJumpVelocity.
StepResult vertical_step(const std::vector<VerticalContactRow>& contacts, const std::vector<bool>& jumps,
                         const std::vector<bool>& sneak, const Num& threshold, std::int64_t tick, const Num& position,
                         const Num& velocity, bool on_ladder, bool grounded, double tolerance);

Num vertical_carry(const Num& movement, bool on_ladder, bool previous_horizontal_collision);

// trace_ladder_schedule(initial, ladder, sneak, hit, jump=[f32(.42) if j else None], contacts=contacts,
// stop_on_landing=False, threshold=threshold): the form append_choice traces, without target_top or
// occupied_y_bounds. Failures are not itemized; callers read only the status.
Trace trace_ladder_schedule(const ScalarState& initial, const std::vector<bool>& ladder,
                            const std::vector<bool>& sneak, const std::vector<bool>& hit,
                            const std::vector<bool>& jump, const std::vector<VerticalContact>& contacts,
                            const Num& threshold);

}  // namespace pkc
