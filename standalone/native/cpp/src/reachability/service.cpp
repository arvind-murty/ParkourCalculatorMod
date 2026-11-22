// Port of reachability/service.py's InteractionGate for native callers.
#include "pkc/reachability/service.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/coefficients.hpp"
#include "pkc/constraints/contacts.hpp"
#include "pkc/envelopes.hpp"
#include "pkc/gate.hpp"
#include "pkc/movement/normal.hpp"
#include "pkc/numerics/builders.hpp"
#include "pkc/numerics/disk.hpp"
#include "pkc/scene/geometry.hpp"
#include "pkc/scene/route.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const double kBodyHeight = f32(1.8);  // scene/geometry.BODY_HEIGHT

Value scene_without_search(const Value& scene) {
    Object kept;
    for (const auto& [k, v] : fields(scene))
        if (k != "movementSearch") kept.emplace_back(k, v);
    return object(std::move(kept));
}

// walk_off_tick(program): the last run-off walk-off tick without a carried face selection, else nullopt.
std::optional<std::int64_t> walk_off_tick(const Program& program) {
    const Profile& p = program.profile;
    std::optional<std::int64_t> cut;
    for (std::int64_t c = p.duration - 1; c >= 0; --c)
        if (p.grounded[c] && !p.jump[c] && !p.ladder_ticks[c] && p.position[c + 1].value < p.position[c].value) {
            cut = c;
            break;
        }
    if (cut && program.walk_offs)
        for (const auto& w : *program.walk_offs)
            if (w.offset == *cut) return std::nullopt;
    return cut;
}

Value boxes_value(const std::vector<Box>& boxes) {
    Array out;
    for (const auto& box : boxes) {
        Array b;
        for (const Num& n : box) b.push_back(value(n));
        out.push_back(array(std::move(b)));
    }
    return array(std::move(out));
}

// landing_boxes(program, cut): the boxes a run-off from `cut` first lands on, or null.
Value landing_boxes(const Program& program, std::int64_t cut) {
    std::optional<std::pair<std::int64_t, Value>> first;  // min(..., key=tick) keeps the first of equal ticks
    for (const auto& e : program.profile.contact_events)
        if (e.offset >= cut && (e.role == "landing" || e.role == "step") && e.boxes && (!first || e.offset < first->first))
            first = {e.offset, boxes_value(*e.boxes)};
    for (const auto& e : program.profile.case_events)
        if (e.input >= cut && e.lower_boxes && (!first || e.input < first->first))
            first = {e.input, boxes_value(*e.lower_boxes)};
    return first ? first->second : Value{nullptr};
}

}  // namespace

const std::set<std::string>& FaceTable::of(const std::string& kind) const {
    if (kind == "blocks") return blocks;
    if (kind == "landing") return landing;
    if (kind == "stepping") return stepping;
    if (kind == "ceiling") return ceiling;
    fail("Unknown face table kind: " + kind);
}

InteractionGate::InteractionGate(Value scene)
    : scene_(std::move(scene)),
      scene_json_(canonical_json(scene_without_search(scene_))),
      geometry_(at(fields(at(fields(scene_), "collisionScene")), "solids")),
      top_faces_(scene_contact_options(scene_, geometry_, 1, 1)),
      owners_(solid_owners(scene_)) {}

void InteractionGate::count(const std::string& metric, std::int64_t by) {
    for (auto& [k, v] : metrics_)
        if (k == metric) {
            v = integer_value(as_int(v) + by);
            return;
        }
    metrics_.emplace_back(metric, integer_value(by));
}

template <class T>
std::shared_ptr<const T> InteractionGate::cached(const std::string& key, const std::string& kind,
                                                 const std::function<std::shared_ptr<const T>()>& build) {
    auto found = index_.find(key);
    count(kind + (found != index_.end() ? "Hits" : "Misses"));
    if (found != index_.end()) {
        cache_.splice(cache_.end(), cache_, found->second);
        return std::any_cast<std::shared_ptr<const T>>(found->second->second);
    }
    std::shared_ptr<const T> value = build();  // a timeout never commits a result
    cache_.emplace_back(key, value);
    index_[key] = std::prev(cache_.end());
    while (cache_.size() > 512) {
        index_.erase(cache_.front().first);
        cache_.pop_front();
    }
    return value;
}

