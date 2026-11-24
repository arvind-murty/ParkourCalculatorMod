// Port of search/application.run, search/engine.find_checkpoint_route, search/events/engine.search_event_routes and
// search/dfs.search_goals' native branch, with the Python services a native search calls: work.py, the Python
// bridge's compiler and bridge/counting, reachability/service.InteractionGate's preparation cache and analysis,
// search/engine.solve_services, search/bounds/flight, reachability/intervals.motion_bound, observability/turns.summary
// and validation/witness.replay_witness.
#include "pkc/search/application.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "pkc/coefficients.hpp"
#include "pkc/constraints/variants.hpp"
#include "pkc/envelopes.hpp"
#include "pkc/gate.hpp"
#include "pkc/io/search_checkpoint.hpp"
#include "pkc/movement/generation.hpp"
#include "pkc/movement/normal.hpp"
#include "pkc/movement/settings.hpp"
#include "pkc/numerics/disk.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/numerics/solver.hpp"
#include "pkc/program.hpp"
#include "pkc/reachability/service.hpp"
#include "pkc/scene/geometry.hpp"
#include "pkc/scene/normalize.hpp"
#include "pkc/scene/route.hpp"
#include "pkc/scene/static_collision.hpp"
#include "pkc/search/anchors/adapter.hpp"
#include "pkc/search/anchors/policy.hpp"
#include "pkc/search/comparison.hpp"
#include "pkc/search/dfs_search.hpp"
#include "pkc/search/dfs_state.hpp"
#include "pkc/search/goals.hpp"
#include "pkc/search/repair.hpp"
#include "pkc/spatial/event_routes.hpp"
#include "pkc/spatial/regions.hpp"
#include "pkc/validation/numerical.hpp"
#include "pkc/validation/vertical.hpp"
#include "pkc/vertical.hpp"
#include "pkc/work.hpp"

namespace pkc {
namespace {

constexpr std::int64_t kComparisonPolicy = 1;  // search/comparison.POLICY_VERSION
constexpr std::int64_t kMinimumRunPolicy = 3;  // search/deferrals.POLICY_VERSION
constexpr std::size_t kGateCacheSize = 512;    // InteractionGate._cached's bound

// physics/vertical.REST_CARRY; a function, since kYDrag is initialized dynamically in another translation unit.
double rest_carry() { return -kGravity * kYDrag; }

// search/events/engine.NUMERICAL_COUNTS.
const char* const kNumericalCounts[] = {"workerLaunches", "feasibleSeedReturns", "conicCandidates", "relaxedFallbacks",
                                        "confirmations", "confirmationIpoptCalls", "confirmationClarabelSolves",
                                        "materialGaps", "frameEnds", "frameEndRejections", "frameEndIpoptCalls"};
// search/application.RETIRED_SETTINGS.
const char* const kRetiredSettings[] = {"fair_parents", "beam_width", "unknown_slots", "objective_gap", "score_seconds",
                                        "score_call_limit", "final_call_limit", "spatial_seconds", "spatial_calls",
                                        "spatial_candidates", "spatial_prefixes"};

Value without_key(const Value& value, std::string_view key) {
    Object out = fields(value);
    erase(out, key);
    return object(std::move(out));
}

Value list_of(const Array& values) { return array(values); }

Value every_other(const Value& vector3) {  // [x, y, z][::2]
    const Array& v = items(vector3);
    return array({v.at(0), v.at(2)});
}

// ---- the compiler: bridge/python_compiler.PythonCompiler under the native core, counted as bridge/counting does

class Compiler {
public:
    explicit Compiler(bool counted) : counted_(counted) {}

    Value compile(const Value& route, const Value& plan, double seconds) {
        return invoke("compile", seconds, [&] { return compile_coefficients(coefficient_message(route, plan)); });
    }

    Value footprints(const Value& route, double seconds, bool standing) {
        return invoke(standing ? "derive_standing_footprints" : "derive_footprints", seconds,
                      [&] { return route_footprints(at(fields(route), "blocks"), standing); });
    }

    Value check_scene(const Value& geometry, const Value& positions, const Value& movement, const Value& ground,
                      double seconds, const Value& contacts, const Object& contract) {
        for (const auto& [key, _] : contract)
            if (key != "sneak" && key != "ladderTicks" && key != "velocities" && key != "friction")
                throw std::invalid_argument("Unsupported scene contract");
        Object message{{"action", text("sceneCheck")}, {"scene", geometry},   {"positions", positions},
                       {"movement", movement},         {"expectedGround", ground},
                       {"contacts", is_null(contacts) ? array({}) : contacts}};
        for (const auto& [key, value] : contract) put(message, key, value);
        const double deadline = seconds_now() + seconds;
        const Value result = invoke("check_scene", seconds, [&] {
            return pkc::check_scene(object(std::move(message)), deadline - seconds_now());
        });
        if (counted_ && !py_truthy(get_or_null(fields(result), "feasible"))) count("sceneValidationFailures");
        return result;
    }

    // The repair loop's compiler services.
    SolveHooks hooks() {
        SolveHooks h;
        h.compile = [this](const Value& route, const Value& plan, double seconds) { return compile(route, plan, seconds); };
        h.derive_footprints = [this](const Value& route, double seconds) { return footprints(route, seconds, false); };
        h.derive_standing_footprints = [this](const Value& route, double seconds) { return footprints(route, seconds, true); };
        h.check_scene = [this](const Value& geometry, const Value& positions, const Value& movement, const Value& ground,
                               double seconds, const Value& contacts, const Object& contract) {
            return check_scene(geometry, positions, movement, ground, seconds, contacts, contract);
        };
        return h;
    }

    Value summary() const {
        Object calls, seconds;
        for (const auto& [name, n] : counts_) calls.emplace_back(name, integer_value(n));
        for (const auto& [name, s] : seconds_) seconds.emplace_back(name, real(s));
        return object({{"calls", object(std::move(calls))}, {"elapsedSeconds", object(std::move(seconds))}});
    }

private:
    // bridge/compiler.coefficient_request(route, plan).
    static Value coefficient_message(const Value& route_value, const Value& plan_value) {
        const Object &route = fields(route_value), &plan = fields(plan_value);
        Object message{{"grounded", at(plan, "grounded")}, {"jump", at(plan, "jump")}, {"inputs", at(route, "inputs")}};
        if (py_truthy(get_or_null(route, "sharedMovement"))) {
            put(message, "sharedMovement", boolean(true));
            const Value* contacts = get(plan, "sceneContacts");
            put(message, "contacts", contacts ? *contacts : array({}));
            for (const char* key : {"sneak", "ladderTicks", "vertical"})
                if (const Value* v = get(plan, key)) put(message, key, *v);
        }
        return object(std::move(message));
    }

    // PythonCompiler._exchange's budget around one reply, counted as CountingCompiler.invoke counts it.
    template <class Call>
    Value invoke(const std::string& name, double seconds, Call&& call) {
        const double started = seconds_now();
        if (counted_) count(name);
        struct Elapsed {
            Compiler& self;
            const std::string& name;
            double started;
            ~Elapsed() {
                if (self.counted_) self.add_seconds(name, seconds_now() - started);
            }
        } elapsed{*this, name, started};
        Value result = call();
        if (seconds_now() > started + seconds) throw Timeout("Movement coefficient compilation exceeded the budget");
        return result;
    }

    void count(const std::string& name) {
        for (auto& [key, n] : counts_)
            if (key == name) {
                ++n;
                return;
            }
        counts_.emplace_back(name, 1);
    }

    void add_seconds(const std::string& name, double elapsed) {
        for (auto& [key, s] : seconds_)
            if (key == name) {
                s += elapsed;
                return;
            }
        seconds_.emplace_back(name, elapsed);
    }

    bool counted_;
    std::vector<std::pair<std::string, std::int64_t>> counts_;
    std::vector<std::pair<std::string, double>> seconds_;
};

// ---- the interaction gate's Python side: InteractionGate's cache, preparations and analysis over its native core

// The cache and metrics an event search's gates share (the adapter's interaction_cache and interaction_metrics).
struct GateCache {
    std::vector<std::pair<std::string, Value>> entries;  // least recently used first
    Object metrics;
};

class Gate {
public:
    Gate(std::shared_ptr<InteractionGate> core, GateCache& cache, Compiler& compiler, double seconds)
        : core(std::move(core)), cache_(cache), compiler_(compiler), seconds_(seconds),
          core_checked_(this->core->checked), core_excluded_(static_cast<std::int64_t>(this->core->exclusions.size())),
          relaxation_start_(disk_stats()) {}

    // InteractionGate.prepare(program, scene=scene, require_complete=...): the preparation with this program's own
    // warm yaw, or null.
    Value prepare(const Value& program_value, const Value& scene, bool require_complete) {
        const Object& program = fields(program_value);
        if (py_truthy(get_or_null(program, "ladderEvents")))
            throw Unsupported("ladder programs prepare in Python; the native solve entry runs without ladders");
        const Program typed = program_from_value(program_value);
        const std::string key = digest(object({{"scene", without_key(scene, "movementSearch")},
                                               {"program", text(program_physics_key(typed))},
                                               {"complete", boolean(require_complete)}}));
        const Value prepared = cached("prepared\x1f" + key, "preparation", [&] {
            const std::optional<Value> result = gate_prepare(scene, typed, require_complete);
            if (!result) return Value{nullptr};
            Object out{{"key", text(key)}};
            for (const auto& [name, field] : fields(*result)) out.emplace_back(name, field);
            return object(std::move(out));
        });
        if (is_null(prepared)) return prepared;
        Object route = fields(at(fields(prepared), "route"));
        erase(route, "warmYaw");
        if (const Value* yaw = get(program, "guideSeedYaw")) put(route, "warmYaw", *yaw);
        return object(with(fields(prepared), "route", object(std::move(route))));
    }

