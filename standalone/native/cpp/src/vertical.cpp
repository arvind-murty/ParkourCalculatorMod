// Port of physics/vertical.py and movement/vertical_profiles.py. Arithmetic
// that reaches a trace uses Num so int/float kinds match Python's.
#include "pkc/vertical.hpp"

#include <cmath>
#include <map>

#include "value_ops.hpp"

namespace pkc {
namespace {

bool same(const std::optional<Num>& a, const std::optional<Num>& b) {
    return a.has_value() == b.has_value() && (!a || a->value == b->value);
}

// contact_combinations.merge_contacts([dict(c, axis=1) for c in contacts]): one
// contact per state, sorted by state; the first of equal contacts is kept.
std::vector<VerticalContact> merge_vertical(const std::vector<VerticalContact>& contacts) {
    std::map<std::int64_t, const VerticalContact*> merged;
    for (const auto& contact : contacts) {
        auto [found, inserted] = merged.try_emplace(contact.state, &contact);
        if (inserted) continue;
        const VerticalContact& previous = *found->second;
        if (previous.plane.value != contact.plane.value || previous.direction != contact.direction ||
            previous.movement_case != contact.movement_case || !same(previous.lower_height, contact.lower_height) ||
            !same(previous.higher_height, contact.higher_height) || !same(previous.clearance, contact.clearance))
            fail("Conflicting contacts on one input axis");
    }
    std::vector<VerticalContact> result;
    result.reserve(merged.size());
    for (const auto& [state, contact] : merged) result.push_back(*contact);
    return result;
}

}  // namespace

StepResult vertical_step(const std::vector<VerticalContactRow>& contacts, const std::vector<bool>& jumps,
                         const std::vector<bool>& sneak, const Num& threshold, std::int64_t tick, const Num& position,
                         const Num& velocity, bool on_ladder, bool grounded, double tolerance) {
    Num movement = std::abs(velocity.value) < threshold.value ? flt(0.0) : velocity;
    bool jumping = jumps[tick];
    if (jumping) movement = flt(kJumpVelocity);
    if (on_ladder) {
        if (-0.15 > movement.value) movement = flt(-0.15);
        if (sneak[tick] && movement.value < 0) movement = flt(0.0);
    }
    Num proposed = add(position, movement), carry = movement;
    bool ground = false, failed = jumping && !grounded;
    for (const auto& contact : contacts) {
        if (contact.state != tick + 1) continue;
        const Num& plane = contact.plane;
        if (contact.movement_case) {
            const std::string& kind = *contact.movement_case;
            double delta = sub(plane, position).value;
            double lower = contact.lower_height ? contact.lower_height->value : position.value;
            double upper = contact.higher_height ? contact.higher_height->value : plane.value;
            double m = movement.value, y = position.value;
            bool valid = false;
            if (kind == "normal_step") {
                valid = grounded && !jumping && 0 < delta && delta <= kStepHeight + tolerance;
            } else if (kind == "slab_boost") {
                valid = grounded && jumping && m < delta && delta <= kStepHeight + tolerance;
            } else if (kind == "blip" || kind == "blip_up" || kind == "corner_blip_up") {
                valid = !grounded && m < 0 && y >= lower && proposed.value < lower;
                if (kind == "blip") {
                    valid = valid && 0 <= delta && delta <= kStepHeight + tolerance;
                } else {
                    valid = valid && plane.value == y;
                    double gap = upper - y;
                    valid = valid && (kind == "blip_up" ? y > upper : 0 <= gap && gap <= kStepHeight + tolerance);
                }
            } else if (kind == "corner_jump_cancel") {
                double gap = upper - y;
                valid = grounded && jumping && kJumpVelocity < gap && gap <= kStepHeight + tolerance &&
                        plane.value == y;
            } else if (kind == "grind") {
                double clearance = contact.clearance ? contact.clearance->value : -1;
                valid = grounded && jumping && 0 <= clearance && clearance < m && plane.value == y;
            }
            if (valid) {
                proposed = plane;
                carry = flt(0.0);
                ground = kind == "normal_step" || kind == "blip" || kind == "blip_up" || kind == "corner_blip_up";
            } else {
                failed = true;
            }
        } else if (contact.step) {
            double rise = sub(plane, position).value;
            bool valid = (grounded || movement.value < 0) && movement.value < rise && 0 < rise &&
                         rise <= kStepHeight + tolerance;
            if (valid) {
                proposed = plane;
                carry = flt(0.0);
                ground = movement.value < 0;
            } else {
                failed = true;
            }
        } else {
            double direction = static_cast<double>(contact.direction);
            double approach = direction * sub(position, plane).value;
            double reach = direction * sub(proposed, plane).value;
            if (approach > tolerance || reach < 0) {
                failed = true;
            } else {
                proposed = plane;
                carry = flt(0.0);
                ground = direction < 0 && movement.value < 0;
            }
        }
    }
    if (!std::isfinite(proposed.value) || !std::isfinite(carry.value)) fail("Non-finite vertical replay");
    return {proposed, carry, ground, failed};
}

Num vertical_carry(const Num& movement, bool on_ladder, bool previous_horizontal_collision) {
    if (on_ladder && previous_horizontal_collision) return flt(kClimbCarry);
    return mul(sub(movement, flt(kGravity)), flt(kYDrag));
}

Trace trace_ladder_schedule(const ScalarState& initial, const std::vector<bool>& ladder,
                            const std::vector<bool>& sneak, const std::vector<bool>& hit,
                            const std::vector<bool>& jump, const std::vector<VerticalContact>& contacts,
                            const Num& threshold) {
    const auto n = static_cast<std::int64_t>(hit.size());
    if (n < 1 || n > 200) fail("ticks is out of range");
    if (static_cast<std::int64_t>(ladder.size()) != n + 1) fail("ladder has the wrong length");
    if (static_cast<std::int64_t>(sneak.size()) != n) fail("Sneak has the wrong length");
    if (static_cast<std::int64_t>(jump.size()) != n) fail("Jump impulses have the wrong length");
    for (const auto& contact : contacts) {
        if (contact.state < 1 || contact.state > n) fail("vertical contact state is out of range");
        if (contact.direction != -1 && contact.direction != 1) fail("Vertical contact direction must be -1 or 1");
    }
    std::vector<VerticalContact> selected = merge_vertical(contacts);
    if (threshold.value < 0) fail("Velocity threshold must be nonnegative");
    std::vector<VerticalContactRow> rows;
    rows.reserve(selected.size());
    for (const auto& c : selected)
        rows.push_back({c.state, c.direction, c.plane, false, c.movement_case, c.lower_height, c.higher_height,
                        c.clearance});

    Trace trace;
    trace.position.reserve(n + 1);
    trace.velocity.reserve(n + 1);
    trace.on_ground.reserve(n + 1);
    Num y = initial.position, vy = initial.velocity;
    bool ground = initial.on_ground, failed = false, landed = false;
    trace.position.push_back(y);
    trace.velocity.push_back(vy);
    trace.on_ground.push_back(ground);
    for (std::int64_t t = 0; t < n; ++t) {
        StepResult step = vertical_step(rows, jump, sneak, threshold, t, y, vy, ladder[t], ground, 0);
        failed |= step.failed;
        y = step.proposed;
        ground = step.ground;
        vy = vertical_carry(step.carry, ladder[t + 1], hit[t]);
        trace.position.push_back(y);
        trace.velocity.push_back(vy);
        trace.on_ground.push_back(ground);
        trace.movement.push_back(step.carry);
        trace.climb_refreshed.push_back(ladder[t + 1] && hit[t]);
        if (ground)
            for (const auto& c : selected)
                if (c.state == t + 1 && c.direction < 0 && !c.movement_case) landed = true;
    }
    trace.status = failed ? "invalid" : landed ? "landed" : "simulated";
    trace.ticks = n;
    trace.ladder_ticks = ladder;
    trace.sneak = sneak;
    trace.hit = hit;
    trace.resume = {trace.on_ground.back(), ladder[n], hit[n - 1], trace.position.back(), trace.velocity.back()};
    VerticalTrace& vertical = trace.vertical;
    vertical.contacts = std::move(selected);
    vertical.jump_velocity.reserve(n);
    for (bool j : jump) vertical.jump_velocity.push_back(j ? Nullable<Num>{flt(kJumpVelocity)} : Nullable<Num>{});
    vertical.sneak = sneak;
    vertical.on_ground = initial.on_ground;
    vertical.previous_horizontal_collision = initial.previous_horizontal_collision;
    vertical.position = initial.position;
    vertical.velocity = initial.velocity;
    vertical.threshold = threshold;
    vertical.profile = "minecraft-1.8.9";
    return trace;
}

}  // namespace pkc
