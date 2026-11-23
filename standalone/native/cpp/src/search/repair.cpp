#include "pkc/search/repair.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../schema.hpp"
#include "pkc/canonical.hpp"
#include "pkc/constraints/avoidance.hpp"
#include "pkc/constraints/contacts.hpp"
#include "pkc/envelopes.hpp"
#include "pkc/search/identity.hpp"
#include "pkc/scene/static_collision.hpp"
#include "pkc/validation/vertical.hpp"
#include "pkc/vertical.hpp"

namespace pkc {

namespace {

// Close losers are kept only to recover when the route fails later; objectives are positions in blocks.
constexpr std::size_t kFallbackLimit = 2;
constexpr double kFallbackGap = 0.3;
// The confirmation walk stops after this many examined candidates once one is confirmed.
constexpr std::int64_t kWalkLimit = 3;

double monotonic() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double d(const Value& v) {
    double out = 0.;
    if (!numeric(v, out)) fail("expected a number");
    return out;
}

Num any_num(const Value& v) {
    if (auto* integer = std::get_if<std::int64_t>(&v.data)) return whole(*integer);
    return flt(d(v));
}

Array listed(const Object& object, const char* key) {
    const Value* value = get(object, key);
    return value ? items(*value) : Array{};
}

Array joined(Array a, const Array& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Value sorted_keys(const Value& value) {  // json.loads(json.dumps(value, sort_keys=True))
    if (is_object(value)) {
        Object out;
        for (const auto& [k, v] : fields(value)) out.emplace_back(k, sorted_keys(v));
        std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return object(std::move(out));
    }
    if (is_list(value)) {
        Array out;
        for (const auto& v : items(value)) out.push_back(sorted_keys(v));
        return array(std::move(out));
    }
    return value;
}

// sorted(set(tuples)): equal tuples (1 == 1.0) keep the first seen.
Value sorted_set(std::vector<Value> tuples) {
    std::vector<Value> unique;
    for (auto& t : tuples)
        if (!contains(unique, t)) unique.push_back(std::move(t));
    std::stable_sort(unique.begin(), unique.end(), [](const Value& a, const Value& b) { return py_compare(a, b) < 0; });
    return array(std::move(unique));
}

// repair_state.context(route, profiles, geometry, scope).
const char* kVariableRouteFields[] = {"airtimes", "constraints", "objective", "guideObjective", "warmYaw",
                                      "verticalProfiles", "movementUnits", "contactEvents", "ceilingEvents",
                                      "terminalSupportRequired", "profileDerivation"};

bool probe_row(const Value& row) {
    const Value& name = get_or_null(fields(row), "name");
    return is_object(row) && std::holds_alternative<std::string>(name.data) &&
           as_text(name).rfind("checkpoint_probe:", 0) == 0;
}

Value repair_context(const Object& route, const Value& profiles, const Value& geometry, const Value& scope) {
    Object kept;
    for (const auto& [k, v] : route)
        if (std::none_of(std::begin(kVariableRouteFields), std::end(kVariableRouteFields), [&](const char* f) { return k == f; }))
            kept.emplace_back(k, v);
    Array constraints, probes;
    for (const auto& r : items(at(route, "constraints"))) (probe_row(r) ? probes : constraints).push_back(r);
    const Value* guide = get(route, "guideObjective");
    const Value* terminal = get(route, "terminalSupportRequired");
    return sorted_keys(object({{"route", object(std::move(kept))},
                               {"constraints", array(std::move(constraints))},
                               {"profiles", profiles},
                               {"geometry", geometry},
                               {"scope", scope},
                               {"objective", guide ? *guide : at(route, "objective")},
                               {"terminalSupportRequired", terminal ? *terminal : boolean(true)},
                               {"probeConstraints", array(std::move(probes))}}));
}

std::set<std::string> row_keys(const Array& rows) {  // repair_state._rows
    std::set<std::string> out;
    for (const auto& r : rows) {
        if (probe_row(r)) continue;
        Object row;
        for (const auto& field : fields(r))
            if (field.first != "name") row.push_back(field);
        out.insert(dumps(object(std::move(row)), true, false));
    }
    return out;
}

bool subset(const std::set<std::string>& a, const std::set<std::string>& b) {
    return std::includes(b.begin(), b.end(), a.begin(), a.end());
}

Array without_anchors(const Array& events, const char* time_key, std::int64_t n, bool inclusive, bool filter) {
    Array out;
    for (const auto& e : events) {
        const Object& event = fields(e);
        if (filter) {
            const std::int64_t t = as_int(at(event, time_key));
            if (!(inclusive ? t <= n : t < n)) continue;
        }
        Object kept;
        for (const auto& field : event)
            if (field.first != "anchors") kept.push_back(field);
        out.push_back(object(std::move(kept)));
    }
    return out;
}

// repair_state.compatible(previous, current).
bool compatible(const Value& previous_value, const Value& current_value) {
    if (!is_object(previous_value)) return false;
    const Object &previous = fields(previous_value), &current = fields(current_value);
    for (const char* key : {"route", "geometry", "scope"})
        if (!py_equal(get_or_null(previous, key), at(current, key))) return false;
    for (const char* key : {"objective", "probeConstraints", "terminalSupportRequired"})
        if (!py_equal(get_or_null(previous, key), get_or_null(current, key))) return false;
    if (!subset(row_keys(listed(previous, "constraints")), row_keys(items(at(current, "constraints"))))) return false;
    const Array old_profiles = listed(previous, "profiles");
    const Array& new_profiles = items(at(current, "profiles"));
    if (py_equal(array(old_profiles), array(new_profiles))) return true;
    if (old_profiles.size() != 1 || new_profiles.size() != 1 ||
        !py_equal(get_or_null(fields(old_profiles[0]), "composition"), text("movement_program")) ||
        !py_equal(get_or_null(fields(new_profiles[0]), "composition"), text("movement_program")))
        return false;
    const Object &old = fields(old_profiles[0]), &now = fields(new_profiles[0]);
    const std::int64_t n = as_int(at(old, "duration"));
    if (n > as_int(at(now, "duration"))) return false;
    for (const char* key : {"position", "velocity", "grounded", "ladderTicks", "jump", "sneak"}) {
        const Array &a = items(at(old, key)), &b = items(at(now, key));
        if (a.size() > b.size() || !py_equal(a, Array(b.begin(), b.begin() + a.size()))) return false;
    }
    const std::tuple<const char*, const char*, bool> events[] = {
        {"contacts", "state", true}, {"contactEvents", "offset", false}, {"caseEvents", "input", false}};
    for (const auto& [key, time_key, inclusive] : events)
        if (!py_equal(without_anchors(listed(old, key), time_key, n, inclusive, false),
                      without_anchors(listed(now, key), time_key, n, inclusive, true)))
            return false;
    return subset(row_keys(listed(old, "positionConstraints")), row_keys(listed(now, "positionConstraints")));
}

// constraints/contacts.contact_conflicts(checks, plan, candidate).
Array contact_conflicts(const Array& checks, const Object& plan, const Object& candidate) {
    Array conflicts;
    const Array& events = items(at(plan, "events"));
    const Array& positions = items(at(candidate, "positions"));
    for (const auto& c : checks) {
        const Object& check = fields(c);
        const std::int64_t tick = as_int(at(fields(events.at(as_int(at(check, "leg")))), "jump")) + as_int(at(check, "offset"));
        const Array& p = items(positions.at(tick));
        const double x = d(p[0]), z = d(p[1]);
        const Array& region = items(at(check, "region"));
        if (d(region[0]) < x && x < d(region[2]) && d(region[1]) < z && z < d(region[3]))
            conflicts.push_back(object(with(with(check, "state", integer_value(tick)), "position", array({p[0], p[1]}))));
    }
    return conflicts;
}

// avoidance.gate_timeline(passages, events) and blockage_usage(passages).
Array gate_timeline(const Array& passages, const Array& events) {
    static const char* kPhases[3] = {"Y", "X", "Z"};
    static const auto order = [](const std::string& axis) { return axis == "Y" ? 0 : axis == "X" ? 1 : axis == "Z" ? 2 : -1; };
    std::vector<Object> timeline;
    for (const auto& p_value : passages) {
        const Object& passage = fields(p_value);
        const Array& faces = items(at(passage, "faces"));
        if (get(passage, "phaseSpans")) {
            const Array& switches = items(at(passage, "switchPhases"));
            for (std::size_t i = 0; i + 1 < faces.size() && i < switches.size(); ++i) {
                const Object &first = fields(faces[i]), &second = fields(faces[i + 1]);
                std::int64_t at_phase = as_int(switches[i]);
                if (const Value* references = get(passage, "phaseReferences")) {
                    const Object& reference = fields(items(*references).at(i));
                    at_phase = 3 * (as_int(at(fields(events.at(as_int(at(reference, "leg")))), "jump")) +
                                    as_int(at(reference, "offset"))) +
                               as_int(at(reference, "phase"));
                }
                const std::int64_t previous = std::max<std::int64_t>(0, at_phase - 1);
                auto axes = [](const Object& face) {
                    const std::string& axis = as_text(at(face, "axis"));
                    return axis == "XZ" ? std::vector<std::string>{"X", "Z"} : std::vector<std::string>{axis};
                };
                std::int64_t first_state = std::numeric_limits<std::int64_t>::min();
                for (const auto& a : axes(first))
                    for (auto s : phase_states(previous / 3, kPhases[previous % 3], a)) first_state = std::max(first_state, s);
                std::int64_t second_state = std::numeric_limits<std::int64_t>::max();
                for (const auto& a : axes(second))
                    for (auto s : phase_states(at_phase / 3, kPhases[at_phase % 3], a)) second_state = std::min(second_state, s);
                const std::string &fa = as_text(at(first, "axis")), &sa = as_text(at(second, "axis"));
                timeline.push_back({{"blockId", at(passage, "blockId")}, {"leg", at(passage, "leg")},
                                    {"firstState", integer_value(first_state)}, {"secondState", integer_value(second_state)},
                                    {"phase", integer_value(at_phase)}, {"type", text(fa + "->" + sa)},
                                    {"kind", text(fa == "Y" || sa == "Y" ? "vertical" : "horizontal")}});
            }
            continue;
        }
        const std::int64_t leg = as_int(at(passage, "leg"));
        if (leg >= static_cast<std::int64_t>(events.size())) continue;
        const Object& event = fields(events[leg]);
        if (is_null(at(event, "landing"))) continue;
        const Array& gates = items(at(passage, "gates"));
        for (std::size_t i = 0; i + 1 < faces.size() && i < gates.size(); ++i) {
            const Object &first = fields(faces[i]), &second = fields(faces[i + 1]);
            const std::string &fa = as_text(at(first, "axis")), &sa = as_text(at(second, "axis"));
            if (order(fa) < 0 || order(sa) < 0) fail("KeyError: '" + (order(fa) < 0 ? fa : sa) + "'");
            const std::int64_t state = as_int(at(event, "jump")) + as_int(gates[i]);
            const Value& block = get_or_null(passage, "blockId");
            const Value block_id = get(passage, "blockId") ? block
                                                           : text("solid:" + (is_null(get_or_null(passage, "solidIndex"))
                                                                                  ? std::string("None")
                                                                                  : std::to_string(as_int(at(passage, "solidIndex")))));
            timeline.push_back({{"blockId", block_id}, {"solidIndex", get_or_null(passage, "solidIndex")}, {"leg", at(passage, "leg")},
                                {"firstState", integer_value(state)},
                                {"secondState", integer_value(state + (order(fa) > order(sa)))},
                                {"type", text(fa + "->" + sa)},
                                {"kind", text(fa == "Y" || sa == "Y" ? "vertical" : "horizontal")}});
        }
    }
    std::stable_sort(timeline.begin(), timeline.end(), [](const Object& a, const Object& b) {
        for (const char* key : {"firstState", "secondState", "blockId", "leg"})
            if (int c = py_compare(at(a, key), at(b, key))) return c < 0;
        return false;
    });
    Array out;
    for (auto& g : timeline) out.push_back(object(std::move(g)));
    return out;
}

Array blockage_usage(const Array& passages) {
    std::vector<std::pair<std::pair<std::string, Value>, std::array<std::int64_t, 3>>> usage;
    for (const auto& p_value : passages) {
        const Object& passage = fields(p_value);
        const bool block = get(passage, "blockId") != nullptr;
        const std::pair<std::string, Value> key{block ? "block" : "solid",
                                                block ? at(passage, "blockId") : get_or_null(passage, "solidIndex")};
        auto found = std::find_if(usage.begin(), usage.end(),
                                  [&](const auto& u) { return u.first.first == key.first && py_equal(u.first.second, key.second); });
        if (found == usage.end()) {
            usage.push_back({key, {0, 0, 0}});
            found = usage.end() - 1;
        }
        const Object counts = fields(blockage_counts(items(at(passage, "faces"))));
        found->second[0] += as_int(at(counts, "horizontal"));
        found->second[1] += as_int(at(counts, "vertical"));
        found->second[2] += as_int(at(counts, "total"));
    }
    Array out;
    for (const auto& [key, counts] : usage) {
        const Value id = key.first == "block" ? key.second
                                              : text("solid:" + (is_null(key.second) ? std::string("None") : std::to_string(as_int(key.second))));
        out.push_back(object({{"blockId", id}, {"horizontal", integer_value(counts[0])},
                              {"vertical", integer_value(counts[1])}, {"total", integer_value(counts[2])}}));
    }
    return out;
}

// physics/vertical.clamp.
Value clamp(const Value& v) { return pkc::value(py_max(flt(-kCap), py_min(flt(kCap), any_num(v)))); }

// validation/scene.check_geometry(geometry, plan, candidate, vertical, compiler, seconds).
Value check_geometry(const Value& geometry_value, const Object& plan, const Object& candidate, const Object& vertical,
                     const SolveHooks& hooks, double seconds) {
    const Object& geometry = fields(geometry_value);
    const std::int64_t ticks = as_int(at(plan, "ticks"));
    const Array &xz = items(at(candidate, "positions")), &y = items(at(vertical, "y")), &vy = items(at(vertical, "vy"));
    Array positions;
    for (std::size_t t = 0; t < std::min(xz.size(), y.size()); ++t)
        positions.push_back(array({items(xz[t])[0], y[t], items(xz[t])[1]}));
    const Array ladder = get(plan, "ladderTicks") ? items(at(plan, "ladderTicks")) : Array(ticks + 1, boolean(false));
    const Value* raw = get(candidate, "rawMovement");
    const Object& config = fields(at(vertical, "vertical"));
    Array movement;
    for (std::int64_t t = 0; t < ticks; ++t) {
        Value x, z;
        if (raw && !is_null(*raw)) {
            x = items(items(*raw)[t])[0];
            z = items(items(*raw)[t])[1];
        } else {
            x = value(sub(any_num(items(positions[t + 1])[0]), any_num(items(positions[t])[0])));
            z = value(sub(any_num(items(positions[t + 1])[2]), any_num(items(positions[t])[2])));
        }
        const bool on_ladder = py_truthy(ladder.at(t));
        movement.push_back(array({on_ladder ? clamp(x) : x, vertical_move(config, t, vy.at(t), on_ladder),
                                  on_ladder ? clamp(z) : z}));
    }
    const Value positions_value = array(positions), movement_value = array(movement);
    bool full = py_truthy(get_or_null(plan, "movementComposition")) || py_truthy(get_or_null(geometry, "ladders")) ||
                std::any_of(ladder.begin(), ladder.end(), [](const Value& v) { return py_truthy(v); });
    for (const auto& c : listed(plan, "sceneContacts")) full = full || py_truthy(get_or_null(fields(c), "movementCase"));
    Value checked_value;
    if (full) {
        const Array& velocity = items(at(candidate, "velocity"));
        Array velocities;
        for (std::size_t t = 0; t < std::min(velocity.size(), vy.size()); ++t)
            velocities.push_back(array({items(velocity[t])[0], vy[t], items(velocity[t])[1]}));
        const Value* sneak = get(plan, "sneak");
        const Object contract{{"sneak", sneak ? *sneak : array(Array(ticks, boolean(false)))},
                              {"ladderTicks", array(ladder)},
                              {"velocities", array(std::move(velocities))},
                              {"friction", at(candidate, "friction")}};
        checked_value = hooks.check_scene(geometry_value, positions_value, movement_value, at(vertical, "onGround"), seconds,
                                          array(listed(plan, "sceneContacts")), contract);
    } else if (py_truthy(get_or_null(plan, "sceneContacts"))) {
        checked_value = hooks.check_scene(geometry_value, positions_value, movement_value, at(vertical, "onGround"), seconds,
                                          at(plan, "sceneContacts"), {});
    } else {
        checked_value = hooks.check_scene(geometry_value, positions_value, movement_value, at(vertical, "onGround"), seconds,
                                          Value{nullptr}, {});
    }
    Object checked = fields(checked_value);
    if (!py_truthy(get_or_null(checked, "feasible")) && py_truthy(get_or_null(checked, "clips"))) {
        const Object& clip = fields(items(at(checked, "clips"))[0]);
        const Array& solids = items(at(geometry, "solids"));
        const auto found = std::find_if(solids.begin(), solids.end(), [&](const Value& s) { return py_equal(s, at(clip, "box")); });
        if (found == solids.end()) fail("ValueError: clip box is not in the scene");
        put(checked, "solidIndex", integer_value(found - solids.begin()));
        put(checked, "axis", at(clip, "axis"));
    }
    return object(std::move(checked));
}

Value save_branch(const Value& score, const Array& rows, const Array& passages, const Array& hits, const Value& warm_yaw,
                  const Value& meta) {
    Object out{{"score", score}, {"rows", array(rows)}, {"passages", array(passages)}, {"hits", array(hits)},
               {"warmYaw", warm_yaw}};
    if (!is_null(meta)) out.emplace_back("meta", meta);
    return object(std::move(out));
}

// Python heapq on (score, serial, ...) entries; serials are unique, so the rest never decides.
struct Entry {
    Value score;
    std::int64_t serial;
    Array rows, passages, hits;
    Value warm_yaw, meta;
};

bool entry_less(const Entry& a, const Entry& b) {
    if (int c = py_compare(a.score, b.score)) return c < 0;
    return a.serial < b.serial;
}

void sift_down(std::vector<Entry>& heap, std::size_t start, std::size_t pos) {  // heapq._siftdown
    Entry item = std::move(heap[pos]);
    while (pos > start) {
        const std::size_t parent = (pos - 1) >> 1;
        if (entry_less(item, heap[parent])) {
            heap[pos] = std::move(heap[parent]);
            pos = parent;
            continue;
        }
        break;
    }
    heap[pos] = std::move(item);
}

void sift_up(std::vector<Entry>& heap, std::size_t pos) {  // heapq._siftup
    const std::size_t end = heap.size(), start = pos;
    Entry item = std::move(heap[pos]);
    std::size_t child = 2 * pos + 1;
    while (child < end) {
        const std::size_t right = child + 1;
        if (right < end && !entry_less(heap[child], heap[right])) child = right;
        heap[pos] = std::move(heap[child]);
        pos = child;
        child = 2 * pos + 1;
    }
    heap[pos] = std::move(item);
    sift_down(heap, start, pos);
}

void heap_push(std::vector<Entry>& heap, Entry entry) {
    heap.push_back(std::move(entry));
    sift_down(heap, 0, heap.size() - 1);
}

Entry heap_pop(std::vector<Entry>& heap) {
    Entry last = std::move(heap.back());
    heap.pop_back();
    if (heap.empty()) return last;
    Entry top = std::move(heap[0]);
    heap[0] = std::move(last);
    sift_up(heap, 0);
    return top;
}

void heapify(std::vector<Entry>& heap) {
    for (std::size_t i = heap.size() / 2; i-- > 0;) sift_up(heap, i);
}

struct Member {
    Value switches;
    std::string status = "held";
    Value rank{nullptr}, objective{nullptr};
    Array held;
    std::int64_t pending = 0;
    std::vector<std::string> keys;
};

struct Family {
    std::vector<std::pair<std::string, std::unique_ptr<Member>>> points;
    std::vector<std::string> order;
    Value source{nullptr};
    bool done = false;

    Member* point(const std::string& name) {
        for (auto& [k, m] : points)
            if (k == name) return m.get();
        return nullptr;
    }
};

Value member_value(const Member& m) {
    Array keys;
    for (const auto& k : m.keys) keys.push_back(text(k));
    return object({{"switches", m.switches}, {"status", text(m.status)}, {"rank", m.rank}, {"objective", m.objective},
                   {"held", array(m.held)}, {"pending", integer_value(m.pending)}, {"keys", array(std::move(keys))}});
}

Value family_value(const Family& f) {
    Object points;
    for (const auto& [k, m] : f.points) points.emplace_back(k, member_value(*m));
    Array order;
    for (const auto& k : f.order) order.push_back(text(k));
    return object({{"points", object(std::move(points))}, {"order", array(std::move(order))}, {"source", f.source},
                   {"done", boolean(f.done)}});
}

std::unique_ptr<Family> family_of(const Value& value) {
    const Object& f = fields(value);
    auto family = std::make_unique<Family>();
    for (const auto& [k, m_value] : fields(at(f, "points"))) {
        const Object& m = fields(m_value);
        auto member = std::make_unique<Member>();
        member->switches = at(m, "switches");
        member->status = as_text(at(m, "status"));
        member->rank = at(m, "rank");
        member->objective = at(m, "objective");
        member->held = items(at(m, "held"));
        member->pending = as_int(at(m, "pending"));
        for (const auto& key : items(at(m, "keys"))) member->keys.push_back(as_text(key));
        family->points.emplace_back(k, std::move(member));
    }
    for (const auto& k : items(at(f, "order"))) family->order.push_back(as_text(k));
    family->source = at(f, "source");
    family->done = py_truthy(at(f, "done"));
    return family;
}

std::string switches_name(const Value& switches) { return dumps(switches, false, false); }

// search/repair.usable(accepted).
std::vector<Value> usable(const std::vector<Object>& accepted) {
    std::vector<Value> out;
    for (const auto& v : accepted) {
        const Value& confirmation = get_or_null(v, "confirmation");
        if (py_equal(confirmation, text("rejected"))) continue;
        out.push_back(is_object(confirmation) ? object(with(v, "best", confirmation)) : object(v));
    }
    return out;
}

// search/repair.alarmed(result).
Value alarmed(const Object& result) {
    const Object& best = fields(at(result, "best"));
    if (!py_equal(get_or_null(fields(at(best, "candidate")), "model"), text("relaxed_disk"))) return array({});
    const Array attempts = listed(result, "attempts");
    for (auto a = attempts.rbegin(); a != attempts.rend(); ++a)
        for (const auto& d_value : listed(fields(*a), "diagnostics")) {
            const Object& diagnostic = fields(d_value);
            if (py_equal(get_or_null(diagnostic, "problemHash"), get_or_null(best, "problemHash")) &&
                py_equal(get_or_null(diagnostic, "status"), text("feasible"))) {
                const Value& disk = get_or_null(diagnostic, "disk");
                return py_truthy(disk) ? array(listed(fields(disk), "alarmTicks")) : array({});
            }
        }
    return array({});
}

class RepairLoop {
public:
    RepairLoop(const Value& route_value, const Value& profiles, const Value& checks, const SolveHooks& hooks,
               const RepairOptions& options, ConstraintVariants* initial)
        : route_(fields(route_value)), profiles_(profiles), checks_(items(checks)), hooks_(hooks), o_(options),
          source_(initial) {}

    Value run();

private:
    // ---- closures of solve_contact_branches
    Member* point(const Value& meta);
    void offer(const Value& score, const Array& rows, const Array& passages, const Array& hits, const Value& warm_yaw,
               const Value* selected, const Value& meta);
    bool exhausted(const Family& family) const;
    void visit(Member& member);
    void advance(const std::string& name);
    void advance_all();
    void arrive(const Value& score, const Array& rows, const Array& passages, const Array& hits, const Value& warm_yaw,
                const Value& selected, const Value& meta, const Value& source);
    void settle(const Object& done);
    Value fallbacks() const;
    Value snapshot();
    void publish(const Value* state = nullptr);
    Value finish(Object result);
    Value build_branch(const Array& rows, const Array& passages, const Array& hits, const Value& warm_yaw) const;
    Object audit_point(const Object& best, const Value& branch, const Array& passages);
    Value enrich_point(const Object& best, const Object& checked, const Value& branch, const Array& rows,
                       const Array& passages, const Array& hits, const Value& meta);
    std::vector<std::size_t> walk_order() const;
    Value examine(std::size_t index);
    void walk();
    void grow(const Object& collision);
    Object best_result_fields() const;  // best_result with its aliased attempt lists materialized
    // best_result.update(**fields, attempts=inner_attempts, contactAttempts=attempts), then finish(best_result).
    Value finish_best(const Object& updates);
    std::unique_ptr<ConstraintVariants> for_plan(const Object& record, double seconds);
    ConstraintVariants* source() const { return owned_ ? owned_.get() : source_; }

    Object route_;
    Value profiles_;
    Array checks_;
    const SolveHooks& hooks_;
    const RepairOptions& o_;
    ConstraintVariants* source_;
    std::unique_ptr<ConstraintVariants> owned_;

    double deadline_ = 0;
    std::vector<Entry> queue_;
    std::set<std::string> seen_;
    std::int64_t serial_ = 0;
    Array unresolved_;
    bool source_exhausted_ = true;
    struct Source {
        Object record;
        std::unique_ptr<ConstraintVariants> instance;
    };
    std::vector<Source> repair_sources_;
    std::vector<std::pair<std::string, std::int64_t>> completed_source_metrics_;
    std::vector<std::pair<std::string, Value>> choices_;
    std::vector<std::pair<std::string, Array>> choice_alternatives_;
    bool climbing_ = false;
    std::vector<std::pair<std::string, std::unique_ptr<Family>>> families_;
    std::optional<Object> settling_;
    Value current_context_;
    bool resuming_ = false, exact_resume_ = false;
    std::int64_t resumed_branches_ = 0;
    std::set<std::string> generated_;
    Value reference_yaw_{nullptr};
    Array attempts_, inner_attempts_;
    std::optional<Object> best_result_;
    bool best_aliased_ = false;  // best_result's attempts and contactAttempts are the live lists
    Value best_branch_{nullptr};
    Array completed_;
    std::vector<Object> accepted_variants_;
    Value in_flight_{nullptr}, walking_{nullptr};
    Array resume_attempt_log_, resume_contact_log_;

    Family* family(const std::string& name) {
        for (auto& [k, f] : families_)
            if (k == name) return f.get();
        return nullptr;
    }
    const Value& objective() const { return at(route_, "objective"); }
    void metrics_update(std::vector<std::pair<std::string, std::int64_t>>& into, const Value& metrics) const {
        if (!is_object(metrics)) return;
        for (const auto& field : fields(metrics)) {
            const std::string& k = field.first;
            const Value& v = field.second;
            auto found = std::find_if(into.begin(), into.end(), [&](const auto& e) { return e.first == k; });
            if (found == into.end())
                into.emplace_back(k, as_int(v));
            else
                found->second += as_int(v);
        }
    }
};

Member* RepairLoop::point(const Value& meta) {
    if (!climbing_ || is_null(meta)) return nullptr;
    Family* f = family(as_text(at(fields(meta), "family")));
    return f ? f->point(switches_name(at(fields(meta), "switches"))) : nullptr;
}

void RepairLoop::offer(const Value& score, const Array& rows, const Array& passages, const Array& hits,
                       const Value& warm_yaw, const Value* selected, const Value& meta) {
    const std::string key = branch_key(rows, passages, hits);
    if (selected) {
        if (std::none_of(choices_.begin(), choices_.end(), [&](const auto& c) { return c.first == key; }))
            choices_.emplace_back(key, *selected);
        auto found = std::find_if(choice_alternatives_.begin(), choice_alternatives_.end(),
                                  [&](const auto& c) { return c.first == key; });
        if (found == choice_alternatives_.end()) {
            choice_alternatives_.emplace_back(key, Array{});
            found = choice_alternatives_.end() - 1;
        }
        if (!contains(found->second, *selected)) found->second.push_back(*selected);
    }
    if (seen_.count(key)) return;
    seen_.insert(key);
    ++serial_;
    heap_push(queue_, Entry{score, serial_, rows, passages, hits, warm_yaw, meta});
    if (Member* member = point(meta)) {
        member->status = "visited";
        ++member->pending;
    }
}

bool RepairLoop::exhausted(const Family& f) const {
    if (is_null(f.source)) return source_exhausted_;
    return std::none_of(repair_sources_.begin(), repair_sources_.end(), [&](const Source& s) {
        return branch_key(items(at(s.record, "rows")), items(at(s.record, "passages")), items(at(s.record, "hits"))) ==
               as_text(f.source);
    });
}

void RepairLoop::visit(Member& member) {
    const Array held = std::move(member.held);
    member.held = Array{};
    member.status = "visited";
    for (const auto& b_value : held) {
        const Object& b = fields(b_value);
        const Value& selected = at(b, "selected");
        offer(at(b, "score"), items(at(b, "rows")), items(at(b, "passages")), items(at(b, "hits")), at(b, "warmYaw"),
              &selected, at(b, "meta"));
    }
    if (!member.pending) member.status = "settled";
}

void RepairLoop::advance(const std::string& name) {
    // The objective is unimodal in each switch tick: climb from the best point to its held +-1 neighbours.
    Family& f = *family(name);
    while (true) {
        f.done = false;
        if (std::any_of(f.points.begin(), f.points.end(), [](const auto& p) { return p.second->pending != 0; })) return;
        Member* best = nullptr;
        for (auto& [_, m] : f.points)
            if (!is_null(m->rank) && (!best || rank_less(m->rank, best->rank))) best = m.get();
        if (!best) {
            Member* target = nullptr;
            for (const auto& k : f.order)
                if (Member* m = f.point(k); m && m->status == "held") {
                    target = m;
                    break;
                }
            if (!target) {
                f.done = exhausted(f);
                return;
            }
            visit(*target);
            continue;
        }
        std::vector<std::string> neighbours;
        const Array& switches = items(best->switches);
        for (std::size_t i = 0; i < switches.size(); ++i)
            for (std::int64_t step : {-1, 1}) {
                Array moved = switches;
                moved[i] = integer_value(as_int(switches[i]) + step);
                neighbours.push_back(switches_name(array(std::move(moved))));
            }
        std::vector<Member*> held;
        for (const auto& k : neighbours)
            if (Member* m = f.point(k); m && m->status == "held") held.push_back(m);
        if (held.empty()) {
            f.done = std::all_of(neighbours.begin(), neighbours.end(), [&](const std::string& k) { return f.point(k) != nullptr; }) ||
                     exhausted(f);
            return;
        }
        for (Member* m : held) visit(*m);
    }
}

void RepairLoop::advance_all() {
    std::vector<std::string> names;
    for (const auto& [k, _] : families_) names.push_back(k);
    for (const auto& name : names) advance(name);
}

void RepairLoop::arrive(const Value& score, const Array& rows, const Array& passages, const Array& hits,
                        const Value& warm_yaw, const Value& selected, const Value& meta, const Value& source) {
    if (!climbing_ || is_null(meta)) {
        offer(score, rows, passages, hits, warm_yaw, &selected, meta);
        return;
    }
    const std::string family_name = as_text(at(fields(meta), "family"));
    Family* f = family(family_name);
    if (!f) {
        families_.emplace_back(family_name, std::make_unique<Family>());
        f = families_.back().second.get();
        f->source = source;
    }
    const std::string name = switches_name(at(fields(meta), "switches"));
    Member* member = f->point(name);
    if (!member) {
        auto created = std::make_unique<Member>();
        created->switches = at(fields(meta), "switches");
        member = created.get();
        f->points.emplace_back(name, std::move(created));
        f->order.push_back(name);
    }
    member->keys.push_back(branch_key(rows, passages, hits));
    const bool first = f->points.size() == 1 && member->held.empty();
    if (first || member->status != "held") {
        offer(score, rows, passages, hits, warm_yaw, &selected, meta);
        return;
    }
    member->held.push_back(object(with(fields(save_branch(score, rows, passages, hits, warm_yaw, meta)), "selected", selected)));
    advance(family_name);
}

void RepairLoop::settle(const Object& done) {
    Member* member = point(at(done, "meta"));
    if (!member) return;
    --member->pending;
    const Value& rank = at(done, "rank");
    if (!is_null(rank) && (is_null(member->rank) || rank_less(rank, member->rank))) {
        member->rank = rank;
        member->objective = at(done, "objective");
    }
    if (!member->pending) {
        member->status = "settled";
        advance(as_text(at(fields(at(done, "meta")), "family")));
    }
}

Object RepairLoop::best_result_fields() const {
    Object out = *best_result_;
    if (best_aliased_) {
        put(out, "attempts", array(inner_attempts_));
        put(out, "contactAttempts", array(attempts_));
    }
    return out;
}

Value RepairLoop::finish_best(const Object& updates) {
    for (const auto& [k, v] : updates) put(*best_result_, k, v);
    best_aliased_ = true;
    return finish(best_result_fields());
}

Value RepairLoop::fallbacks() const {
    if (!best_result_ || is_null(best_branch_)) return array({});
    const Value top = candidate_rank(at(*best_result_, "best"), objective());
    const Object& chosen_branch = fields(best_branch_);
    const std::string chosen = branch_key(items(at(chosen_branch, "rows")), items(at(chosen_branch, "passages")),
                                          items(at(chosen_branch, "hits")));
    std::vector<std::pair<Value, Value>> close;
    for (const auto& v : usable(accepted_variants_))
        if (as_text(at(fields(v), "branch")) != chosen) close.emplace_back(candidate_rank(at(fields(v), "best"), objective()), v);
    std::stable_sort(close.begin(), close.end(), [](const auto& a, const auto& b) { return rank_less(a.first, b.first); });
    Array out;
    for (const auto& [rank, v] : close) {
        if (out.size() >= kFallbackLimit) break;
        if (py_equal(items(rank)[0], items(top)[0]) && d(items(rank)[1]) - d(items(top)[1]) <= kFallbackGap) out.push_back(v);
    }
    return array(std::move(out));
}

Value RepairLoop::snapshot() {
    Value saved_best{nullptr};
    if (best_result_) {
        Object kept;
        for (const auto& field : *best_result_)
            if (field.first != "repairState" && field.first != "repairResume" && field.first != "attempts" &&
                field.first != "contactAttempts")
                kept.push_back(field);
        saved_best = object(std::move(kept));
    }
    Array sources;
    for (const auto& s : repair_sources_) {
        Object record = s.record;
        if (s.instance) put(record, "state", s.instance->state());
        sources.push_back(object(std::move(record)));
    }
    Object metrics;
    for (const auto& [k, v] : completed_source_metrics_) metrics.emplace_back(k, integer_value(v));
    std::vector<const Entry*> ordered;
    for (const auto& e : queue_) ordered.push_back(&e);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Entry* a, const Entry* b) { return entry_less(*a, *b); });
    Array pending;
    for (const Entry* e : ordered) pending.push_back(save_branch(e->score, e->rows, e->passages, e->hits, e->warm_yaw, e->meta));
    Array seen, accepted, choices_out, families;
    for (const auto& k : seen_) seen.push_back(text(k));
    for (const auto& v : accepted_variants_) accepted.push_back(object(v));
    Object fams, choice_map, alternative_map;
    for (const auto& [k, f] : families_) fams.emplace_back(k, family_value(*f));
    for (const auto& [k, v] : choices_) choice_map.emplace_back(k, v);
    for (const auto& [k, v] : choice_alternatives_) alternative_map.emplace_back(k, array(v));
    Object out{{"version", integer_value(3)},
               {"context", current_context_},
               {"repairSources", array(std::move(sources))},
               {"sourcesCompletedMetrics", object(std::move(metrics))},
               {"pending", array(std::move(pending))},
               {"unresolved", array(unresolved_)},
               {"seen", array(std::move(seen))},
               {"incumbent", best_branch_},
               {"acceptedVariants", array(std::move(accepted))},
               {"families", object(std::move(fams))}};
    if (o_.rank_equal_cost) out.emplace_back("fallbackVariants", fallbacks());
    ConstraintVariants* generator = source();
    out.emplace_back("acceptedResult", saved_best);
    out.emplace_back("inFlight", in_flight_);
    out.emplace_back("completed", array(completed_));
    out.emplace_back("referenceWarmYaw", reference_yaw_);
    out.emplace_back("interactionChoices", object(std::move(choice_map)));
    out.emplace_back("interactionAlternatives", object(std::move(alternative_map)));
    out.emplace_back("constraintGeneration", generator ? generator->state() : Value{nullptr});
    if (generator && generator->conflict_driven()) {
        Array keys;
        for (const auto& k : generated_) keys.push_back(text(k));
        out.emplace_back("generatedBranches", array(std::move(keys)));
    }
    out.emplace_back("attemptLog", array(joined(resume_attempt_log_, inner_attempts_)));
    out.emplace_back("contactLog", array(joined(resume_contact_log_, attempts_)));
    return object(std::move(out));
}

void RepairLoop::publish(const Value* state) {
    if (!o_.on_progress) return;
    if (state)
        o_.on_progress(*state);
    else
        o_.on_progress(snapshot());
}

Value RepairLoop::finish(Object result) {
    // An unfinished walk's next candidate stays in flight: the check is unfinished and a resume re-walks.
    in_flight_ = walking_;
    const Value state = snapshot();
    put(result, "repairState", state);
    std::vector<std::pair<std::string, std::int64_t>> metrics = completed_source_metrics_;
    if (ConstraintVariants* generator = source()) metrics_update(metrics, get_or_null(fields(generator->state()), "metrics"));
    for (const auto& s : repair_sources_) {
        const Value record_state = s.instance ? s.instance->state() : get_or_null(s.record, "state");
        if (is_object(record_state)) metrics_update(metrics, get_or_null(fields(record_state), "metrics"));
    }
    std::int64_t held = 0;
    for (const auto& [_, f] : families_)
        for (const auto& [__, m] : f->points) held += static_cast<std::int64_t>(m->held.size());
    Object avoidance;
    for (const auto& [k, v] : metrics) avoidance.emplace_back(k, integer_value(v));
    put(avoidance, "generationIncomplete", boolean(!repair_sources_.empty() || !source_exhausted_));
    put(avoidance, "heldUnsolved", integer_value(held));
    put(result, "avoidanceMetrics", object(std::move(avoidance)));
    put(result, "repairResume", object({{"requested", boolean(!is_null(o_.resume_state))}, {"compatible", boolean(resuming_)},
                                        {"exact", boolean(exact_resume_)}, {"resumedBranches", integer_value(resumed_branches_)}}));
    publish(&state);
    return object(std::move(result));
}

Value RepairLoop::build_branch(const Array& rows, const Array& passages, const Array& hits, const Value& warm_yaw) const {
    Object branch = route_;
    put(branch, "constraints", array(joined(items(at(branch, "constraints")), rows)));
    put(branch, "passages", array(passages));
    put(branch, "hitEvents", array(hits));
    if (!is_null(warm_yaw)) put(branch, "warmYaw", warm_yaw);
    return object(std::move(branch));
}

Object RepairLoop::audit_point(const Object& best, const Value& branch, const Array& passages) {
    const Object& plan = fields(at(best, "plan"));
    const Object& candidate = fields(at(best, "candidate"));
    const Array conflicts = contact_conflicts(checks_, plan, candidate);
    Object checked{{"conflicts", array(conflicts)}, {"accepted", boolean(false)}};
    if (!conflicts.empty()) return checked;
    const Value vertical = validate_vertical_schedule(branch, profiles_, at(best, "plan"));
    put(checked, "vertical", vertical);
    if (!py_truthy(at(fields(vertical), "feasible"))) {
        put(checked, "reason", text("vertical_schedule_mismatch"));
        return checked;
    }
    if (!passages.empty()) throw Unsupported("passage Y checks run in Python");
    const Value verdict = o_.candidate_filter ? o_.candidate_filter(at(best, "plan"), at(best, "candidate"), vertical)
                                              : object({{"accepted", boolean(true)}});
    put(checked, "verdict", verdict);
    const Object& v = fields(verdict);
    if (!py_truthy(at(v, "accepted"))) {
        const Value* reason = get(v, "reason");
        put(checked, "reason", reason ? *reason : text("candidate_filter"));
        return checked;
    }
    if (is_null(o_.geometry)) {
        put(checked, "accepted", boolean(true));
        return checked;
    }
    const double remaining = deadline_ - monotonic();
    if (remaining <= 0) throw Timeout("Geometry validation exceeded the contact budget");
    Value geometry_check;
    if (is_null(o_.validation_scope)) {
        geometry_check = check_geometry(o_.geometry, plan, candidate, fields(vertical), hooks_, remaining);
    } else {
        const Object& scope = fields(o_.validation_scope);
        const Array& indices = items(at(scope, "solidIndices"));
        Object selected = fields(o_.geometry);
        Array solids;
        for (const auto& i : indices) solids.push_back(items(at(fields(o_.geometry), "solids")).at(as_int(i)));
        put(selected, "solids", array(std::move(solids)));
        const auto [p, c, vv] = validation_prefix(at(best, "plan"), at(best, "candidate"), vertical, as_int(at(scope, "through")));
        Object g = fields(check_geometry(object(std::move(selected)), fields(p), fields(c), fields(vv), hooks_, remaining));
        if (const Value* index = get(g, "solidIndex")) put(g, "solidIndex", indices.at(as_int(*index)));
        geometry_check = object(std::move(g));
    }
    put(checked, "geometryCheck", geometry_check);
    put(checked, "accepted", boolean(py_truthy(at(fields(geometry_check), "feasible"))));
    return checked;
}

Value RepairLoop::enrich_point(const Object& best_in, const Object& checked, const Value& branch, const Array& rows,
                               const Array& passages, const Array& hits, const Value& meta) {
    Object best = best_in;
    const std::string key = branch_key(rows, passages, hits);
    const Value* chosen = nullptr;
    for (const auto& [k, v] : choices_)
        if (k == key) chosen = &v;
    Array described = passages;
    if (chosen)
        for (const auto& p : items(*chosen))
            if (!contains(passages, p)) described.push_back(p);
    put(best, "alternativeRows", array(rows));
    if (!is_null(meta) && get(fields(meta), "branchChoices")) {
        put(best, "alternativeBranchChoices", at(fields(meta), "branchChoices"));
        put(best, "alternativeBranchKey", at(fields(meta), "branchKey"));
    }
    const Value* own_route = get(best, "route");
    put(best, "route", own_route ? *own_route : branch);
    put(best, "profiles", profiles_);
    put(best, "contactChecks", array(checks_));
    put(best, "passages", array(described));
    put(best, "hitEvents", array(hits));
    put(best, "ceilingEvents", array(listed(fields(branch), "ceilingEvents")));
    put(best, "blockageTimeline", array(gate_timeline(described, items(at(fields(at(best, "plan")), "events")))));
    put(best, "blockageUsage", array(blockage_usage(described)));
    const Object& vertical = fields(at(checked, "vertical"));
    put(best, "vertical", at(vertical, "vertical"));
    const Value selected = chosen ? *chosen : array(passages);
    put(best, "selectedInteractions", selected);
    Value alternatives = array({selected});
    for (const auto& [k, v] : choice_alternatives_)
        if (k == key) alternatives = array(v);
    put(best, "selectedInteractionAlternatives", alternatives);
    Object candidate = fields(at(best, "candidate"));
    for (const char* k : {"y", "vy", "onGround"}) put(candidate, k, at(vertical, k));
    put(best, "candidate", object(std::move(candidate)));
    if (o_.candidate_filter) put(best, "corridorCheck", at(checked, "verdict"));
    if (const Value* g = get(checked, "geometryCheck")) put(best, "geometryCheck", *g);
    return object(std::move(best));
}

std::vector<std::size_t> RepairLoop::walk_order() const {
    // Each family's best relaxed candidate and a close second (same ticks, within the fallback gap), best first.
    std::vector<std::pair<Value, std::vector<std::pair<Value, std::size_t>>>> groups;
    for (std::size_t i = 0; i < accepted_variants_.size(); ++i) {
        const Object& v = accepted_variants_[i];
        const Value& family = get_or_null(v, "family");
        const Value key = py_truthy(family) ? family : at(v, "branch");
        auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return py_equal(g.first, key); });
        if (found == groups.end()) {
            groups.push_back({key, {}});
            found = groups.end() - 1;
        }
        found->second.emplace_back(candidate_rank(at(v, "best"), objective()), i);
    }
    std::vector<std::pair<Value, std::size_t>> kept;
    for (auto& [_, members] : groups) {
        std::stable_sort(members.begin(), members.end(), [](const auto& a, const auto& b) { return rank_less(a.first, b.first); });
        const Value& top = members[0].first;
        kept.push_back(members[0]);
        if (members.size() > 1 && py_equal(items(members[1].first)[0], items(top)[0]) &&
            d(items(members[1].first)[1]) - d(items(top)[1]) <= kFallbackGap)
            kept.push_back(members[1]);
    }
    std::stable_sort(kept.begin(), kept.end(), [](const auto& a, const auto& b) { return rank_less(a.first, b.first); });
    std::vector<std::size_t> out;
    for (const auto& [_, i] : kept) out.push_back(i);
    return out;
}