    // InteractionGate.analyze_model(route, plan, vertical, open_terminal=True, initial_intervals=intervals), without a
    // prepared key, scope or parent ticks: the transfer-time bound's analysis.
    Value analyze_open(const Value& route_value, const Value& plan_value, const Value& vertical_value,
                       const Value& intervals) {
        const Object &route = fields(route_value), &plan = fields(plan_value), &vertical = fields(vertical_value);
        const Value request = coefficient_request(object({{"route", route_value}, {"plan", plan_value}}));
        const Value coefficient = cached("coefficients\x1f" + digest(request), "coefficient",
                                         [&] { return compiler_.compile(route_value, plan_value, seconds_); });
        Array rows;
        for (const Value& row : items(at(plan, "walls"))) {
            const Value* name = get(fields(row), "name");
            if (!(name && as_text(*name).rfind("landing:1:", 0) == 0)) rows.push_back(row);
        }
        const std::int64_t ticks = as_int(at(plan, "ticks"));
        Object model = fields(coefficient);
        put(model, "ticks", at(plan, "ticks"));
        put(model, "walls", array(std::move(rows)));
        put(model, "contacts", at(plan, "contacts"));
        put(model, "position", every_other(at(fields(at(route, "initial")), "position")));
        put(model, "velocity", every_other(at(fields(at(route, "initial")), "velocity")));
        const Value* threshold = get(route, "physicsThreshold");
        put(model, "threshold", threshold ? *threshold : real(.005));
        put(model, "vertical", at(vertical, "vertical"));
        const Value* ladder = get(plan, "ladderTicks");
        put(model, "ladderTicks", ladder ? *ladder : array(Array(static_cast<std::size_t>(ticks + 1), boolean(false))));
        put(model, "initialIntervals", intervals);
        const Value model_value = object(std::move(model));
        const Value& y = at(vertical, "y");
        const std::string key = interaction_context_key(
            object({{"model", model_value}, {"y", y}, {"scope", kNull}, {"parentTicks", kNull}}));
        return cached("analysis\x1f" + key, "analysis", [&] {
            Object envelope = fields(reachable_envelopes(model_value, y, std::nullopt));
            envelope.emplace_back("model", model_value);  // the Python result aliases its model
            put(envelope, "modelKey", text(digest(coefficient)));
            return object({{"key", text(key)},
                           {"model", model_value},
                           {"envelopes", object(std::move(envelope))},
                           {"coefficient", coefficient},
                           {"y", y}});
        });
    }

    // InteractionGate.summary(): this gate's share of the core's query log and the relaxation counts since it began.
    Value summary() const {
        Object metrics = cache_.metrics;
        for (const auto& [name, value] : core->metrics()) {
            const Value* own = get(metrics, name);
            put(metrics, name, own ? integer_value(as_int(*own) + as_int(value)) : value);
        }
        const Array& all = core->exclusions;
        Array exclusions(all.begin() + std::min<std::int64_t>(core_excluded_, static_cast<std::int64_t>(all.size())),
                         all.end());
        Object relaxation;
        const Value stats = disk_stats();
        for (const auto& [kind, counts] : fields(stats)) {
            const Value* before = get(fields(relaxation_start_), kind);
            Object grown;  // Counter subtraction keeps the positive counts
            for (const auto& [name, n] : fields(counts)) {
                const Value* was = before ? get(fields(*before), name) : nullptr;
                const std::int64_t delta = as_int(n) - (was ? as_int(*was) : 0);
                if (delta > 0) grown.emplace_back(name, integer_value(delta));
            }
            relaxation.emplace_back(kind, object(std::move(grown)));
        }
        const std::int64_t excluded = static_cast<std::int64_t>(exclusions.size());
        return object({{"checked", integer_value(core->checked - core_checked_)},
                       {"excluded", integer_value(excluded)},
                       {"exclusions", array(std::move(exclusions))},
                       {"metrics", object(std::move(metrics))},
                       {"relaxation", object(std::move(relaxation))}});
    }

    std::shared_ptr<InteractionGate> core;

private:
    // InteractionGate._cached: a hit moves to the end, a miss builds (a failed build commits nothing), and the oldest
    // entries go past the bound.
    template <class Build>
    Value cached(const std::string& key, const std::string& kind, Build&& build) {
        auto& entries = cache_.entries;
        auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.first == key; });
        const bool hit = found != entries.end();
        const std::string metric = kind + (hit ? "Hits" : "Misses");
        const Value* seen = get(cache_.metrics, metric);
        put(cache_.metrics, metric, integer_value(seen ? as_int(*seen) + 1 : 1));
        Value result;
        if (hit) {
            result = found->second;
            entries.erase(found);
        } else {
            result = build();
        }
        entries.emplace_back(key, result);
        while (entries.size() > kGateCacheSize) entries.erase(entries.begin());
        return result;
    }

    GateCache& cache_;
    Compiler& compiler_;
    double seconds_;
    std::int64_t core_checked_, core_excluded_;
    Value relaxation_start_;
};

// ---- movement and bounds helpers

// movement/program.empty_program(initial, threshold=threshold) for a scene's vector initial state.
Value empty_program(const Value& initial_value, double threshold) {
    const Object& initial = fields(initial_value);
    const Array &position = items(at(initial, "position")), &velocity = items(at(initial, "velocity"));
    const Value* ground = get(initial, "onGround");
    const Value* hit = get(initial, "previousHorizontalCollision");
    const Value scalar = object({{"position", position.at(1)},
                                 {"velocity", velocity.at(1)},
                                 {"onGround", ground ? *ground : boolean(true)},
                                 {"previousHorizontalCollision", hit ? *hit : boolean(false)}});
    const Object& s = fields(scalar);
    Object profile{{"duration", integer_value(0)},
                   {"position", array({at(s, "position")})},
                   {"velocity", array({at(s, "velocity")})},
                   {"grounded", array({at(s, "onGround")})},
                   {"jump", array({})},
                   {"sneak", array({})},
                   {"ladderTicks", array({boolean(false)})},
                   {"contacts", array({})},
                   {"contactEvents", array({})},
                   {"caseEvents", array({})},
                   {"positionConstraints", array({})},
                   {"generatedUnits", array({})},
                   {"composition", text("movement_program")},
                   {"landingPhase", text("post")}};
    Object program{{"initial", scalar},
                   {"profile", object(std::move(profile))},
                   {"ladderEvents", array({})},
                   {"movementUnits", array({})},
                   {"terminal", text("open")},
                   {"terminalSupport", kNull},
                   {"interactions", integer_value(0)},
                   {"trace", kNull}};
    if (threshold != .005) put(program, "threshold", real(threshold));
    return object(std::move(program));
}

ScalarState scalar_state(const Value& program) {
    const Object& initial = fields(at(fields(program), "initial"));
    return ScalarState{py_truthy(at(initial, "onGround")), num(at(initial, "position")),
                       py_truthy(at(initial, "previousHorizontalCollision")), num(at(initial, "velocity"))};
}

// reachability/intervals.motion_bound(scene, compiler, seconds).
Value motion_bound(const Value& scene, Compiler& compiler, double seconds) {
    const Value sample_value = compiler.compile(
        object(with(fields(scene), "sharedMovement", boolean(true))),
        object({{"grounded", array({boolean(true), boolean(true), boolean(false)})},
                {"jump", array({boolean(false), boolean(true), boolean(false)})}}),
        seconds);
    const Object& sample = fields(sample_value);
    const Array &magnitude = items(at(sample, "magnitude")), &friction = items(at(sample, "friction"));
    const Array& velocity = items(at(fields(at(fields(scene), "initial")), "velocity"));
    double carried = -INFINITY, largest = -INFINITY;
    for (std::size_t i = 0; i < std::min(magnitude.size(), friction.size()); ++i) {
        const double m = num(magnitude[i]).value, f = num(friction[i]).value;
        carried = std::max(carried, f * m / (1 - f));
    }
    for (const Value& m : magnitude) largest = std::max(largest, num(m).value);
    const double speed = std::max(python_hypot(num(velocity.at(0)).value, num(velocity.at(2)).value), carried);
    return object({{"storedSpeedNorm", real(speed + 1e-8)},
                   {"movementAfterSupportCheck", real(speed + largest + 1e-8)},
                   {"groundMagnitude", magnitude.at(0)}});
}

// movement/profiles.ordinary_jump_profile(source_top, target_top, threshold=threshold): its duration and positions,
// or nullopt when the jump never lands.
std::optional<std::pair<std::int64_t, std::vector<double>>> ordinary_jump_profile(const Num& source_top,
                                                                                  const Num& target_top,
                                                                                  const Num& threshold) {
    constexpr std::int64_t kMaxInputs = 200;
    std::vector<std::optional<Num>> jumps(kMaxInputs);
    jumps[0] = flt(kJumpVelocity);
    const std::vector<bool> sneak(kMaxInputs, false);
    std::vector<VerticalContactRow> contacts;
    Num y = source_top, vy = flt(rest_carry());
    bool ground = true;
    std::vector<double> positions{y.value};
    for (std::int64_t tick = 0; tick < kMaxInputs; ++tick) {
        Array failures;
        StepResult step = vertical_step(contacts, jumps, sneak, threshold, tick, y, vy, false, ground, 0, &failures);
        if (step.carry.value < 0 && y.value >= target_top.value && step.proposed.value <= target_top.value) {
            contacts = {VerticalContactRow{tick + 1, -1, target_top}};
            failures = Array{};
            step = vertical_step(contacts, jumps, sneak, threshold, tick, y, vy, false, ground, 0, &failures);
        }
        if (!failures.empty())
            throw std::invalid_argument("The existing vertical model rejected the derived profile: " +
                                        dumps(array(failures), false, false));
        vy = vertical_carry(step.carry, false, false);
        y = step.proposed;
        ground = step.ground;
        positions.push_back(y.value);
        if (ground) return std::make_pair(tick + 1, std::move(positions));
    }
    return std::nullopt;
}

