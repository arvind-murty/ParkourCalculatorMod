#include "pkc/constraints/variants.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <utility>

#include "pkc/canonical.hpp"
#include "pkc/constraints/avoidance.hpp"
#include "pkc/constraints/contacts.hpp"
#include "pkc/envelopes.hpp"
#include "pkc/reachability/screen.hpp"
#include "../py_math.hpp"
#include "../value_ops.hpp"
#include "variant_state.hpp"

namespace pkc {

namespace {

constexpr std::size_t kAnalysisCache = 128, kPreparationCache = 256;
constexpr std::int64_t kProofPolicy = 5;  // reachability/service.PROOF_POLICY
constexpr double kInf = std::numeric_limits<double>::infinity();

double d(const Value& v) {
    double out = 0.;
    if (!numeric(v, out)) fail("expected a number");
    return out;
}

std::int64_t i64(const Value& v) {
    auto* held = std::get_if<std::int64_t>(&v.data);
    if (!held) fail("expected an int");
    return *held;
}

Value ints(const std::vector<std::int64_t>& values) {
    Array out;
    for (auto v : values) out.push_back(integer_value(v));
    return array(std::move(out));
}

std::vector<std::int64_t> int_list(const Value& v) {
    std::vector<std::int64_t> out;
    for (const auto& x : items(v)) out.push_back(i64(x));
    return out;
}

Value strings(const std::vector<std::string>& values) {
    Array out;
    for (const auto& v : values) out.push_back(text(v));
    return array(std::move(out));
}

std::vector<std::string> string_list(const Value& v) {
    std::vector<std::string> out;
    for (const auto& x : items(v)) out.push_back(as_text(x));
    return out;
}

Array listed(const Object& object, const char* key) {
    const Value* value = get(object, key);
    return value ? items(*value) : Array{};
}

bool ruled_out(const Value& proof) { return py_truthy(at(fields(proof), "ruledOut")); }

// Adds the elapsed seconds to a timing on scope exit, as the Python finally blocks do.
class Timed {
public:
    explicit Timed(double& total) : total_(total), started_(std::chrono::steady_clock::now()) {}
    ~Timed() { total_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count(); }

private:
    double& total_;
    std::chrono::steady_clock::time_point started_;
};

Value finite(const Value& value) {
    if (auto* real_value = std::get_if<double>(&value.data); real_value && !std::isfinite(*real_value))
        return object({{"nonfiniteBound", text(std::isnan(*real_value) ? "nan" : *real_value > 0 ? "inf" : "-inf")}});
    if (is_object(value)) {
        Object out;
        for (const auto& [k, v] : fields(value)) out.emplace_back(k, finite(v));
        return object(std::move(out));
    }
    if (is_list(value)) {
        Array out;
        for (const auto& v : items(value)) out.push_back(finite(v));
        return array(std::move(out));
    }
    return value;
}

Value obstacles_value(const Obstacles& obstacles) {
    Array out;
    for (const auto& [begin, solid] : obstacles) out.push_back(array({integer_value(begin), integer_value(solid)}));
    return array(std::move(out));
}

Obstacles obstacles_of(const Value& value) {
    Obstacles out;
    for (const auto& pair : items(value)) out.push_back({i64(items(pair).at(0)), i64(items(pair).at(1))});
    return out;
}

// ---- state <-> the Python dict

Value walk_value(const EncounterWalk& walk) {
    return object({{"at", integer_value(walk.at)}, {"rows", array(walk.rows)}, {"history", ints(walk.history)},
                   {"spans", array(walk.spans)}});
}

EncounterWalk walk_of(const Value& value) {
    const Object& w = fields(value);
    return {i64(at(w, "at")), items(at(w, "rows")), int_list(at(w, "history")), items(at(w, "spans"))};
}

Value cursor_value(const VariantCursor& cursor) {
    Object out;
    if (cursor.pending) {
        Array walks;
        for (const auto& w : *cursor.pending) walks.push_back(walk_value(w));
        out.emplace_back("pending", array(std::move(walks)));
    }
    if (cursor.seen) out.emplace_back("seen", strings(*cursor.seen));
    if (cursor.completed) out.emplace_back("completed", strings(*cursor.completed));
    if (cursor.covers) out.emplace_back("covers", array(*cursor.covers));
    if (cursor.index) out.emplace_back("index", integer_value(*cursor.index));
    return object(std::move(out));
}

VariantCursor cursor_of(const Value& value) {
    const Object& c = fields(value);
    VariantCursor out;
    if (const Value* index = get(c, "index")) out.index = i64(*index);
    if (const Value* pending = get(c, "pending")) {
        out.pending.emplace();
        for (const auto& w : items(*pending)) out.pending->push_back(walk_of(w));
    }
    if (const Value* seen = get(c, "seen")) out.seen = string_list(*seen);
    if (const Value* completed = get(c, "completed")) out.completed = string_list(*completed);
    if (const Value* covers = get(c, "covers")) out.covers = items(*covers);
    return out;
}

Value node_value(const VariantNode& node) {
    Object out{{"item", integer_value(node.item)}};
    if (node.remaining) out.emplace_back("remaining", ints(*node.remaining));
    out.emplace_back("rows", array(node.rows));
    out.emplace_back("passages", array(node.passages));
    out.emplace_back("choices", array(node.choices));
    if (node.chosen_item) out.emplace_back("chosenItem", integer_value(*node.chosen_item));
    if (node.prepared) out.emplace_back("prepared", *node.prepared);
    if (node.cursor) out.emplace_back("cursor", cursor_value(*node.cursor));
    if (node.selected) out.emplace_back("selected", *node.selected);
    return object(std::move(out));
}

VariantNode node_of(const Value& value) {
    const Object& n = fields(value);
    VariantNode out;
    out.item = i64(at(n, "item"));
    if (const Value* remaining = get(n, "remaining")) out.remaining = int_list(*remaining);
    out.rows = items(at(n, "rows"));
    out.passages = items(at(n, "passages"));
    out.choices = items(at(n, "choices"));
    if (const Value* chosen = get(n, "chosenItem")) out.chosen_item = i64(*chosen);
    if (const Value* prepared = get(n, "prepared")) out.prepared = *prepared;
    if (const Value* cursor = get(n, "cursor")) out.cursor = cursor_of(*cursor);
    if (const Value* selected = get(n, "selected")) out.selected = *selected;
    return out;
}

}  // namespace

std::string interaction_context_key(const Value& context) {
    return digest(object({{"policy", integer_value(kProofPolicy)}, {"context", finite(context)}}));
}

ConstraintVariants::ConstraintVariants(VariantInputs inputs, std::optional<Obstacles> obstacles, const Value& state)
    : inputs_(std::move(inputs)) {
    if (!is_null(state))
        if (const Value* saved = get(fields(state), "obstacles")) obstacles = obstacles_of(*saved);
    if (obstacles) std::sort(obstacles->begin(), obstacles->end());
    obstacles_ = obstacles;
    const Object& route = fields(inputs_.route);
    const Object& profile = fields(inputs_.profile);
    const Object& scope = fields(inputs_.scope);
    through_ = i64(at(scope, "through"));
    Object routed;
    for (const auto& [k, v] : route)
        if (k != "warmYaw") routed.emplace_back(k, v);
    Value segment_solids{nullptr};
    if (inputs_.segment_solids) {
        Array pairs;
        for (const auto& [begin, solids] : *inputs_.segment_solids) pairs.push_back(array({integer_value(begin), ints(solids)}));
        segment_solids = array(std::move(pairs));
    }
    // gates, signature and routeGuards no longer have producers; their constant entries keep saved states resumable.
    Object context{{"version", integer_value(kGenerationPolicy)}, {"route", object(std::move(routed))},
                   {"profile", inputs_.profile}, {"scope", inputs_.scope}, {"gates", array({})},
                   {"signature", array({})}, {"checks", array(inputs_.checks)}, {"geometry", inputs_.geometry},
                   {"envelope", text(interaction_context_key(inputs_.envelopes))}, {"owners", inputs_.owners},
                   {"segmentSolids", segment_solids}, {"routeGuards", Value{nullptr}},
                   {"settled", integer_value(inputs_.settled)}, {"walkOffs", array(inputs_.walk_offs)}};
    if (obstacles_) context.emplace_back("obstacles", obstacles_value(*obstacles_));
    context_ = digest(object(std::move(context)));
    if (!is_null(state)) {
        const Object& saved = fields(state);
        if (!py_equal(get_or_null(saved, "version"), integer_value(kVariantStateVersion)) ||
            !py_equal(get_or_null(saved, "context"), text(context_)))
            fail("Incompatible constraint-generation continuation (policy" + std::to_string(kGenerationPolicy) + "/state" +
                 std::to_string(kVariantStateVersion) + ")");
    }
    geometry_ = std::make_shared<const SceneGeometry>(at(fields(inputs_.geometry), "solids"));
    state_ = std::make_unique<State>();
    State& s = *state_;
    if (!is_null(state)) {
        const Object& saved = fields(state);
        s.context = as_text(at(saved, "context"));
        s.items = items(at(saved, "items"));
        s.exclusions = items(at(saved, "exclusions"));
        s.screen_exclusions = items(at(saved, "screenExclusions"));
        for (const auto& n : items(at(saved, "pending"))) s.pending.push_back(node_of(n));
        s.seen = string_list(at(saved, "seen"));
        s.partial_seen = string_list(at(saved, "partialSeen"));
        s.emitted = i64(at(saved, "emitted"));
        s.exhausted = py_truthy(at(saved, "exhausted"));
        s.reference = at(saved, "reference");
        s.preparation_steps = i64(at(saved, "preparationSteps"));
        s.duplicate_variants = i64(at(saved, "duplicateVariants"));
        s.obstacles = obstacles_;
        for (const auto& [k, v] : fields(at(saved, "metrics"))) s.metrics.emplace_back(k, i64(v));
        s.uncertain_interactions = items(at(saved, "uncertainInteractions"));
        const Object& timings = fields(at(saved, "timings"));
        s.preparation_seconds = d(at(timings, "preparationSeconds"));
        s.generation_seconds = d(at(timings, "variantGenerationSeconds"));
        s.analysis_seconds = d(at(timings, "conditionalAnalysisSeconds"));
        s.encounter_log = listed(saved, "encounterLog");
        s.dropped_options = listed(saved, "droppedOptions");
        return;
    }
    Array items_out;
    for (const auto& c_value : listed(profile, "regionChoices")) {
        const Object& c = fields(c_value);
        if (i64(at(c, "offset")) > through_) continue;
        items_out.push_back(object({{"rows", at(c, "options")}, {"role", text("contact_region")},
                                    {"surface", at(c, "surface")}, {"owner", at(c, "owner")}, {"offset", at(c, "offset")}}));
    }
    // One option is a fixed row in effect; two (a diagonal tie) branch, without family climbs or conflict joins.
    for (const auto& w_value : inputs_.walk_offs) {
        const Object& w = fields(w_value);
        const std::int64_t offset = i64(at(w, "offset"));
        if (!(inputs_.settled <= offset && offset < through_)) continue;
        Array rows;
        for (const auto& row : items(at(w, "options"))) rows.push_back(array({row}));
        items_out.push_back(object({{"rows", array(std::move(rows))}, {"role", text("walk_off")},
                                    {"solids", at(w, "solids")}, {"offset", at(w, "offset")}}));
    }
    // Temporal order is a traversal preference: all items share the original model.
    const auto scope_indices = int_list(at(scope, "solidIndices"));
    auto name = [&](std::int64_t i) {
        return is_null(inputs_.owners) ? "solid:" + std::to_string(i) : as_text(items(inputs_.owners).at(i));
    };
    for (const auto& segment : landing_segments(profile, through_)) {
        const std::int64_t begin = segment.first, end = segment.second;
        // A settled stretch keeps its rows, but a re-solve can still hit an obstacle they never covered.
        if (end <= inputs_.settled && !obstacles_) continue;
        // The obstacle is built from the solids this stretch can reach: an unreachable member's space is free for the
        // stretch, and dropping it can split a touching cluster.
        std::vector<std::int64_t> basis = scope_indices;
        if (inputs_.segment_solids)
            for (const auto& [b, solids] : *inputs_.segment_solids)
                if (b == begin) {
                    std::set<std::int64_t> both;
                    for (auto i : scope_indices)
                        if (std::find(solids.begin(), solids.end(), i) != solids.end()) both.insert(i);
                    basis.assign(both.begin(), both.end());
                }
        if (obstacles_) {
            // A solve that hit a solid proves the stretch reaches it.
            std::set<std::int64_t> joined(basis.begin(), basis.end());
            for (const auto& [b, solid] : *obstacles_)
                if (b == begin && std::find(scope_indices.begin(), scope_indices.end(), solid) != scope_indices.end())
                    joined.insert(solid);
            basis.assign(joined.begin(), joined.end());
        }
        for (const auto& members : geometry_->cluster_groups(basis)) {
            if (obstacles_ && std::none_of(obstacles_->begin(), obstacles_->end(), [&](const auto& o) {
                    return o[0] == begin && std::find(members.begin(), members.end(), o[1]) != members.end();
                }))
                continue;
            Array names;
            std::set<std::string> distinct;
            for (auto i : members) {
                names.push_back(text(name(i)));
                distinct.insert(name(i));
            }
            std::string owner;
            for (const auto& n : distinct) owner += (owner.empty() ? "" : "+") + n;
            items_out.push_back(object({{"clusterId", integer_value(members.front())}, {"basis", ints(basis)},
                                        {"members", ints(members)}, {"names", array(std::move(names))},
                                        {"begin", integer_value(begin)}, {"end", integer_value(end)},
                                        {"owner", text(owner)}}));
        }
    }
    for (const auto& check_value : inputs_.checks) {
        const Object& check = fields(check_value);
        const std::int64_t solid = i64(at(fields(at(check, "block")), "solidIndex"));
        const std::int64_t offset = i64(at(check, "offset"));
        if (std::find(scope_indices.begin(), scope_indices.end(), solid) == scope_indices.end() ||
            !(inputs_.settled <= offset && offset <= through_))
            continue;
        items_out.push_back(object({{"contactCheck", check_value},
                                    {"identity", object({{"solid", integer_value(solid)}, {"offset", integer_value(offset)}})}}));
    }
    s.context = context_;
    s.items = std::move(items_out);
    s.pending.push_back(VariantNode{});
    s.reference = inputs_.reference;
    s.obstacles = obstacles_;
}

ConstraintVariants::~ConstraintVariants() = default;

const Object& ConstraintVariants::profile() const { return fields(inputs_.profile); }

std::size_t ConstraintVariants::item_count() const { return state_->items.size(); }

bool ConstraintVariants::exhausted() const { return state_->exhausted; }

Value ConstraintVariants::state() const {
    const State& s = *state_;
    Array pending;
    for (const auto& n : s.pending) pending.push_back(node_value(n));
    Object metrics;
    for (const auto& [k, v] : s.metrics) metrics.emplace_back(k, integer_value(v));
    Object out{{"version", integer_value(kVariantStateVersion)},
               {"context", text(s.context)},
               {"items", array(s.items)},
               {"exclusions", array(s.exclusions)},
               {"screenExclusions", array(s.screen_exclusions)},
               {"pending", array(std::move(pending))},
               {"seen", strings(s.seen)},
               {"partialSeen", strings(s.partial_seen)},
               {"emitted", integer_value(s.emitted)},
               {"exhausted", boolean(s.exhausted)},
               {"reference", s.reference},
               {"preparationSteps", integer_value(s.preparation_steps)},
               {"duplicateVariants", integer_value(s.duplicate_variants)}};
    if (s.obstacles) out.emplace_back("obstacles", obstacles_value(*s.obstacles));
    out.emplace_back("metrics", object(std::move(metrics)));
    out.emplace_back("uncertainInteractions", array(s.uncertain_interactions));
    out.emplace_back("timings", object({{"preparationSeconds", real(s.preparation_seconds)},
                                        {"variantGenerationSeconds", real(s.generation_seconds)},
                                        {"conditionalAnalysisSeconds", real(s.analysis_seconds)}}));
    out.emplace_back("encounterLog", array(s.encounter_log));
    out.emplace_back("droppedOptions", array(s.dropped_options));
    return object(std::move(out));
}

std::array<std::int64_t, 2> ConstraintVariants::obstacle_key(std::int64_t solid, std::int64_t tick) const {
    for (const auto& [begin, end] : landing_segments(profile(), through_))
        if (begin <= tick && tick < end) return {begin, solid};
    fail("No stretch holds movement " + std::to_string(tick));
}

std::unique_ptr<ConstraintVariants> ConstraintVariants::with_obstacles(Obstacles obstacles) const {
    return std::make_unique<ConstraintVariants>(inputs_, std::move(obstacles), Value{nullptr});
}

void ConstraintVariants::check() const {
    if (inputs_.check) inputs_.check();
}

void ConstraintVariants::metric(const std::string& name, std::int64_t count) {
    for (auto& [k, v] : state_->metrics)
        if (k == name) {
            v += count;
            return;
        }
    state_->metrics.emplace_back(name, count);
}

Num ConstraintVariants::coordinate(const std::string& axis, std::int64_t tick, const Value& envelopes) const {
    if (!is_null(state_->reference)) {
        const Value& v = items(items(state_->reference).at(tick)).at(axis == "X" ? 0 : 1);
        if (auto* integer = std::get_if<std::int64_t>(&v.data)) return whole(*integer);
        return flt(d(v));
    }
    const Array& range = items(at(fields(items(at(fields(envelopes), "states")).at(tick)), axis));
    return flt(python_sum({d(range[0]), d(range[1])}) / 2);
}

Value ConstraintVariants::face_row(const Object& face, std::int64_t t) const {
    const Object& route = fields(inputs_.route);
    const Num margin = face_clearance(route, 0, t, face);
    const std::int64_t direction = i64(at(face, "direction"));
    const Value& plane_value = at(face, "plane");
    const Num plane = std::holds_alternative<std::int64_t>(plane_value.data) ? whole(i64(plane_value)) : flt(d(plane_value));
    return object({{"leg", integer_value(0)}, {"offset", integer_value(t)}, {"mode", at(face, "axis")},
                   {"t1", integer_value(t)}, {"cmp", text(direction > 0 ? "GE" : "LE")},
                   {"rhs", value(add(plane, mul(whole(direction), margin)))}, {"name", text("avoidance")}});
}

std::string ConstraintVariants::effective_key(const Array& rows) const {
    // Base-context entailment only: sibling branches never borrow one another's tightened bounds.
    const Array& states = items(at(fields(inputs_.envelopes), "states"));
    Array bounds;
    for (const auto& r : canonical_rows(rows)) {
        const Object& row = fields(r);
        const std::int64_t t = i64(at(row, "t1"));
        const std::string& axis = as_text(at(row, "mode"));
        const Array& range = items(at(fields(states.at(t)), axis));
        const double lo = d(range[0]), hi = d(range[1]), rhs = d(at(row, "rhs"));
        const std::string& cmp = as_text(at(row, "cmp"));
        if ((cmp == "GE" && lo >= rhs) || (cmp == "LE" && hi <= rhs)) continue;
        bounds.push_back(array({integer_value(t), text(axis), text(cmp), at(row, "rhs")}));
    }
    return digest(array(std::move(bounds)));
}

Value ConstraintVariants::analyze(const Array& rows) {
    check();
    const std::string key = effective_key(rows);
    for (const auto& [k, v] : analysis_cache_)
        if (k == key) {
            metric("analysisHits");
            return v;
        }
    Timed timed(state_->analysis_seconds);
    metric("analysisMisses");
    const Value result = condition_envelopes(inputs_.envelopes, rows, !obstacles_,
                                             is_null(inputs_.objective) ? nullptr : &inputs_.objective);
    // A cache is disposable acceleration, not a retained-alternative cap.
    if (analysis_cache_.size() >= kAnalysisCache) analysis_cache_.erase(analysis_cache_.begin());
    analysis_cache_.emplace_back(key, result);
    return result;
}

void ConstraintVariants::excluded(const Array& rows, const Value& proof, const Value* item, const Value* option) {
    Object entry{{"rowsHash", text(digest(array(rows)))}, {"proof", proof}};
    if (option) {
        entry.emplace_back("item", *item);
        entry.emplace_back("optionRows", at(fields(*option), "rows"));
        entry.emplace_back("passage", get_or_null(fields(*option), "passage"));
    }
    state_->screen_exclusions.push_back(object(std::move(entry)));
    metric("screenExclusions");
}

Value ConstraintVariants::prepare_item(Value item_value) {
    Timed timed(state_->preparation_seconds);
    const Value& envelopes = active_envelopes_ ? *active_envelopes_ : inputs_.envelopes;
    if (const Value* check_value = get(fields(item_value), "contactCheck")) {
        check();
        const Object& c = fields(*check_value);
        const Array& r = items(at(c, "region"));
        const std::int64_t t = i64(at(c, "offset"));
        const Value contact = object({{"regions", array({object({{"tick", integer_value(t)},
                                                                  {"bounds", array({r[0], real(-kInf), r[1], r[2], real(kInf), r[3]})},
                                                                  {"interior", boolean(true)}})})}});
        Array rows;
        if (ruled_out(interaction_reachability(envelopes, contact))) {
            rows.push_back(array({}));
        } else {
            double x, z;
            if (!is_null(state_->reference)) {
                const Array& point = items(items(state_->reference).at(t));
                x = d(point[0]);
                z = d(point[1]);
            } else {
                x = (d(r[0]) + d(r[2])) / 2;
                z = (d(r[1]) + d(r[3])) / 2;
            }
            for (const auto& row : avoid_contact_rows(i64(at(c, "leg")), t, as_text(at(c, "reason")),
                                                      as_text(at(fields(at(c, "block")), "id")), r, x, z))
                if (!ruled_out(interaction_reachability(envelopes, array({row})))) rows.push_back(array({row}));
        }
        Object item = fields(item_value);
        put(item, "rows", array(std::move(rows)));
        erase(item, "contactCheck");
        item_value = object(std::move(item));
    }
    if (get(fields(item_value), "clusterId")) item_value = prepare_encounter(*this, item_value, envelopes);
    return item_value;
}

std::optional<Value> ConstraintVariants::option(const Value& item_value, VariantCursor& cursor) {
    const Value item_prepared = prepare_item(item_value);
    const Object& item = fields(item_prepared);
    if (const Value* rows = get(item, "rows")) {
        const std::int64_t index = cursor.index.value_or(0);
        if (index == static_cast<std::int64_t>(items(*rows).size())) return std::nullopt;
        cursor.index = index + 1;
        return object({{"rows", items(*rows).at(index)}});
    }
    return encounter_option(*this, item_prepared, cursor, active_envelopes_ ? *active_envelopes_ : inputs_.envelopes);
}

void ConstraintVariants::choose_item(VariantNode& node) {
    if (!node.remaining) {
        node.remaining.emplace();
        for (auto i = node.item; i < static_cast<std::int64_t>(state_->items.size()); ++i) node.remaining->push_back(i);
    }
    if (node.prepared) return;
    using Rank = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
    std::optional<std::tuple<Rank, std::int64_t, Value>> best;
    const std::string context = effective_key(node.rows);
    for (auto index : *node.remaining) {
        check();
        const std::string key = context + ":" + std::to_string(index);
        auto found = std::find_if(preparation_cache_.begin(), preparation_cache_.end(),
                                  [&](const auto& entry) { return entry.first == key; });
        if (found == preparation_cache_.end()) {
            Value prepared = prepare_item(state_->items.at(index));
            if (preparation_cache_.size() >= kPreparationCache) preparation_cache_.erase(preparation_cache_.begin());
            preparation_cache_.emplace_back(key, std::move(prepared));
            found = preparation_cache_.end() - 1;
        }
        const Value item_value = found->second;
        const Object& item = fields(item_value);
        Rank rank{-1, index, 0};
        if (get(item, "clusterId")) {
            const Array& phases = items(at(item, "phases"));
            std::int64_t options = 0;
            for (const auto& p : phases) options += static_cast<std::int64_t>(fields(at(fields(p), "options")).size());
            rank = {phases.empty() ? -1 : i64(at(fields(phases[0]), "at")), options, index};
        }
        if (!best || rank < std::get<0>(*best)) best.emplace(rank, index, item_value);
        if (py_equal(get_or_null(item, "role"), text("contact_region"))) break;
    }
    node.chosen_item = std::get<1>(*best);
    node.prepared = std::get<2>(*best);
}

Value ConstraintVariants::branch_choice(const Object& item, const Object& option, const Object& choice) const {
    // The intermediate choice a decision check makes for this item: a closed obstacle segment's side family, or a
    // contact region's or contact check's exact rows. Open segments stay undecided.
    if (get(item, "clusterId")) {
        const Array ground = listed(profile(), "grounded");
        if (i64(at(item, "end")) < through_ ||
            (through_ < static_cast<std::int64_t>(ground.size()) && py_truthy(ground[through_]))) {
            const Value* sides = get(choice, "sides");
            return object({{"kind", text("cluster")}, {"clusterId", at(item, "clusterId")}, {"begin", at(item, "begin")},
                           {"end", at(item, "end")}, {"sides", sides ? *sides : array({})}});
        }
        return Value{nullptr};
    }
    const Value rows = text(digest(array(canonical_rows(items(at(option, "rows"))))));
    const Value& role = get_or_null(item, "role");
    if (py_equal(role, text("contact_region")))
        return object({{"kind", text("region")}, {"surface", at(item, "surface")}, {"offset", at(item, "offset")},
                       {"rows", rows}});
    if (py_equal(role, text("walk_off")))
        return object({{"kind", text("walk_off")}, {"solids", at(item, "solids")}, {"offset", at(item, "offset")},
                       {"rows", rows}});
    if (const Value* identity = get(item, "identity")) {
        Object out{{"kind", text("contact")}};
        for (const auto& field : fields(*identity)) out.push_back(field);
        out.emplace_back("rows", rows);
        return object(std::move(out));
    }
    return Value{nullptr};
}

std::optional<Value> ConstraintVariants::next() {
    struct Reset {
        std::optional<Value>& active;
        ~Reset() { active.reset(); }
    } reset{active_envelopes_};
    Timed timed(state_->generation_seconds);
    return next_variant();
}

std::optional<Value> ConstraintVariants::next_variant() {
    State& s = *state_;
    while (!s.pending.empty()) {
        check();
        const Value analysis = analyze(s.pending.back().rows);
        const Object& analyzed = fields(analysis);
        if (ruled_out(at(analyzed, "proof"))) {
            excluded(s.pending.back().rows, at(analyzed, "proof"));
            s.pending.pop_back();
            continue;
        }
        active_envelopes_ = at(analyzed, "envelopes");
        VariantNode& node = s.pending.back();
        if (!node.remaining) {
            node.remaining.emplace();
            for (auto i = node.item; i < static_cast<std::int64_t>(s.items.size()); ++i) node.remaining->push_back(i);
        }
        if (node.remaining->empty()) {
            const Array rows = canonical_rows(node.rows);
            const std::string key = effective_key(rows);
            const VariantNode done = std::move(s.pending.back());
            s.pending.pop_back();
            std::vector<Value> ordered(done.choices.begin(), done.choices.end());
            std::stable_sort(ordered.begin(), ordered.end(), [](const Value& a, const Value& b) {
                return i64(at(fields(a), "item")) < i64(at(fields(b), "item"));
            });
            std::vector<std::pair<std::string, Value>> branches;
            for (const auto& c : ordered)
                if (const Value* branch = get(fields(c), "branch")) branches.emplace_back(dumps(*branch, true, false), *branch);
            std::stable_sort(branches.begin(), branches.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            Array choices;
            for (auto& [_, branch] : branches) choices.push_back(std::move(branch));
            const std::string choices_key = digest(array(choices));
            if (std::find(s.seen.begin(), s.seen.end(), key) != s.seen.end()) {
                ++s.duplicate_variants;
                continue;
            }
            s.seen.push_back(key);
            ++s.emitted;
            Array out_rows, observations, family, switches;
            for (const auto& r : rows) {
                Object row;
                for (const auto& field : fields(r))
                    if (field.first != "t1") row.push_back(field);
                out_rows.push_back(object(std::move(row)));
            }
            for (const auto& p : done.passages) observations.push_back(object(with(fields(p), "kind", text("passage"))));
            for (const auto& c_value : ordered) {
                const Object& c = fields(c_value);
                const Value* sides = get(c, "sides");
                family.push_back(array({at(c, "item"), sides ? *sides : get_or_null(c, "key")}));
                for (const auto& t : listed(c, "switches")) switches.push_back(t);
            }
            return object({{"rows", array(std::move(out_rows))},
                           {"passages", array({})},
                           {"selectedPassages", array(done.passages)},
                           {"semanticObservations", array(std::move(observations))},
                           {"score", integer_value(s.emitted)},
                           {"family", text(digest(array(std::move(family))))},
                           {"switches", array(std::move(switches))},
                           {"branchChoices", array(choices)},
                           {"branchKey", text(choices_key)}});
        }
        choose_item(node);
        const Value item_value = *node.prepared;
        const Object& item = fields(item_value);
        if (const Value* item_rows = get(item, "rows")) {
            bool entailed = false;
            for (const auto& rows : items(*item_rows))
                if (rows_entailed(at(analyzed, "envelopes"), {}, items(rows))) {
                    entailed = true;
                    break;
                }
            if (entailed) {
                auto& remaining = *node.remaining;
                remaining.erase(std::find(remaining.begin(), remaining.end(), *node.chosen_item));
                node.chosen_item.reset();
                node.cursor.reset();
                node.prepared.reset();
                continue;
            }
        }
        // An option can be expensive to analyze: publish it before that interruptible boundary, so its cursor cannot
        // lose the selected child.
        if (!node.selected) {
            if (!node.cursor) node.cursor.emplace();
            std::optional<Value> chosen = option(item_value, *node.cursor);
            if (!chosen) {
                s.pending.pop_back();
                continue;
            }
            node.selected = std::move(*chosen);
        }
        const Value option_value = *node.selected;
        const Object& option = fields(option_value);
        const Value& passage = get_or_null(option, "passage");
        Array joined = node.rows;
        for (const auto& r : items(at(option, "rows"))) joined.push_back(r);
        const Array rows = canonical_rows(joined);
        const Value child_analysis = analyze(rows);
        VariantNode& current = s.pending.back();
        if (ruled_out(at(fields(child_analysis), "proof"))) {
            const Value chosen_item = integer_value(*current.chosen_item);
            excluded(rows, at(fields(child_analysis), "proof"), &chosen_item, &option_value);
            current.selected.reset();
            continue;
        }
        Object choice;
        if (!is_null(passage)) {
            Array sides, switches;
            for (const auto& side : items(at(fields(passage), "faces"))) sides.push_back(at(fields(side), "key"));
            for (const auto& a : items(at(fields(passage), "switchPhases"))) switches.push_back(integer_value(i64(a) / 3));
            choice = {{"item", integer_value(*current.chosen_item)}, {"sides", array(std::move(sides))},
                      {"switches", array(std::move(switches))}};
        } else {
            choice = {{"item", integer_value(*current.chosen_item)}, {"key", text(effective_key(items(at(option, "rows"))))}};
        }
        const Value branch = branch_choice(item, option, choice);
        if (!is_null(branch)) choice.emplace_back("branch", branch);
        VariantNode child;
        child.item = 0;
        child.remaining.emplace();
        for (auto i : *current.remaining)
            if (i != *current.chosen_item) child.remaining->push_back(i);
        child.rows = rows;
        child.passages = current.passages;
        if (!is_null(passage)) child.passages.push_back(passage);
        child.choices = current.choices;
        child.choices.push_back(object(std::move(choice)));
        // Counts and nominal face histories have no remaining resource capability once an encounter is assigned; the
        // unassigned item set and exact original model are part of this iterator's context.
        const std::string partial_key = digest(array({ints(*child.remaining), text(effective_key(rows))}));
        current.selected.reset();
        if (std::find(s.partial_seen.begin(), s.partial_seen.end(), partial_key) != s.partial_seen.end()) {
            ++s.duplicate_variants;
            continue;
        }
        s.partial_seen.push_back(partial_key);
        s.pending.push_back(std::move(child));
    }
    s.exhausted = true;
    return std::nullopt;
}

}  // namespace pkc