Value RepairLoop::examine(std::size_t index) {
    // One IPOPT solve of an alarmed candidate's own branch, whose point must pass the same audit: the confirmed
    // point, 'rejected', or null when there was no budget to run it.
    const Object saved = fields(at(accepted_variants_[index], "state"));
    if (deadline_ - monotonic() <= 0 || hooks_.is_cancelled()) return Value{nullptr};
    if (hooks_.work_check) {
        try {
            hooks_.work_check();
        } catch (const Timeout&) {
            return Value{nullptr};
        }
    }
    const Array rows = items(at(saved, "rows")), passages = items(at(saved, "passages")), hits = items(at(saved, "hits"));
    const Value branch = build_branch(rows, passages, hits, get_or_null(saved, "warmYaw"));
    RouteSearch search;
    search.seconds = deadline_ - monotonic();
    search.candidate_seconds = o_.candidate_seconds;
    search.retries = o_.retries;
    search.improve = o_.improve;
    search.rank_equal_cost = o_.rank_equal_cost;
    search.optimize_goal = o_.optimize_goal;
    search.stop_on_feasible = o_.stop_on_feasible;
    search.numerical = o_.confirm;
    const Object result = fields(find_route(branch, hooks_, search));
    for (const auto& a : items(at(result, "attempts"))) inner_attempts_.push_back(a);
    const std::string& status = as_text(at(result, "status"));
    if (status == "cancelled" || status == "error") return Value{nullptr};
    if (is_null(at(result, "best"))) {
        const Value& branch_key_value = at(accepted_variants_[index], "branch");
        for (auto& c : completed_)
            if (py_equal(at(fields(c), "branch"), branch_key_value)) {
                c = object(with(fields(c), "oracleRejected", boolean(true)));
                break;
            }
        return text("rejected");
    }
    Object checked;
    try {
        checked = audit_point(fields(at(result, "best")), branch, passages);
    } catch (const Unsupported&) {
        throw;
    } catch (const std::invalid_argument&) {
        return Value{nullptr};
    } catch (const std::runtime_error&) {  // Timeout included
        return Value{nullptr};
    }
    if (!py_truthy(at(checked, "accepted"))) return text("rejected");
    return enrich_point(fields(at(result, "best")), checked, branch, rows, passages, hits, get_or_null(saved, "meta"));
}

