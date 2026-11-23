// The interaction gate (reachability/service.py InteractionGate) for native
// callers: one session cache of preparations, coefficients and analyses, the
// generators' approach envelopes and queries, metrics and the exclusion log.
// It compiles coefficients from the native tables, so it stands in for a
// gate whose compiler has table_coefficients.
#pragma once

#include <any>
#include <functional>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "pkc/movement/contact_combinations.hpp"
#include "pkc/movement/generation.hpp"
#include "pkc/program.hpp"
#include "pkc/screen.hpp"
#include "pkc/spatial/surfaces.hpp"
#include "pkc/value.hpp"

namespace pkc {

struct FaceTable {  // face_table's dict
    std::set<std::string> blocks, landing, stepping, ceiling;
    std::set<std::int64_t> solids, ceiling_solids;
    std::optional<std::int64_t> ticks;
    const std::set<std::string>& of(const std::string& kind) const;  // table[kind] for a block-name kind
};

struct GatePrepared {  // a preparation and the keys the gate derived from it
    std::shared_ptr<const Program> program;
    Value result;  // {route, profiles, plan, vertical}
    std::string key;
};

class InteractionGate {
public:
    explicit InteractionGate(Value scene);

    const Value& scene() const { return scene_; }
    const SceneGeometry& geometry() const { return geometry_; }
    const std::vector<FaceOption>& top_faces() const { return top_faces_; }
    const SolidOwners& owners() const { return owners_; }

    const FaceTable& reachable_faces(const std::optional<std::string>& name);  // nullopt: None, a face of no owners
    // _prepared / _analysis for the default scene; nullptr when the vertical schedule is infeasible.
    std::shared_ptr<const GatePrepared> prepared(const std::shared_ptr<const Program>& program,
                                                 bool require_complete = false);
    std::shared_ptr<const GateAnalysis> analysis(const std::shared_ptr<const Program>& program);
    // The analysis with the preparation it models, both null when the vertical schedule is infeasible.
    std::pair<std::shared_ptr<const GatePrepared>, std::shared_ptr<const GateAnalysis>> analyzed(
        const std::shared_ptr<const Program>& program);
    Value required(const std::shared_ptr<const Program>& program);  // a proof or null
    // analyze_model(route, plan, vertical, scope=scope) for a node solve's own route: the analysis dict, keyed by its
    // model's content, with the Python result's model alias in its envelopes.
    Value analyze_model(const Value& route, const Value& plan, const Value& vertical, const Value& scope);
    // approach's envelopes, or null when unknown.
    Value approach(const GenerationContext& context, std::int64_t tick, bool jumping);
    // query_many(context, bounds, tick, label=label, jumping=jumping, phases=phases): one proof per box.
    Array query_many(const GenerationContext& context, const std::vector<Array>& bounds, std::int64_t tick,
                     const Object& label, bool jumping, const std::vector<std::string>& phases);

    // sequence(context, choice): a rejection proof of the choice's shared named outcome, or null.
    Value sequence(const GenerationContext& context, const MovementChoice& choice);
    // check_program(program, objective): (proof or null, the run-off's launch-support face selection or
    // null); the Python appends the selection to program['walkOffs']. A null objective is the scene's.
    std::pair<Value, Value> check_program(const std::shared_ptr<const Program>& program, const Value& objective);
    // relaxation(program, analysis, objective, rows): disk.screen's verdict, or null outside its class.
    Value relaxation(const std::shared_ptr<const Program>& program, const GateAnalysis& analysis,
                     const Value& objective, const Array& rows);

    // walk_off(context, landing_boxes, label=label): whether a no-jump run-off can leave its launch support.
    bool walk_off(const GenerationContext& context, const Value& landing_boxes, const Object& label);

    // RelaxedDispatch's IPOPT confirmations, kept in this cache as Python keeps them in gate.cache: a read neither
    // counts nor refreshes, and an insert is trimmed by the next cached lookup.
    const Value* confirmation(const std::string& fingerprint) const;
    void remember_confirmation(const std::string& fingerprint, const Value& result);
    void count(const std::string& metric, std::int64_t by = 1);
    const Object& metrics() const { return metrics_; }
    std::int64_t checked = 0;
    Array exclusions;

private:
    template <class T>
    std::shared_ptr<const T> cached(const std::string& key, const std::string& kind,
                                    const std::function<std::shared_ptr<const T>()>& build);
    const Value& remember(const std::string& key, const std::function<Value()>& build);

    Value scene_;
    std::string scene_json_;  // canonical JSON of the scene without movementSearch, for preparation keys
    SceneGeometry geometry_;
    std::vector<FaceOption> top_faces_;
    SolidOwners owners_;
    Object metrics_;
    std::map<std::optional<std::string>, FaceTable> face_tables_;
    std::map<double, Trace> jump_traces_;
    // _cached's dict: least recently used first, at most 512 entries of every kind together.
    std::list<std::pair<std::string, std::any>> cache_;
    std::unordered_map<std::string, std::list<std::pair<std::string, std::any>>::iterator> index_;
    // approaches: insertion order, at most 512 (a hit does not refresh).
    std::list<std::pair<std::string, Value>> approaches_;
    std::unordered_map<std::string, std::list<std::pair<std::string, Value>>::iterator> approach_index_;
};

// The module functions generators call with a context; the context's gate must be set.
std::vector<FaceOption> candidate_faces(const GenerationContext& context, const std::string& kind);
bool candidate_option(const GenerationContext& context, const FaceOption& option, const std::string& kind = "solids");
std::vector<bool> bounds_allowed_many(const GenerationContext& context, const std::vector<Array>& bounds,
                                      std::int64_t tick, const Object& label, bool jumping,
                                      const std::vector<std::string>& phases);
bool interaction_allowed(const GenerationContext& context, const Value& box, std::int64_t tick, const Object& label,
                         bool jumping);
bool behind_decision(const GenerationContext& context, const Array& regions, std::int64_t tick, bool jumping,
                     const Object& label);
bool entry_backward(const GenerationContext& context, const std::string& axis, std::int64_t sign, const Array& region,
                    const Object& label);
bool walk_off_allowed(const GenerationContext& context, const Value& landing_boxes, const Object& label);

}  // namespace pkc
