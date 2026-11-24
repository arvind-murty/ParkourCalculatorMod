// Port of numerics/post_alarm.py. Every constant is the module's (measured in research/post-alarm/README.md).
#include "pkc/numerics/post_alarm.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <tuple>

#include "../py_math.hpp"
#include "../schema.hpp"
#include "pkc/validation/numerical.hpp"

namespace pkc {
namespace {

const double G = 1e-2;  // an answer this close to its node's bound stops the push and needs no probe
const double W0 = .3, RAMP = 1.2, W_MAX = 1e3, MOMENTUM = .5;
const int PUSH_CAP = 10;
const int QUADRANT_BUDGET = 7;    // Clarabel solves of the sector check
const double QUADRANT_FULL = 1e-7;  // a sector solution this little short ends the check open
const double LEVEL_STEP = 1e-2;
const int LEVEL_PROBES = 3, PROBE_CAP = 5;
const std::int64_t RACE_WINDOW = 6;
const double RACE_CUT = .5;
const int RACE_STEPS = 5, RACE_FINISH_CAP = 30;
const double SHORT = 1e-3;  // a tick this short is a reversal candidate whatever its kind
const int DECAY_CAP = 30;

std::string termination_for(const std::string& method) { return "post_alarm_" + method; }  // TERMINATIONS

struct Stop {  // _Stop: the deadline passed or the caller cancelled
    std::string reason;
};

Controls controls_of(const Value& value) {
    Controls out;
    for (const auto& c : items(value)) out.push_back({num(items(c)[0]).value, num(items(c)[1]).value});
    return out;
}

Value controls_value(const Controls& controls) {
    Array out;
    for (const auto& c : controls) out.push_back(array({real(c[0]), real(c[1])}));
    return array(std::move(out));
}

Value without_objective(const Value& content) {
    Object out;
    for (const auto& [k, v] : fields(content))
        if (k != "objective") out.emplace_back(k, v);
    return object(std::move(out));
}

double objective_of(const Value& candidate) { return num(at(fields(candidate), "objective")).value; }

// _Node: a Clarabel problem the push steps re-solve with new linear costs: the request's rows plus cuts and a level row.
struct Node {
    bool feasible = false;
    Controls central, disk;
    double bound = 0.;
    std::shared_ptr<DiskProblem> built;
};

struct PushState {  // (y, w, last_full, best, history)
    Controls y;
    double w = W0;
    std::optional<Controls> last_full;
    Value best{nullptr};
    std::vector<std::optional<double>> history;
};

struct Pushed {  // (best accepted or None, last iterate, steps, state)
    Value best{nullptr};
    std::optional<Controls> last;
    int steps = 0;
    PushState state;
};

class Pipeline {
public:
    Pipeline(const Value& problem, const Value& relaxed_candidate, const Value& relaxed, double seconds,
             const RelaxedServices& services)
        : started(seconds_now()),
          sense(as_text(at(fields(at(fields(problem), "objective")), "sense")) == "MAX" ? 1 : -1),
          problem_(problem), relaxed_candidate_(relaxed_candidate), relaxed_(relaxed), services_(services),
          deadline_(started + seconds), content_(disk_spec(problem, array({}), nullptr, true)) {
        Array magnitude;
        for (const auto& m : items(at(fields(content_), "magnitude")))
            magnitude.push_back(real(num(m).value - kDiskAllowance));
        exact_ = object(with(fields(content_), "magnitude", array(std::move(magnitude))));
    }

    std::pair<Value, std::string> answer();

    double started;
    int sense;
    int clarabel = 0;
    Value best{nullptr};  // the best answer so far, which a deadline does not discard
    // info
    std::string method = "none";
    int push_steps = 0, probes_made = 0, raced = 0;
    std::optional<std::int64_t> reversal_tick;
    std::optional<std::string> quadrant_verdict;

private:
    void check() const {
        if (services_.cancelled && services_.cancelled()) throw Stop{"cancelled"};
        if (seconds_now() >= deadline_) throw Stop{"time_limit"};
    }

    std::pair<Value, std::shared_ptr<DiskProblem>> decide(const Value& content, const std::vector<DiskCut>& cuts = {},
                                                          const std::optional<DiskLevel>& level = std::nullopt) {
        check();
        auto result = disk_decide(content, deadline_ - seconds_now(), services_.limits, cuts, level);
        const Object& r = fields(result.first);
        clarabel += 1 + static_cast<int>(py_truthy(get_or_null(r, "retried")));
        const std::string& status = as_text(at(r, "status"));
        if (status == "MaxIterations" || status == "MaxTime") check();  // a limit the deadline cut short is no verdict
        return result;
    }