void RepairLoop::walk() {
    // Confirm the kept candidates once, in relaxed rank order: an unalarmed point is accepted as is, an alarmed one by
    // its IPOPT re-solve. Stops when the best confirmed point ranks at least as well as the next relaxed candidate, or
    // after the walk limit once one is confirmed; with none confirmed it keeps walking.
    std::optional<std::pair<Value, std::size_t>> incumbent;
    std::int64_t examined = 0;
    for (std::size_t index : walk_order()) {
        Object& variant = accepted_variants_[index];
        if (incumbent && (examined >= kWalkLimit ||
                          !rank_less(candidate_rank(at(variant, "best"), objective()), candidate_rank(incumbent->first, objective()))))
            break;
        Value point = at(variant, "best");
        if (py_truthy(at(variant, "alarmTicks"))) {
            if (is_null(get_or_null(variant, "confirmation"))) {
                const Value outcome = examine(index);
                if (is_null(outcome)) {
                    walking_ = at(accepted_variants_[index], "state");
                    break;
                }
                put(accepted_variants_[index], "confirmation", outcome);
            }
            point = at(accepted_variants_[index], "confirmation");
        }
        ++examined;
        if (!py_equal(point, text("rejected")) &&
            (!incumbent || rank_less(candidate_rank(point, objective()), candidate_rank(incumbent->first, objective()))))
            incumbent.emplace(point, index);
    }
    if (!incumbent) {
        best_result_.reset();
        best_branch_ = Value{nullptr};
        return;
    }
    const Object& saved = fields(at(accepted_variants_[incumbent->second], "state"));
    best_branch_ = save_branch(at(saved, "score"), items(at(saved, "rows")), items(at(saved, "passages")),
                               items(at(saved, "hits")), at(fields(at(fields(incumbent->first), "candidate")), "yaw"),
                               get_or_null(saved, "meta"));
    best_result_ = Object{{"status", text("feasible")}, {"best", incumbent->first},
                          {"termination", text("validated_incumbent")}, {"minimumProven", boolean(false)}};
    best_aliased_ = false;
}