const FaceTable& InteractionGate::reachable_faces(const std::string& name) {
    if (auto found = face_tables_.find(name); found != face_tables_.end()) return found->second;
    const std::clock_t started = std::clock();
    const Array& blocks = items(at(fields(scene_), "blocks"));
    double lowest = std::numeric_limits<double>::infinity();
    std::map<std::int64_t, std::vector<std::string>> by_solid;
    std::map<std::string, double> tops;
    std::map<std::string, std::int64_t> solid_of;
    for (const auto& b : blocks) {
        const Object& block = fields(b);
        const std::string& id = as_text(at(block, "id"));
        const double top = num(at(block, "top")).value;
        lowest = std::min(lowest, top);
        by_solid[as_int(at(block, "solidIndex"))].push_back(id);
        tops[id] = top;
        solid_of[id] = as_int(at(block, "solidIndex"));
    }
    FaceTable table;
    std::vector<std::pair<std::string, double>> primaries;
    const std::set<std::string> owners = face_owners(top_faces_, owners_, name);
    auto near = [&](const NumBox& box, double radius) {
        const double foot[4] = {box[0].value, box[2].value, box[3].value, box[5].value};
        std::vector<std::int64_t> found;
        for (auto i : geometry_.near(foot[0] - radius, foot[1] - radius, foot[2] + radius, foot[3] + radius)) {
            const NumBox& other = geometry_.boxes()[i];
            const double dx = std::max({0., other[0].value - foot[2], foot[0] - other[3].value});
            const double dz = std::max({0., other[2].value - foot[3], foot[1] - other[5].value});
            if (python_hypot(dx, dz) <= radius) found.push_back(i);
        }
        return found;
    };
    for (const auto& b : blocks) {
        const Object& block = fields(b);
        const Value* source = get(block, "sourceBlock");
        if (!owners.count(as_text(source ? *source : at(block, "id")))) continue;
        const std::string& id = as_text(at(block, "id"));
        table.blocks.insert(id);
        const double feet = num(at(block, "top")).value;
        auto trace = jump_traces_.find(feet);
        if (trace == jump_traces_.end()) {
            const ScalarState state{true, num(at(block, "top")), false, Num{-kGravity * kYDrag, false}};
            trace = jump_traces_.emplace(feet, replay(state, 200, true, {}, {}, Num{0., false})).first;
        }
        const auto descent = first_descent(trace->second, lowest);
        const std::int64_t flight = descent ? *descent + 1 : 200;
        table.ticks = table.ticks ? std::min(*table.ticks, flight) : flight;
        for (auto i : near(geometry_.boxes()[as_int(at(block, "solidIndex"))], .4 + .7 + .4 * (flight - 1))) {
            const double bottom = geometry_.boxes()[i][1].value + kBodyHeight;
            if (bottom > feet + kBodyHeight + jump_rise()) continue;
            auto names = by_solid.find(i);
            if (names == by_solid.end()) continue;
            table.blocks.insert(names->second.begin(), names->second.end());
            if (feet + kBodyHeight <= bottom) table.ceiling.insert(names->second.begin(), names->second.end());
            for (const auto& n : names->second)
                if (tops[n] <= feet + jump_rise()) {
                    table.landing.insert(n);
                    primaries.emplace_back(n, feet + jump_rise());
                }
        }
    }
    table.stepping = table.landing;
    for (const auto& [n, peak] : primaries)
        for (auto i : near(geometry_.boxes()[solid_of[n]], .4))
            if (auto names = by_solid.find(i); names != by_solid.end())
                for (const auto& other : names->second)
                    if (tops[other] <= peak + kStepHeight) table.stepping.insert(other);
    for (const auto& n : table.blocks) table.solids.insert(solid_of[n]);
    for (const auto& n : table.ceiling) table.ceiling_solids.insert(solid_of[n]);
    count("blockTableBuilds");  // reports and profiling read these metric names
    double seconds = static_cast<double>(std::clock() - started) / CLOCKS_PER_SEC;
    bool found = false;
    for (auto& [k, v] : metrics_)
        if (k == "blockTableSeconds") {
            v = real(num(v).value + seconds);
            found = true;
        }
    if (!found) metrics_.emplace_back("blockTableSeconds", real(0. + seconds));
    return face_tables_.emplace(name, std::move(table)).first->second;
}

