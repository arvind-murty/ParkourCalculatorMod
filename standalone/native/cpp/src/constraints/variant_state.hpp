// ConstraintVariants' saved frontier, typed: the dict constraints/variants.py
// keeps in self.state, shared with the encounter covers that extend it.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pkc/constraints/variants.hpp"
#include "pkc/value.hpp"

namespace pkc {

// One partial side walk of an encounter cover (encounter_option's pending entries).
struct EncounterWalk {
    std::int64_t at = 0;
    Array rows;
    std::vector<std::int64_t> history;
    Array spans;  // [first phase, last phase, side index]
};

// A node's option cursor: {index} for an item with rows, else an encounter cover's walk.
struct VariantCursor {
    std::optional<std::int64_t> index;
    std::optional<std::vector<EncounterWalk>> pending;
    std::optional<std::vector<std::string>> seen, completed;
    std::optional<Array> covers;  // completed rows lists
};

struct VariantNode {
    std::int64_t item = 0;
    std::optional<std::vector<std::int64_t>> remaining;
    Array rows, passages, choices;
    std::optional<std::int64_t> chosen_item;
    std::optional<Value> prepared, selected;
    std::optional<VariantCursor> cursor;
};

struct ConstraintVariants::State {
    std::string context;
    Array items, exclusions, screen_exclusions;
    std::vector<VariantNode> pending;
    std::vector<std::string> seen, partial_seen;
    std::int64_t emitted = 0, preparation_steps = 0, duplicate_variants = 0;
    bool exhausted = false;
    Value reference;
    std::optional<Obstacles> obstacles;
    std::vector<std::pair<std::string, std::int64_t>> metrics;
    Array uncertain_interactions, encounter_log, dropped_options;
    double preparation_seconds = 0., generation_seconds = 0., analysis_seconds = 0.;
};

// encounters.prepare_encounter(owner, item, envelopes) on an item copy: the prepared item, or the item itself when
// it already has phases.
Value prepare_encounter(ConstraintVariants& owner, const Value& item, const Value& envelopes);
// encounters.encounter_option(owner, item, cursor, envelopes) for a prepared item.
std::optional<Value> encounter_option(ConstraintVariants& owner, const Value& item, VariantCursor& cursor,
                                      const Value& envelopes);
// encounters.canonical_rows and rows_entailed.
Array canonical_rows(const Array& rows);
bool rows_entailed(const Value& envelopes, const Array& selected, const Array& required);

}  // namespace pkc
