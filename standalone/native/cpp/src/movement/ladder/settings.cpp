// Port of movement/ladder/settings.ladder_settings.
#include "pkc/movement/ladder/settings.hpp"

#include <string>
#include <utility>

#include "../../schema.hpp"

namespace pkc {

Value ladder_settings(const Object& scene) {
    static const std::pair<const char*, std::int64_t> kDefaults[] = {
        {"maxEntryInputs", 14}, {"maxInputs", 48}, {"entryDelay", 5}, {"maxSkeletons", 2048}, {"maxSchedules", 2000},
        {"maxMaskWork", 30000}, {"maxRanked", 256}, {"representatives", 3}, {"maxNumericalSchedules", 32},
        {"maxOrdinaryPaths", 8}, {"maxSuffixPaths", 3}, {"maxRouteContexts", 24}, {"maxRunAllocations", 4},
        {"minClimbInputs", 0}, {"maxInteractions", 12}, {"requireLadder", -1}, {"maxContactVariants", 16},
        {"maxMixedContacts", 3}, {"maxTransferPrograms", 16}, {"maxTransferWindows", 4}, {"maxTransferPrefixes", 64},
        {"maxTransferQueuedPrefixes", 256}};
    const Value* declared = get(scene, "ladderSearch");
    const Value settings_value = declared ? *declared : object({});
    auto known = [](const std::string& key) {
        for (const auto& [name, _] : kDefaults)
            if (key == name) return true;
        return false;
    };
    if (!is_object(settings_value) ||
        std::any_of(fields(settings_value).begin(), fields(settings_value).end(), [&](const auto& f) { return !known(f.first); }))
        fail("Unsupported ladder search settings");
    Object settings = fields(settings_value);
    for (const auto& [key, fallback] : kDefaults) {
        const std::string name = key;
        if (name == "requireLadder") {
            if (!std::holds_alternative<bool>(setdefault(settings, key, boolean(false)).data))
                fail("requireLadder must be boolean");
            continue;
        }
        const bool zero = name == "entryDelay" || name == "minClimbInputs" || name == "maxContactVariants" ||
                          name == "maxTransferPrograms" || name == "maxTransferWindows";
        const std::int64_t upper = name == "maxInteractions" ? 200 : name == "entryDelay" ? 5 : 200000;
        integer(setdefault(settings, key, integer_value(fallback)), key, zero ? 0 : 1, upper);
    }
    if (as_int(at(settings, "maxInputs")) > 200 || as_int(at(settings, "maxEntryInputs")) > 199)
        fail("Ladder input horizons exceed 200");
    return object(std::move(settings));
}

}  // namespace pkc