// reachability/service.query_interaction(request, context) without a session or budget.
Value query_interaction(const Value& request_value, const Value& context) {
    const Object& request = fields(request_value);
    const std::string& role = as_text(at(request, "role"));
    if (role != "required_contact" && role != "unwanted_contact" && role != "approach_prerequisite")
        throw std::invalid_argument("Unknown interaction role");
    const Array& windows = items(at(request, "windows"));
    for (const Value& w : windows) {
        const Object& window = fields(w);
        const auto* tick = std::get_if<std::int64_t>(&at(window, "tick").data);
        const std::string& phase = as_text(at(window, "phase"));
        if (!tick || *tick < 0 || (phase != "Y" && phase != "X" && phase != "Z"))
            throw std::invalid_argument("Interaction windows require nonnegative ticks and Y/X/Z phases");
    }
    const std::string context_key = interaction_context_key(context);
    const std::string key = digest(object({{"policy", integer_value(kProofPolicy)},
                                           {"context", text(context_key)},
                                           {"request", request_value}}));
    const Value& envelope = get_or_null(fields(context), "envelopes");
    const Value* interior_value = get(request, "interior");
    const bool interior = interior_value ? py_truthy(*interior_value) : role == "unwanted_contact";
    Array phases;
    std::string status = "unknown", reason = "unknown_motion_family";
    if (!is_null(envelope) && !windows.empty()) {
        Box box;
        const Array& raw = items(at(request, "box"));
        for (std::size_t i = 0; i < 6; ++i) box[i] = num(raw.at(i));
        Array bounds;
        for (const Num& n : expanded_box(box)) bounds.push_back(value(n));
        bool all = true;
        for (const Value& w : windows) {
            const Object& window = fields(w);
            const std::int64_t tick = as_int(at(window, "tick"));
            if (tick >= static_cast<std::int64_t>(items(at(fields(envelope), "phases")).size())) {
                phases.push_back(object(with(with(window, "ruledOut", boolean(false)), "reason",
                                             text("window_outside_context"))));
                all = false;
                continue;
            }
            const Value region = object({{"tick", integer_value(tick)},
                                         {"phase", at(window, "phase")},
                                         {"bounds", array(bounds)},
                                         {"interior", boolean(interior)}});
            const Value interaction = object({{"alternatives", array({object({{"regions", array({region})}})})}});
            Object checked = fields(interaction_reachability(envelope, interaction));
            for (const auto& [k, v] : window) put(checked, k, v);
            all = all && py_truthy(at(checked, "ruledOut"));
            phases.push_back(object(std::move(checked)));
        }
        status = all ? "absent" : "not_ruled_out";
        reason = all ? "all_scoped_phases_excluded" : "interaction_retained";
    }
    return object({{"policy", integer_value(kProofPolicy)},
                   {"id", text(key)},
                   {"context", text(context_key)},
                   {"role", text(role)},
                   {"ruledOut", boolean(status == "absent")},
                   {"status", text(status)},
                   {"reason", text(reason)},
                   {"phases", array(std::move(phases))},
                   {"owner", get_or_null(request, "owner")},
                   {"interior", boolean(interior)},
                   {"invalidation", text("exact_context_only")}});
}

// search/bounds/flight.state_covers(guides, name, motion).
Array state_covers(const Object& guides, const std::string& name, const Object& motion) {
    const Value* covers = get(fields(at(guides, "covers")), name);
    if (!covers || items(*covers).empty()) return {};
    double pad = 0.;
    const Value* phase = get(guides, "coverPhase");
    if (!phase || as_text(*phase) != "post") {
        if (phase) {
            const Value* after = get(motion, "movementAfterSupportCheck");
            pad = after ? num(*after).value : 0.;
            if (!std::isfinite(pad) || pad <= 0) return {};
        }
    }
    Array out;
    for (const Value& b : items(*covers)) {
        const Array& box = items(b);
        out.push_back(array({real(num(box[0]).value - pad), box[1], real(num(box[2]).value - pad),
                             real(num(box[3]).value + pad), box[4], real(num(box[5]).value + pad)}));
    }
    return out;
}

// search/bounds/flight.transfer_bound(scene, guides, source, target, motion, service, threshold=0., check=check).
Value transfer_bound(const Value& scene_value, const Object& guides, const std::string& source, const std::string& target,
                     const Object& motion, Gate& gate, const std::function<void()>& check) {
    const Object& scene = fields(scene_value);
    const Value* a = nullptr;
    const Value* b = nullptr;
    for (const Value& block : items(at(scene, "blocks"))) {
        const std::string& id = as_text(at(fields(block), "id"));
        if (id == source) a = &block;
        if (id == target) b = &block;
    }
    if (!a || !b) throw std::out_of_range("transfer bound block");
    const Object &from = fields(*a), &to = fields(*b);
    const Array& solids = items(at(fields(at(scene, "collisionScene")), "solids"));
    Object record{{"version", integer_value(1)},
                  {"source", text(source)},
                  {"target", text(target)},
                  {"sourceTop", at(from, "top")},
                  {"sourceBox", solids.at(static_cast<std::size_t>(as_int(at(from, "solidIndex"))))},
                  {"threshold", real(0.)},
                  {"assumption", text("hard_transfer_without_stepping")},
                  {"ticks", integer_value(0)},
                  {"status", text("unknown")},
                  {"proofIds", array({})},
                  {"ceilings", array({})}};
    const auto with_reason = [&](const char* reason) { return object(with(record, "reason", text(reason))); };
    const auto profile = ordinary_jump_profile(num(at(from, "top")), num(at(to, "top")), flt(0.));
    if (!profile) return with_reason("no_ordinary_profile");
    const auto& [duration, positions] = *profile;
    put(record, "ordinaryTicks", integer_value(duration));
    const double body = f32(1.8);  // scene/geometry.BODY_HEIGHT
    struct Possible {
        std::int64_t solid;
        Value box;
        std::int64_t tick;
    };
    std::vector<Possible> possible;
    for (std::size_t i = 0; i < solids.size(); ++i) {
        if (check) check();
        const double plane = num(items(solids[i]).at(1)).value - body;
        for (std::int64_t t = 0; t + 1 < duration; ++t)
            if (positions[t] <= plane && plane < positions[t + 1]) {
                possible.push_back({static_cast<std::int64_t>(i), solids[i], t});
                break;
            }
    }
    bool ceilings = true;
    if (const Value* search = get(scene, "movementSearch")) {
        if (const Value* types = get(fields(*search), "types")) {
            ceilings = false;
            for (const Value& t : items(*types)) ceilings = ceilings || as_text(t) == "ceiling";
        }
    }
    const Value* hits = get(scene, "ceilingHits");
    if (!ceilings || (hits && !py_truthy(*hits))) {
        possible.clear();
        put(record, "ceilingDomain", text("disabled"));
    }
    if (possible.empty()) {
        put(record, "ticks", integer_value(duration));
        put(record, "status", text("bounded"));
        return with_reason("no_shortening_ceiling_height_window");
    }
    const Array covers = state_covers(guides, source, motion);
    const Value* speed_value = get(motion, "storedSpeedNorm");
    if (covers.empty() || !speed_value || is_null(*speed_value) || !std::isfinite(num(*speed_value).value))
        return with_reason("unknown_launch_enclosure");
    const double speed = num(*speed_value).value;
    Array ceiling_records, proof_ids;
    for (const Value& cover_value : covers) {
        if (check) check();
        const Array& cover = items(cover_value);
        const Array& bounds = items(at(from, "bounds"));
        Object initial = fields(at(scene, "initial"));
        put(initial, "position", array({real((num(bounds[0]).value + num(bounds[1]).value) / 2), at(from, "top"),
                                        real((num(bounds[2]).value + num(bounds[3]).value) / 2)}));
        put(initial, "velocity", array({real(0.), real(rest_carry()), real(0.)}));
        const Value initial_value = object(std::move(initial));
        Object family = scene;
        put(family, "initial", initial_value);
        put(family, "startBlock", text(source));
        put(family, "goalBlock", text(target));
        const Value family_scene = object(std::move(family));
        const Value prefix_value = object(with(fields(empty_program(initial_value, 0.)), "terminalSupport", text(source)));
        GenerationContext context;
        context.program = std::make_shared<const Program>(program_from_value(prefix_value));
        context.state = scalar_state(prefix_value);
        context.max_inputs = duration;
        context.threshold = flt(0.);
        const std::optional<MovementChoice> choice =
            event_choice(context, "normal", context.state, duration, true, ChoiceParts{});
        if (!choice) return with_reason("unknown_scalar_approach");
        const std::optional<Program> program = append_choice(*context.program, *choice);
        if (!program) throw std::invalid_argument("append_choice rejected the transfer's ordinary jump");
        const Value prepared = gate.prepare(to_value(*program), family_scene, false);
        if (is_null(prepared)) return with_reason("unknown_scalar_approach");
        const Object& p = fields(prepared);
        const Value intervals = array({array({cover[0], cover[2]}), array({cover[3], cover[5]}),
                                       array({real(-speed), real(-speed)}), array({real(speed), real(speed)})});
        const Value analysis = gate.analyze_open(at(p, "route"), at(p, "plan"), at(p, "vertical"), intervals);
        const Value context_value = object({{"envelopes", at(fields(analysis), "envelopes")}});
        for (const Possible& c : possible) {
            if (check) check();
            const Value request = object(
                {{"box", c.box},
                 {"role", text("approach_prerequisite")},
                 {"interior", boolean(false)},
                 {"owner", integer_value(c.solid)},
                 {"windows", array({object({{"tick", integer_value(c.tick)}, {"phase", text("Y")}})})}});
            const Value proof = query_interaction(request, context_value);
            const Object& q = fields(proof);
            ceiling_records.push_back(object({{"solid", integer_value(c.solid)},
                                              {"input", integer_value(c.tick)},
                                              {"status", at(q, "status")},
                                              {"proof", at(q, "id")}}));
            proof_ids.push_back(at(q, "id"));
            put(record, "ceilings", list_of(ceiling_records));
            put(record, "proofIds", list_of(proof_ids));
            if (as_text(at(q, "status")) != "absent") return with_reason("shortening_ceiling_not_excluded");
        }
    }
    put(record, "ticks", integer_value(duration));
    put(record, "status", text("bounded"));
    put(record, "reason", text("shortening_ceilings_unreachable"));
    return object(std::move(record));
}

// search/bounds/flight.transfer_bounds(scene, guides, motion, service, saved=saved, threshold=0., check=check).
Value transfer_bounds(const Value& scene, const Value& guides_value, const Value& motion, Gate& gate, const Value& saved,
                      const std::function<void()>& check) {
    Object out = is_null(saved) ? Object{} : fields(saved);
    const Object& guides = fields(guides_value);
    const Array& anchors = items(at(guides, "anchors"));
    for (std::size_t i = 0; i + 1 < anchors.size(); ++i) {
        const std::string &source = as_text(anchors[i]), &target = as_text(anchors[i + 1]);
        const std::string key = source + "→" + target;
        if (get(out, key)) continue;
        if (check) check();
        Object result = fields(transfer_bound(scene, guides, source, target, fields(motion), gate, check));
        put(result, "id", text("transfer-time:" + digest(object(result))));
        out.emplace_back(key, object(std::move(result)));
    }
    return object(std::move(out));
}