void RepairLoop::grow(const Object& collision) {
    // Conflict-driven avoidance: a solve hit an obstacle, which the generator enumerates from now on. It restarts
    // over the larger obstacle set; the old generator's unsolved branches lack the new obstacle's rows and are
    // dropped (the new one emits them refined); accepted points stay. A hit on an enumerated obstacle or a settled
    // stretch has no repair.
    ConstraintVariants* generator = source();
    const auto key = generator->obstacle_key(as_int(at(collision, "solidIndex")), as_int(at(collision, "tick")));
    auto set_termination = [&](const char* termination) {
        attempts_.back() = object(with(fields(attempts_.back()), "termination", text(termination)));
    };
    const Obstacles& enumerated = *generator->obstacles();
    if (!key || std::find(enumerated.begin(), enumerated.end(), *key) != enumerated.end()) {
        set_termination(key ? "enumerated_obstacle_hit" : "settled_obstacle_hit");
        return;
    }
    set_termination("obstacle_added");
    Obstacles grown = enumerated;
    grown.push_back(*key);
    owned_ = generator->with_obstacles(std::move(grown));
    source_exhausted_ = false;
    std::vector<Entry> kept;
    for (auto& entry : queue_) {
        const std::string k = branch_key(entry.rows, entry.passages, entry.hits);
        if (generated_.count(k))
            seen_.erase(k);
        else
            kept.push_back(std::move(entry));
    }
    queue_ = std::move(kept);
    heapify(queue_);
    generated_.clear();
    families_.clear();
}

