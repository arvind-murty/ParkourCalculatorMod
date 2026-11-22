// Port of numerics/builders.numerical_problem. waypoint_seed and
// ladder_waypoints seed ladder events and stay in Python with the ladder code.
#include "pkc/numerics/builders.hpp"

#include "../schema.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/scene/route.hpp"

namespace pkc {

Value numerical_problem(const Value& route_value, const Value& plan_value, const Value& coefficients,
                        const Value* parent) {
    const Object& route = fields(route_value);
    const Object& plan = fields(plan_value);
    const Object& initial = fields(at(route, "initial"));
    const Array& position = items(at(initial, "position"));
    const Array& velocity = items(at(initial, "velocity"));
    const Value* guide = get(route, "guideObjective");
    const Value* threshold = get(route, "physicsThreshold");
    const Value* contacts = get(plan, "contacts");
    Object problem = fields(coefficients);  // dict(coefficients, ...): existing keys keep their place
    put(problem, "version", integer_value(1));
    put(problem, "ticks", at(plan, "ticks"));
    put(problem, "position", array({position[0], position[2]}));
    put(problem, "velocity", array({velocity[0], velocity[2]}));
    put(problem, "yaw", initial_guess(route_value, plan_value, parent));
    put(problem, "threshold", threshold ? *threshold : real(.005));
    put(problem, "walls", at(plan, "walls"));
    put(problem, "contacts", contacts ? *contacts : array({}));
    put(problem, "objective", object(with(fields(guide ? *guide : at(route, "objective")), "tick", at(plan, "ticks"))));
    if (const Value* ladder = get(plan, "ladderTicks")) put(problem, "ladderTicks", *ladder);
    return prepare_problem(object(std::move(problem)));
}

}  // namespace pkc
