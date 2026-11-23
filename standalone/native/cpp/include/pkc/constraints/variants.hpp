// Conditioned joint avoidance alternatives and their durable lazy frontier:
// constraints/variants.py, with the encounter covers of constraints/encounters.py.
// The state converts to and from the Python state dict that repair saves.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pkc/spatial/surfaces.hpp"
#include "pkc/value.hpp"

namespace pkc {

struct VariantCursor;
struct VariantNode;

inline constexpr std::int64_t kGenerationPolicy = 23;  // variants.GENERATION_POLICY
inline constexpr std::int64_t kVariantStateVersion = 11;  // variants.STATE_VERSION

using Obstacles = std::vector<std::array<std::int64_t, 2>>;  // [stretch begin, solid index]

// The constructor's inputs, as ConstraintVariants.__init__ takes them.
struct VariantInputs {
    Value route, profile, geometry, envelopes, scope;  // geometry: the collision scene
    Array checks, walk_offs;
    Value reference, owners, objective;                       // each may be null
    std::optional<std::vector<std::pair<std::int64_t, std::vector<std::int64_t>>>> segment_solids;  // sorted by begin
    std::int64_t settled = 0;
    // Raises to interrupt (cancel event, budget, deadline): ConstraintVariants.check.
    std::function<void()> check;
};

class ConstraintVariants {
public:
    // `obstacles`: null enumerates every cluster (screens refine with the disk relaxation); a list is conflict-driven.
    // `state` resumes a saved frontier and must match this context.
    ConstraintVariants(VariantInputs inputs, std::optional<Obstacles> obstacles, const Value& state);
    ~ConstraintVariants();

    // __next__: the next variant, or nullopt once exhausted (StopIteration). A raised check leaves the state resumable.
    std::optional<Value> next();
    Value state() const;
    bool conflict_driven() const { return obstacles_.has_value(); }
    bool exhausted() const;  // state['exhausted']
    const std::optional<Obstacles>& obstacles() const { return obstacles_; }
    // [stretch begin, solid] for a solve that hit `solid` on movement `tick` (< through): a stretch's encounter
    // sweeps its movements begin..end-1, so a movement into a landing belongs to the stretch before it.
    std::array<std::int64_t, 2> obstacle_key(std::int64_t solid, std::int64_t tick) const;
    std::unique_ptr<ConstraintVariants> with_obstacles(Obstacles obstacles) const;
    std::size_t item_count() const;

    struct State;
    // Hooks constraints/encounters uses on its owner.
    void check() const;
    void metric(const std::string& name, std::int64_t count = 1);
    Num coordinate(const std::string& axis, std::int64_t tick, const Value& envelopes) const;
    Value face_row(const Object& face, std::int64_t t) const;
    std::string effective_key(const Array& rows) const;
    const SceneGeometry& geometry() const { return *geometry_; }
    const Object& profile() const;
    State& mutable_state() { return *state_; }

private:
    Value analyze(const Array& rows);
    void excluded(const Array& rows, const Value& proof, const Value* item = nullptr, const Value* option = nullptr);
    Value prepare_item(Value item);
    std::optional<Value> option(const Value& item, VariantCursor& cursor);
    void choose_item(VariantNode& node);
    Value branch_choice(const Object& item, const Object& option, const Object& choice) const;
    std::optional<Value> next_variant();

    VariantInputs inputs_;
    std::optional<Obstacles> obstacles_;
    std::int64_t through_ = 0;
    std::string context_;
    std::shared_ptr<const SceneGeometry> geometry_;
    std::unique_ptr<State> state_;
    std::vector<std::pair<std::string, Value>> analysis_cache_;     // FIFO, 128
    std::vector<std::pair<std::string, Value>> preparation_cache_;  // FIFO, 256; key: effective key + index
    std::optional<Value> active_envelopes_;  // the expanded node's conditioned envelopes during next()
};

// encounters.landing_segments(profile, through).
std::vector<std::pair<std::int64_t, std::int64_t>> landing_segments(const Object& profile, std::int64_t through);

// encounters.settled_through(profile, through).
std::int64_t settled_through(const Object& profile, std::int64_t through);

// reachability/service.interaction_context_key(context).
std::string interaction_context_key(const Value& context);

}  // namespace pkc