std::shared_ptr<const GatePrepared> InteractionGate::prepared(const std::shared_ptr<const Program>& program,
                                                              bool require_complete) {
    const std::string physics = program_physics_key(*program);
    // digest(dict(scene=..., program=..., complete=...)), with the scene's canonical JSON computed once.
    const std::string key = sha256_hex(std::string("{\"complete\": ") + (require_complete ? "true" : "false") +
                                       ", \"program\": \"" + physics + "\", \"scene\": " + scene_json_ + "}");
    return cached<GatePrepared>("prepared:" + key, "preparation", [&]() -> std::shared_ptr<const GatePrepared> {
        auto result = gate_prepare(scene_, *program, require_complete);
        if (!result) return nullptr;
        return std::make_shared<const GatePrepared>(GatePrepared{program, std::move(*result), key});
    });
}

std::shared_ptr<const GateAnalysis> InteractionGate::analysis(const std::shared_ptr<const Program>& program) {
    auto prepared = this->prepared(program);
    if (!prepared) return nullptr;
    const Value parent = program->parent_ticks ? integer_value(*program->parent_ticks) : Value{nullptr};
    // analysis_key: interaction_context_key over the preparation it models.
    const std::string key = digest(object({{"policy", integer_value(kProofPolicy)},
                                           {"context", object({{"prepared", text(prepared->key)},
                                                               {"openTerminal", boolean(true)},
                                                               {"scope", Value{nullptr}},
                                                               {"initialIntervals", Value{nullptr}},
                                                               {"parentTicks", parent}})}}));
    return cached<GateAnalysis>("analysis:" + key, "analysis", [&]() -> std::shared_ptr<const GateAnalysis> {
        const Value request = coefficient_request(prepared->result);
        auto coefficient = cached<Value>("coefficients:" + digest(request), "coefficient",
                                         [&]() -> std::shared_ptr<const Value> {
            return std::make_shared<const Value>(compile_coefficients(request));
        });
        return std::make_shared<const GateAnalysis>(analyze_prepared(
            prepared->result, *coefficient, true,
            program->parent_ticks ? std::optional<std::int64_t>(*program->parent_ticks) : std::nullopt, key));
    });
}

Value InteractionGate::required(const std::shared_ptr<const Program>& program) {
    auto analysis = this->analysis(program);
    if (!analysis) return object({{"reason", text("vertical_schedule")}});
    return required_proof(*analysis, *program);
}

const Value& InteractionGate::remember(const std::string& key, const std::function<Value()>& build) {
    auto found = approach_index_.find(key);
    if (found == approach_index_.end()) {
        Value value = build();
        approaches_.emplace_back(key, std::move(value));
        found = approach_index_.emplace(key, std::prev(approaches_.end())).first;
        while (approaches_.size() > 512) {
            approach_index_.erase(approaches_.front().first);
            approaches_.pop_front();
        }
    }
    return found->second->second;
}

