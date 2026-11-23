// Port of search/identity.solve_scope, solve_identity and validation_prefix.
#include "pkc/search/identity.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "../schema.hpp"
#include "../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "pkc/program.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const double kHalfWidth = f32(0.6) / 2;  // scene/route.HALF_WIDTH
const double kBodyHeight = f32(1.8);     // scene/geometry.BODY_HEIGHT

Array listed(const Object& object, const char* key) {
    const Value* value = get(object, key);
    return value ? items(*value) : Array{};
}

}  // namespace

Value solve_scope(const Value& scene_value, const Value& program_value, const Value& supplied) {
    const Object& scene = fields(scene_value);
    const Object& program = fields(program_value);
    const Value* saved = get(program, "solveScope");
    const Value source = !is_null(supplied) ? supplied : saved ? *saved : object({});
    const Object& value = fields(source);
    for (const auto& [key, unused] : value)
        if (key != "purpose" && key != "modeledBlocks" && key != "through") fail("Unknown solve scope field");
    const Value* purpose_value = get(value, "purpose");
    const Value purpose = purpose_value ? *purpose_value : text("complete");
    if (!py_equal(purpose, text("preliminary")) && !py_equal(purpose, text("complete")))
        fail("Solve purpose must be preliminary or complete");
    const Object& profile = fields(at(program, "profile"));
    const Value& n = at(profile, "duration");
    const Value* through_value = get(value, "through");
    const Value through = through_value ? *through_value : n;
    if (!std::holds_alternative<std::int64_t>(through.data) || !(0 < as_int(through) && as_int(through) <= as_int(n)))
        fail("Solve scope must cover a nonempty prefix of the program");
    const Array& blocks = items(at(scene, "blocks"));
    std::set<std::string> names;
    for (const auto& b : blocks) {
        names.insert(as_text(at(fields(b), "id")));
        names.insert(as_text(at(fields(b), "sourceBlock")));
    }
    Array defaults;
    for (const auto& name : names) defaults.push_back(text(name));
    const Value* requested_value = get(value, "modeledBlocks");
    const Value requested = requested_value ? *requested_value : array(defaults);
    auto named = [&](const Value& name) {
        const auto* s = std::get_if<std::string>(&name.data);
        return s && names.count(*s);
    };
    if (!is_list(requested) || !std::all_of(items(requested).begin(), items(requested).end(), named))
        fail("Unknown modeled block");
    const Array& solids_list = items(at(fields(at(scene, "collisionScene")), "solids"));
    Array solids;
    for (const auto& b : blocks) {
        const Object& block = fields(b);
        if (contains(items(requested), at(block, "id")) || contains(items(requested), at(block, "sourceBlock")))
            solids.push_back(at(block, "solidIndex"));
    }
    if (as_text(purpose) == "complete" && (as_int(through) != as_int(n) || solids.size() != solids_list.size()))
        fail("Complete solves must model the full declared scene and horizon");
    Array active;
    for (const auto& i : solids) active.push_back(solids_list.at(as_int(i)));
    for (const auto& e : listed(profile, "contactEvents")) {
        const Object& event = fields(e);
        if (as_int(at(event, "offset")) >= as_int(through)) continue;
        for (const auto& box : items(at(event, "boxes")))
            if (!contains(active, box)) fail("Selected contacts must belong to the modeled scene");
    }
    std::set<std::string> modeled;
    for (const auto& name : items(requested)) modeled.insert(as_text(name));
    Array modeled_blocks;
    for (const auto& name : modeled) modeled_blocks.push_back(text(name));
    return object({{"version", integer_value(1)}, {"purpose", purpose}, {"through", through},
                   {"solidIndices", array(std::move(solids))}, {"modeledBlocks", array(std::move(modeled_blocks))}});
}

std::string solve_identity(const Value& scene_value, const Value& program, const Value& objective, const Value& scope,
                           const Value& route, const Value& coefficients) {
    const Object& scene = fields(scene_value);
    return digest(object({{"version", integer_value(1)}, {"initial", at(scene, "initial")},
                          {"geometry", at(scene, "collisionScene")}, {"inputs", at(scene, "inputs")},
                          {"program", text(program_physics_key(program_from_value(program)))},
                          {"objective", objective}, {"scope", scope}, {"route", route},
                          {"body", array({real(kHalfWidth), real(kBodyHeight)})}, {"coefficients", coefficients}}));
}

std::tuple<Value, Value, Value> validation_prefix(const Value& plan_value, const Value& candidate_value,
                                                  const Value& vertical_value, std::int64_t through) {
    if (through == as_int(at(fields(plan_value), "ticks"))) return {plan_value, candidate_value, vertical_value};
    auto prefix = [](const Value& list, std::int64_t n) {
        const Array& a = items(list);
        return array(Array(a.begin(), a.begin() + std::min<std::int64_t>(n, static_cast<std::int64_t>(a.size()))));
    };
    Object p = fields(plan_value), c = fields(candidate_value), v = fields(vertical_value);
    put(p, "ticks", integer_value(through));
    for (const char* key : {"jump", "grounded", "sneak"})
        if (const Value* list = get(p, key)) put(p, key, prefix(*list, through));
    if (const Value* ladder = get(p, "ladderTicks")) put(p, "ladderTicks", prefix(*ladder, through + 1));
    Array contacts, checks;
    for (const auto& e : listed(p, "sceneContacts"))
        if (as_int(at(fields(e), "offset")) < through) contacts.push_back(e);
    for (const auto& e : listed(p, "supportChecks"))
        if (as_int(items(e)[0]) < through) checks.push_back(e);
    put(p, "sceneContacts", array(std::move(contacts)));
    put(p, "supportChecks", array(std::move(checks)));
    for (const char* key : {"positions", "velocity"})
        if (const Value* list = get(c, key)) put(c, key, prefix(*list, through + 1));
    for (const char* key : {"yaw", "rawMovement", "movement", "friction"})
        if (const Value* list = get(c, key)) put(c, key, prefix(*list, through));
    for (const char* key : {"y", "vy", "onGround"}) put(v, key, prefix(at(v, key), through + 1));
    if (const Value* vertical = get(v, "vertical")) {
        Object inner = fields(*vertical);
        for (const char* key : {"jumpVelocity", "sneak"})
            if (const Value* list = get(inner, key)) put(inner, key, prefix(*list, through));
        Array kept;
        for (const auto& e : listed(inner, "contacts"))
            if (as_int(at(fields(e), "state")) <= through) kept.push_back(e);
        put(inner, "contacts", array(std::move(kept)));
        put(v, "vertical", object(std::move(inner)));
    }
    return {object(std::move(p)), object(std::move(c)), object(std::move(v))};
}

}  // namespace pkc