std::unique_ptr<ConstraintVariants> RepairLoop::for_plan(const Object& record, double seconds) {
    // ConstraintVariants.for_plan: the same builder for a fixed-allocation collision repair, its inputs rebuilt from
    // the record (completed numerical requests and the generation cursor are not).
    const Value& route_value = at(record, "route");
    const Object& route = fields(route_value);
    const Object& plan = fields(at(record, "plan"));
    const Object& vertical = fields(at(record, "vertical"));
    const Value coefficient = hooks_.compile(route_value, at(record, "plan"), seconds);
    const std::int64_t ticks = as_int(at(plan, "ticks"));
    auto every_other = [](const Value& list) {
        const Array& a = items(list);
        Array out;
        for (std::size_t i = 0; i < a.size(); i += 2) out.push_back(a[i]);
        return array(std::move(out));
    };
    const Object& initial = fields(at(route, "initial"));
    Object model = fields(coefficient);
    put(model, "ticks", at(plan, "ticks"));
    put(model, "walls", at(plan, "walls"));
    put(model, "contacts", at(plan, "contacts"));
    put(model, "position", every_other(at(initial, "position")));
    put(model, "velocity", every_other(at(initial, "velocity")));
    const Value* threshold = get(route, "physicsThreshold");
    put(model, "threshold", threshold ? *threshold : real(.005));
    put(model, "vertical", at(vertical, "vertical"));
    put(model, "ladderTicks", get(plan, "ladderTicks") ? at(plan, "ladderTicks") : array(Array(ticks + 1, boolean(false))));
    Array contacts;
    static const char* kAxes[] = {"X", "Y", "Z"};
    for (const auto& c_value : listed(plan, "sceneContacts")) {
        const Object& c = fields(c_value);
        const std::int64_t axis = as_int(at(c, "axis"));
        Object contact = with(c, "axis", text(kAxes[axis]));
        put(contact, "offset", integer_value(as_int(at(c, "state")) - 1));
        put(contact, "leg", integer_value(0));
        put(contact, "role", text(axis == 1 && d(at(c, "direction")) == -1 ? "landing" : "contact"));
        contacts.push_back(object(std::move(contact)));
    }
    Array cases;
    const Array& profiles = items(profiles_);
    const Array& events = items(at(plan, "events"));
    for (std::size_t i = 0; i < std::min(profiles.size(), events.size()); ++i)
        for (const auto& c : listed(fields(profiles[i]), "caseEvents"))
            cases.push_back(object(with(fields(c), "input", integer_value(as_int(at(fields(c), "input")) +
                                                                          as_int(at(fields(events[i]), "jump"))))));
    VariantInputs inputs;
    inputs.profile = object({{"position", at(vertical, "y")}, {"grounded", at(vertical, "onGround")},
                             {"contactEvents", array(contacts)}, {"caseEvents", array(std::move(cases))}});
    Object absolute = route;
    put(absolute, "contactEvents", array(std::move(contacts)));
    put(absolute, "hitEvents", array({}));
    put(absolute, "ladderEvents", array({}));
    inputs.route = object(std::move(absolute));
    inputs.geometry = o_.geometry;
    const Value model_value = object(std::move(model));
    Object envelopes = fields(reachable_envelopes(model_value, at(vertical, "y"), std::nullopt));
    envelopes.emplace_back("model", model_value);
    inputs.envelopes = object(std::move(envelopes));
    if (py_truthy(o_.validation_scope)) {
        inputs.scope = o_.validation_scope;
    } else {
        Array all;
        for (std::size_t i = 0; i < items(at(fields(o_.geometry), "solids")).size(); ++i)
            all.push_back(integer_value(static_cast<std::int64_t>(i)));
        inputs.scope = object({{"through", at(plan, "ticks")}, {"solidIndices", array(std::move(all))}});
    }
    inputs.reference = at(fields(at(record, "candidate")), "positions");
    inputs.owners = o_.solid_blocks;
    const Value* guide = get(route, "guideObjective");
    inputs.objective = guide ? *guide : at(route, "objective");
    const double deadline = deadline_;
    const SolveHooks& hooks = hooks_;
    inputs.check = [&hooks, deadline] {
        if (hooks.is_cancelled()) throw Timeout("cancelled");
        if (hooks.work_check) hooks.work_check();
        if (monotonic() >= deadline) throw Timeout("constraint_generation_deadline");
    };
    return std::make_unique<ConstraintVariants>(std::move(inputs), std::nullopt, get_or_null(record, "state"));
}

