// Port of search/feasibility.node_identity and meaningful_choice.
#include "pkc/search/feasibility.hpp"

#include <algorithm>

#include "pkc/canonical.hpp"
#include "../value_ops.hpp"

namespace pkc {

std::string node_identity(const Program& program, const Value& context) {
    // Repair cursors, warm yaws, observations and mutable queue state do not identify a physical node.
    Program physical = program;
    physical.repair_state.reset();
    return digest(object({{"version", integer_value(1)}, {"program", text(program_search_key(physical))}, {"context", context}}));
}

bool meaningful_choice(const MovementChoice& choice) {
    const Profile& p = choice.profile;
    if (std::find(p.jump.begin(), p.jump.end(), true) != p.jump.end() || !p.case_events.empty()) return true;
    return std::any_of(p.contact_events.begin(), p.contact_events.end(),
                       [&](const ContactEvent& e) { return e.axis != "Y" || !p.grounded.at(e.offset); });
}

}  // namespace pkc