    Node node(const Value& content, const std::vector<DiskCut>& cuts = {},
              const std::optional<DiskLevel>& level = std::nullopt,
              const std::optional<std::pair<double, Controls>>& disk_point = std::nullopt) {
        Node out;
        const auto central = decide(without_objective(content), cuts, level);
        if (as_text(at(fields(central.first), "outcome")) != "feasible") return out;
        out.central = controls_of(at(fields(central.first), "controls"));
        if (disk_point) {
            // The request's own disk solve already gave the bound and its point (setup reuse).
            std::tie(out.bound, out.disk) = *disk_point;
            try {
                out.built = std::make_shared<DiskProblem>(content, services_.limits.cap, services_.limits.max_iter);
            } catch (const std::exception&) {
                return out;
            }
        } else {
            auto result = decide(content, cuts, level);
            out.built = result.second;
            const Object& r = fields(result.first);
            if (as_text(at(r, "outcome")) != "feasible") return out;
            out.bound = num(at(r, "objective")).value;
            out.disk = controls_of(at(r, "controls"));
        }
        out.feasible = true;
        return out;
    }

    // Minimizes w*cost - sum_t y_t.u_t; the point whatever the status when finite (the validator judges it).
    std::optional<Controls> step(Node& node, const Controls& y, double w) {
        check();
        std::vector<double> q = node.built->cost();
        for (double& v : q) v = w * v;
        const auto& columns = node.built->columns();
        for (std::size_t t = 0; t < std::min(y.size(), columns.size()); ++t)
            for (int a = 0; a < 2; ++a) q[columns[t][a]] -= y[t][a];
        node.built->update_q(q);
        ++clarabel;
        return node.built->solve_controls();
    }

    bool better(double value, const Value& other) const {
        return is_null(other) || sense * value > sense * objective_of(other);
    }

    double peak(const Controls& controls) const {
        return num(items(disk_deficits(problem_, controls_value(controls)))[0]).value;
    }

    // The validated candidate of a full-strength point, else None.
    Value accept(const Controls& controls) const {
        if (peak(controls) >= kPostAlarmFull) return kNull;
        Value candidate = validate(problem_, post_alarm_facings(problem_, controls_value(controls)));
        return py_truthy(at(fields(candidate), "feasible")) ? candidate : kNull;
    }

    // The penalty push from `start`, or continuing an earlier push's `state`, stopped within G of the node's bound,
    // after a gain under G/10 over 3 steps, or at the cap. `decaying` halves w after every step short of full strength
    // until the first full-strength one: at W0 the objective can hold ticks short indefinitely.
    Pushed push(Node& node, const Controls* start, int cap, bool decaying = false, const PushState* state = nullptr) {
        PushState s = state ? *state : PushState{*start};
        Pushed out;
        for (int k = 0; k < cap; ++k) {
            auto point = step(node, s.y, s.w);
            if (!point) break;
            out.last = *point;
            ++out.steps;
            const Controls& u = *point;
            Value accepted = accept(u);
            if (!is_null(accepted) && (is_null(s.best) || better(objective_of(accepted), s.best))) s.best = accepted;
            s.history.push_back(is_null(s.best) ? std::nullopt : std::optional<double>(objective_of(s.best)));
            if (!is_null(s.best) && sense * (node.bound - objective_of(s.best)) <= G) break;
            const std::size_t n = s.history.size();
            if (!is_null(s.best) && n > 3 && s.history[n - 4] &&
                sense * (objective_of(s.best) - *s.history[n - 4]) < G / 10)
                break;
            const bool full = peak(u) < kPostAlarmFull;
            if (s.last_full) {
                if (full) {
                    s.w = std::min(s.w * RAMP, W_MAX);
                    Controls y;
                    for (std::size_t t = 0; t < std::min(u.size(), s.last_full->size()); ++t)
                        y.push_back({u[t][0] + MOMENTUM * (u[t][0] - (*s.last_full)[t][0]),
                                     u[t][1] + MOMENTUM * (u[t][1] - (*s.last_full)[t][1])});
                    s.y = std::move(y);
                    s.last_full = u;
                } else {
                    s.w = s.w / 2;
                    s.y = *s.last_full;
                }
            } else {
                if (full) {
                    s.last_full = u;
                } else if (decaying) {
                    s.w = s.w / 2;
                }
                s.y = u;
            }
        }
        out.best = s.best;
        out.state = std::move(s);
        return out;
    }

