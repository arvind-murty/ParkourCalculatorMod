// Port of InteractionGate.analyze_model's build and InteractionGate.required
// with query_interaction/block_interaction/interaction_reachability for the
// single-region alternatives it asks. Proofs must equal the Python's,
// including their digests and int/float kinds.
#include "pkc/screen.hpp"


#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "py_math.hpp"
#include "pkc/canonical.hpp"
#include "pkc/envelopes.hpp"
#include "value_ops.hpp"

namespace pkc {
namespace {

const double kHalfWidth = static_cast<double>(static_cast<float>(0.6)) / 2;  // scene/route.HALF_WIDTH
const double kBodyHeight = static_cast<double>(static_cast<float>(1.8));    // scene/geometry.BODY_HEIGHT
constexpr std::int64_t kProofPolicy = 5;                                     // service.PROOF_POLICY

std::array<Num, 6> expanded_box(const Box& box) {  // scene/geometry.expanded_box
    return {sub(box[0], flt(kHalfWidth)), sub(box[1], flt(kBodyHeight)), sub(box[2], flt(kHalfWidth)),
            add(box[3], flt(kHalfWidth)), box[4], add(box[5], flt(kHalfWidth))};
}

Value box_value(const Box& box) {
    Array out;
    for (const Num& n : box) out.push_back(value(n));
    return array(std::move(out));
}

// interaction_reachability(envelopes, {alternatives: [{regions: [region]}]}) for one
// non-interior region, as query_interaction asks it.
Value region_check(const Value& envelopes, const std::array<Num, 6>& box, std::int64_t tick, const char* phase) {
    Array bounds;
    for (const Num& n : box) bounds.push_back(value(n));
    Value region = object({{"tick", integer_value(tick)}, {"phase", text(phase)}, {"bounds", array(std::move(bounds))},
                           {"interior", boolean(false)}});
    return interaction_reachability(envelopes, object({{"alternatives", array({object({{"regions", array({region})}})})}}));
}

// query_interaction for a required_contact request on one box at one tick.
Value required_contact(const Value& envelopes_value, const std::string& context_key, const Box& box, std::int64_t tick) {
    const Object& envelopes = fields(envelopes_value);
    const char* phases[3] = {"Y", "X", "Z"};
    Array windows;
    for (const char* p : phases) windows.push_back(object({{"tick", integer_value(tick)}, {"phase", text(p)}}));
    Value request = object({{"box", box_value(box)}, {"role", text("required_contact")},
                            {"windows", array(windows)}, {"interior", boolean(false)}});
    std::string id = digest(object({{"policy", integer_value(kProofPolicy)}, {"context", text(context_key)},
                                    {"request", request}}));
    std::size_t count = items(at(envelopes, "phases")).size();
    auto expanded = expanded_box(box);
    Array checked_phases;
    bool absent = true;
    for (int k = 0; k < 3; ++k) {
        Object entry;
        if (tick >= static_cast<std::int64_t>(count)) {
            entry = {{"tick", integer_value(tick)}, {"phase", text(phases[k])}, {"ruledOut", boolean(false)},
                     {"reason", text("window_outside_context")}};
        } else {
            entry = fields(region_check(envelopes_value, expanded, tick, phases[k]));  // dict(checked, **window)
            put(entry, "tick", integer_value(tick));
            put(entry, "phase", text(phases[k]));
        }
        absent = absent && std::get<bool>(at(entry, "ruledOut").data);
        checked_phases.push_back(object(std::move(entry)));
    }
    return object({{"policy", integer_value(kProofPolicy)}, {"id", text(id)}, {"context", text(context_key)},
                   {"role", text("required_contact")}, {"ruledOut", boolean(absent)},
                   {"status", text(absent ? "absent" : "not_ruled_out")},
                   {"reason", text(absent ? "all_scoped_phases_excluded" : "interaction_retained")},
                   {"phases", array(std::move(checked_phases))}, {"owner", kNull}, {"interior", boolean(false)},
                   {"invalidation", text("exact_context_only")}});
}

}  // namespace

Value coefficient_request(const Value& prepared) {
    const Object& route = fields(at(fields(prepared), "route"));
    const Object& plan = fields(at(fields(prepared), "plan"));
    Object message = {{"grounded", at(plan, "grounded")}, {"jump", at(plan, "jump")}, {"inputs", at(route, "inputs")}};
    if (py_truthy(get_or_null(route, "sharedMovement"))) {
        message.emplace_back("sharedMovement", boolean(true));
        const Value* contacts = get(plan, "sceneContacts");
        message.emplace_back("contacts", contacts ? *contacts : array({}));
        for (const char* key : {"sneak", "ladderTicks", "vertical"})
            if (const Value* v = get(plan, key)) message.emplace_back(key, *v);
    }
    return object(std::move(message));
}

GateAnalysis analyze_prepared(const Value& prepared, const Value& coefficient, bool open_terminal,
                              std::optional<std::int64_t> parent_ticks, std::string key) {
    const Object& result = fields(prepared);
    const Object& route = fields(at(result, "route"));
    const Object& plan = fields(at(result, "plan"));
    const Object& vertical = fields(at(result, "vertical"));
    Array rows;
    for (const auto& row : items(at(plan, "walls"))) {
        const Value& name = get_or_null(fields(row), "name");
        bool landing = !is_null(name) && as_text(name).rfind("landing:1:", 0) == 0;
        if (!open_terminal || !landing) rows.push_back(row);
    }
    const Object& initial = fields(at(route, "initial"));
    auto horizontal = [&](const char* field) {
        const Array& v = items(at(initial, field));
        return array({v.at(0), v.at(2)});
    };
    std::int64_t ticks = std::get<std::int64_t>(at(plan, "ticks").data);
    Value ladder;
    if (const Value* listed = get(plan, "ladderTicks")) {
        ladder = *listed;
    } else {
        Array flags(ticks + 1, boolean(false));
        ladder = array(std::move(flags));
    }
    Object model = fields(coefficient);  // dict(coefficient, ticks=..., ...)
    put(model, "ticks", integer_value(ticks));
    put(model, "walls", array(std::move(rows)));
    put(model, "contacts", at(plan, "contacts"));
    put(model, "position", horizontal("position"));
    put(model, "velocity", horizontal("velocity"));
    const Value* threshold = get(route, "physicsThreshold");
    put(model, "threshold", threshold ? *threshold : real(.005));
    put(model, "vertical", at(vertical, "vertical"));
    put(model, "ladderTicks", ladder);
    GateAnalysis analysis{std::move(key), object(std::move(model)), kNull, coefficient, at(vertical, "y")};
    Object envelopes = fields(reachable_envelopes(analysis.model, analysis.y, parent_ticks));
    envelopes.emplace_back("modelKey", text(digest(coefficient)));
    analysis.envelopes = object(std::move(envelopes));
    return analysis;
}

Value required_proof(const GateAnalysis& analysis, const Program& program) {
    const Object& envelopes = fields(analysis.envelopes);
    const Value& contradiction = get_or_null(envelopes, "contradiction");
    if (py_truthy(contradiction)) {
        Object proof = {{"ruledOut", boolean(true)}};
        for (const auto& field : fields(contradiction)) proof.push_back(field);
        return object({{"reason", text("required_interaction_unreachable")},
                       {"input", get_or_null(fields(contradiction), "state")},
                       {"proof", object(std::move(proof))}});
    }
    auto check = [&](const std::vector<Box>& boxes, std::int64_t tick) -> Value {
        Value first = kNull;
        for (const Box& box : boxes) {
            Value proof = required_contact(analysis.envelopes, analysis.key, box, tick);
            if (is_null(first)) first = proof;
            if (std::get<std::string>(at(fields(proof), "status").data) != "absent") return kNull;
        }
        Array listed;
        for (const Box& box : boxes) listed.push_back(box_value(box));
        return object({{"reason", text("required_interaction_unreachable")}, {"input", integer_value(tick)},
                       {"boxes", array(std::move(listed))}, {"proof", first}});
    };
    for (const auto& event : program.profile.contact_events) {
        if (!event.boxes) continue;
        Value proof = check(*event.boxes, event.offset);
        if (!is_null(proof)) return proof;
    }
    for (const auto& event : program.profile.case_events) {
        Value proof = check(event.boxes, event.input);
        if (!is_null(proof)) return proof;
    }
    return kNull;
}

}  // namespace pkc
