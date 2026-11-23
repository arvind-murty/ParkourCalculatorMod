// Port of search/goals.landing_satisfied and _landing_prefixes.
#include "pkc/search/goals.hpp"

#include <cmath>
#include <set>

#include "../schema.hpp"

namespace pkc {
namespace {

bool holds(const std::vector<Box>& boxes, const Array& box) {  // box in e['boxes']
    for (const auto& b : boxes) {
        bool equal = true;
        for (std::size_t k = 0; k < 6; ++k) equal = equal && b[k].value == num(box[k]).value;
        if (equal) return true;
    }
    return false;
}

}  // namespace

bool landing_satisfied(const Value& scene, const std::string& block_id, const Value& vertical_value) {
    const Object* block = nullptr;
    for (const auto& b : items(at(fields(scene), "blocks")))
        if (as_text(at(fields(b), "id")) == block_id) {
            block = &fields(b);
            break;
        }
    if (!block) fail("StopIteration");
    const Object& vertical = fields(vertical_value);
    return py_truthy(items(at(vertical, "onGround")).back()) &&
           std::fabs(num(items(at(vertical, "y")).back()).value - num(at(*block, "top")).value) <= 1e-7;
}

std::vector<LandingPrefix> landing_prefixes(const Value& scene_value, const Program& program, const std::string& block_id,
                                            bool reuse_full) {
    const Object& scene = fields(scene_value);
    const Object* block = nullptr;
    for (const auto& b : items(at(scene, "blocks")))
        if (as_text(at(fields(b), "id")) == block_id) {
            block = &fields(b);
            break;
        }
    if (!block) fail("StopIteration: no block " + block_id);
    const Array& box = items(items(at(fields(at(scene, "collisionScene")), "solids")).at(as_int(at(*block, "solidIndex"))));
    const double top = num(at(*block, "top")).value;
    const Profile& p = program.profile;
    const bool supported = program.terminal_support.value && *program.terminal_support.value == block_id;
    std::set<std::int64_t> times;
    for (const auto& e : p.contact_events)
        if (e.boxes && holds(*e.boxes, box) && e.axis == "Y" && e.direction == -1) times.insert(e.offset + 1);
    for (const auto& e : p.case_events)
        if (holds(e.boxes, box) && (e.movement_case == "normal_step" || e.movement_case == "blip")) times.insert(e.input + 1);
    if (supported) times.insert(p.duration);
    std::vector<LandingPrefix> out;
    for (auto tick : times) {
        if (!(0 < tick && tick <= p.duration) || !p.grounded[tick] || std::fabs(p.position[tick].value - top) > 1e-7) continue;
        const bool reused = reuse_full && tick == p.duration && supported;
        LandingPrefix candidate{reused ? program : truncate_program(program, tick), reused};
        candidate.program.terminal_support.value = block_id;
        bool step = false, landing = false;
        for (const auto& e : p.contact_events)
            if (e.boxes && holds(*e.boxes, box) && e.offset == tick - 1 && e.axis == "Y" && e.direction == -1) landing = true;
        for (const auto& e : p.case_events)
            if (holds(e.boxes, box) && e.input == tick - 1 && (e.movement_case == "normal_step" || e.movement_case == "blip"))
                step = true;
        if (step) {
            candidate.program.profile.landing_phase = "post";
        } else if (landing) {
            candidate.program.profile.landing_phase = "pre";
        }
        out.push_back(std::move(candidate));
    }
    return out;
}

}  // namespace pkc