Value InteractionGate::approach(const GenerationContext& context, std::int64_t tick, bool jumping) {
    if (!context.prefix_key)
        context.prefix_key = program_physics_key(*context.program) + "/" + float_repr(context.threshold.value);
    const std::string prefix = *context.prefix_key + (jumping ? "/jump/" : "/run/");
    auto approach_at = [&](std::int64_t inputs) {
        return remember(prefix + std::to_string(inputs), [&]() -> Value {
            count("approachBuilds");
            auto choice = event_choice(context, "normal", context.state, inputs, jumping, {});
            if (!choice) return Value{nullptr};
            auto child = append_choice(*context.program, *choice);
            if (!child) return Value{nullptr};
            auto analysis = this->analysis(std::make_shared<const Program>(std::move(*child)));
            return analysis ? analysis->envelopes : Value{nullptr};
        });
    };
    const std::int64_t horizon = as_int(remember(prefix + "horizon/" + std::to_string(context.max_inputs), [&] {
        double lowest = std::numeric_limits<double>::infinity();
        for (const auto& b : items(at(fields(scene_), "blocks"))) lowest = std::min(lowest, num(at(fields(b), "top")).value);
        std::optional<std::int64_t> landing;
        for (const auto& a : approaches(context))
            if (a.jumping == jumping) {
                landing = first_descent(a.trace, lowest);
                break;
            }
        return integer_value(landing ? std::min(*landing + 2, context.max_inputs) : context.max_inputs);
    }));
    Value envelope = tick < horizon ? approach_at(horizon) : Value{nullptr};
    if (is_null(envelope)) {
        count("approachFallbacks");
        envelope = approach_at(tick + 1);
    }
    return envelope;
}

Array InteractionGate::query_many(const GenerationContext& context, const std::vector<Array>& bounds,
                                  std::int64_t tick, const Object& label, bool jumping,
                                  const std::vector<std::string>& phases) {
    struct Prepared {
        Value envelope;
        Array box;
        std::int64_t at;
    };
    std::vector<Prepared> prepared;
    Array requests;
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto& given : bounds) {
        Value envelope = approach(context, tick, jumping);
        Array box = given;
        box[1] = real(-inf);
        box[4] = real(inf);
        const std::int64_t at = context.program->profile.duration + tick;
        Array alternatives;
        for (const auto& phase : phases)
            alternatives.push_back(object({{"regions", array({object({{"tick", integer_value(at)},
                                                                        {"phase", text(phase)},
                                                                        {"bounds", array(box)}})})}}));
        requests.push_back(object({{"alternatives", array(std::move(alternatives))}}));
        prepared.push_back({std::move(envelope), std::move(box), at});
    }
    auto same = [](const Value& a, const Value& b) {
        if (is_null(a) || is_null(b)) return is_null(a) && is_null(b);
        return &fields(a) == &fields(b);
    };
    Array proofs;
    std::size_t index = 0;
    while (index < prepared.size()) {
        const Value& envelope = prepared[index].envelope;
        std::size_t end = index + 1;
        while (end < prepared.size() && same(prepared[end].envelope, envelope)) ++end;
        Array checked;
        if (is_null(envelope)) {
            for (std::size_t i = index; i < end; ++i)
                checked.push_back(object({{"ruledOut", boolean(false)}, {"reason", text("unknown_approach")}}));
        } else {
            checked = items(interaction_reachability_many(
                envelope, array(Array(requests.begin() + index, requests.begin() + end))));
        }
        for (std::size_t i = index; i < end; ++i) {
            const Value& proof = checked[i - index];
            if (!is_null(prepared[i].envelope)) {
                ++this->checked;
                if (py_truthy(at(fields(proof), "ruledOut"))) {
                    const Array& box = prepared[i].box;
                    Object exclusion = label;
                    exclusion.emplace_back("frame", context.frame_id);
                    exclusion.emplace_back("horizontal", array({box[0], box[2], box[3], box[5]}));
                    exclusion.emplace_back("input", integer_value(prepared[i].at));
                    exclusion.emplace_back("proof", proof);
                    exclusions.push_back(object(std::move(exclusion)));
                }
            }
            proofs.push_back(proof);
        }
        index = end;
    }
    return proofs;
}

