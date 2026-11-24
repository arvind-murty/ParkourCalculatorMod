// The anchor adapter's stage services for the native search: search/anchors/adapter.py's AnchorAdapter (with the
// EventGuideAdapter parts it inherits) for one adapter's domain, event graph, anchor policy and guide plan.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "pkc/search/anchors/policy.hpp"
#include "pkc/value.hpp"

namespace pkc {

class AnchorAdapter {
public:
    // data, graph, anchor_policy and guide_plan are the adapter's; policy_settings is semantic_settings(anchor_policy).
    AnchorAdapter(Value data, Value graph, Value anchor_policy, Value guide_plan, Value policy_settings,
                  std::shared_ptr<AnchorScene> scene);

    using Candidate = std::function<Value(const Value& plan, const Value& candidate, const Value& vertical,
                                          const Value& scope)>;

    Value initial_cursor(const Array& roots) const;
    std::int64_t group_end(std::int64_t index) const;
    // next_goals(cursor, parents, continuation, budget=dict(check=check)) as {requests, continuation, report}.
    Value next_goals(const Value& cursor, const Array& parents, const Value& continuation,
                     const std::function<void()>& check) const;
    // for_goal(request, cursor)'s facts: {guide, scoreContext, cursor, objective, corridor}.
    Value stage(const Value& request, const Value& cursor) const;
    // advance(cursor, child, observed_route(data, child's profile, witness, after=request's afterTick), request).
    Value advance(const Value& cursor, const Value& child, const Value& witness, const Value& request) const;
    // The stage view for (request, cursor), bound to `program` (for_node(program) when node_query), as its
    // candidate(plan, candidate, vertical, partial=True) scoped to each call's scope.
    Candidate candidate(const Value& program, const Value& request, const Value& cursor, bool node_query);

    // The counts the adapter's summary reports, from this adapter's candidate checks.
    std::int64_t event_audits() const { return event_audits_; }
    std::int64_t required_event_rejections() const { return required_event_rejections_; }

private:
    Value data_, graph_, guide_plan_, policy_settings_;
    std::vector<std::string> anchors_;
    std::shared_ptr<AnchorScene> scene_;
    std::int64_t event_audits_ = 0, required_event_rejections_ = 0;
};

}  // namespace pkc
