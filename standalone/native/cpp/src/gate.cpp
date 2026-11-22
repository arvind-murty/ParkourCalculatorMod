// Port of InteractionGate.prepare's build. Each function names the Python it
// mirrors; results must equal Python's canonical JSON, including int/float
// kinds, so arithmetic on values that reach the output uses Num.
#include "pkc/gate.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "pkc/movement/contact_combinations.hpp"
#include "pkc/scene/route.hpp"
#include "pkc/vertical.hpp"
#include "schema.hpp"

namespace pkc {
namespace {

// scene/route.py, scene/geometry.py, schema.py, spatial/surfaces.py
const double kHalfWidth = f32(0.6) / 2;
const double kBodyHeight = f32(1.8);

struct Schedule {
    Object plan;
    std::vector<bool> ladder;  // plan.get('ladderTicks', [False]*(n+1))
};

std::optional<Num> optional_number(const Object& object, std::string_view key, const std::string& name) {
    if (const Value* v = get(object, key)) return number(*v, name);
    return std::nullopt;
}

// validation/vertical.validate_vertical_schedule (with numerics.prepare's
// checks and validation.numerical._replay for its synthetic problem).
Object validate_vertical_schedule(const Object& route, const Object& profile, const Schedule& schedule) {
    const Object& plan = schedule.plan;
    std::int64_t n = std::get<std::int64_t>(at(plan, "ticks").data);
    Array contacts = items(at(profile, "contacts"));  // event jump is 0, so states are unchanged
    if (const Value* extra = get(plan, "verticalContacts"))
        contacts.insert(contacts.end(), items(*extra).begin(), items(*extra).end());
    Array merged = merge_contacts(contacts, false, 1);
    const auto& initial = fields(at(route, "initial"));
    std::vector<std::optional<Num>> jumps;
    Array jump_velocity;
    for (const auto& j : items(at(plan, "jump"))) {
        bool jumping = std::get<bool>(j.data);
        jumps.push_back(jumping ? std::optional<Num>(flt(kJumpVelocity)) : std::nullopt);
        jump_velocity.push_back(jumping ? real(kJumpVelocity) : kNull);
    }
    std::vector<bool> sneak(n, false);
    Array sneak_values;
    if (const Value* s = get(plan, "sneak")) {
        sneak_values = items(*s);
        for (std::int64_t t = 0; t < n; ++t) sneak[t] = std::get<bool>(sneak_values[t].data);
    } else {
        for (std::int64_t t = 0; t < n; ++t) sneak_values.push_back(boolean(false));
    }
    Array vertical_contacts;
    std::vector<VerticalContactRow> rows;
    std::vector<std::int64_t> seen_states;
    static const char* kCases[] = {"normal_step", "slab_boost", "blip", "blip_up",
                                   "corner_jump_cancel", "corner_blip_up", "grind"};
    for (const auto& c_value : merged) {
        const Object& c = fields(c_value);
        // numerics.prepare's vertical contact checks
        std::int64_t state = integer(get_or_null(c, "state"), "vertical contact state", 1, n);
        Num plane = number(get_or_null(c, "plane"), "vertical contact plane");
        const auto* direction = std::get_if<std::int64_t>(&get_or_null(c, "direction").data);
        if (!direction || (*direction != -1 && *direction != 1) ||
            std::count(seen_states.begin(), seen_states.end(), state))
            fail("Invalid or duplicate vertical contact");
        const Value& step_value = get(c, "step") ? at(c, "step") : boolean(false);
        if (!std::holds_alternative<bool>(step_value.data) || (std::get<bool>(step_value.data) && *direction != -1))
            fail("A prescribed step must be a lower contact");
        bool step = std::get<bool>(step_value.data);
        VerticalContactRow row{state, *direction, plane, step, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
        const Value& case_value = get_or_null(c, "case");
        bool has_case = !is_null(case_value) && !(std::holds_alternative<std::string>(case_value.data) &&
                                                   as_text(case_value).empty());
        if (has_case) {
            if (*direction != -1 || step) fail("A movement case requires its own lower contact");
            const auto* kind = std::get_if<std::string>(&case_value.data);
            if (!kind || std::none_of(std::begin(kCases), std::end(kCases), [&](const char* k) { return *kind == k; }))
                fail("Unsupported vertical movement case");
            row.movement_case = *kind;
            row.lower_height = optional_number(c, "lowerHeight", "vertical case lowerHeight");
            row.higher_height = optional_number(c, "higherHeight", "vertical case higherHeight");
            row.clearance = optional_number(c, "clearance", "vertical case clearance");
        }
        seen_states.push_back(state);
        rows.push_back(row);
        if (get(c, "axis")) {  // the plan's verticalContacts carry axis; the vertical problem drops it
            Object untagged = c;
            erase(untagged, "axis");
            vertical_contacts.push_back(object(std::move(untagged)));
        } else {
            vertical_contacts.push_back(c_value);
        }
    }
    const Value& threshold_value = get(route, "physicsThreshold") ? at(route, "physicsThreshold") : real(.005);
    Num threshold = number(threshold_value, "vertical.threshold");
    if (threshold.value < 0) fail("vertical.threshold must be nonnegative");
    const Value& y0 = items(at(initial, "position"))[1];
    const Value& vy0 = items(at(initial, "velocity"))[1];
    Object vertical = {{"profile", text("minecraft-1.8.9")}, {"position", y0},
                       {"velocity", vy0},                    {"onGround", boolean(true)},
                       {"previousHorizontalCollision", boolean(false)},
                       {"threshold", threshold_value},       {"jumpVelocity", array(std::move(jump_velocity))},
                       {"sneak", array(std::move(sneak_values))},
                       {"contacts", array(std::move(vertical_contacts))}};
    // Synthetic horizontal replay: x advances .1 per tick, .05 into each hit state,
    // with zero friction and a zero inertia threshold.
    std::vector<bool> hit_states(n + 2, false);
    for (const auto& c : items(at(plan, "contacts")))
        hit_states.at(std::get<std::int64_t>(at(fields(c), "state").data)) = true;
    std::vector<double> planes(n + 2, NAN);
    double x = 0.;
    for (std::int64_t t = 0; t < n; ++t) {
        x += hit_states[t + 1] ? .05 : .1;
        if (hit_states[t + 1]) planes[t + 1] = x;
    }
    const std::vector<bool>& ladder = schedule.ladder;
    double controls[2] = {.1 * std::cos(0.0), .1 * std::sin(0.0)};
    double positions[2] = {0., 0.}, velocities[2] = {0., 0.};
    bool missed_or_wrong = false;
    std::vector<bool> previous_hits = {false};
    std::vector<Num> y = {number(y0, "vertical.position")}, vy = {number(vy0, "vertical.velocity")};
    std::vector<bool> grounded = {true};
    bool vertical_failed = false;
    for (std::int64_t t = 0; t < n; ++t) {
        bool hit = false;
        double next_positions[2], next_velocities[2];
        for (int axis = 0; axis < 2; ++axis) {
            double displacement = velocities[axis] + controls[axis];
            if (ladder[t]) displacement = std::max(-kCap, std::min(kCap, displacement));
            double proposed = positions[axis] + displacement;
            double carry = 0. * displacement;
            if (axis == 0 && hit_states[t + 1]) {
                double approach = positions[axis] - planes[t + 1];
                double reach = proposed - planes[t + 1];
                if (approach > kTolerance || reach < 0) missed_or_wrong = true;
                if (approach <= kTolerance && reach >= 0) {
                    hit = hit || reach > 0;
                    proposed = planes[t + 1];
                    carry = 0.;
                }
            }
            if (!std::isfinite(proposed)) fail("replayed position must be a finite number");
            next_positions[axis] = proposed;
            next_velocities[axis] = carry;
        }
        std::copy(next_positions, next_positions + 2, positions);
        std::copy(next_velocities, next_velocities + 2, velocities);
        previous_hits.push_back(hit);
        StepResult step = vertical_step(rows, jumps, sneak, threshold, t, y[t], vy[t], ladder[t], grounded[t],
                                        kTolerance);
        y.push_back(step.proposed);
        double carry = ladder[t + 1] && previous_hits[t + 1] ? kClimbCarry : (step.carry.value - kGravity) * kYDrag;
        vy.push_back(flt(carry));
        grounded.push_back(step.ground);
        vertical_failed |= step.failed;
    }
    bool checked_feasible = !missed_or_wrong && !vertical_failed;
    const auto& profile_grounded = items(at(profile, "grounded"));
    bool terminal_ground = std::get<bool>(profile_grounded.back().data);
    Array y_values, vy_values, ground_values;
    for (const auto& v : y) y_values.push_back(value(v));
    for (const auto& v : vy) vy_values.push_back(value(v));
    for (bool g : grounded) ground_values.push_back(boolean(g));
    bool matched = py_equal(y_values, items(at(profile, "position"))) &&
                   py_equal(vy_values, items(at(profile, "velocity")));
    const auto& plan_grounded = items(at(plan, "grounded"));  // expected: plan grounded + [terminal]
    bool ground_matched = ground_values.size() == plan_grounded.size() + 1 &&
                          py_equal(ground_values.back(), boolean(terminal_ground));
    for (std::size_t t = 0; ground_matched && t < plan_grounded.size(); ++t)
        ground_matched = py_equal(ground_values[t], plan_grounded[t]);
    bool feasible = matched && checked_feasible && ground_matched;
    return {{"feasible", boolean(feasible)},
            {"vertical", object(std::move(vertical))},
            {"y", array(std::move(y_values))},
            {"vy", array(std::move(vy_values))},
            {"onGround", array(std::move(ground_values))}};
}

}  // namespace

std::optional<Value> gate_prepare(const Value& scene_value, const Program& program, bool require_complete) {
    const Object& scene = fields(scene_value);
    // movement/program.compile_program
    if (!program.profile.duration) fail("An empty prefix is not a complete movement route");
    const std::string& goal = as_text(at(scene, "goalBlock"));
    if (require_complete && (!program.profile.grounded.back() || !program.terminal_support.value ||
                             *program.terminal_support.value != goal))
        fail("An open prefix is not a completed goal route");
    if (!program.ladder_events.empty()) fail("native gate preparation does not support ladder events");
    if (as_text(at(scene, "sceneModel")) != "collision_boxes")
        fail("native gate preparation supports collision_boxes scenes only");
    // movement/program.compile_program's one-leg route from the start support to the goal support
    const std::string& start_id = as_text(at(scene, "startBlock"));
    Array blocks;
    std::size_t index = 0;
    for (const std::string* id : {&start_id, &goal}) {
        const Object* found = nullptr;  // {b['id']: b} keeps the last block with an id
        for (const auto& block : items(at(scene, "blocks")))
            if (py_equal(at(fields(block), "id"), text(*id))) found = &fields(block);
        if (!found) fail("Unknown support in candidate path");
        blocks.push_back(object({{"id", text(*id + "@" + std::to_string(index++))},
                                 {"bounds", at(*found, "bounds")},
                                 {"top", at(*found, "top")}}));
    }
    Object profile = fields(to_value(program.profile));
    put(profile, "sourceBlock", at(fields(blocks[0]), "id"));
    put(profile, "targetBlock", at(fields(blocks[1]), "id"));
    put(profile, "profileKind", text("movement"));
    Object route = {{"version", integer_value(1)},
                    {"initial", at(scene, "initial")},
                    {"blocks", array(blocks)},
                    {"inputs", at(scene, "inputs")},
                    {"objective", at(scene, "objective")},
                    {"airtimes", array({integer_value(program.profile.duration)})},
                    {"kinds", array({text("movement")})},
                    {"landingPhases", array({text(program.profile.landing_phase)})},
                    {"contactEntries", array({kNull})},
                    {"walkOffExits", array({kNull})},
                    {"maxTotalRunTicks", at(scene, "maxTotalRunTicks")},
                    {"runBounds", array({array({integer_value(0), at(scene, "maxRunTicksPerJump")})})},
                    {"supportClearance", at(scene, "supportClearance")}};
    route = fields(prepare_route(object(std::move(route))));
    put(route, "profileDerivation",
        object({{"kind", text("selected_height_profiles")}, {"suppliedAirtimes", boolean(false)},
                {"verticalModel", text("parkour_solver.physics.vertical")}, {"solidSideFaces", boolean(false)}}));
    put(route, "sharedMovement", boolean(true));
    put(route, "ceilingEvents", array({}));
    put(route, "ladderEvents", array({}));
    put(route, "verticalProfiles", array({object(profile)}));
    Array contact_events;
    for (const auto& event : items(at(profile, "contactEvents")))
        contact_events.push_back(object(with(fields(event), "leg", integer_value(0))));
    put(route, "contactEvents", array(std::move(contact_events)));
    // compile_program's edits to the compiled route
    put(route, "maxTotalRunTicks", integer_value(0));
    put(route, "runBounds", array({array({integer_value(0), integer_value(0)})}));
    if (!require_complete) put(route, "terminalSupportRequired", boolean(false));
    if (program.threshold) put(route, "physicsThreshold", value(*program.threshold));
    if (program.guide_seed_yaw) {
        Array yaw;
        for (const auto& v : *program.guide_seed_yaw) yaw.push_back(value(v));
        put(route, "warmYaw", array(std::move(yaw)));
    }
    // InteractionGate.prepare's build
    route = fields(prepare_route(object(std::move(route))));
    Schedule plan{fields(schedule(object(route), {0})), {}};
    if (const Value* ladder = get(plan.plan, "ladderTicks")) {
        for (const auto& v : items(*ladder)) plan.ladder.push_back(py_truthy(v));
    } else {
        plan.ladder.assign(as_int(at(plan.plan, "ticks")) + 1, false);
    }
    Object vertical = validate_vertical_schedule(route, profile, plan);
    if (!std::get<bool>(at(vertical, "feasible").data)) return std::nullopt;
    Object plan_vertical = fields(at(vertical, "vertical"));
    put(plan_vertical, "referencePosition", at(vertical, "y"));
    put(plan_vertical, "referenceVelocity", at(vertical, "vy"));
    put(plan.plan, "vertical", object(std::move(plan_vertical)));
    return object({{"route", object(std::move(route))},
                   {"profiles", array({object(std::move(profile))})},
                   {"plan", object(std::move(plan.plan))},
                   {"vertical", object(std::move(vertical))}});
}

}  // namespace pkc