bool InteractionGate::walk_off(const GenerationContext& context, const Value& landing_boxes, const Object& label) {
    const Value envelope = approach(context, 0, false);
    if (is_null(envelope)) return true;
    const std::int64_t cut = context.program->profile.duration;
    auto faces = walk_off_faces(scene_, geometry_, *context.program, cut, envelope, landing_boxes);
    ++checked;
    if (!faces || !items(at(fields(*faces), "options")).empty()) return true;
    Object exclusion = label;
    exclusion.emplace_back("frame", context.frame_id);
    exclusion.emplace_back("horizontal", at(fields(*faces), "region"));
    exclusion.emplace_back("input", integer_value(cut));
    exclusion.emplace_back("proof", object({{"ruledOut", boolean(true)}, {"reason", text("launch_support_not_left")},
                                            {"tick", integer_value(cut)}, {"solids", at(fields(*faces), "solids")}}));
    exclusions.push_back(object(std::move(exclusion)));
    return false;
}

Value InteractionGate::sequence(const GenerationContext& context, const MovementChoice& choice) {
    context.budget();
    auto program = append_choice(*context.program, choice);
    count("sequenceChecks");
    const Value proof = program ? required(std::make_shared<const Program>(std::move(*program)))
                                : object({{"reason", text("composition_rejected")}});
    if (py_truthy(proof)) {
        count("sequenceExclusions");
        const Value profile = to_value(choice.profile);
        exclusions.push_back(object({{"reason", text("joint_prerequisites_unreachable")},
                                     {"proof", proof},
                                     {"frame", context.frame_id},
                                     {"input", integer_value(context.program->profile.duration)},
                                     {"cases", at(fields(profile), "caseEvents")}}));
    }
    return proof;
}

Value InteractionGate::relaxation(const std::shared_ptr<const Program>& program, const GateAnalysis& analysis,
                                  const Value& objective, const Array& rows) {
    auto prepared = this->prepared(program);
    Object route = fields(at(fields(prepared->result), "route"));
    put(route, "objective", object({{"axis", text("X")}, {"sense", text("MAX")}}));
    put(route, "guideObjective", objective);
    Array constraints = items(at(route, "constraints"));
    constraints.insert(constraints.end(), rows.begin(), rows.end());
    put(route, "constraints", array(std::move(constraints)));
    // derive_footprints with the table compiler: its footprints are route_footprints'.
    Array blocks = items(at(route, "blocks"));
    const Value footprints = route_footprints(array(blocks), false);
    bool standing = false;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        Object block = fields(blocks[i]);
        standing = standing || get(block, "standingBounds");
        put(block, "_derivedFootprint", items(footprints).at(i));
        blocks[i] = object(std::move(block));
    }
    if (standing) {
        const Value standing_footprints = route_footprints(array(blocks), true);
        for (std::size_t i = 0; i < blocks.size(); ++i)
            blocks[i] = object(with(fields(blocks[i]), "_derivedStandingFootprint", items(standing_footprints).at(i)));
    }
    put(route, "blocks", array(std::move(blocks)));
    const Value route_value = object(std::move(route));
    Object plan = fields(schedule(route_value, {0}));
    put(plan, "vertical", at(fields(at(fields(prepared->result), "plan")), "vertical"));
    const Value problem = numerical_problem(route_value, object(std::move(plan)), analysis.coefficient, nullptr);
    return disk_screen(problem, array({}), nullptr, kDiskCap, kDiskMaxIter);
}

