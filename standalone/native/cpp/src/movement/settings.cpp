// Port of movement/settings.py.
#include "pkc/movement/settings.hpp"

#include <algorithm>
#include <set>

#include "../schema.hpp"

namespace pkc {

Value movement_settings(const Value& scene, const std::vector<std::string>& registered) {
    const Value* declared = get(fields(scene), "movementSearch");
    Object settings = declared ? fields(*declared) : Object{};
    Array types;
    for (const auto& name : registered) types.push_back(text(name));
    const std::vector<std::pair<std::string, Value>> defaults = {
        {"maxInputs", integer_value(200)},       {"maxPrefixes", integer_value(256)},
        {"maxQueuedPrefixes", integer_value(512)}, {"maxChoicesPerType", integer_value(64)},
        {"maxInteractions", integer_value(200)}, {"maxEntryInputs", integer_value(14)},
        {"expansionsPerProposal", integer_value(8)}, {"types", array(std::move(types))}};
    for (const auto& field : settings)
        if (std::none_of(defaults.begin(), defaults.end(), [&](const auto& d) { return d.first == field.first; }))
            fail("Unsupported movement search settings");
    for (const auto& [key, value] : defaults) {
        setdefault(settings, key, value);
        if (key != "types")
            integer(at(settings, key), key, key == "maxEntryInputs" ? 0 : 1,
                    key == "maxInputs" || key == "maxInteractions" ? 200 : 200000);
    }
    const Value& chosen = at(settings, "types");
    bool valid = is_list(chosen) && !items(chosen).empty();
    if (valid) {
        std::set<std::string> seen;
        for (const auto& kind : items(chosen)) {
            const auto* name = std::get_if<std::string>(&kind.data);
            valid = valid && name && seen.insert(*name).second &&
                    std::find(registered.begin(), registered.end(), *name) != registered.end();
        }
    }
    if (!valid) fail("Select registered movement types");
    return object(std::move(settings));
}

}  // namespace pkc
