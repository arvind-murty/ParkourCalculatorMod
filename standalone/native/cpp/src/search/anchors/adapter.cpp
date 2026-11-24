// Port of search/anchors/adapter.py's stage services (and the EventGuideAdapter parts they inherit).
#include "pkc/search/anchors/adapter.hpp"

#include <algorithm>
#include <stdexcept>

#include "../../schema.hpp"
#include "../../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "pkc/program.hpp"
#include "pkc/spatial/event_routes.hpp"

namespace pkc {
namespace {

Value audit_value(const Audit& a) {  // audit's dict
    Array departed, ticks;
    for (const auto& d : a.visits.departed) departed.push_back(text(d));
    for (std::int64_t t : a.ticks) ticks.push_back(integer_value(t));
    return object({{"accepted", boolean(!a.reason)}, {"reason", a.reason ? text(*a.reason) : Value{nullptr}},
                   {"visits", object({{"version", integer_value(1)},
                                      {"current", a.visits.current ? text(*a.visits.current) : Value{nullptr}},
                                      {"departed", array(std::move(departed))}, {"valid", boolean(a.visits.valid)}})},
                   {"reached", integer_value(a.reached)}, {"ticks", array(std::move(ticks))}});
}

Value goal_blocks(const Object& goal) {  // goal.get('blocks', [goal['block']])
    const Value& block = at(goal, "block");
    const Value* blocks = get(goal, "blocks");
    return blocks ? *blocks : array({block});
}

}  // namespace

AnchorAdapter::AnchorAdapter(Value data, Value graph, Value anchor_policy, Value guide_plan, Value policy_settings,
                             std::shared_ptr<AnchorScene> scene)
    : data_(std::move(data)),
      graph_(std::move(graph)),
      guide_plan_(std::move(guide_plan)),
      policy_settings_(std::move(policy_settings)),
      scene_(std::move(scene)) {
    for (const auto& a : items(at(fields(anchor_policy), "anchors"))) anchors_.push_back(as_text(a));
}

std::int64_t AnchorAdapter::group_end(std::int64_t index) const {
    // The last pair of the encounter group holding pair `index`; a frame searches the whole group.
    for (const auto& g : items(at(fields(guide_plan_), "groups"))) {
        const Object& group = fields(g);
        if (as_int(at(group, "first")) <= index && index <= as_int(at(group, "last"))) return as_int(at(group, "last"));
    }
    throw std::runtime_error("StopIteration");
}

Value AnchorAdapter::initial_cursor(const Array& roots) const {
    Object cursor = fields(pkc::initial_cursor(data_, roots));
    std::vector<Audit> checks;
    for (const auto& p : roots) checks.push_back(scene_->audit(program_from_value(p), anchors_));
    bool same = true;
    for (const auto& a : checks) same = same && a.reached == checks.front().reached;
    if (std::any_of(checks.begin(), checks.end(), [](const Audit& a) { return a.reason.has_value(); }) || !same ||
        checks.empty())
        throw std::invalid_argument("Anchor roots violate ordered/no-return domain");
    const std::int64_t index = checks.front().reached;
    put(cursor, "anchorIndex", integer_value(index));
    put(cursor, "groupEnd", integer_value(group_end(index)));
    put(cursor, "spanStart", integer_value(index ? checks.front().ticks.back() : 0));
    return object(std::move(cursor));
}

Value AnchorAdapter::next_goals(const Value& cursor_value, const Array& parents, const Value& continuation,
                                const std::function<void()>& check) const {
    if (!is_null(continuation) && py_truthy(at(fields(continuation), "exhausted")))
        return object({{"requests", array({})}, {"continuation", continuation},
                       {"report", object({{"expansions", integer_value(0)}, {"materializations", integer_value(0)}})}});
    check();
    const Object& cursor = fields(cursor_value);
    const Object& data = fields(data_);
    const std::int64_t end = as_int(at(cursor, "groupEnd"));
    const bool terminal = end == static_cast<std::int64_t>(anchors_.size());
    const std::string target = end < static_cast<std::int64_t>(anchors_.size())
                                   ? anchors_[static_cast<std::size_t>(end)]
                                   : as_text(at(fields(at(data, "task")), "block"));
    Value after = at(fields(at(fields(parents.front()), "profile")), "duration");
    for (const auto& p : parents) {
        const Value& duration = at(fields(at(fields(p), "profile")), "duration");
        if (num(duration).value < num(after).value) after = duration;
    }
    Array requests;
    for (const auto& event_value : items(at(fields(graph_), "events"))) {
        const Object& event = fields(event_value);
        if (py_truthy(at(event, "terminal")) != terminal) continue;
        const Object& goal = fields(at(event, "goal"));
        if (as_text(at(goal, "kind")) != "landing" || !contains(items(goal_blocks(goal)), text(target))) continue;
        requests.push_back(object(
            {{"schema", integer_value(1)},
             {"id", text("anchor-request:" + digest(array({cursor_value, at(event, "id"), text(target)})))},
             {"event", at(event, "id")},
             {"goal", object(with(with(goal, "block", text(target)), "blocks", array({text(target)})))},
             {"terminal", boolean(terminal)},
             {"commitments", array({})},
             {"semanticPrefix", array({})},
             {"afterTick", after},
             {"connector", object({{"entry", at(cursor, "attachments")}, {"exit", at(event, "attachments")}})}}));
    }
    if (requests.empty()) throw std::invalid_argument("Required anchor has no exposed contact event: " + target);
    const auto materialized = static_cast<std::int64_t>(requests.size());
    return object({{"requests", array(std::move(requests))}, {"continuation", object({{"exhausted", boolean(true)}})},
                   {"report", object({{"expansions", integer_value(0)}, {"materializations", integer_value(materialized)}})}});
}

Value AnchorAdapter::stage(const Value& request, const Value& cursor_value) const {
    const Object& cursor = fields(cursor_value);
    const Object& plan = fields(guide_plan_);
    const std::int64_t span = as_int(at(cursor, "anchorIndex")), end = as_int(at(cursor, "groupEnd"));
    const Array& points = items(at(plan, "points"));
    const auto size = static_cast<std::int64_t>(points.size());
    if (end + 1 >= size) throw std::out_of_range("list index out of range");
    const auto first = std::clamp<std::int64_t>(span, 0, size), last = std::clamp<std::int64_t>(end + 2, first, size);
    const std::string id = as_text(at(plan, "id")) + ":" + std::to_string(span);
    const Value guide = object({{"version", integer_value(1)}, {"id", text(id)},
                                {"points", array(Array(points.begin() + first, points.begin() + last))},
                                {"target", points[static_cast<std::size_t>(end + 1)]},
                                {"hardConstraints", boolean(false)}});
    const Object& data = fields(data_);
    const Value objective = onward_objective(guide_plan_, end, at(fields(at(data, "scene")), "objective"));
    const Value score_context = text(digest(
        object({{"request", request}, {"guide", guide}, {"objective", objective}, {"policy", policy_settings_}})));
    const Value corridor = object({{"kind", text("event_guidance")}, {"schema", integer_value(1)},
                                   {"domainId", at(data, "domainId")}, {"request", at(fields(request), "id")},
                                   {"guide", text(id)}});
    return object({{"guide", guide}, {"scoreContext", score_context}, {"cursor", cursor_value},
                   {"objective", objective}, {"corridor", corridor}});
}

Value AnchorAdapter::advance(const Value& cursor, const Value& child, const Value& witness, const Value& request) const {
    const Value& profile = at(fields(child), "profile");
    const Value observed = observed_route(data_, profile, witness, at(fields(request), "afterTick"));
    Object result = fields(advance_cursor(data_, graph_, cursor, request, observed));
    const std::int64_t index = as_int(at(result, "groupEnd")) + 1;
    put(result, "anchorIndex", integer_value(index));
    put(result, "groupEnd", integer_value(group_end(index)));
    put(result, "spanStart", at(fields(profile), "duration"));
    return object(std::move(result));
}

AnchorAdapter::Candidate AnchorAdapter::candidate(const Value& program, const Value& request_value,
                                                   const Value& cursor_value, bool node_query) {
    const Object& cursor = fields(cursor_value);
    const auto anchors = static_cast<std::int64_t>(anchors_.size());
    // A group-end arrival must have visited every anchor inside the group.
    const std::int64_t required =
        node_query ? as_int(at(cursor, "anchorIndex")) : std::min(as_int(at(cursor, "groupEnd")) + 1, anchors);
    const Audit prior = scene_->audit(program_from_value(program), anchors_, required);
    if (prior.reason) {
        const Value verdict = object(with(fields(audit_value(prior)), "alternatives", array({})));
        return [verdict](const Value&, const Value&, const Value&, const Value&) { return verdict; };
    }
    if (node_query) {
        const Value verdict = object({{"accepted", boolean(true)}, {"requiredEventsSatisfied", boolean(true)},
                                      {"taskSatisfied", boolean(false)},
                                      {"validation", text("exact_node_prior_anchors_only")}, {"alternatives", array({})}});
        return [verdict](const Value&, const Value&, const Value&, const Value&) { return verdict; };
    }
    // EventGuideAdapter.candidate: the program's support episodes are the same for every candidate of the solve.
    const Object& request = fields(request_value);
    const Object& goal = fields(at(request, "goal"));
    const Value blocks = goal_blocks(goal);
    const Array& names = items(blocks);
    const Object& data = fields(data_);
    std::vector<std::string> target_owners;
    std::vector<Value> solids;
    for (const auto& b_value : items(at(fields(at(data, "scene")), "blocks"))) {
        const Object& b = fields(b_value);
        if (!contains(names, at(b, "id"))) continue;
        const Value* source = get(b, "sourceBlock");
        target_owners.push_back(as_text(source ? *source : at(b, "id")));
        solids.push_back(at(b, "solidIndex"));
    }
    const Value episodes = support_episodes(data_, at(fields(program), "profile"), target_owners);
    const Value& after = at(request, "afterTick");
    bool observed = false;
    std::vector<std::int64_t> states;  // the ticks a goal-surface check reads
    for (const auto& e_value : items(episodes)) {
        const Object& e = fields(e_value);
        const bool touched = std::any_of(items(at(e, "solids")).begin(), items(at(e, "solids")).end(),
                                         [&](const Value& s) { return contains(Array(solids.begin(), solids.end()), s); });
        if (!touched || num(at(e, "input")).value < num(after).value) continue;
        observed = true;
        states.push_back(as_int(at(e, "input")) + (as_text(at(e, "kind")) == "named_contact"));
    }
    Value surface{nullptr};
    if (const Value* id = get(goal, "surface"); id && observed) {
        for (const auto& s : items(at(data, "surfaces")))
            if (py_equal(at(fields(s), "id"), *id)) {
                surface = s;
                break;
            }
        if (is_null(surface)) throw std::runtime_error("StopIteration");
    }
    const Value terminal = at(request, "terminal");
    return [this, episodes, observed, states, surface, terminal](const Value&, const Value& candidate, const Value&,
                                                                 const Value& scope) {
        if (py_truthy(scope) && py_equal(at(fields(scope), "purpose"), text("preliminary")))
            return object({{"accepted", boolean(true)}, {"requiredEventsSatisfied", boolean(false)},
                           {"taskSatisfied", boolean(false)}, {"guideClassification", text("unknown")},
                           {"validation", text("preliminary_required_event_omitted")}, {"alternatives", array({})}});
        ++event_audits_;
        bool required = observed;
        if (required && !is_null(surface)) {
            const Object& s = fields(surface);
            const double height = num(at(s, "height")).value;
            const Array& positions = items(at(fields(candidate), "positions"));
            required = false;
            for (std::int64_t t : states) {
                const Array& p = items(positions[static_cast<std::size_t>(t)]);
                const double point[3] = {num(p[0]).value, height, num(p[1]).value};
                for (const auto& box_value : items(at(s, "cover"))) {
                    const Array& box = items(box_value);
                    bool in = true;
                    for (std::size_t a = 0; a < 3; ++a)
                        in = in && num(box[a]).value - 1e-7 <= point[a] && point[a] <= num(box[a + 3]).value + 1e-7;
                    required = required || in;
                }
            }
        }
        required_event_rejections_ += !required;
        return object({{"accepted", boolean(required)},
                       {"reason", required ? Value{nullptr} : text("required_event_not_observed")},
                       {"requiredEventsSatisfied", boolean(required)},
                       {"taskSatisfied", required ? terminal : boolean(false)},
                       {"guideClassification", text("uncommitted")}, {"observedEvents", episodes},
                       {"alternatives", array({})}});
    };
}

}  // namespace pkc