    // 'infeasible' when every full-strength completion is excluded, else 'open': branch on the worst tick's direction
    // (4 quadrants, then halves), each sector relaxed to its chord, diving on the parent's peak shortfall.
    std::string quadrant() {
        const Value free = without_objective(content_);
        const Value& magnitude = at(fields(problem_), "magnitude");
        using Entry = std::tuple<double, std::int64_t, Sectors>;
        auto later = [](const Entry& a, const Entry& b) {
            return std::tie(std::get<0>(a), std::get<1>(a)) > std::tie(std::get<0>(b), std::get<1>(b));
        };
        std::priority_queue<Entry, std::vector<Entry>, decltype(later)> heap(later);
        heap.emplace(0., 0, Sectors{});
        std::int64_t counter = 1;
        int solves = 0;
        while (!heap.empty()) {
            if (solves >= QUADRANT_BUDGET) return "open";
            const Sectors sectors = std::get<2>(heap.top());
            heap.pop();
            const auto result = decide(free, sector_cuts(sectors, magnitude));
            ++solves;
            const Object& r = fields(result.first);
            if (as_text(at(r, "outcome")) != "feasible") continue;
            const Value deficit = disk_deficits(problem_, at(r, "controls"));
            const double worst = num(items(deficit)[0]).value;
            if (worst <= QUADRANT_FULL) return "open";
            const std::int64_t tick = as_int(items(deficit)[2]);
            const Array& point = items(items(at(r, "controls"))[tick]);
            for (auto& child : sector_children(sectors, tick, {num(point[0]).value, num(point[1]).value}))
                heap.emplace(worst, counter++, std::move(child));
        }
        return "infeasible";
    }

    // Objective-cut probes: push inside objective >= best+LEVEL_STEP (MAX) while best is more than G below the bound.
    // The level row holds the objective, so the probe push decays.
    Value probes(const Node& plain, Value found_best) {
        while (probes_made < LEVEL_PROBES && sense * (plain.bound - objective_of(found_best)) > G) {
            ++probes_made;
            const DiskLevel level{at(fields(content_), "objective"), objective_of(found_best) + sense * LEVEL_STEP};
            Node probe = node(exact_, {}, level);
            if (!probe.feasible) break;
            const Pushed pushed = push(probe, &probe.central, PROBE_CAP, true);
            if (is_null(pushed.best) || !better(objective_of(pushed.best), found_best)) break;
            found_best = best = pushed.best;
            method = "probe";
        }
        return found_best;
    }

