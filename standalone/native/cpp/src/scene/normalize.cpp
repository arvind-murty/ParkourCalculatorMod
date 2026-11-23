// Port of scene/normalize.py.
#include "pkc/scene/normalize.hpp"

#include <algorithm>
#include <set>
#include <string>

#include "../schema.hpp"
#include "pkc/movement/ladder/settings.hpp"
#include "pkc/scene/geometry.hpp"
#include "pkc/scene/objectives.hpp"
#include "pkc/scene/route.hpp"

namespace pkc {
namespace {

const char* const kAllowed[] = {
    "version", "sceneModel", "initial", "blocks", "startBlock", "goalBlock", "inputs", "maxJumps", "maxPrefixes",
    "maxQueuedPrefixes", "maxRunTicksPerJump", "maxTotalRunTicks", "maxFlightInputs", "maxContactBranches",
    "stairBoost", "supportClearance", "objective", "geometryBounds", "collisionScene", "maxBlockagesPerObstacle",
    "maxPassageCandidates", "maxHorizontalBlockagesPerObstacle", "maxVerticalBlockagesPerObstacle", "maxWallHits",
    "maxWallHitCandidates", "ceilingHits", "climbableCells", "ladderSearch", "maxContactCombinations",
    "maxCombinedEvents", "maxSteppingProfiles", "movementSearch"};

bool allowed(const std::string& key) {
    return std::any_of(std::begin(kAllowed), std::end(kAllowed), [&](const char* k) { return key == k; });
}

bool is_bool(const Value& value) { return std::holds_alternative<bool>(value.data); }

}  // namespace

Value prepare_scene(const Value& request) {
    if (!is_object(request)) fail("Expected a block scene");
    Object scene = fields(request);
    std::set<std::string> extra;
    for (const auto& field : scene)
        if (!allowed(field.first)) extra.insert(field.first);
    if (!extra.empty()) {
        std::string listed;
        for (const auto& key : extra) listed += (listed.empty() ? "" : ", ") + key;
        fail("Unsupported block-scene fields: " + listed);
    }
    const Value& model = get_or_null(scene, "sceneModel");
    auto* model_text = std::get_if<std::string>(&model.data);
    if (!model_text || (*model_text != "coplanar_support_planes" && *model_text != "support_planes" &&
                        *model_text != "collision_boxes"))
        fail("Declare a support_planes, coplanar_support_planes or collision_boxes scene");
    const std::string scene_model = *model_text;
    put(scene, "objective", objective_spec(get(scene, "objective")));
    const Value& blocks_value = get_or_null(scene, "blocks");
    if (!is_list(blocks_value) || items(blocks_value).size() < 2 || items(blocks_value).size() > 64)
        fail("Supply 2 through 64 candidate supports");
    const bool solid_scene = scene_model == "collision_boxes";
    if (solid_scene) {
        integer(setdefault(scene, "maxWallHits", integer_value(2)), "maxWallHits", 0, 8);
        integer(setdefault(scene, "maxWallHitCandidates", integer_value(24)), "maxWallHitCandidates", 1, 512);
        integer(setdefault(scene, "maxContactCombinations", integer_value(64)), "maxContactCombinations", 0, 100000);
        integer(setdefault(scene, "maxSteppingProfiles", integer_value(128)), "maxSteppingProfiles", 1, 100000);
        integer(setdefault(scene, "maxCombinedEvents", integer_value(3)), "maxCombinedEvents", 1, 32);
        if (!is_bool(setdefault(scene, "ceilingHits", boolean(true)))) fail("ceilingHits must be boolean");
        scene = fields(normalize_geometry(object(std::move(scene))));
        if (items(at(scene, "blocks")).size() > 64) fail("Supply at most 64 collision sub-boxes");
    } else if (get(scene, "collisionScene") || get(scene, "geometryBounds")) {
        fail("Full geometry requires sceneModel=collision_boxes");
    }
    const Array blocks = items(at(scene, "blocks"));
    std::vector<std::string> ids;
    for (const auto& block_value : blocks) {
        bool shaped = is_object(block_value);
        if (shaped)
            for (const auto& field : fields(block_value)) {
                const std::string& k = field.first;
                bool known = k == "id" || k == "bounds" || k == "top" ||
                             (solid_scene && (k == "sourceBlock" || k == "solidIndex"));
                shaped = shaped && known;
            }
        if (!shaped) fail("Each candidate is one rectangular support with id, bounds and top");
        auto* id = std::get_if<std::string>(&get_or_null(fields(block_value), "id").data);
        if (!id || id->empty()) fail("Each candidate support needs a nonempty ID");
        ids.push_back(*id);
    }
    if (std::set<std::string>(ids.begin(), ids.end()).size() != ids.size())
        fail("Candidate support IDs must be unique");
    auto named = [&](const char* key) -> const std::string* {
        auto* s = std::get_if<std::string>(&get_or_null(scene, key).data);
        return s && std::find(ids.begin(), ids.end(), *s) != ids.end() ? s : nullptr;
    };
    const std::string* start = named("startBlock");
    const std::string* goal = named("goalBlock");
    if (!start || !goal) fail("Start and goal must name candidate support IDs");
    if (*start == *goal) fail("This route search requires different start and goal supports");
    const std::string start_id = *start;
    if (!is_bool(setdefault(scene, "stairBoost", boolean(true)))) fail("stairBoost must be boolean");
    const Value* inputs_value = get(scene, "inputs");
    if (inputs_value && !is_object(*inputs_value)) fail("Input policy must be an object");
    Object inputs = inputs_value ? fields(*inputs_value) : Object{};
    for (const auto& field : inputs) {
        const std::string& k = field.first;
        if (k != "forward" && k != "strafe" && k != "sprint" && k != "incomingSprint" && k != "force45")
            fail("Unsupported input-policy fields");
    }
    setdefault(inputs, "sprint", boolean(true));
    setdefault(inputs, "force45", boolean(true));
    if (!py_equal(at(inputs, "sprint"), boolean(true)) || !is_bool(at(inputs, "sprint")) ||
        !py_equal(at(inputs, "force45"), boolean(true)) || !is_bool(at(inputs, "force45")))
        fail("Block search uses sprint jump and sprint 45");
    put(scene, "inputs", object(inputs));
    for (auto [key, fallback, lo, hi] : {std::tuple{"maxJumps", 4, 1, 12}, std::tuple{"maxPrefixes", 200, 1, 100000},
                                         std::tuple{"maxQueuedPrefixes", 4000, 1, 100000},
                                         std::tuple{"maxRunTicksPerJump", 6, 0, 1000},
                                         std::tuple{"maxTotalRunTicks", 12, 0, 1000},
                                         std::tuple{"maxFlightInputs", 200, 2, 200}})
        integer(setdefault(scene, key, integer_value(fallback)), key, lo, hi);
    for (const char* key : {"maxContactBranches", "maxBlockagesPerObstacle", "maxPassageCandidates",
                            "maxHorizontalBlockagesPerObstacle", "maxVerticalBlockagesPerObstacle",
                            "supportClearance"})
        erase(scene, key);
    Array ordered;
    for (std::size_t i = 0; i < blocks.size(); ++i)
        if (ids[i] == start_id) {
            ordered.push_back(blocks[i]);
            break;
        }
    for (std::size_t i = 0; i < blocks.size(); ++i)
        if (ids[i] != start_id) ordered.push_back(blocks[i]);
    Object checked = fields(prepare_route(object(
        {{"version", get_or_null(scene, "version")}, {"initial", get_or_null(scene, "initial")},
         {"blocks", array(ordered)}, {"airtimes", array(Array(blocks.size() - 1, integer_value(12)))},
         {"inputs", object(inputs)}, {"maxTotalRunTicks", at(scene, "maxTotalRunTicks")}})));
    if (scene_model == "coplanar_support_planes")
        for (const auto& block : blocks)
            if (!py_equal(at(fields(block), "top"), at(fields(ordered[0]), "top")))
                fail("Use sceneModel=support_planes for different support heights");
    put(scene, "inputs", at(checked, "inputs"));
    put(scene, "initial", at(checked, "initial"));
    if (solid_scene)
        for (const auto& field : fields(at(scene, "initial")))
            if (field.first != "position" && field.first != "velocity" && field.first != "yaw")
                fail("Collision-box avoidance requires a clear initial input state; additional resume fields need "
                     "shared movement compilation");
    if (py_truthy(get_or_null(scene, "climbableCells"))) {
        if (!solid_scene) fail("Climbable cells require collision_boxes");
        put(scene, "ladderSearch", ladder_settings(scene));
        return object(std::move(scene));
    }
    if (get(scene, "ladderSearch")) fail("Ladder search needs supplied climbable cells");
    return object(std::move(scene));
}

}  // namespace pkc