Value RepairLoop::run() {
    if (const Value* passages = get(route_, "passages"); passages && py_truthy(*passages))
        throw Unsupported("routes with passages repair in Python");
    deadline_ = monotonic() + o_.seconds;
    source_exhausted_ = !source_ || source_->exhausted();
    climbing_ = o_.climb_families && o_.rank_equal_cost;
    const Value policy = object({{"stopOnFeasible", o_.stop_on_feasible ? boolean(*o_.stop_on_feasible) : Value{nullptr}},
                                 {"optimizeGoal", boolean(o_.optimize_goal)},
                                 {"rankEqualCost", boolean(o_.rank_equal_cost)},
                                 {"oracleRejection", boolean(o_.oracle_rejection)}});
    current_context_ = repair_context(route_, profiles_, o_.geometry,
                                      object({{"scope", o_.repair_scope}, {"solvePolicy", policy},
                                              {"avoidancePolicy", integer_value(14)}}));
    const Value& resume = o_.resume_state;
    resuming_ = is_object(resume) && py_equal(get_or_null(fields(resume), "version"), integer_value(3)) &&
                compatible(get_or_null(fields(resume), "context"), current_context_);
    exact_resume_ = resuming_ && py_equal(at(fields(resume), "context"), current_context_);
    if (exact_resume_) {
        const Object& r = fields(resume);
        metrics_update(completed_source_metrics_, get_or_null(r, "sourcesCompletedMetrics"));
        for (const auto& s : listed(r, "repairSources")) repair_sources_.push_back({fields(s), nullptr});
        for (const auto& k : listed(r, "seen")) seen_.insert(as_text(k));
        for (const auto& k : listed(r, "generatedBranches")) generated_.insert(as_text(k));
        resume_attempt_log_ = listed(r, "attemptLog");
        resume_contact_log_ = listed(r, "contactLog");
    }
    if (resuming_) reference_yaw_ = get_or_null(fields(resume), "referenceWarmYaw");

    if (resuming_) {
        const Object& r = fields(resume);
        if (const Value* saved = get(r, "interactionChoices"))
            for (const auto& [k, v] : fields(*saved)) choices_.emplace_back(k, v);
        if (const Value* saved = get(r, "interactionAlternatives"))
            for (const auto& [k, v] : fields(*saved)) choice_alternatives_.emplace_back(k, items(v));
        if (climbing_) {
            if (const Value* saved = get(r, "families"))
                for (const auto& [k, v] : fields(*saved)) {
                    auto f = family_of(v);
                    for (auto& [_, m] : f->points) m->pending = 0;
                    families_.emplace_back(k, std::move(f));
                }
        }
        const Array pending = listed(r, "pending");
        Array deferred;
        if (o_.retry_unresolved) deferred = listed(r, "unresolved");
        if (py_truthy(get_or_null(r, "inFlight"))) deferred.push_back(at(r, "inFlight"));
        if (!o_.retry_unresolved)
            for (const auto& u : listed(r, "unresolved")) unresolved_.push_back(u);
        // max([scores] + [0.]) + 1: Python's max keeps the first maximal value.
        Num floor = pending.empty() ? flt(0.) : any_num(at(fields(pending[0]), "score"));
        for (const auto& b : pending) floor = py_max(floor, any_num(at(fields(b), "score")));
        if (!pending.empty()) floor = py_max(floor, flt(0.));
        floor = add(floor, whole(1));
        // An accepted branch left in flight is an unfinished walk's candidate: keep its result, re-walk.
        std::vector<std::string> restored;
        if (exact_resume_)
            for (const auto& v : listed(r, "acceptedVariants")) restored.push_back(as_text(at(fields(v), "branch")));
        const Array all = joined(pending, deferred);
        for (std::size_t index = 0; index < all.size(); ++index) {
            const Object& saved = fields(all[index]);
            const Array rows = items(at(saved, "rows")), passages = items(at(saved, "passages")), hits = items(at(saved, "hits"));
            const std::string key = branch_key(rows, passages, hits);
            if (std::find(restored.begin(), restored.end(), key) != restored.end()) continue;
            restored.push_back(key);
            seen_.erase(key);
            const Value score = index < pending.size() ? at(saved, "score") : value(add(floor, any_num(at(saved, "score"))));
            offer(score, rows, passages, hits, get_or_null(saved, "warmYaw"), nullptr, get_or_null(saved, "meta"));
        }
        resumed_branches_ = static_cast<std::int64_t>(queue_.size());
        advance_all();
    }
    auto min_score_less_one = [&] {  // min([b[0] for b in queue] + [0.]) - 1, over the heap's list order
        Num m = flt(0.);
        bool first = true;
        for (const auto& e : queue_) {
            m = first ? any_num(e.score) : py_min(m, any_num(e.score));
            first = false;
        }
        m = first ? flt(0.) : py_min(m, flt(0.));
        return value(sub(m, whole(1)));
    };
    const Array route_passages = listed(route_, "passages"), route_hits = listed(route_, "hitEvents");
    if (!exact_resume_ && !source_)
        offer(min_score_less_one(), {}, route_passages, route_hits, get_or_null(route_, "warmYaw"), nullptr, Value{nullptr});

    if (exact_resume_) {
        const Object& r = fields(resume);
        if (py_truthy(get_or_null(r, "acceptedResult"))) best_result_ = fields(at(r, "acceptedResult"));
        best_branch_ = get_or_null(r, "incumbent");
        completed_ = listed(r, "completed");
        for (const auto& v : listed(r, "acceptedVariants")) accepted_variants_.push_back(fields(v));
    }
    if (best_result_) {
        const Object& saved = fields(at(*best_result_, "best"));
        const Value valid = validate_vertical_schedule(object(route_), profiles_, at(saved, "plan"));
        bool accepted = py_truthy(at(fields(valid), "feasible"));
        if (accepted && o_.candidate_filter)
            accepted = py_truthy(at(fields(o_.candidate_filter(at(saved, "plan"), at(saved, "candidate"), valid)), "accepted"));
        if (accepted && !is_null(o_.geometry)) {
            if (is_null(o_.validation_scope)) {
                accepted = py_truthy(at(fields(check_geometry(o_.geometry, fields(at(saved, "plan")), fields(at(saved, "candidate")),
                                                             fields(valid), hooks_, o_.seconds)), "feasible"));
            } else {
                const Object& scope = fields(o_.validation_scope);
                Object selected = fields(o_.geometry);
                Array solids;
                for (const auto& i : items(at(scope, "solidIndices")))
                    solids.push_back(items(at(fields(o_.geometry), "solids")).at(as_int(i)));
                put(selected, "solids", array(std::move(solids)));
                const auto [p, c, v] = validation_prefix(at(saved, "plan"), at(saved, "candidate"), valid, as_int(at(scope, "through")));
                accepted = py_truthy(at(fields(check_geometry(object(std::move(selected)), fields(p), fields(c), fields(v), hooks_,
                                                             o_.seconds)), "feasible"));
            }
        }
        if (!accepted) {
            best_result_.reset();
            best_branch_ = Value{nullptr};
            seen_.erase(branch_key({}, route_passages, route_hits));
            offer(integer_value(-1), {}, route_passages, route_hits, get_or_null(route_, "warmYaw"), nullptr, Value{nullptr});
        }
    }

    std::string termination = "contact_search_exhausted";
    const double clearance = std::max(d(at(route_, "supportClearance")), 2e-7);
    while (!queue_.empty() || !source_exhausted_ || !repair_sources_.empty()) {
        if (settling_) {
            const Object done = std::move(*settling_);
            settling_.reset();
            settle(done);
        }
        in_flight_ = Value{nullptr};
        publish();
        if (hooks_.is_cancelled()) {
            if (best_result_) return finish_best({{"termination", text("cancelled")}});
            return finish({{"status", text("cancelled")}, {"termination", text("cancelled")}, {"best", Value{nullptr}},
                           {"attempts", array(inner_attempts_)}, {"contactAttempts", array(attempts_)}});
        }
        if (hooks_.work_check) {
            try {
                hooks_.work_check();
            } catch (const Timeout& error) {
                termination = error.what();
                break;
            }
        }
        const double remaining = deadline_ - monotonic();
        if (remaining <= 0) {
            termination = "deadline";
            break;
        }
        if (!repair_sources_.empty() && queue_.empty()) {
            Source& record = repair_sources_.back();
            std::optional<Value> variant;
            try {
                if (!record.instance) record.instance = for_plan(record.record, remaining);
                variant = record.instance->next();
                put(record.record, "state", record.instance->state());
            } catch (const Timeout& error) {
                if (record.instance) put(record.record, "state", record.instance->state());
                termination = error.what();
                break;
            }
            if (!variant) {
                const Value& state = get_or_null(record.record, "state");
                if (is_object(state)) metrics_update(completed_source_metrics_, get_or_null(fields(state), "metrics"));
                if (is_object(state) && py_truthy(get_or_null(fields(state), "uncertainInteractions"))) {
                    const Value unknown = save_branch(at(record.record, "score"), items(at(record.record, "rows")),
                                                      items(at(record.record, "passages")), items(at(record.record, "hits")),
                                                      at(fields(at(record.record, "candidate")), "yaw"), get_or_null(record.record, "meta"));
                    const Object& u = fields(unknown);
                    const std::string key = branch_key(items(at(u, "rows")), items(at(u, "passages")), items(at(u, "hits")));
                    if (std::none_of(unresolved_.begin(), unresolved_.end(), [&](const Value& b) {
                            return branch_key(items(at(fields(b), "rows")), items(at(fields(b), "passages")),
                                              items(at(fields(b), "hits"))) == key;
                        }))
                        unresolved_.push_back(unknown);
                }
                repair_sources_.pop_back();
                advance_all();
                continue;
            }
            const Object& v = fields(*variant);
            const Array& events = items(at(fields(at(record.record, "plan")), "events"));
            auto leg_of = [&](std::int64_t tick) {
                std::int64_t leg = -1;
                for (std::size_t i = 0; i < events.size(); ++i)
                    if (as_int(at(fields(events[i]), "landed")) <= tick) leg = static_cast<std::int64_t>(i);
                if (leg < 0) fail("ValueError: max() arg is an empty sequence");
                return leg;
            };
            Array converted;
            for (const auto& row_value : items(at(v, "rows"))) {
                const Object& row = fields(row_value);
                const std::int64_t tick = as_int(at(row, "offset"));
                const std::int64_t leg = leg_of(tick);
                converted.push_back(object(with(with(row, "leg", integer_value(leg)), "offset",
                                                integer_value(tick - as_int(at(fields(events[leg]), "jump"))))));
            }
            Array selected;
            for (const auto& p_value : listed(v, "selectedPassages")) {
                Object passage = fields(p_value);
                Array references;
                for (const auto& a : items(at(passage, "switchPhases"))) {
                    const std::int64_t at_phase = as_int(a);
                    const std::int64_t leg = leg_of(at_phase / 3);
                    references.push_back(object({{"leg", integer_value(leg)},
                                                 {"offset", integer_value(at_phase / 3 - as_int(at(fields(events[leg]), "jump")))},
                                                 {"phase", integer_value(at_phase % 3)}}));
                }
                put(passage, "phaseReferences", array(std::move(references)));
                selected.push_back(object(std::move(passage)));
            }
            const std::string source_key = branch_key(items(at(record.record, "rows")), items(at(record.record, "passages")),
                                                      items(at(record.record, "hits")));
            Value meta{nullptr};
            if (get(v, "family"))
                meta = object({{"family", text(digest(array({text(source_key), at(v, "family")})))},
                               {"switches", at(v, "switches")}, {"branchChoices", at(v, "branchChoices")},
                               {"branchKey", at(v, "branchKey")}});
            const Value score = value(add(any_num(at(record.record, "score")), mul(any_num(at(v, "score")), flt(1e-9))));
            arrive(score, joined(items(at(record.record, "rows")), converted), items(at(record.record, "passages")),
                   items(at(record.record, "hits")), at(fields(at(record.record, "candidate")), "yaw"), array(std::move(selected)),
                   meta, text(source_key));
            continue;
        }
        if (source() && !source_exhausted_ && queue_.empty()) {
            std::optional<Value> variant;
            try {
                variant = source()->next();
            } catch (const Timeout& error) {
                termination = error.what();
                break;
            }
            if (!variant) {
                source_exhausted_ = true;
                advance_all();
            } else {
                const Object& v = fields(*variant);
                Value meta{nullptr};
                if (get(v, "family"))
                    meta = object({{"family", at(v, "family")}, {"switches", at(v, "switches")},
                                   {"branchChoices", at(v, "branchChoices")}, {"branchKey", at(v, "branchKey")}});
                const Array rows = items(at(v, "rows")), passages = listed(v, "passages");
                arrive(min_score_less_one(), rows, passages, route_hits,
                       py_truthy(reference_yaw_) ? reference_yaw_ : get_or_null(route_, "warmYaw"),
                       array(listed(v, "selectedPassages")), meta, Value{nullptr});
                generated_.insert(branch_key(rows, passages, route_hits));
            }
            if (queue_.empty()) continue;
        }
        // Generation can consume the remaining budget: leave the next branch queued instead of dispatching it into an
        // unstarted unknown attempt.
        if (hooks_.work_check) {
            try {
                hooks_.work_check();
            } catch (const Timeout& error) {
                termination = error.what();
                break;
            }
        }
        if (monotonic() >= deadline_) {
            termination = "deadline";
            break;
        }
        Entry entry = heap_pop(queue_);
        const Value score = entry.score;
        const Array rows = entry.rows, passages = entry.passages, hits = entry.hits;
        Value proposal_yaw = entry.warm_yaw;
        const Value meta = entry.meta;
        in_flight_ = save_branch(score, rows, passages, hits, proposal_yaw, meta);
        if (point(meta)) settling_ = Object{{"meta", meta}, {"rank", Value{nullptr}}, {"objective", Value{nullptr}}};
        publish();
        const std::string key = branch_key(rows, passages, hits);
        Object branch_fields = fields(build_branch(rows, passages, hits, !is_null(reference_yaw_) ? reference_yaw_ : proposal_yaw));
        if (best_result_)
            put(branch_fields, "maxTotalRunTicks",
                integer_value(std::min(as_int(at(branch_fields, "maxTotalRunTicks")), as_int(at(fields(at(*best_result_, "best")), "cost")))));
        const Value branch = object(std::move(branch_fields));
        std::vector<std::pair<std::string, std::shared_ptr<Object>>> audit_cache;
        Array audit_errors;
        auto audit = [&](const Object& best) -> std::shared_ptr<Object> {
            // Equal facings with different relaxed accelerations are different trajectories.
            const Object& candidate = fields(at(best, "candidate"));
            const Value* controls = get(candidate, "controls");
            const std::string k = dumps(array({at(best, "allocation"), at(fields(at(best, "plan")), "ticks"),
                                               at(candidate, "yaw"), controls ? text(digest(*controls)) : Value{nullptr}}),
                                        false, true);
            for (const auto& [cached_key, cached] : audit_cache)
                if (cached_key == k) return cached;
            auto checked = std::make_shared<Object>(audit_point(best, branch, passages));
            audit_cache.emplace_back(k, checked);
            return checked;
        };
        auto retain = [&](const Value& best_value) {
            const Object& best = fields(best_value);
            proposal_yaw = at(fields(at(best, "candidate")), "yaw");
            if (best_result_ &&
                !rank_less(candidate_rank(best_value, objective()), candidate_rank(at(*best_result_, "best"), objective())))
                return;
            std::shared_ptr<Object> checked;
            try {
                checked = audit(best);
            } catch (const Unsupported&) {
                throw;
            } catch (const std::invalid_argument& error) {
                audit_errors.push_back(text(error.what()));
                return;
            } catch (const std::runtime_error& error) {  // Timeout included
                audit_errors.push_back(text(error.what()));
                return;
            }
            if (!py_truthy(at(*checked, "accepted"))) return;
            const Value accepted = enrich_point(best, *checked, branch, rows, passages, hits, meta);
            const Value& yaw = at(fields(at(fields(accepted), "candidate")), "yaw");
            reference_yaw_ = yaw;
            best_branch_ = save_branch(score, rows, passages, hits, yaw, meta);
            if (hooks_.work_accepted) {
                hooks_.work_accepted(accepted);
                put(*checked, "reportedAcceptance", boolean(true));
            }
            best_result_ = Object{{"status", text("feasible")}, {"best", accepted}, {"termination", text("validated_incumbent")},
                                  {"attempts", array({})}, {"contactAttempts", array({})}, {"minimumProven", boolean(false)}};
            best_aliased_ = true;
            publish();
        };
        RouteSearch search;
        search.seconds = remaining;
        search.candidate_seconds = o_.candidate_seconds;
        search.retries = o_.retries;
        search.improve = o_.improve;
        search.rank_equal_cost = o_.rank_equal_cost;
        search.optimize_goal = o_.optimize_goal;
        search.stop_on_feasible = o_.stop_on_feasible;
        search.numerical = o_.numerical;
        search.on_candidate = retain;
        Object result = fields(find_route(branch, hooks_, search));
        const Array tried = listed(result, "attempts");
        for (const auto& a : tried) inner_attempts_.push_back(a);
        const bool has_best = py_truthy(get_or_null(result, "best"));
        Array hashes;
        for (const auto& a : tried)
            if (py_truthy(get_or_null(fields(a), "problemHash"))) hashes.push_back(at(fields(a), "problemHash"));
        Object completion{{"branch", text(key)}, {"status", at(result, "status")},
                          {"validationStatus", text(has_best ? "not_run" : "no_candidate")}, {"problemHashes", array(std::move(hashes))}};
        completed_.push_back(object(completion));
        auto update_completion = [&](const Object& c) { completed_.back() = object(c); };
        if (has_best) proposal_yaw = at(fields(at(fields(at(result, "best")), "candidate")), "yaw");
        std::shared_ptr<Object> checked;
        try {
            if (has_best) {
                checked = audit(fields(at(result, "best")));
                const Value& accepted_flag = get_or_null(*checked, "accepted");
                const Value* reason = get(*checked, "reason");
                put(completion, "validationStatus", py_truthy(accepted_flag) ? text("accepted") : reason ? *reason : text("rejected"));
                update_completion(completion);
            }
        } catch (const Timeout&) {
            unresolved_.push_back(save_branch(score, rows, passages, hits, proposal_yaw, meta));
            termination = "geometry_deadline";
            break;
        } catch (const Unsupported&) {
            throw;
        } catch (const std::exception& error) {
            if (!dynamic_cast<const std::invalid_argument*>(&error) && !dynamic_cast<const std::runtime_error*>(&error)) throw;
            unresolved_.push_back(save_branch(score, rows, passages, hits, proposal_yaw, meta));
            if (best_result_) return finish_best({{"termination", text("geometry_error")}, {"error", text(error.what())}});
            return finish({{"status", text("error")}, {"termination", text("geometry_check")}, {"error", text(error.what())},
                           {"best", Value{nullptr}}, {"attempts", array(inner_attempts_)}, {"contactAttempts", array(attempts_)}});
        }
        const Array conflicts = checked ? items(at(*checked, "conflicts")) : Array{};
        Array kinds, conflict_rows;
        const Value* chosen = nullptr;
        for (const auto& [k, v] : choices_)
            if (k == key) chosen = &v;
        for (const auto& p : chosen ? items(*chosen) : passages) kinds.push_back(at(fields(p), "kind"));
        for (const auto& c : conflicts)
            conflict_rows.push_back(object({{"block", at(fields(at(fields(c), "block")), "id")}, {"state", at(fields(c), "state")},
                                            {"reason", at(fields(c), "reason")}}));
        attempts_.push_back(object({{"branch", text(key)},
                                    {"status", at(result, "status")},
                                    {"constraintCount", integer_value(static_cast<std::int64_t>(rows.size()))},
                                    {"passageCount", integer_value(static_cast<std::int64_t>(passages.size()))},
                                    {"wallHitCount", integer_value(static_cast<std::int64_t>(hits.size()))},
                                    {"wallHitGeneration", object({{"generated", integer_value(0)}, {"candidateDrops", integer_value(0)}})},
                                    {"passageKinds", array(std::move(kinds))},
                                    {"incumbentAuditErrors", array(audit_errors)},
                                    {"conflicts", array(std::move(conflict_rows))},
                                    {"termination", at(result, "termination")}}));
        auto last_attempt = [&]() -> Object { return fields(attempts_.back()); };
        auto set_last = [&](Object a) { attempts_.back() = object(std::move(a)); };
        const std::string& status = as_text(at(result, "status"));
        if (status == "error" || status == "cancelled") {
            unresolved_.push_back(save_branch(score, rows, passages, hits, proposal_yaw, meta));
            if (best_result_) return finish_best({{"termination", text(status)}});
            put(result, "attempts", array(inner_attempts_));
            put(result, "contactAttempts", array(attempts_));
            return finish(std::move(result));
        }
        if (!has_best) {
            Array certificates, diagnostics;
            for (const auto& a : tried) {
                const Object& attempt = fields(a);
                if (py_equal(get_or_null(attempt, "status"), text("blocked"))) certificates.push_back(at(attempt, "certificate"));
                for (const auto& dg : listed(attempt, "diagnostics")) diagnostics.push_back(dg);
            }
            const bool proven = py_equal(get_or_null(result, "termination"), text("search_exhausted")) && !tried.empty() &&
                                certificates.size() == tried.size();
            bool oracle_completed = o_.oracle_rejection && !diagnostics.empty();
            for (const auto& dg : diagnostics) {
                if (!oracle_completed) break;
                const Object& g = fields(dg);
                const Value& t = get_or_null(g, "termination");
                const Value& w = get_or_null(g, "workerStatus");
                auto in = [&](std::initializer_list<const char*> names) {
                    return std::any_of(names.begin(), names.end(), [&](const char* n) { return py_equal(w, text(n)); });
                };
                oracle_completed =
                    py_equal(get_or_null(g, "status"), text("unknown")) &&
                    ((py_equal(t, text("worker_finished")) &&
                      in({"Infeasible_Problem_Detected", "Search_Direction_Becomes_Too_Small", "Restoration_Failed",
                          "Solve_Succeeded", "Solved_To_Acceptable_Level"})) ||
                     (py_equal(t, text("disk_finished")) && in({"PrimalInfeasible", "AlmostPrimalInfeasible"})));
            }
            put(completion, "oracleRejected", boolean(oracle_completed));
            put(completion, "proven", boolean(proven));
            put(completion, "certificates", array(certificates));
            update_completion(completion);
            if (status == "unknown" && !oracle_completed && !proven)
                unresolved_.push_back(save_branch(score, rows, passages, hits, proposal_yaw, meta));
            continue;
        }
        if (conflicts.empty()) {
            if (get(*checked, "verdict") && o_.candidate_filter) {
                Object a = last_attempt();
                const Object& verdict = fields(at(*checked, "verdict"));
                Object filter;
                for (const auto& field : verdict)
                    if (field.first != "alternatives") filter.push_back(field);
                if (const Value* corridor = get(verdict, "corridorAudit")) {
                    Object logged = fields(*corridor);
                    put(logged, "selectedPassages", chosen ? *chosen : array(passages));
                    put(logged, "selectedHits", array(hits));
                    put(logged, "extraRows", array(rows));
                    Array requests;
                    for (const auto& t : tried) {
                        Object request;
                        for (const char* k : {"problemHash", "status", "allocation", "stage", "leg"})
                            if (const Value* v = get(fields(t), k)) request.emplace_back(k, *v);
                        requests.push_back(object(std::move(request)));
                    }
                    put(logged, "numericalRequests", array(std::move(requests)));
                    const bool configured = !is_null(o_.geometry);
                    put(logged, "physicalReplay",
                        object({{"status", text(configured ? "not_run" : "not_configured")},
                                {"reason", text(configured ? "corridor_rejected_before_scene_validation" : "no_scene_configured")}}));
                    put(filter, "corridorAudit", object(std::move(logged)));
                }
                put(a, "candidateFilter", object(std::move(filter)));
                set_last(std::move(a));
            }
            if (const Value* reason = get(*checked, "reason")) {
                Object a = last_attempt();
                put(a, "status", text("unknown"));
                put(a, "termination", *reason);
                set_last(std::move(a));
                const Value* verdict = get(*checked, "verdict");
                if (verdict)
                    for (const auto& extra : listed(fields(*verdict), "alternatives"))
                        offer(value(add(any_num(score), whole(1))), joined(rows, items(extra)), passages, hits, proposal_yaw,
                              nullptr, meta);
                continue;
            }
            const std::shared_ptr<Object> audit_result = checked;
            Object best = fields(at(result, "best"));
            if (!is_null(o_.geometry)) {
                const Object geometry_check = fields(at(*audit_result, "geometryCheck"));
                Object a = last_attempt();
                put(a, "geometryCheck", object(geometry_check));
                set_last(std::move(a));
                if (!py_truthy(at(geometry_check, "feasible"))) {
                    Object b = last_attempt();
                    put(b, "status", text("unknown"));
                    if (!get(geometry_check, "solidIndex")) {
                        put(b, "termination", at(geometry_check, "reason"));
                        set_last(std::move(b));
                        continue;
                    }
                    set_last(std::move(b));
                    if (source() && source()->conflict_driven()) {
                        grow(geometry_check);
                        continue;
                    }
                    // Save construction inputs before any interruptible preparation: the shared builder assigns all
                    // applicable boxes jointly.
                    repair_sources_.push_back({Object{{"route", branch}, {"plan", at(best, "plan")},
                                                      {"vertical", at(*audit_result, "vertical")},
                                                      {"candidate", at(best, "candidate")}, {"score", score},
                                                      {"rows", array(rows)}, {"passages", array(passages)},
                                                      {"hits", array(hits)}, {"meta", meta}},
                                               nullptr});
                    continue;
                }
                put(best, "geometryCheck", object(geometry_check));
            }
            const Value enriched = enrich_point(best, *audit_result, branch, rows, passages, hits, meta);
            put(result, "best", enriched);
            if (std::none_of(accepted_variants_.begin(), accepted_variants_.end(),
                             [&](const Object& v) { return as_text(at(v, "branch")) == key; })) {
                const Value& family_value = is_null(meta) ? Value{nullptr} : get_or_null(fields(meta), "family");
                accepted_variants_.push_back({{"branch", text(key)},
                                              {"best", enriched},
                                              {"family", family_value},
                                              {"alarmTicks", o_.confirm ? alarmed(result) : array({})},
                                              {"state", save_branch(score, rows, passages, hits,
                                                                    at(fields(at(fields(enriched), "candidate")), "yaw"), meta)}});
            }
            const Value& yaw = at(fields(at(fields(enriched), "candidate")), "yaw");
            if (is_null(reference_yaw_)) reference_yaw_ = yaw;
            if (hooks_.work_accepted && !py_truthy(get_or_null(*audit_result, "reportedAcceptance"))) hooks_.work_accepted(enriched);
            put(result, "attempts", array(inner_attempts_));
            put(result, "contactAttempts", array(attempts_));
            if (settling_) {
                put(*settling_, "rank", candidate_rank(enriched, objective()));
                put(*settling_, "objective", at(fields(at(fields(enriched), "candidate")), "objective"));
            }
            if (!o_.rank_equal_cost) {
                if (best_result_ && rank_less(candidate_rank(at(*best_result_, "best"), objective()), candidate_rank(enriched, objective())))
                    return finish(best_result_fields());
                best_branch_ = save_branch(score, rows, passages, hits, yaw, meta);
                return finish(std::move(result));
            }
            if (!best_result_ || !rank_less(candidate_rank(at(*best_result_, "best"), objective()), candidate_rank(enriched, objective()))) {
                best_result_ = result;
                best_aliased_ = true;
                best_branch_ = save_branch(score, rows, passages, hits, yaw, meta);
                reference_yaw_ = yaw;
            }
            continue;
        }
        const Object& conflict = fields(conflicts[0]);
        const Array& position = items(at(conflict, "position"));
        for (const auto& row : avoid_contact_rows(as_int(at(conflict, "leg")), as_int(at(conflict, "offset")),
                                                  as_text(at(conflict, "reason")), as_text(at(fields(at(conflict, "block")), "id")),
                                                  items(at(conflict, "region")), d(position[0]), d(position[1]), clearance)) {
            const Object& r = fields(row);
            const double delta = std::fabs(d(position[as_text(at(r, "mode")) == "X" ? 0 : 1]) - d(at(r, "rhs")));
            offer(value(add(any_num(score), flt(delta))), joined(rows, {row}), passages, hits, proposal_yaw, nullptr, meta);
        }
    }
    if (settling_) {
        const Object done = std::move(*settling_);
        settling_.reset();
        settle(done);
    }
    if (o_.confirm && o_.rank_equal_cost) walk();
    if (best_result_) return finish_best({{"termination", text(termination)}});
    return finish({{"status", text(termination == "cancelled" ? "cancelled" : "unknown")}, {"termination", text(termination)},
                   {"best", Value{nullptr}}, {"attempts", array(inner_attempts_)}, {"contactAttempts", array(attempts_)}});
}

}  // namespace