    // Reversal race: each jump tick near an alarm tick, and the disk point's most short tick whatever its kind, turned
    // against the disk point's direction. Candidates are screened with RACE_STEPS push steps in order of their cut
    // node's bound until no remaining bound can beat the answer; only when no screen answers does the best-bound
    // candidate's push go on, to RACE_FINISH_CAP steps.
    Value race(const Node& plain) {
        const Object& p = fields(problem_);
        const std::int64_t ticks = as_int(at(p, "ticks"));
        const Value& vertical = get_or_null(p, "vertical");
        const Value& jump_value = py_truthy(vertical) ? get_or_null(fields(vertical), "jumpVelocity") : kNull;
        auto is_jump = [&](std::int64_t t) { return py_truthy(jump_value) && !is_null(items(jump_value).at(t)); };
        std::set<std::int64_t> candidates;
        for (const auto& alarm : items(at(fields(relaxed_), "alarmTicks"))) {
            const std::int64_t a = as_int(alarm);
            for (std::int64_t t = std::max<std::int64_t>(0, a - RACE_WINDOW); t < std::min(ticks, a + RACE_WINDOW + 1); ++t)
                if (is_jump(t)) candidates.insert(t);
        }
        const Value deficit = disk_deficits(problem_, controls_value(plain.disk));
        const Array& shortfall = items(items(deficit)[1]);
        std::int64_t top = 0;
        for (std::int64_t t = 1; t < ticks; ++t)
            if (num(shortfall[t]).value > num(shortfall[top]).value) top = t;
        if (num(shortfall[top]).value >= SHORT) candidates.insert(top);
        const Array& magnitude = items(at(p, "magnitude"));
        std::vector<std::pair<std::int64_t, Node>> nodes;
        for (std::int64_t t : candidates) {
            const double direction = std::atan2(plain.disk[t][1], plain.disk[t][0]) + M_PI;
            const DiskCut cut{t, std::cos(direction), std::sin(direction), RACE_CUT * num(magnitude[t]).value};
            Node cut_node = node(exact_, {cut});
            if (cut_node.feasible) nodes.emplace_back(t, std::move(cut_node));
        }
        std::stable_sort(nodes.begin(), nodes.end(),
                         [&](const auto& a, const auto& b) { return -sense * a.second.bound < -sense * b.second.bound; });
        Value found{nullptr};
        std::optional<PushState> first;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            auto& [t, cut_node] = nodes[i];
            Pushed pushed = push(cut_node, &cut_node.central, RACE_STEPS);
            ++raced;
            if (!first) first = pushed.state;
            if (!is_null(pushed.best) && (is_null(found) || better(objective_of(pushed.best), found))) {
                found = best = pushed.best;
                method = "race";
                reversal_tick = t;
            }
            if (!is_null(found) &&
                (i + 1 == nodes.size() || sense * (objective_of(found) - nodes[i + 1].second.bound) >= 0))
                break;
        }
        if (is_null(found) && !nodes.empty()) {
            auto& [t, cut_node] = nodes[0];
            const Pushed pushed = push(cut_node, nullptr, RACE_FINISH_CAP - RACE_STEPS, false, &*first);
            if (!is_null(pushed.best)) {
                found = best = pushed.best;
                method = "race";
                reversal_tick = t;
            }
        }
        return found;
    }

    const Value& problem_;
    const Value& relaxed_candidate_;
    const Value& relaxed_;
    const RelaxedServices& services_;
    double deadline_;
    Value content_, exact_;
};

// The best accepted candidate or None, and the termination.
std::pair<Value, std::string> Pipeline::answer() {
    const std::pair<double, Controls> alarmed_point{num(at(fields(relaxed_), "objective")).value,
                                                    controls_of(at(fields(relaxed_candidate_), "controls"))};
    Node plain = node(exact_, {}, std::nullopt, alarmed_point);
    if (!plain.feasible) return {kNull, "post_alarm_none"};
    Controls start = plain.central;
    const Object& p = fields(problem_);
    const Value& warm = get_or_null(p, "warmYaw");
    if (py_truthy(warm)) {
        // A child starts its shared prefix from the parent's answer at full strength.
        const Array& magnitude = items(at(p, "magnitude"));
        const Array& base = items(at(p, "base"));
        const Array& yaw = items(warm);
        Controls warmed;
        for (std::size_t t = 0; t < std::min({magnitude.size(), base.size(), yaw.size()}); ++t) {
            const double m = num(magnitude[t]).value, b = num(base[t]).value, y = num(yaw[t]).value;
            warmed.push_back({m * std::cos(b + radians(y)), m * std::sin(b + radians(y))});
        }
        for (std::size_t t = yaw.size(); t < start.size(); ++t) warmed.push_back(start[t]);
        start = std::move(warmed);
    }
    Pushed pushed = push(plain, &start, PUSH_CAP);
    push_steps = pushed.steps;
    Value found = pushed.best;
    if (!is_null(found)) {
        best = found;
        method = "push";
        found = probes(plain, found);
        return {found, termination_for(method)};
    }
    quadrant_verdict = quadrant();
    if (*quadrant_verdict == "infeasible") {
        method = "quadrant";
        return {kNull, "quadrant_infeasible"};
    }
    found = race(plain);
    if (!is_null(found)) {
        // The race answer is held below its cut node's bound; the probes are not.
        found = probes(plain, found);
        return {found, termination_for(method)};
    }
    // A restart, not the first push's state: continuing a stalled push answers fewer requests.
    pushed = push(plain, &plain.central, DECAY_CAP, true);
    if (!is_null(pushed.best)) {
        best = pushed.best;
        method = "decay";
        return {pushed.best, termination_for("decay")};
    }
    return {kNull, "post_alarm_none"};
}

}  // namespace

