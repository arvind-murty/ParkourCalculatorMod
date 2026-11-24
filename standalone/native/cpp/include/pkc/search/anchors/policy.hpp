// The anchor policy's program readers: search/anchors/policy.py. An
// AnchorScene holds what face_namer and owner derive from one scene.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "pkc/movement/contact_combinations.hpp"
#include "pkc/program.hpp"
#include "pkc/reachability/service.hpp"
#include "pkc/spatial/surfaces.hpp"
#include "pkc/value.hpp"

namespace pkc {

inline constexpr std::int64_t kHardJumpPolicyVersion = 5;  // policy.HARD_JUMP_POLICY_VERSION
inline constexpr std::int64_t kSearchPolicyVersion = 22;   // semantic_settings' searchPolicyVersion

using Visit = std::pair<std::int64_t, std::string>;  // (tick, face name)

struct VisitState {
    std::optional<std::string> current;
    std::vector<std::string> departed;  // sorted
    bool valid = true;
};

struct Audit {
    std::optional<std::string> reason;
    VisitState visits;
    std::int64_t reached = 0;
    std::vector<std::int64_t> ticks;
};

class AnchorScene {
public:
    explicit AnchorScene(Value scene);

    std::string owner(const std::string& block) const;                // owner(scene, block)
    std::optional<std::string> face(const std::vector<Box>& boxes) const;  // face_namer(scene)(boxes)
    std::string support_face(const std::string& support) const;
    std::vector<Visit> visits(const Program& program) const;
    std::optional<Visit> launch(const Program& program, std::optional<std::int64_t> begin = std::nullopt,
                                std::optional<std::int64_t> end = std::nullopt) const;
    std::vector<Visit> feet_touches(const Program& program) const;
    VisitState visit_state(const Program& program) const;
    // audit(scene, program, anchors, reached=reached)
    Audit audit(const Program& program, const std::vector<std::string>& anchors,
                std::optional<std::int64_t> reached = std::nullopt) const;
    std::string next_anchor(const Program& program, const std::vector<std::string>& anchors,
                            const std::string& task) const;
    // transfer_structure(scene, program, start, source, target, complete=..., until_arrival=...)'s dict.
    Value transfer_structure(const Program& program, std::int64_t start, const std::string& source,
                             const std::string& target, bool complete, bool until_arrival) const;
    // within_caps(request, program): the request's maxJumps, maxTotalRunTicks and maxRunTicksPerJump.
    bool within_caps(const Object& request, const Program& program) const;
    // AnchorAdapter.decision_point(program) on the guide plan: [x, y, z].
    Value decision_point(const Program& program, const Value& guides) const;
    // AnchorAdapter.program_objective: decision_objective toward the next required anchor.
    Value program_objective(const Program& program, const Value& guides, const std::vector<std::string>& anchors) const;
    // advantageous_targets: (kind, name) keys; a slab boost or corner cancel on no face has no name.
    std::set<std::pair<std::string, std::optional<std::string>>> advantageous_targets(
        const Program& base, const Program& child, const std::string& next_owner) const;

private:
    Value scene_;
    SceneGeometry geometry_;
    SolidOwners owners_;
    std::map<std::string, std::string> block_owner_;      // first block of an id: sourceBlock or id
    std::map<std::string, std::int64_t> block_solid_;     // first block of an id: its solid
    std::map<std::vector<double>, std::int64_t> solid_index_;  // owned solids by their box (the last of equal boxes)
    std::map<std::vector<double>, std::string> box_owner_;     // box_owners: the last block on a box names it
    std::map<std::int64_t, std::vector<std::pair<std::set<std::int64_t>, std::string>>> faces_;
};

// spatial/anchor_guides.py: classify(guides, span, point), decision_objective(guides, reached, point, fallback) and
// onward_objective(guides, span, fallback).
// `point` keeps its numbers' kinds in the label, as list(point) does.
Value classify(const Value& guides, std::int64_t span, const Value& point);
Value classify(const Value& guides, std::int64_t span, double x, double z);
Value decision_objective(const Value& guides, std::int64_t reached, const Value& point, const Value& fallback);
Value onward_objective(const Value& guides, std::int64_t span, const Value& fallback);

// spatial/anchor_guides.leg_class(direction): a unit horizontal guide leg's heading and the moves ('X'|'Z', +1|-1)
// its direction class allows. Within kAxisLegDegrees of an axis: that axis, forward and both sideways; otherwise its
// quadrant's diagonal and its two forward axes.
inline constexpr double kAxisLegDegrees = 22.5;  // anchor_guides.AXIS_LEG_DEGREES
using Move = std::pair<std::string, std::int64_t>;
struct LegClass {
    std::array<double, 2> heading;
    std::set<Move> allowed;
};
LegClass leg_class(double dx, double dz);

// spatial/anchor_guides.build_guides(data, policy): the guide plan over the start, the anchors and the task.
Value build_guides(const Value& data, const Value& policy);

// policy.settings(value, scene): the anchor policy, or std::invalid_argument with the Python's message.
Value anchor_settings(const Value& value, const Value& scene);
// policy.semantic_settings(policy).
Value semantic_settings(const Value& policy);
// policy.encounter_groups(scene, names), its face tables read from `gate` (a gate of the same scene).
Value encounter_groups(const Value& scene, const std::vector<std::string>& names, InteractionGate& gate,
                       const AnchorScene& anchors);

}  // namespace pkc
