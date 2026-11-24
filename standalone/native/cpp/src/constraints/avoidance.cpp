// Port of constraints/avoidance.py's route validation and rows.
#include "pkc/constraints/avoidance.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "../schema.hpp"

namespace pkc {
namespace {

int order(const std::string& axis) { return axis == "Y" ? 0 : axis == "X" ? 1 : 2; }  // ORDER

bool consecutive(const Object& first, const Object& second) {
    return order(as_text(at(first, "axis"))) > order(as_text(at(second, "axis")));
}

std::optional<std::vector<std::pair<std::int64_t, std::int64_t>>> intervals(const Array& chain, const Array& gates,
                                                                            std::int64_t begin, std::int64_t end) {
    std::int64_t start = begin;
    std::vector<std::pair<std::int64_t, std::int64_t>> result;
    for (std::size_t i = 0; i < gates.size(); ++i) {
        std::int64_t gate = as_int(gates[i]);
        if (gate < start || gate > end) return std::nullopt;
        result.emplace_back(start, gate);
        start = gate + consecutive(fields(chain[i]), fields(chain[i + 1]));
    }
    if (start > end) return std::nullopt;
    result.emplace_back(start, end);
    return result;
}

Array listed(const Object& route, const char* key) {
    const Value* value = get(route, key);
    return value ? items(*value) : Array{};
}

}  // namespace

Num face_clearance(const Object& route, std::int64_t leg, std::int64_t offset, const Object& face) {
    Array hits = listed(route, "hitEvents");
    for (const auto& e : listed(route, "contactEvents")) hits.push_back(e);
    for (const auto& hit_value : hits) {
        const Object& hit = fields(hit_value);
        if (as_int(at(hit, "leg")) == leg && as_int(at(hit, "offset")) + 1 == offset &&
            py_equal(at(hit, "axis"), at(face, "axis")) && py_equal(at(hit, "plane"), at(face, "plane")) &&
            num(at(hit, "direction")).value == -num(at(face, "direction")).value)
            return flt(0.);
    }
    for (const auto& episode_value : listed(route, "ladderEvents")) {
        const Object& episode = fields(episode_value);
        const Object& wall = fields(at(episode, "wall"));
        const Array& hits = items(at(fields(at(episode, "schedule")), "hit"));
        if (as_int(at(episode, "leg")) == leg && 0 < offset && offset <= static_cast<std::int64_t>(hits.size()) &&
            py_truthy(hits[offset - 1]) && py_equal(at(wall, "axis"), at(face, "axis")) &&
            py_equal(at(wall, "plane"), at(face, "plane")) &&
            num(at(wall, "direction")).value == -num(at(face, "direction")).value)
            return flt(0.);
    }
    return flt(kSolverMargin);
}

std::vector<std::int64_t> phase_states(std::int64_t tick, const std::string& phase, const std::string& axis) {
    if (axis == "X") {
        if (phase == "Y") return {tick};
        if (phase == "X") return {tick, tick + 1};
        return {tick + 1};
    }
    if (axis == "Z") return phase == "Z" ? std::vector<std::int64_t>{tick, tick + 1} : std::vector<std::int64_t>{tick};
    return phase == "Y" ? std::vector<std::int64_t>{tick, tick + 1} : std::vector<std::int64_t>{tick + 1};
}

Value blockage_counts(const Array& chain) {
    std::int64_t horizontal = 0;
    for (std::size_t i = 0; i + 1 < chain.size(); ++i)
        horizontal += as_text(at(fields(chain[i]), "axis")) != "Y" && as_text(at(fields(chain[i + 1]), "axis")) != "Y";
    const auto total = static_cast<std::int64_t>(chain.size()) - 1;
    return object({{"horizontal", integer_value(horizontal)}, {"vertical", integer_value(total - horizontal)},
                   {"total", integer_value(total)}});
}

std::string passage_kind(const Array& chain) {
    const auto count = std::get<std::int64_t>(at(fields(blockage_counts(chain)), "horizontal").data);
    return count == 1 ? "cross_neo" : count == 2 ? "wide_neo" : count == 0 ? "vertical_passage" : "long_neo";
}

void validate_passages(Object& route) {
    erase(route, "passageCaps");
    const Value& passages = setdefault(route, "passages", array({}));
    if (!is_list(passages)) fail("Passages must be a list");
    const Array& airtimes = items(at(route, "airtimes"));
    for (const auto& passage_value : items(passages)) {
        const Value& leg_value = is_object(passage_value) ? get_or_null(fields(passage_value), "leg") : kNull;
        const auto* leg = std::get_if<std::int64_t>(&leg_value.data);
        if (!leg || *leg < 0 || *leg >= static_cast<std::int64_t>(airtimes.size())) fail("Invalid passage leg");
        const Object& passage = fields(passage_value);
        const Value& chain = get_or_null(passage, "faces");
        const Value& gates = get_or_null(passage, "gates");
        if (!is_list(chain) || items(chain).empty() || !is_list(gates) ||
            items(gates).size() != items(chain).size() - 1)
            fail("Invalid blockage chain");
        for (const auto& face_value : items(chain)) {
            if (!is_object(face_value)) fail("Invalid blockage face");
            const Object& face = fields(face_value);
            const auto* axis = std::get_if<std::string>(&get_or_null(face, "axis").data);
            const auto* direction = std::get_if<std::int64_t>(&get_or_null(face, "direction").data);
            if (!axis || (*axis != "X" && *axis != "Y" && *axis != "Z") || !direction ||
                (*direction != -1 && *direction != 1))
                fail("Invalid blockage face");
            const Value& plane = get_or_null(face, "plane");
            double v;
            if (std::holds_alternative<bool>(plane.data) || !numeric(plane, v) || !std::isfinite(v))
                fail("Invalid blockage plane");
        }
        for (std::size_t i = 0; i + 1 < items(chain).size(); ++i)
            if (py_equal(at(fields(items(chain)[i]), "axis"), at(fields(items(chain)[i + 1]), "axis")))
                fail("A blockage joins faces on different axes");
        for (const auto& k : items(gates)) {
            const auto* gate = std::get_if<std::int64_t>(&k.data);
            if (!gate || *gate < -1000 || *gate > as_int(airtimes[*leg])) fail("Invalid blockage gate offset");
        }
        if (const Value* id = get(passage, "blockId")) {
            const auto* text_value = std::get_if<std::string>(&id->data);
            if (!text_value || text_value->empty()) fail("Invalid passage block ID");
        }
    }
}

std::pair<Array, Array> compile_passages(const Object& route, const Array& events) {
    Array rows, failures;
    for (const auto& passage_value : items(at(route, "passages"))) {
        const Object& passage = fields(passage_value);
        std::int64_t leg = as_int(at(passage, "leg"));
        if (leg >= static_cast<std::int64_t>(events.size())) continue;
        const Object& event = fields(events[leg]);
        if (is_null(get_or_null(event, "landing"))) continue;
        std::int64_t jump = as_int(at(event, "jump"));
        const Array& chain = items(at(passage, "faces"));
        auto ranges = intervals(chain, items(at(passage, "gates")), -as_int(at(event, "runTicks")),
                                as_int(at(event, "landing")) - jump);
        if (!ranges) {
            failures.push_back(object({{"leg", integer_value(leg)}, {"reason", text("passage_timing")}}));
            continue;
        }
        const Value& solid = get_or_null(passage, "solidIndex");
        std::string name = "blockage:" + (is_null(solid) ? std::string("None") : std::to_string(as_int(solid)));
        for (std::size_t i = 0; i < chain.size(); ++i) {
            const Object& face = fields(chain[i]);
            const std::string& axis = as_text(at(face, "axis"));
            if (axis == "Y") continue;
            std::int64_t direction = as_int(at(face, "direction"));
            for (std::int64_t offset = (*ranges)[i].first; offset <= (*ranges)[i].second; ++offset) {
                Num rhs = add(num(at(face, "plane")),
                              mul(whole(direction), face_clearance(route, leg, offset, face)));
                rows.push_back(object({{"mode", text(axis)}, {"t1", integer_value(jump + offset)},
                                       {"cmp", text(direction > 0 ? "GE" : "LE")}, {"rhs", value(rhs)},
                                       {"name", text(name)}}));
            }
        }
    }
    return {rows, failures};
}

}  // namespace pkc