Value post_alarm_run(const Value& problem, const Value& relaxed_candidate, const Value& relaxed, double seconds,
                     const RelaxedServices& services) {
    Pipeline pipeline(problem, relaxed_candidate, relaxed, seconds, services);
    Value found{nullptr};
    std::string termination;
    bool cancelled = false;
    try {
        std::tie(found, termination) = pipeline.answer();
    } catch (const Stop& stop) {
        cancelled = stop.reason == "cancelled";
        found = pipeline.best;
        termination = !is_null(found) ? termination_for(pipeline.method) : stop.reason;
    }
    if (cancelled) found = kNull;
    const char* status = cancelled ? "cancelled" : !is_null(found) ? "feasible" : "unknown";
    const Object& r = fields(relaxed);
    Object result = fields(decided_result(problem, pipeline.started, status, found, termination, at(r, "status"), relaxed));
    // search.engine.numerical_counts counts a confirmation without ipoptCalls as one IPOPT call.
    const Value& disk_objective = at(fields(relaxed_candidate), "objective");
    Object confirmation{{"peak", at(r, "peakRelativeDeficit")},
                        {"tick", at(r, "peakTick")},
                        {"cached", boolean(false)},
                        {"diskObjective", disk_objective},
                        {"ipoptObjective", kNull},
                        {"gap", kNull},
                        {"material", boolean(false)},
                        {"method", text(pipeline.method)},
                        {"pushSteps", integer_value(pipeline.push_steps)},
                        {"probes", integer_value(pipeline.probes_made)},
                        {"reversalTick", pipeline.reversal_tick ? integer_value(*pipeline.reversal_tick) : kNull},
                        {"raced", integer_value(pipeline.raced)},
                        {"quadrant", pipeline.quadrant_verdict ? text(*pipeline.quadrant_verdict) : kNull},
                        {"ipoptCalls", integer_value(0)},
                        {"clarabelSolves", integer_value(pipeline.clarabel)}};
    if (!is_null(found)) {
        const double gap = pipeline.sense * (num(disk_objective).value - objective_of(found));
        put(confirmation, "ipoptObjective", at(fields(found), "objective"));
        put(confirmation, "gap", real(gap));
        put(confirmation, "material", boolean(gap > services.material_gap));
    }
    put(result, "confirmation", object(std::move(confirmation)));
    return object(std::move(result));
}

Value post_alarm_facings(const Value& problem, const Value& controls) {
    const Array& base = items(at(fields(problem), "base"));
    const Array& rows = items(controls);
    Array out;
    for (std::size_t t = 0; t < std::min(rows.size(), base.size()); ++t) {
        const Array& c = items(rows[t]);
        out.push_back(real(degrees(std::atan2(num(c[1]).value, num(c[0]).value) - num(base[t]).value)));
    }
    return array(std::move(out));
}

std::vector<DiskCut> sector_cuts(const Sectors& sectors, const Value& magnitude) {
    std::vector<DiskCut> out;
    for (const auto& [t, sector] : sectors) {
        const auto [lo, hi] = sector;
        out.push_back({t, std::cos((lo + hi) / 2), std::sin((lo + hi) / 2),
                       num(items(magnitude)[t]).value * std::cos((hi - lo) / 2)});
    }
    return out;
}

std::vector<Sectors> sector_children(const Sectors& sectors, std::int64_t tick, const std::array<double, 2>& controls) {
    auto found = std::find_if(sectors.begin(), sectors.end(), [&](const auto& s) { return s.first == tick; });
    auto with_sector = [&](std::pair<double, double> sector) {  // {**sectors, t: sector}
        Sectors out = sectors;
        auto same = std::find_if(out.begin(), out.end(), [&](const auto& s) { return s.first == tick; });
        if (same != out.end()) {
            same->second = sector;
        } else {
            out.emplace_back(tick, sector);
        }
        return out;
    };
    if (found == sectors.end()) {
        const double angle = std::atan2(controls[1], controls[0]);
        std::vector<Sectors> out;
        for (int k = 0; k < 4; ++k)
            out.push_back(with_sector({angle - M_PI / 4 + k * M_PI / 2, angle + M_PI / 4 + k * M_PI / 2}));
        return out;
    }
    const auto [lo, hi] = found->second;
    return {with_sector({lo, (lo + hi) / 2}), with_sector({(lo + hi) / 2, hi})};
}

}  // namespace pkc
