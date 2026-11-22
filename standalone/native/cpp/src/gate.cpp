// Port of InteractionGate.prepare's build. Each function names the Python it
// mirrors; results must equal Python's canonical JSON, including int/float
// kinds, so arithmetic on values that reach the output uses Num.
#include "pkc/gate.hpp"

#include "pkc/scene/route.hpp"
#include "pkc/validation/vertical.hpp"
#include "schema.hpp"

namespace pkc {

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
    Object plan = fields(schedule(object(route), {0}));
    Object vertical = fields(validate_vertical_schedule(object(route), array({object(profile)}), object(plan)));
    if (!std::get<bool>(at(vertical, "feasible").data)) return std::nullopt;
    Object plan_vertical = fields(at(vertical, "vertical"));
    put(plan_vertical, "referencePosition", at(vertical, "y"));
    put(plan_vertical, "referenceVelocity", at(vertical, "vy"));
    put(plan, "vertical", object(std::move(plan_vertical)));
    return object({{"route", object(std::move(route))},
                   {"profiles", array({object(std::move(profile))})},
                   {"plan", object(std::move(plan))},
                   {"vertical", object(std::move(vertical))}});
}

}  // namespace pkc
