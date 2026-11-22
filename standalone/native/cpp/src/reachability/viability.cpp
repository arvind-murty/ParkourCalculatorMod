// Port of reachability/viability.prefix_viability.
#include "pkc/reachability/viability.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "../schema.hpp"
#include "pkc/envelopes.hpp"
#include "pkc/movement/normal.hpp"

namespace pkc {

Value prefix_viability(const Value& scene_value, const Program& program, const std::vector<std::string>& required_contacts,
                       std::int64_t terminal_tick, const std::optional<std::vector<std::string>>& enabled_types) {
    auto names = [](const std::vector<std::string>& v) {
        Array out;
        for (const auto& s : v) out.push_back(text(s));
        return array(std::move(out));
    };
    if (required_contacts.empty())
        return object({{"dead", boolean(false)}, {"reason", text("no_outstanding_contact_requirement")}});
    const Object& scene = fields(scene_value);
    const ScalarState state = boundary_state(program);
    const std::int64_t remaining = terminal_tick - program.profile.duration;
    if (remaining <= 0)
        return object({{"dead", boolean(true)}, {"reason", text("mandatory_contacts_after_horizon")},
                       {"remaining", names(required_contacts)}});
    const std::vector<std::string> given = enabled_types && !enabled_types->empty()
                                               ? *enabled_types
                                               : std::vector<std::string>{"normal", "stepping", "ceiling"};
    const std::set<std::string> kinds(given.begin(), given.end());
    const bool other = std::any_of(kinds.begin(), kinds.end(),
                                   [](const std::string& k) { return k != "normal" && k != "stepping" && k != "ceiling"; });
    if (other || py_truthy(get_or_null(scene, "climbableCells")) || py_truthy(get_or_null(scene, "maxWallHits")))
        return object({{"dead", boolean(false)}, {"reason", text("unsupported_first_event_scope")}});
    if (state.on_ground || state.velocity.value >= 0)
        return object({{"dead", boolean(false)}, {"reason", text("grounded_or_rising_continuation")}});
    const Trace trace = replay(state, remaining, false, {}, {}, program.threshold.value_or(Num{.005, false}));
    if (trace.status == "invalid")
        return object({{"dead", boolean(false)}, {"reason", text("unknown_scalar_continuation")}});
    const double inf = std::numeric_limits<double>::infinity();
    const Value open = array({real(-inf), real(inf)});
    Array states, phases;
    for (const auto& y : trace.position)
        states.push_back(object({{"X", open}, {"Z", open}, {"Y", array({value(y), value(y)})}}));
    for (std::size_t t = 0; t + 1 < trace.position.size(); ++t) {
        const Num &a = trace.position[t], &b = trace.position[t + 1];
        phases.push_back(object({{"Y", object({{"X", open}, {"Z", open},
                                              {"Y", array({value(py_min(a, b)), value(py_max(a, b))})}})}}));
    }
    const Value envelopes = object({{"states", array(std::move(states))}, {"phases", array(std::move(phases))},
                                    {"contradiction", Value{nullptr}}});
    Num highest = trace.position.front();
    for (const auto& y : trace.position) highest = py_max(highest, y);
    Array excluded;
    for (const auto& bv : items(at(scene, "blocks"))) {
        const Object& block = fields(bv);
        const Array& bounds = items(at(block, "bounds"));
        const Value& top = at(block, "top");
        Value first{nullptr};
        for (std::int64_t t = 0; t < remaining; ++t) {
            const Value checked = interaction_reachability(
                envelopes, object({{"regions", array({object({{"tick", integer_value(t)}, {"phase", text("Y")},
                                                              {"bounds", array({bounds[0], top, bounds[2], bounds[1], top, bounds[3]})}})})}}));
            if (!py_truthy(at(fields(checked), "ruledOut")))
                return object({{"dead", boolean(false)}, {"reason", text("possible_first_floor_contact")},
                               {"block", at(block, "id")}, {"input", integer_value(t)}});
            if (t == 0) first = checked;
        }
        excluded.push_back(object({{"block", at(block, "id")}, {"top", top}, {"checkedInputs", integer_value(remaining)},
                                   {"maxFeetY", value(highest)}, {"firstExclusion", first}}));
    }
    Array types;
    for (const auto& k : kinds) types.push_back(text(k));
    return object({{"dead", boolean(true)}, {"reason", text("descending_without_any_first_floor_contact")},
                   {"remaining", names(required_contacts)}, {"through", integer_value(terminal_tick)},
                   {"enabledTypes", array(std::move(types))}, {"floorExclusions", array(std::move(excluded))},
                   {"justification", text("airborne descent cannot jump or hit a ceiling upward; supported steps require a first floor contact")}});
}

}  // namespace pkc