// observability/turns.summary(state).
Value turns_summary(const Value& observations) {
    Object result;
    const Value* decisions = get(fields(observations), "decisions");
    if (!decisions) return object({});
    struct Group {
        std::int64_t ordinary = 0, turnaround = 0, replay_flips = 0;
        Num selected = whole(0), solver_seconds = flt(0.), wrapper_calls = whole(0), worker_launches = whole(0),
            feasible_seed_returns = whole(0);
        Object outcomes;
        Array raw, shifted;
    };
    std::vector<std::pair<std::string, Group>> groups;
    const auto metric = [](const Object& query, const char* name) {
        const Value* metrics = get(query, "metrics");
        const Value* v = metrics ? get(fields(*metrics), name) : nullptr;
        return v ? num(*v) : whole(0);
    };
    for (const auto& [_, e_value] : fields(*decisions)) {
        const Object& e = fields(e_value);
        const std::string key = std::to_string(as_int(at(e, "span")));
        auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == key; });
        if (found == groups.end()) found = groups.insert(groups.end(), {key, Group{}});
        Group& g = found->second;
        (as_text(at(e, "mode")) == "turnaround" ? g.turnaround : g.ordinary) += 1;
        if (const Value* selected = get(e, "selected")) g.selected = add(g.selected, num(*selected));
        for (const Value& d : items(at(e, "distances"))) {
            g.raw.push_back(at(fields(d), "rawDistance"));
            g.shifted.push_back(at(fields(d), "shiftedDistance"));
        }
        for (const Value& q_value : items(at(e, "queries"))) {
            const Object& q = fields(q_value);
            const std::string& status = as_text(at(q, "status"));
            const Value* seen = get(g.outcomes, status);
            put(g.outcomes, status, integer_value(seen ? as_int(*seen) + 1 : 1));
            g.solver_seconds = add(g.solver_seconds, num(at(q, "elapsedSeconds")));
            g.wrapper_calls = add(g.wrapper_calls, metric(q, "solverCalls"));
            g.worker_launches = add(g.worker_launches, metric(q, "workerLaunches"));
            g.feasible_seed_returns = add(g.feasible_seed_returns, metric(q, "feasibleSeedReturns"));
        }
        for (const Value& r : items(at(e, "replays"))) g.replay_flips += py_truthy(at(fields(r), "wouldFlip"));
    }
    for (auto& [key, g] : groups)
        result.emplace_back(key, object({{"ordinary", integer_value(g.ordinary)},
                                         {"turnaround", integer_value(g.turnaround)},
                                         {"outcomes", object(std::move(g.outcomes))},
                                         {"solverSeconds", value(g.solver_seconds)},
                                         {"rawDistances", array(std::move(g.raw))},
                                         {"shiftedDistances", array(std::move(g.shifted))},
                                         {"replayFlips", integer_value(g.replay_flips)},
                                         {"selected", value(g.selected)},
                                         {"wrapperCalls", value(g.wrapper_calls)},
                                         {"workerLaunches", value(g.worker_launches)},
                                         {"feasibleSeedReturns", value(g.feasible_seed_returns)}}));
    return object(std::move(result));
}

// ---- the run

Value replay_witness(const Value& scene, const Value& best, Compiler& compiler);  // defined with the public one

// RouteDomain.from_dict(value)'s checks on a saved domain.
void check_route_domain(const Value& domain) {
    const Object& d = fields(domain);
    if (!py_equal(get_or_null(d, "schema"), integer_value(1))) throw std::invalid_argument("Incompatible event domain schema");
    const Value& identity = get_or_null(d, "domainId");
    if (!py_equal(identity, text(geometry_key(without_key(domain, "domainId")))))
        throw std::invalid_argument("Event domain content does not match domainId");
}

// One solve's environment: the services every session's search shares.
struct Run {
    const SolveCaseOptions& options;
    Compiler counted{true};
    GateCache gate_cache;
    std::shared_ptr<InteractionGate> core;
    std::shared_ptr<SearchSaver> saver;  // the current search's, when the run saves progress
    std::vector<std::string> windows;    // the windows the run's saves stored, in order
    bool cancelled() const { return options.cancelled && options.cancelled(); }
    NumericalSolve full_solve() const {
        return [this](const Value& problem, double seconds, bool stop_on_feasible,
                      const std::function<void(const Value&)>& on_incumbent) {
            return solve_numerical(problem, seconds, stop_on_feasible, options.worker,
                                   [this] { return cancelled(); }, on_incumbent);
        };
    }
};

// search/engine.solve_services(None, program, work, gate, numerical, seconds=seconds, cancel_event=...) as the native
// search's node solve: the scoped work, the relaxed dispatch on the gate's core and the core's analysis.
NodeSolve solve_services(Run& run, Gate& gate, const std::shared_ptr<SearchWork>& work, const std::string& numerical,
                         double seconds) {
    const double now = seconds_now();
    auto scoped = std::make_shared<ScopedWork>(work, std::min(work->deadline.value_or(INFINITY), now + seconds));
    const std::optional<double> deadline = scoped->deadline();
    NodeSolve out;
    out.seconds = seconds;
    EvaluateHooks& h = out.hooks;
    h.solve = run.counted.hooks();
    h.solve.solve = run.full_solve();
    h.solve.work_check = [scoped] { scoped->check(); };
    h.solve.work_proposal = [scoped](const Value&) { scoped->proposal(); };
    h.solve.work_before_solve = [scoped](const Value&) { scoped->before_solve(); };
    h.solve.work_accepted = [scoped](const Value&) { scoped->accepted(); };
    h.solve.cancelled = [&run] { return run.cancelled(); };
    if (numerical == "relaxed" || numerical == "relaxed_pipeline" || numerical == "full_strength") {
        RelaxedServices relaxed;
        relaxed.full = run.full_solve();
        relaxed.cancelled = [&run] { return run.cancelled(); };
        std::shared_ptr<InteractionGate> core = gate.core;
        relaxed.answer = [core](const std::string& kind, const std::string& fingerprint) {
            return core->answer(kind, fingerprint);
        };
        relaxed.remember = [core](const std::string& kind, const std::string& fingerprint, const Value& result) {
            core->remember_answer(kind, fingerprint, result);
        };
        relaxed.pipeline = numerical == "relaxed_pipeline";
        const auto entry = [relaxed](DispatchMode mode) -> NumericalSolve {
            return [relaxed, mode](const Value& problem, double s, bool stop_on_feasible,
                                   const std::function<void(const Value&)>& on_incumbent) {
                return relaxed_dispatch(problem, s, stop_on_feasible, mode, on_incumbent, relaxed);
            };
        };
        if (numerical == "full_strength") {
            h.numerical = entry(DispatchMode::full_strength);
        } else {
            h.numerical = entry(DispatchMode::solve);
            h.confirm = entry(DispatchMode::confirmed);
        }
    }
    std::shared_ptr<InteractionGate> core = gate.core;
    h.analyze_model = [core](const Value& route, const Value& plan, const Value& vertical, const Value& scope) {
        return core->analyze_model(route, plan, vertical, scope);
    };
    h.reachable_faces = [core](const std::string& face) {
        const FaceTable& table = core->reachable_faces(face);
        Array solids;
        for (std::int64_t solid : table.solids) solids.push_back(Value{solid});
        return object({{"ticks", table.ticks ? Value{*table.ticks} : kNull}, {"solids", array(std::move(solids))}});
    };
    h.check_generation = [&run, scoped, deadline] {  // variants.check_generation(cancel_event, work.check, deadline)
        if (run.cancelled()) throw Timeout("cancelled");
        scoped->check();
        if (deadline && seconds_now() >= *deadline) throw Timeout("constraint_generation_deadline");
    };
    h.record_counts = [work, scoped](const Value& counts) {
        work->update(counts);
        scoped->local.update(counts);
    };
    h.accepted = [work](const Value&) { work->accepted(); };
    h.work_summary = [scoped] { return scoped->local.summary(); };
    return out;
}

Value frames_of(const Object& state) {  // frames + stack + goalQueue, in that order
    Array out;
    for (const char* group : {"frames", "stack", "goalQueue"})
        if (const Value* frames = get(state, group))
            for (const Value& f : items(*frames)) out.push_back(f);
    return array(std::move(out));
}

struct Session {
    Value scene, cfg, motion, data, graph, anchor_policy, guide_plan, checkpoint_identity, initial_jump_inputs;
    Object caps;
    std::int64_t event_audits = 0, required_event_rejections = 0;  // the adapter's counts
    double deadline = 0, started = 0, candidate_seconds = 1, score_seconds = 30;
    std::int64_t batch_size = 24;
    bool minimum_run_pruning = true;
    std::string numerical_backend;
    Object request;
};