std::string branch_key(const Array& rows, const Array& passages, const Array& hits) {
    if (!passages.empty()) throw Unsupported("passage branch keys are built in Python");
    std::vector<Value> row_tuples, hit_tuples;
    for (const auto& r : rows) {
        const Object& row = fields(r);
        row_tuples.push_back(array({at(row, "leg"), at(row, "offset"), at(row, "mode"), at(row, "cmp"), at(row, "rhs")}));
    }
    for (const auto& h : hits) {
        const Object& hit = fields(h);
        hit_tuples.push_back(array({at(hit, "leg"), at(hit, "offset"), at(hit, "axis"), at(hit, "direction"), at(hit, "solidIndex")}));
    }
    std::vector<Value> sorted_hits = hit_tuples;
    std::stable_sort(sorted_hits.begin(), sorted_hits.end(), [](const Value& a, const Value& b) { return py_compare(a, b) < 0; });
    return dumps(array({sorted_set(std::move(row_tuples)), array({}), array(Array(sorted_hits.begin(), sorted_hits.end()))}),
                 true, false);
}

Value solve_contact_branches(const Value& route, const Value& profiles, const Value& checks, const SolveHooks& hooks,
                             const RepairOptions& options, ConstraintVariants* initial_variants) {
    RepairLoop loop(route, profiles, checks, hooks, options, initial_variants);
    return loop.run();
}