std::pair<Value, Value> InteractionGate::check_program(const std::shared_ptr<const Program>& program,
                                                       const Value& given_objective) {
    auto analysis = this->analysis(program);
    if (!analysis) return {object({{"reason", text("vertical_schedule")}}), Value{nullptr}};
    const Value objective = is_null(given_objective) ? at(fields(scene_), "objective") : given_objective;
    const auto cut = walk_off_tick(*program);
    const std::string key = "screen:" + analysis->key + ":" + digest(objective) + ":" +
                            (cut ? std::to_string(*cut) : std::string("None"));
    const bool wall_hits = py_truthy(get_or_null(fields(scene_), "maxWallHits"));
    bool undecided = false;
    auto relax = [&](const Array& rows) {
        count("relaxationChecks");
        Value checked = relaxation(program, *analysis, objective, rows);
        undecided = undecided || (!is_null(checked) && as_text(at(fields(checked), "outcome")) == "undecided");
        return checked;
    };
    auto infeasible = [](const Value& checked) {
        return !is_null(checked) && as_text(at(fields(checked), "outcome")) == "infeasible";
    };
    using Verdict = std::pair<Value, Value>;
    auto verdict = cached<Verdict>(key, "screen", [&]() -> std::shared_ptr<const Verdict> {
        auto made = [](Value proof, Value selection) {
            return std::make_shared<const Verdict>(std::move(proof), std::move(selection));
        };
        Value proof = required(program);
        if (py_truthy(proof)) return made(proof, Value{nullptr});
        std::optional<Value> faces;
        if (cut) faces = walk_off_faces(scene_, geometry_, *program, *cut, analysis->envelopes, landing_boxes(*program, *cut));
        if (!faces) {
            if (wall_hits) return made(Value{nullptr}, Value{nullptr});
            const Value checked = relax({});
            return made(infeasible(checked) ? at(fields(checked), "proof") : Value{nullptr}, Value{nullptr});
        }
        const Object& f = fields(*faces);
        if (items(at(f, "options")).empty())
            return made(object({{"reason", text("launch_support_not_left")}, {"input", integer_value(*cut)},
                                {"solids", at(f, "solids")}}),
                        Value{nullptr});
        Array kept;
        proof = Value{nullptr};
        for (const auto& row : items(at(f, "options"))) {
            const Value checked = wall_hits ? Value{nullptr} : relax({row});
            if (infeasible(checked)) {
                proof = at(fields(checked), "proof");
                continue;
            }
            kept.push_back(row);
            // Without the relaxed screen (wall hits, or outside its class) every open reachable face goes to the
            // node solve; restricting where the screen runs widens what reaches this fallback.
            if (!is_null(checked) && !py_truthy(at(f, "diagonal"))) break;
        }
        if (kept.empty()) return made(proof, Value{nullptr});
        return made(Value{nullptr}, object({{"offset", integer_value(*cut)}, {"solids", at(f, "solids")},
                                            {"options", array(std::move(kept))}}));
    });
    if (undecided) {
        auto found = index_.find(key);
        cache_.erase(found->second);
        index_.erase(found);
    }
    return *verdict;
}

namespace {

InteractionGate& gate_of(const GenerationContext& context) {
    if (!context.gate) fail("Native generation needs an interactionGate");
    return *context.gate;
}

Value first_extreme(const Array& regions, std::size_t k, bool lowest) {  // min()/max() keep the first of equals
    Value best = items(regions.front())[k];
    for (const auto& r : regions) {
        const double v = num(items(r)[k]).value;
        if (lowest ? v < num(best).value : v > num(best).value) best = items(r)[k];
    }
    return best;
}

}  // namespace

std::vector<FaceOption> candidate_faces(const GenerationContext& context, const std::string& kind) {
    InteractionGate& gate = gate_of(context);
    if (!context.launch_face) return gate.top_faces();
    const std::set<std::string>& kept = gate.reachable_faces(*context.launch_face).of(kind);
    std::set<std::int64_t> solids;
    for (const auto& b : items(at(fields(gate.scene()), "blocks")))
        if (kept.count(as_text(at(fields(b), "id")))) solids.insert(as_int(at(fields(b), "solidIndex")));
    std::vector<FaceOption> out;
    for (const auto& f : gate.top_faces())
        if (std::any_of(f.members.begin(), f.members.end(), [&](std::int64_t m) { return solids.count(m); }))
            out.push_back(f);
    return out;
}

bool candidate_option(const GenerationContext& context, const FaceOption& option, const std::string& kind) {
    InteractionGate& gate = gate_of(context);
    if (!context.launch_face) return true;
    const FaceTable& table = gate.reachable_faces(*context.launch_face);
    const std::set<std::int64_t>& solids = kind == "ceilingSolids" ? table.ceiling_solids : table.solids;
    return std::any_of(option.members.begin(), option.members.end(), [&](std::int64_t m) { return solids.count(m); });
}