// search/dfs.search_goals' native branch for one session of the event search.
Value search_goals(Run& run, Session& s, const std::shared_ptr<SearchWork>& work, const Value& resume_state,
                   const Value& outer_state) {
    const Object& scene = fields(s.scene);
    Object seed = fields(empty_program(at(scene, "initial"), 0.));
    put(seed, "terminalSupport", at(scene, "startBlock"));
    const Value seed_value = object(std::move(seed));
    Array roots;
    if (!is_null(resume_state)) {
        const Value* saved = get(fields(resume_state), "initialRoots");
        roots = saved ? items(*saved) : Array{seed_value};
    } else {
        roots = {seed_value};
    }
    const Value& seed_initial = at(fields(seed_value), "initial");
    if (roots.empty()) throw std::invalid_argument("DFS roots must preserve original state and zero inertia thresholds");
    for (const Value& p : roots) {
        const Value* threshold = get(fields(p), "threshold");
        if (!py_equal(at(fields(p), "initial"), seed_initial) || (threshold ? num(*threshold).value : .005) != 0.)
            throw std::invalid_argument("DFS roots must preserve original state and zero inertia thresholds");
    }
    auto anchors = std::make_shared<AnchorScene>(s.scene);
    for (const Value& p : roots) {
        const Program typed = program_from_value(p);
        bool accepted = anchors->within_caps(s.request, typed);
        if (accepted) {
            const Value label = comparison_label(typed, 0, std::nullopt, "", "ordinary", {}, {});
            const Object& c = fields(label);
            if (!is_null(at(c, "J"))) {
                for (const auto& [name, field] : {std::pair<const char*, const char*>{"maxTotalRunTicks", "R"},
                                                  std::pair<const char*, const char*>{"maxJumps", "J"}})
                    if (const Value* cap = get(s.request, name))
                        accepted = accepted && !(num(at(c, field)).value > num(*cap).value);
            }
        }
        if (accepted && !is_null(s.initial_jump_inputs)) {  // InitialJumpDomain(program)
            const Object& profile = fields(at(fields(p), "profile"));
            const Array &jump = items(at(profile, "jump")), &grounded = items(at(profile, "grounded"));
            std::optional<std::int64_t> first;
            for (std::size_t t = 0; t < jump.size(); ++t)
                if (py_truthy(jump[t]) && py_truthy(grounded[t])) {
                    first = static_cast<std::int64_t>(t);
                    break;
                }
            const Array& inputs = items(s.initial_jump_inputs);
            if (first) {
                accepted = contains(inputs, integer_value(*first));
            } else {
                std::int64_t latest = 0;
                for (const Value& t : inputs) latest = std::max(latest, as_int(t));
                accepted = as_int(at(profile, "duration")) <= latest;
            }
        }
        if (!accepted) throw std::invalid_argument("DFS roots violate the decision domain");
    }
    const Value corridor = object({{"kind", text("event_guidance")},
                                   {"schema", integer_value(1)},
                                   {"domainId", at(fields(s.data), "domainId")},
                                   {"request", text("")},
                                   {"guide", text("")}});
    Object domain = fields(s.cfg);
    for (const char* key : {"maxPrefixes", "maxQueuedPrefixes", "maxChoicesPerType", "expansionsPerProposal"})
        erase(domain, key);
    Array root_keys;
    for (const Value& p : roots) root_keys.push_back(text(program_search_key(program_from_value(p))));
    const Value policy_settings = semantic_settings(s.anchor_policy);
    const std::string context = digest(object({
        {"version", integer_value(8)},
        {"kind", text("ordered_goal_dfs")},
        {"movementGenerationPolicy", integer_value(17)},
        {"constraintGenerationPolicy", integer_value(kGenerationPolicy)},
        {"scene", without_key(s.scene, "movementSearch")},
        {"domain", object(domain)},
        {"roots", array(root_keys)},
        {"goals", array({})},
        {"terminal", kNull},
        {"corridor", corridor},
        {"filter", s.checkpoint_identity},
        {"physics", s.motion},
        {"eventStagePolicy", integer_value(4)},
        {"anchorPolicy", policy_settings},
        {"guidePlan", at(fields(s.guide_plan), "id")},
        {"minimumRunPolicy", s.minimum_run_pruning ? integer_value(kMinimumRunPolicy) : kNull},
        {"comparisonPolicy", integer_value(kComparisonPolicy)},
        {"numericalBackend", text(s.numerical_backend)},
        {"routeStagePolicy", integer_value(2)},
        {"routeCommitPolicy", integer_value(1)},
        {"routeProposalPolicy", integer_value(1)},
        {"frameEndPolicy", integer_value(1)},
        {"queuePolicy", integer_value(1)},
    }));
    Object state;
    if (!is_null(resume_state)) {
        state = fields(resume_state);
        const Value* version = get(state, "version");
        if (!version || !py_equal(*version, integer_value(8)) || !py_equal(get_or_null(state, "kind"), text("ordered_goal_dfs")) ||
            !py_equal(get_or_null(state, "context"), text(context)))
            throw std::invalid_argument("Incompatible DFS continuation; old checkpoint queues require explicit reconstruction");
    } else {
        state = Object{{"version", integer_value(8)},     {"kind", text("ordered_goal_dfs")},
                       {"context", text(context)},        {"initialRoots", array(roots)},
                       {"stack", array({})},              {"suspended", array({})},
                       {"frames", array({})},             {"transitions", array({})},
                       {"windows", array({})},            {"finalAttempts", array({})},
                       {"best", kNull},                   {"cumulativeCalls", integer_value(0)},
                       {"session", integer_value(0)},     {"expanded", integer_value(0)},
                       {"nextFrame", integer_value(0)}};
    }
    setdefault(state, "goalQueue", array({}));
    setdefault(state, "goalOwners", object({}));
    setdefault(state, "discovery", object({}));
    setdefault(state, "hardLatches", object({}));
    setdefault(state, "latchRevision", integer_value(0));
    setdefault(state, "nodeResults", object({}));
    const Value observability = at(fields(s.anchor_policy), "observability");
    setdefault(state, "observations", object({{"enabled", observability}, {"decisions", object({})}}));
    put(state, "observations", object(with(fields(at(state, "observations")), "enabled", observability)));
    put(state, "session", integer_value(as_int(at(state, "session")) + 1));
    const std::int64_t prior_calls = as_int(at(state, "cumulativeCalls")),
                       initial_expanded = as_int(at(state, "expanded"));
    if (!run.core) run.core = std::make_shared<InteractionGate>(s.scene);
    Gate gate(run.core, run.gate_cache, run.counted, s.candidate_seconds);
    setdefault(state, "timeBoundExclusions", array({}));
    setdefault(state, "minimumRunExclusions", array({}));

    auto adapter = std::make_shared<AnchorAdapter>(s.data, s.graph, s.anchor_policy, s.guide_plan, policy_settings, anchors);
    const Value scene_value = s.scene;
    DfsHooks h;
    h.monotonic = [] { return seconds_now(); };
    h.cancelled = [&run] { return run.cancelled(); };
    h.progress = run.options.save_progress && run.options.store;
    if (h.progress) {
        run.saver = std::make_shared<SearchSaver>(run.options.store, run.options.metadata, outer_state, run.windows);
        h.saver = run.saver;
    }
    h.check = [work] { work->check(); };
    h.solver_calls = [work] { return work->counts("solverCalls"); };
    h.count = [work](const std::string& name, std::int64_t by) { work->count(name, by); };
    h.accepted = [work](const Value&) { work->accepted(); };
    h.termination = [work] { return work->termination ? text(*work->termination) : kNull; };
    h.evaluate = [](std::int64_t, const Value&, const Value&, const Value&, const Value&) -> Value {
        throw Unsupported("a route with wall hits or ladder events: the native solve entry runs without either");
    };
    h.solve_route = [](std::int64_t, const Value&, const Value&, const Value&,
                       const std::function<void(const Value&)>&) -> Value {
        throw Unsupported("a route with wall hits or ladder events: the native solve entry runs without either");
    };
    h.transfer_bounds = [&s, &gate, work](const Value& saved) {
        return transfer_bounds(s.scene, s.guide_plan, s.motion, gate, saved, [work] { work->check(); });
    };
    h.prepare = [&gate, scene_value](const Value& program) { return gate.prepare(program, scene_value, false); };
    h.prepare_leg = [&gate, scene_value](const Value& program, const Value& landing) {
        return gate.prepare(program, object(with(fields(scene_value), "goalBlock", landing)), true);
    };
    h.begin_solve = [&run, &gate, &s, work](const Value&) {
        return solve_services(run, gate, work, s.numerical_backend,
                              std::max(.001, std::min(s.score_seconds, s.deadline - seconds_now())));
    };
    h.begin_leg = [&run, &gate, work](const Value&, const std::string& numerical, double seconds, bool own_work) {
        // A frame-end re-solve runs on its own work, outside the run's deadline and call budget.
        return solve_services(run, gate, own_work ? std::make_shared<SearchWork>() : work, numerical, seconds);
    };
    DfsOptions o;
    o.scene = s.scene;
    o.cfg = s.cfg;
    o.motion = s.motion;
    o.guide_plan = s.guide_plan;
    o.anchor_policy = s.anchor_policy;
    o.policy_settings = policy_settings;
    o.domain_id = at(fields(s.data), "domainId");
    o.roots = std::vector<Value>(roots.begin(), roots.end());
    o.task_block = as_text(at(fields(at(fields(s.data), "task")), "block"));
    o.initial_jump_inputs = s.initial_jump_inputs;
    o.caps = s.caps;
    o.minimum_run_pruning = s.minimum_run_pruning;
    o.deadline = s.deadline;
    o.started = s.started;
    o.candidate_seconds = s.candidate_seconds;
    o.score_seconds = s.score_seconds;
    o.batch_size = s.batch_size;
    o.numerical_backend = s.numerical_backend;
    o.prior_calls = prior_calls;
    o.initial_expanded = initial_expanded;
    o.comparison_policy = kComparisonPolicy;
    DfsSearch search(dfs_state_from_value(object(std::move(state))), std::move(o), std::move(h), run.core, anchors,
                     adapter);
    std::string termination;
    struct Counts {  // the adapter summary's counts, added however the run ends
        Session& s;
        const AnchorAdapter& adapter;
        ~Counts() {
            s.event_audits += adapter.event_audits();
            s.required_event_rejections += adapter.required_event_rejections();
        }
    };
    {
        Counts counts{s, *adapter};
        termination = search.run();
    }
    const Value final_state = search.state();
    const Object& f = fields(final_state);
    Object adapter_summary{{"eventAudits", integer_value(s.event_audits)},
                           {"requiredEventRejections", integer_value(s.required_event_rejections)},
                           {"backend", text("anchor_goals_v1")},
                           {"domainId", at(fields(s.data), "domainId")},
                           {"filtering", text("physical_constraints_and_required_events")},
                           {"coverage", at(fields(s.data), "coverage")},
                           {"anchorPolicy", s.anchor_policy},
                           {"guidePlan", s.guide_plan},
                           {"visitScope", text("acyclic_owners_with_current_owner_hops")}};
    Array generations;
    const Value frames = frames_of(f);
    for (const Value& frame : items(frames)) generations.push_back(at(fields(frame), "generation"));
    const Value& best = at(f, "best");
    const std::string status = !is_null(best)            ? "feasible"
                               : termination == "cancelled" ? "cancelled"
                               : termination == "error"     ? "error"
                                                            : "unknown";
    const std::int64_t expanded = as_int(at(f, "expanded"));
    return object({{"status", text(status)},
                   {"termination", text(termination)},
                   {"best", best},
                   {"windows", at(f, "windows")},
                   {"finalAttempts", at(f, "finalAttempts")},
                   {"corridor", corridor},
                   {"routeGeneration", kNull},
                   {"adapter", object(std::move(adapter_summary))},
                   {"movementGeneration", array(std::move(generations))},
                   {"expandedPrefixes", integer_value(expanded - initial_expanded)},
                   {"totalExpandedPrefixes", integer_value(expanded)},
                   {"work", work->summary()},
                   {"compiler", run.counted.summary()},
                   {"elapsedSeconds", real(seconds_now() - s.started)},
                   {"threshold", real(0.)},
                   {"order", text("goal_arrival_depth_first")},
                   {"comparisonPolicy", text("goal_arrival_dfs")},
                   {"objectiveOptimalityProven", boolean(false)},
                   {"minimumTicksProven", boolean(false)},
                   {"searchState", final_state},
                   {"resume", object({{"exact", boolean(as_int(at(f, "session")) > 1)}, {"session", at(f, "session")}})},
                   {"interactionGate", gate.summary()},
                   {"viability", object({{"checked", integer_value(search.viability_checked())},
                                         {"excluded", integer_value(search.viability_excluded())}})},
                   {"minimumRunPruning",
                    object({{"enabled", boolean(s.minimum_run_pruning)},
                            {"version", integer_value(kMinimumRunPolicy)},
                            {"excluded", integer_value(static_cast<std::int64_t>(items(at(f, "minimumRunExclusions")).size()))},
                            {"scope", text("protected_hard_anchor_ticks")},
                            {"comparisonPolicy", integer_value(kComparisonPolicy)},
                            {"certifiedDominance", boolean(false)}})}});
}