Array node_alternatives(const Array& accepted, const Value& incumbent, const Value& objective) {
    std::optional<std::string> chosen;
    if (py_truthy(incumbent)) {
        const Object& i = fields(incumbent);
        chosen = branch_key(items(at(i, "rows")), items(at(i, "passages")), items(at(i, "hits")));
    }
    std::vector<Object> variants;
    for (const auto& v : accepted) variants.push_back(fields(v));
    using Entry = std::pair<Value, Value>;  // (rank, variant)
    std::vector<std::pair<Value, std::vector<Entry>>> classes;  // keyed by alternativeBranchKey, first seen first
    for (const auto& variant : usable(variants)) {
        const Value& best = at(fields(variant), "best");
        const Value& key = get_or_null(fields(best), "alternativeBranchKey");
        auto found = std::find_if(classes.begin(), classes.end(), [&](const auto& c) { return py_equal(c.first, key); });
        if (found == classes.end()) found = classes.insert(classes.end(), {key, {}});
        found->second.emplace_back(candidate_rank(best, objective), variant);
    }
    auto by_rank = [](const Entry& a, const Entry& b) { return rank_less(a.first, b.first); };
    std::vector<Entry> kept;
    for (auto& [key, members] : classes) {
        std::stable_sort(members.begin(), members.end(), by_rank);
        const Value& top = members[0].first;
        kept.push_back(members[0]);
        std::size_t close = 0;
        for (std::size_t k = 1; k < members.size() && close < kFallbackLimit; ++k)
            if (py_equal(items(members[k].first)[0], items(top)[0]) &&
                d(items(members[k].first)[1]) - d(items(top)[1]) <= kFallbackGap) {
                kept.push_back(members[k]);
                ++close;
            }
    }
    std::stable_sort(kept.begin(), kept.end(), by_rank);
    Array out;
    for (const auto& [rank, variant] : kept)
        if (!chosen || as_text(at(fields(variant), "branch")) != *chosen) out.push_back(variant);
    return out;
}

}  // namespace pkc
