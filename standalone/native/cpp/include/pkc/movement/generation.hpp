// A generator context (movement/generation.py contexts in open mode) as the
// native generators and gate read it.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pkc/program.hpp"

namespace pkc {

class InteractionGate;

struct Forward {  // context['forward']: an ordinary decision's point and the moves its guide leg allows (leg_class)
    std::array<double, 2> point;
    std::set<std::pair<std::string, std::int64_t>> allowed;
    Num margin;
};

struct GenerationContext {
    std::shared_ptr<const Program> program;
    ScalarState state;                  // boundary_state(program)
    std::int64_t max_inputs = 200;      // context.get('maxInputs', 200)
    Num threshold{.005, false};         // context.get('threshold', .005)
    bool run_tick_choices = true;
    std::optional<std::string> launch_face;
    std::function<bool(const std::string&)> feet_allowed_fn;  // feetAllowed (unset: every face)
    std::function<void()> check_budget;                        // checkBudget (unset: no budget)
    std::optional<std::set<std::string>> cases;           // context['cases'] (None: every case)
    std::optional<Forward> forward;
    Value decision_point;  // context['decision'].get('decisionPoint'): null without a decision or its point
    Value frame_id;  // context.get('frameId'), null when absent
    bool region_items = false;
    InteractionGate* gate = nullptr;  // None: no reachability gate
    mutable std::optional<std::string> prefix_key;  // InteractionGate.approach's '_prefixKey' digest

    bool feet_allowed(const std::string& name) const { return !feet_allowed_fn || feet_allowed_fn(name); }
    void budget() const {
        if (check_budget) check_budget();
    }
};

// generate_choices(kind, context) for the open generators ('normal', 'stepping', 'ceiling'): every choice in
// the Python's order. A context checkBudget that throws stops it.
std::vector<MovementChoice> generate_choices(const std::string& kind, const GenerationContext& context);

}  // namespace pkc