// search/events/engine.discovery_settings(settings).
Object discovery_settings(const Value& settings) {
    Object limits{{"version", integer_value(1)},
                  {"maxGraphExpansions", integer_value(5000)},
                  {"maxGuideMaterializations", integer_value(50)}};
    if (!is_null(settings)) {
        for (const auto& [key, value] : fields(settings)) {
            if (!get(limits, key)) throw std::invalid_argument("Unsupported discoverySettings field");
            put(limits, key, value);
        }
    }
    const auto* version = std::get_if<std::int64_t>(&at(limits, "version").data);
    if (!version || *version != 1) throw std::invalid_argument("Unsupported discoverySettings version");
    for (const char* key : {"maxGraphExpansions", "maxGuideMaterializations"}) {
        const auto* n = std::get_if<std::int64_t>(&at(limits, key).data);
        if (!n || *n < 1) throw std::invalid_argument("Discovery work limits must be positive integers");
    }
    return limits;
}

struct Settings {
    double seconds = 30, candidate_seconds = 1;
    std::int64_t max_solver_calls = 128, max_candidates = 24;
    bool minimum_run_pruning = true;
    std::string post_alarm = "pipeline";
    bool work_only = false;  // seconds and candidate_seconds are checked, then unlimited (pkc/work.hpp)
};

std::int64_t max_index(const Array& frames) {
    std::int64_t out = -1;
    for (const Value& f : frames) out = std::max(out, as_int(at(fields(f), "index")));
    return out;
}

// One between-search save of the event state, as SearchCheckpoint.__call__ saves it: a full save with no repair.
void save_state(Run& run, const Object& state) {
    const Value& dfs = at(state, "dfs");
    if (is_null(dfs)) throw std::logic_error("a between-search save follows a search");
    SearchSaver saver(run.options.store, run.options.metadata, object(with(state, "dfs", kNull)), run.windows);
    saver.save(dfs, RepairTargets{});
    run.windows = saver.windows();
}