std::vector<bool> bounds_allowed_many(const GenerationContext& context, const std::vector<Array>& bounds,
                                      std::int64_t tick, const Object& label, bool jumping,
                                      const std::vector<std::string>& phases) {
    if (bounds.empty()) return {};
    std::vector<bool> out;
    for (const auto& proof : gate_of(context).query_many(context, bounds, tick, label, jumping, phases))
        out.push_back(!py_truthy(at(fields(proof), "ruledOut")));
    return out;
}

bool interaction_allowed(const GenerationContext& context, const Value& box, std::int64_t tick, const Object& label,
                         bool jumping) {
    const Array& raw = items(box);
    Box typed;
    for (std::size_t i = 0; i < 6; ++i) typed[i] = num(raw.at(i));
    Array expanded;
    for (const Num& n : expanded_box(typed)) expanded.push_back(value(n));
    return bounds_allowed_many(context, {expanded}, tick, label, jumping, {"Y", "X", "Z"})[0];
}

bool behind_decision(const GenerationContext& context, const Array& regions, std::int64_t tick, bool jumping,
                     const Object& label) {
    if (!context.forward) return false;
    const Forward& forward = *context.forward;
    InteractionGate& gate = gate_of(context);
    const std::int64_t at_tick = context.program->profile.duration + tick;
    const Value envelope = gate.approach(context, tick, jumping);
    const Object* reach = nullptr;
    if (!is_null(envelope)) {
        const Array& states = items(at(fields(envelope), "states"));
        if (at_tick < static_cast<std::int64_t>(states.size())) reach = &fields(states[at_tick]);
    }
    const double px = forward.point[0], pz = forward.point[1], dx = forward.direction[0], dz = forward.direction[1];
    double ahead = -std::numeric_limits<double>::infinity();
    for (const auto& r : regions) {
        const Array& region = items(r);
        double x0 = num(region[0]).value, z0 = num(region[1]).value, x1 = num(region[2]).value, z1 = num(region[3]).value;
        if (reach) {
            const Array &rx = items(at(*reach, "X")), &rz = items(at(*reach, "Z"));
            x0 = std::max(x0, num(rx[0]).value);
            x1 = std::min(x1, num(rx[1]).value);
            z0 = std::max(z0, num(rz[0]).value);
            z1 = std::min(z1, num(rz[1]).value);
            if (x0 > x1 || z0 > z1) continue;
        }
        ahead = std::max(ahead, std::max(dx * (x0 - px), dx * (x1 - px)) + std::max(dz * (z0 - pz), dz * (z1 - pz)));
    }
    if (ahead >= -forward.margin.value) return false;
    Object exclusion = label;
    exclusion.emplace_back("frame", context.frame_id);
    exclusion.emplace_back("horizontal", array({first_extreme(regions, 0, true), first_extreme(regions, 1, true),
                                                first_extreme(regions, 2, false), first_extreme(regions, 3, false)}));
    exclusion.emplace_back("input", integer_value(at_tick));
    exclusion.emplace_back("proof", object({{"reason", text("behind_decision_point")}, {"ahead", real(ahead)},
                                            {"margin", value(forward.margin)}}));
    gate.exclusions.push_back(object(std::move(exclusion)));
    return true;
}

bool entry_backward(const GenerationContext& context, const std::string& axis, std::int64_t sign, const Array& region,
                    const Object& label) {
    if (!context.forward || sign * context.forward->direction[axis == "X" ? 0 : 1] <= 0) return false;
    Object exclusion = label;
    exclusion.emplace_back("frame", context.frame_id);
    exclusion.emplace_back("horizontal", array(region));
    exclusion.emplace_back("input", integer_value(context.program->profile.duration));
    exclusion.emplace_back("proof", object({{"reason", text("entry_against_guide")},
                                            {"side", text(axis + (sign > 0 ? "+" : "-"))}}));
    gate_of(context).exclusions.push_back(object(std::move(exclusion)));
    return true;
}

bool walk_off_allowed(const GenerationContext& context, const Value& landing_boxes, const Object& label) {
    return gate_of(context).walk_off(context, landing_boxes, label);
}

}  // namespace pkc