// search/events/engine.search_event_routes(request, compiler, ...).
Value search_event_routes(Run& run, const Value& fixture_request, const Value& task_goal, const Value& discovery,
                          const Value& anchor_search, const Settings& settings, const Value& initial_jump_inputs,
                          const Value& checkpoint_identity) {
    const double started = seconds_now(), deadline = started + settings.seconds;
    const Object limits = discovery_settings(discovery);
    Value request_value = fixture_request;  // a resumed run's is the saved effectiveRequest
    Value scene = prepare_scene(request_value);
    const Value task = goal_spec(scene, task_goal);
    if (as_text(at(fields(task), "kind")) != "landing")
        throw std::invalid_argument("Anchor search requires a finite physical landing task");
    const Value anchor_policy =
        anchor_settings(is_null(anchor_search) || !py_truthy(anchor_search) ? object({}) : anchor_search, scene);
    Object cfg = fields(movement_settings(scene));
    Array types;
    for (const Value& t : items(at(cfg, "types")))
        if (as_text(t) == "normal" || as_text(t) == "ceiling" || as_text(t) == "stepping") types.push_back(t);
    put(cfg, "types", array(std::move(types)));
    const Value cfg_value = object(cfg);
    scene = object(with(fields(scene), "movementSearch", cfg_value));
    Object domain_settings = cfg;
    for (const char* key : {"maxPrefixes", "maxQueuedPrefixes", "maxChoicesPerType", "expansionsPerProposal"})
        erase(domain_settings, key);
    const std::string context = digest(object({{"policy", integer_value(5)},
                                               {"comparisonPolicy", integer_value(kComparisonPolicy)},
                                               {"anchorPolicy", semantic_settings(anchor_policy)},
                                               {"constraintPolicy", integer_value(16)},
                                               {"numericalPolicy", integer_value(1)},
                                               {"scene", without_key(scene, "movementSearch")},
                                               {"task", task},
                                               {"roots", kNull},
                                               {"filter", checkpoint_identity},
                                               {"minimumRuns", boolean(settings.minimum_run_pruning)},
                                               {"domain", object(std::move(domain_settings))},
                                               {"postAlarm", text(settings.post_alarm)}}));
    const Value& saved = run.options.resume_state;
    if (!is_null(saved)) {
        const Object& s = fields(saved);
        if (!py_equal(get_or_null(s, "schema"), integer_value(2)) ||
            !py_equal(get_or_null(s, "kind"), text("event_route_search")) ||
            !py_equal(get_or_null(s, "context"), text(context)))
            throw std::invalid_argument(
                "Incompatible automatic event continuation; cell-walk/schema6 states require their source-era runner");
    }
    Value data;
    Object state;
    if (!is_null(saved)) {
        state = fields(saved);
        data = at(state, "domain");
        check_route_domain(data);
        request_value = at(state, "effectiveRequest");
        scene = object(with(fields(prepare_scene(request_value)), "movementSearch", cfg_value));
    } else {
        data = build_route_domain(scene, task, deadline - seconds_now(), 20000, kMovementTypes);
        state = Object{{"schema", integer_value(2)},
                       {"kind", text("event_route_search")},
                       {"context", text(context)},
                       {"domain", data},
                       {"graph", build_event_graph(data)},
                       {"dfs", kNull},
                       {"initialRoots", kNull},
                       {"effectiveRequest", request_value},
                       {"best", kNull},
                       {"sessions", integer_value(0)},
                       {"allocations", array({})}};
    }
    put(state, "sessions", integer_value(as_int(at(state, "sessions")) + 1));
    if (!is_null(saved)) {
        const Value& dfs = at(state, "dfs");
        const Value& dfs_best = is_null(dfs) ? kNull : get_or_null(fields(dfs), "best");
        const Value saved_best = py_truthy(dfs_best) ? dfs_best : get_or_null(state, "best");
        if (py_truthy(saved_best)) {
            if (!py_truthy(at(fields(replay_witness(scene, saved_best, run.counted)), "passed")))
                throw std::invalid_argument("Saved automatic incumbent no longer passes independent replay");
            put(state, "best", saved_best);
        }
    }
    Value motion = get_or_null(state, "motion");
    if (is_null(motion)) {
        motion = motion_bound(scene, run.counted,
                              std::max(.001, std::min(settings.candidate_seconds, deadline - seconds_now())));
        put(state, "motion", motion);
    }
    // The anchor adapter's guide plan, with its encounter groups over the domain's scene.
    Object guide_plan = fields(build_guides(data, anchor_policy));
    {
        const Value& domain_scene = at(fields(data), "scene");
        InteractionGate tables(domain_scene);  // its face tables only
        std::vector<std::string> names;
        for (const Value& n : items(at(guide_plan, "anchors"))) names.push_back(as_text(n));
        put(guide_plan, "groups", encounter_groups(domain_scene, names, tables, AnchorScene(domain_scene)));
    }
    const Value guide_plan_value = object(std::move(guide_plan));
    put(state, "anchorPolicy", anchor_policy);
    put(state, "guidePlan", guide_plan_value);

    Session s;
    s.scene = scene;
    s.motion = motion;
    s.data = data;
    s.graph = at(state, "graph");
    s.anchor_policy = anchor_policy;
    s.guide_plan = guide_plan_value;
    s.checkpoint_identity = checkpoint_identity;
    s.initial_jump_inputs = initial_jump_inputs;
    s.request = fields(request_value);
    for (const char* key : {"maxJumps", "maxTotalRunTicks", "maxRunTicksPerJump"})
        if (const Value* cap = get(s.request, key)) s.caps.emplace_back(key, *cap);
    s.caps.emplace_back("initialJumpInputs", initial_jump_inputs);
    s.deadline = deadline;
    s.started = started;
    s.candidate_seconds = settings.candidate_seconds;
    s.score_seconds = settings.seconds;
    s.batch_size = settings.max_candidates;
    s.minimum_run_pruning = settings.minimum_run_pruning;
    s.numerical_backend = settings.post_alarm == "ipopt" ? "relaxed" : "relaxed_pipeline";

    const bool progress = run.options.save_progress && run.options.store;
    const std::int64_t max_prefixes = as_int(at(cfg, "maxPrefixes"));
    std::int64_t calls = 0, expanded = 0, used_expansions = 0, used_materializations = 0;
    Array allocations;
    std::vector<std::pair<std::string, std::int64_t>> numerical;
    std::string termination = "exhausted";
    Value last;
    while (seconds_now() < deadline && calls < settings.max_solver_calls && expanded < max_prefixes) {
        if (run.cancelled()) {
            termination = "cancelled";
            break;
        }
        auto work = std::make_shared<SearchWork>(settings.max_solver_calls - calls, deadline);
        const Value dfs_before = at(state, "dfs");
        const std::int64_t before_depth = is_null(dfs_before) ? -1 : max_index(items(at(fields(dfs_before), "stack")));
        const std::int64_t before_frames = is_null(dfs_before) ? 0 : as_int(at(fields(dfs_before), "nextFrame"));
        s.cfg = object(with(cfg, "maxPrefixes", integer_value(max_prefixes - expanded)));
        last = search_goals(run, s, work, dfs_before, object(with(state, "dfs", kNull)));
        if (run.saver) run.windows = run.saver->windows();  // SearchCheckpoint.sync()
        const Object& l = fields(last);
        put(state, "dfs", at(l, "searchState"));
        put(state, "best", at(l, "best"));
        const Object& dfs = fields(at(state, "dfs"));
        const Object& discovery_work = fields(at(dfs, "discoveryWork"));
        used_expansions += as_int(at(discovery_work, "expansions"));
        used_materializations += as_int(at(discovery_work, "materializations"));
        const Object& session_work = fields(at(l, "work"));
        const Value* session_calls = get(session_work, "solverCalls");
        const std::int64_t made = session_calls ? as_int(*session_calls) : 0;
        calls += made;
        const std::int64_t session_expanded = as_int(at(l, "expandedPrefixes"));
        expanded += session_expanded;
        for (const auto& [name, n] : session_work) {
            bool counted = name.rfind("relaxedFallback:", 0) == 0;
            for (const char* k : kNumericalCounts) counted = counted || name == k;
            if (!counted) continue;
            const std::string& key = name;
            auto found = std::find_if(numerical.begin(), numerical.end(), [&](const auto& e) { return e.first == key; });
            if (found == numerical.end()) numerical.emplace_back(name, as_int(n));
            else found->second += as_int(n);
        }
        const std::int64_t after_depth = max_index(items(at(dfs, "stack")));
        const Value allocation = object({{"calls", integer_value(made)},
                                         {"expanded", integer_value(session_expanded)},
                                         {"termination", at(l, "termination")},
                                         {"beforeDepth", integer_value(before_depth)},
                                         {"afterDepth", integer_value(after_depth)},
                                         {"advanced", boolean(after_depth > before_depth)}});
        allocations.push_back(allocation);
        Array recorded = items(at(state, "allocations"));
        recorded.push_back(allocation);
        put(state, "allocations", array(std::move(recorded)));
        if (progress) save_state(run, state);
        termination = as_text(at(l, "termination"));
        if (termination == "error" || termination == "cancelled") break;
        const Object& now_dfs = fields(at(state, "dfs"));
        const bool active = !items(at(now_dfs, "stack")).empty() || !items(at(now_dfs, "goalQueue")).empty();
        bool open = false;
        for (const auto& [_, d] : fields(at(now_dfs, "discovery"))) open = open || !py_truthy(at(fields(d), "exhausted"));
        const bool discoverable = open && used_expansions < as_int(at(limits, "maxGraphExpansions")) &&
                                  used_materializations < as_int(at(limits, "maxGuideMaterializations"));
        if (!active && !discoverable) break;
        if (!made && !session_expanded && as_int(at(now_dfs, "nextFrame")) == before_frames && !active) break;
    }
    if (seconds_now() >= deadline) termination = "deadline";
    else if (calls >= settings.max_solver_calls) termination = "numerical_call_limit";
    else if (expanded >= max_prefixes) termination = "prefix_limit";
    Object report = is_null(last) ? Object{} : fields(last);
    const Value& best = at(state, "best");
    put(report, "status", text(!is_null(best) ? "feasible" : termination == "cancelled" ? "cancelled" : "unknown"));
    put(report, "termination", text(termination));
    put(report, "best", best);
    put(report, "searchState", object(state));
    put(report, "effectiveRequest", request_value);
    put(report, "taskGoal", task);
    Object work;
    for (const char* k : kNumericalCounts) work.emplace_back(k, integer_value(0));
    for (const auto& [name, n] : numerical) put(work, name, integer_value(n));
    put(work, "solverCalls", integer_value(calls));
    put(work, "maxSolverCalls", integer_value(settings.max_solver_calls));
    put(report, "work", object(std::move(work)));
    put(report, "expandedPrefixes", integer_value(expanded));
    put(report, "elapsedSeconds", real(seconds_now() - started));
    put(report, "compiler", run.counted.summary());
    put(report, "backend", text("event_goals_v1"));
    const Object& domain = fields(data);
    Object route_generation{{"backend", text("event_goals_v1")},
                            {"events", integer_value(static_cast<std::int64_t>(items(at(fields(s.graph), "events")).size()))},
                            {"expansions", integer_value(used_expansions)},
                            {"materializations", integer_value(used_materializations)},
                            {"coverage", at(domain, "coverage")},
                            {"completeEnumeration", boolean(false)}};
    put(report, "routeAttempts", array(allocations));
    const Value& dfs_value = at(state, "dfs");
    std::int64_t work_units = 0;
    if (!is_null(dfs_value))
        for (const auto& [_, owners] : fields(at(fields(dfs_value), "goalOwners")))
            work_units += static_cast<std::int64_t>(items(owners).size());
    put(report, "routeWorkUnits", integer_value(work_units));
    put(report, "coverageProven", boolean(false));
    put(report, "objectiveOptimalityProven", boolean(false));
    put(report, "minimumTicksProven", boolean(false));
    put(report, "resume", object({{"exact", boolean(!is_null(saved))}, {"session", at(state, "sessions")}}));
    if (!is_null(dfs_value)) {
        const Object& dfs = fields(dfs_value);
        std::int64_t restorations = 0;
        for (const Value& r : items(at(dfs, "minimumRunExclusions")))
            restorations += py_truthy(get_or_null(fields(r), "restored"));
        std::int64_t unresolved = 0;
        for (const char* group : {"frames", "stack", "suspended", "goalQueue"}) {
            const Value* frames = get(dfs, group);
            if (!frames) continue;
            for (const Value& frame : items(*frames)) {
                const Object& fr = fields(frame);
                std::vector<const Value*> programs;
                for (const Value& r : items(at(fr, "records"))) programs.push_back(&at(fields(r), "program"));
                const Value* continuation = get(fields(at(fr, "generation")), "continuation");
                if (continuation)
                    if (const Value* nodes = get(fields(*continuation), "nodes"))
                        for (const auto& [_, n] : fields(*nodes)) programs.push_back(&at(fields(n), "program"));
                for (const Value* p : programs) {
                    const Value& repair = get_or_null(fields(*p), "repairState");
                    if (!py_truthy(repair)) continue;
                    if (const Value* u = get(fields(repair), "unresolved"))
                        unresolved += static_cast<std::int64_t>(items(*u).size());
                }
            }
        }
        const Value* bounds = get(dfs, "transferTimeBounds");
        const Value* comparison = get(dfs, "comparisonWork");
        put(report, "anchorSearch",
            object({{"policy", anchor_policy},
                    {"guidePlan", guide_plan_value},
                    {"hardLatches", at(dfs, "hardLatches")},
                    {"latchRevision", at(dfs, "latchRevision")},
                    {"transferTimeBounds", bounds ? *bounds : object({})},
                    {"comparisonWork", comparison ? *comparison : object({})},
                    {"comparisonPolicy", integer_value(kComparisonPolicy)},
                    {"nodes", at(dfs, "nodeResults")},
                    {"observations", turns_summary(at(dfs, "observations"))},
                    {"runDeferrals", integer_value(static_cast<std::int64_t>(items(at(dfs, "minimumRunExclusions")).size()))},
                    {"runRestorations", integer_value(restorations)},
                    {"unresolvedBranches", integer_value(unresolved)}}));
        put(report, "backend", text("anchor_goals_v1"));
        put(route_generation, "backend", text("anchor_goals_v1"));
    }
    put(report, "routeGeneration", object(std::move(route_generation)));
    if (progress) save_state(run, state);
    return object(std::move(report));
}

Settings task_settings(const Value& fixture, const SolveCaseOptions& options) {
    Settings out;
    const Value* settings = get(fields(fixture), "settings");
    Object given = settings ? fields(*settings) : Object{};
    for (const char* retired : kRetiredSettings) erase(given, retired);
    if (options.seconds) put(given, "seconds", real(*options.seconds));
    if (options.max_solver_calls) put(given, "max_solver_calls", integer_value(*options.max_solver_calls));
    for (const auto& [key, value] : given) {
        if (key == "seconds") {
            out.seconds = number(value, key).value;
        } else if (key == "candidate_seconds") {
            out.candidate_seconds = number(value, key).value;
        } else if (key == "max_solver_calls") {
            out.max_solver_calls = integer(value, key, 1, 100000);
        } else if (key == "max_candidates") {
            out.max_candidates = integer(value, key, 1, 100000);
        } else if (key == "minimum_run_pruning") {
            const auto* flag = std::get_if<bool>(&value.data);
            if (!flag) throw std::invalid_argument("minimum_run_pruning must be boolean");
            out.minimum_run_pruning = *flag;
        } else if (key == "post_alarm") {
            out.post_alarm = as_text(value);
            if (out.post_alarm != "ipopt" && out.post_alarm != "pipeline")
                throw std::invalid_argument("post_alarm must be ipopt or pipeline");
        } else if (key == "work_only") {
            const auto* flag = std::get_if<bool>(&value.data);
            if (!flag) throw std::invalid_argument("work_only must be boolean");
            out.work_only = *flag;
        } else {
            throw std::invalid_argument("Unsupported setting for the native solve entry: " + key);
        }
    }
    if (out.seconds <= 0) throw std::invalid_argument("seconds must be positive");
    if (out.candidate_seconds <= 0) throw std::invalid_argument("candidate_seconds must be positive");
    if (out.work_only) out.seconds = out.candidate_seconds = INFINITY;
    return out;
}

// search/engine.find_checkpoint_route(request, compiler, ...) for a run with an initial-jump domain.
Value find_checkpoint_route(Run& run, const Value& request_value, const Value& fixture, const Settings& settings,
                            const Value& initial_jump_inputs, const Value& checkpoint_identity) {
    const double started = seconds_now();
    const Object& request = fields(request_value);
    if (const Value* hits = get(request, "maxWallHits"); hits && py_truthy(*hits))
        throw std::invalid_argument("Intentional wall-hit experiments are excluded");
    const Value scene = prepare_scene(object(with(request, "maxWallHits", integer_value(0))));
    const Object& s = fields(scene);
    if (as_text(at(s, "sceneModel")) != "collision_boxes" || py_truthy(get_or_null(s, "climbableCells")) ||
        py_truthy(get_or_null(s, "maxWallHits")))
        throw std::invalid_argument("Checkpoint search requires non-ladder collision geometry with maxWallHits=0");
    if (const Value* search = get(request, "movementSearch"))
        if (const Value* types = get(fields(*search), "types"))
            for (const Value& t : items(*types))
                if (as_text(t) != "normal" && as_text(t) != "ceiling" && as_text(t) != "stepping")
                    throw std::invalid_argument("Select only normal, ceiling and stepping movement types");
    SearchWork work(settings.max_solver_calls, started + settings.seconds);
    const auto unfinished = [&](const std::string& reason) {
        return object({{"status", text(reason == "cancelled" ? "cancelled" : "unknown")},
                       {"termination", text(reason)},
                       {"best", kNull},
                       {"windows", array({})},
                       {"finalAttempts", array({})},
                       {"routeGeneration", kNull},
                       {"expandedPrefixes", integer_value(0)},
                       {"movementGeneration", array({})},
                       {"work", work.summary()},
                       {"compiler", Compiler(true).summary()},
                       {"elapsedSeconds", real(seconds_now() - started)},
                       {"threshold", real(0.)},
                       {"objectiveOptimalityProven", boolean(false)},
                       {"minimumTicksProven", boolean(false)}});
    };
    if (run.cancelled()) return unfinished("cancelled");
    const Object& f = fields(fixture);
    // work.work_only(settings.work_only) for the search: set, then restored however the search ends.
    struct WorkOnlyMode {
        bool previous = work_only();
        explicit WorkOnlyMode(bool enabled) { set_work_only(enabled); }
        ~WorkOnlyMode() { set_work_only(previous); }
    } mode(settings.work_only);
    try {
        return search_event_routes(run, request_value, get_or_null(f, "terminalGoal"), get_or_null(f, "discoverySettings"),
                                   get_or_null(f, "anchorSearch"), settings, initial_jump_inputs, checkpoint_identity);
    } catch (const Timeout& error) {
        return unfinished(error.what());
    }
}

}  // namespace

Value transfer_bounds(const Value& scene, const Value& guides, const Value& motion, const Value& saved) {
    Compiler compiler(true);
    GateCache cache;
    Gate gate(std::make_shared<InteractionGate>(scene), cache, compiler, 1);
    return transfer_bounds(scene, guides, motion, gate, saved, {});
}

namespace {

// validation/witness.replay_witness(scene, best, compiler).
Value replay_witness(const Value& scene_value, const Value& best_value, Compiler& compiler) {
    const SolveHooks hooks = compiler.hooks();
    const Object &scene = fields(scene_value), &best = fields(best_value);
    const Value route_value = prepare_route(at(best, "route"));
    const Object& route = fields(route_value);
    if (!py_equal(at(route, "initial"), at(scene, "initial")))
        throw std::invalid_argument("Replay changed the original initial state");
    Object plan = fields(schedule(route_value, {0}));
    const Value vertical_value = validate_vertical_schedule(route_value, at(best, "profiles"), object(plan));
    const Object& vertical = fields(vertical_value);
    if (!py_truthy(at(vertical, "feasible")))
        return object({{"passed", boolean(false)}, {"reason", text("vertical_replay")}});
    put(plan, "vertical", at(vertical, "vertical"));
    const Value plan_value = object(plan);
    Object request = fields(compiler.compile(route_value, plan_value, 5));
    const Object& initial = fields(at(scene, "initial"));
    const Value* objective = get(route, "guideObjective");
    const Value* threshold = get(route, "physicsThreshold");
    const Object& candidate_in = fields(at(best, "candidate"));
    put(request, "version", integer_value(1));
    put(request, "ticks", at(plan, "ticks"));
    put(request, "position", every_other(at(initial, "position")));
    put(request, "velocity", every_other(at(initial, "velocity")));
    put(request, "yaw", at(candidate_in, "yaw"));
    put(request, "threshold", threshold ? *threshold : real(.005));
    put(request, "walls", at(plan, "walls"));
    put(request, "contacts", at(plan, "contacts"));
    put(request, "objective", object(with(fields(objective ? *objective : at(route, "objective")), "tick", at(plan, "ticks"))));
    const Value problem_value = prepare_problem(object(std::move(request)));
    const Object& problem = fields(problem_value);
    const Value& model = get_or_null(candidate_in, "model");
    Object candidate;
    if (!is_null(model) && as_text(model) == "relaxed_disk") {
        // A relaxed search witness replays its own acceleration vectors.
        candidate = fields(validate_controls(problem_value, at(candidate_in, "controls")));
        Array raw;
        const Array &velocity = items(at(candidate, "velocity")), &controls = items(at(candidate, "controls"));
        for (std::size_t t = 0; t < std::min(velocity.size(), controls.size()); ++t) {
            const Array &v = items(velocity[t]), &a = items(controls[t]);
            Array row;
            for (std::size_t i = 0; i < std::min(v.size(), a.size()); ++i) row.push_back(value(add(num(v[i]), num(a[i]))));
            raw.push_back(array(std::move(row)));
        }
        put(candidate, "rawMovement", array(std::move(raw)));
    } else {
        candidate = fields(validate(problem_value, at(problem, "yaw")));
        const double limit = num(at(problem, "threshold")).value;
        const Array &velocity = items(at(candidate, "velocity")), &yaw = items(at(problem, "yaw")),
                    &base = items(at(problem, "base")), &magnitude = items(at(problem, "magnitude"));
        Array raw;
        for (std::size_t t = 0; t < yaw.size(); ++t) {
            const double angle = num(base[t]).value + num(yaw[t]).value * (M_PI / 180.0);  // math.radians
            const double q[2] = {std::cos(angle), std::sin(angle)};
            const Array& v = items(velocity[t]);
            Array row;
            for (std::size_t i = 0; i < 2; ++i) {
                const Num kept = std::fabs(num(v[i]).value) >= limit ? num(v[i]) : whole(0);
                row.push_back(value(add(kept, mul(num(magnitude[t]), flt(q[i])))));
            }
            raw.push_back(array(std::move(row)));
        }
        put(candidate, "rawMovement", array(std::move(raw)));
    }
    bool support = true;
    const Array& blocks = items(at(route, "blocks"));
    const Array& positions = items(at(candidate, "positions"));
    for (const Value& check : items(at(plan, "supportChecks"))) {
        const Array& c = items(check);
        const Array& at_tick = items(positions.at(static_cast<std::size_t>(as_int(c.at(0)))));
        support = support && supports(fields(blocks.at(static_cast<std::size_t>(as_int(c.at(1))))),
                                      num(at_tick.at(0)).value, num(at_tick.at(1)).value, as_text(c.at(2)));
        if (!support) break;
    }
    put(candidate, "friction", at(problem, "friction"));
    const Value checked = check_geometry(at(scene, "collisionScene"), plan, candidate, vertical, hooks, 5);
    const bool same = py_equal(at(candidate, "positions"), at(candidate_in, "positions")) &&
                      py_equal(at(candidate, "velocity"), at(candidate_in, "velocity"));
    const bool feasible = py_truthy(at(candidate, "feasible"));
    return object({{"passed", boolean(feasible && support && py_truthy(at(fields(checked), "feasible")) && same)},
                   {"scalarFeasible", at(candidate, "feasible")},
                   {"supports", boolean(support)},
                   {"geometry", checked},
                   {"exactHorizontalReplay", boolean(same)},
                   {"ticks", at(plan, "ticks")},
                   {"threshold", at(problem, "threshold")},
                   {"objective", at(candidate, "objective")},
                   {"objectiveHasY", boolean(false)},
                   {"model", model}});
}

}  // namespace

Value replay_witness(const Value& scene_value, const Value& best_value) {
    Compiler compiler(false);
    return replay_witness(scene_value, best_value, compiler);
}

Value solve_case(const Value& fixture_value, const SolveCaseOptions& options) {
    const Object& fixture = fields(fixture_value);
    if (get(fixture, "spatialSettings"))
        throw std::invalid_argument("Legacy spatialSettings is not event discovery; use discoverySettings version1");
    const Value& request = at(fixture, "request");
    const Value cfg = movement_settings(prepare_scene(request));
    const std::int64_t max_inputs = as_int(at(fields(cfg), "maxInputs"));
    // search/domain.InitialJumpDomain(fixture.get('initialJumpInputs'), max_inputs=...).
    Value inputs = get_or_null(fixture, "initialJumpInputs");
    if (!is_null(inputs)) {
        std::vector<std::int64_t> sorted;
        for (const Value& t : items(inputs)) {
            const auto* n = std::get_if<std::int64_t>(&t.data);
            if (!n || *n < 0 || *n >= max_inputs) throw std::invalid_argument("Initial jump inputs must fit the solve horizon");
            if (std::find(sorted.begin(), sorted.end(), *n) == sorted.end()) sorted.push_back(*n);
        }
        if (sorted.empty()) throw std::invalid_argument("Initial jump inputs must fit the solve horizon");
        std::sort(sorted.begin(), sorted.end());
        Array out;
        for (std::int64_t t : sorted) out.push_back(integer_value(t));
        inputs = array(std::move(out));
    }
    const Value identity = object({{"initialJumpInputs", inputs}, {"searchHorizon", integer_value(max_inputs)}});
    const Settings settings = task_settings(fixture_value, options);
    Run run{options};
    const Value result = find_checkpoint_route(run, request, fixture_value, settings, inputs, identity);
    Array validation;
    const Object& r = fields(result);
    if (!is_null(at(r, "best"))) {
        const Value checked = replay_witness(prepare_scene(at(r, "effectiveRequest")), at(r, "best"));
        validation.push_back(checked);
        if (!py_truthy(at(fields(checked), "passed")))
            throw std::runtime_error("Automatic route incumbent failed independent physical replay");
    }
    const Value validation_value = array(std::move(validation));
    if (options.store) {
        SearchSaver saver(options.store, options.metadata, kNull, run.windows);
        saver.finish(result, validation_value);
    }
    return object({{"result", result}, {"validation", validation_value}});
}

}  // namespace pkc
