// Port of scene/route.py. Routes stay JSON values: their output must equal the
// Python's, key order and int/float kinds included, so arithmetic on values
// that reach a plan uses Num.
#include "pkc/scene/route.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

#include "../schema.hpp"
#include "pkc/constraints/avoidance.hpp"
#include "pkc/constraints/hit_events.hpp"
#include "pkc/movement/contact_combinations.hpp"
#include "pkc/movement/ladder/events.hpp"
#include "pkc/movement/units.hpp"
#include "pkc/scene/objectives.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

const double kHalfWidth = f32(0.6) / 2;  // HALF_WIDTH
const double kRadToDeg = 180.0 / M_PI;   // CPython math.degrees

bool is_bool(const Value& value) { return std::holds_alternative<bool>(value.data); }
bool is_text(const Value& value, std::initializer_list<const char*> allowed) {
    auto* s = std::get_if<std::string>(&value.data);
    return s && std::any_of(allowed.begin(), allowed.end(), [&](const char* a) { return *s == a; });
}

void check_rectangle(const Value* bounds, const char* shape, const char* bound, const char* empty) {
    if (!bounds || !is_list(*bounds) || items(*bounds).size() != 4) fail(shape);
    const Array& b = items(*bounds);
    for (const auto& v : b) number(v, bound);
    if (num(b[0]).value >= num(b[1]).value || num(b[2]).value >= num(b[3]).value) fail(empty);
}

Array nulls(std::size_t n) { return Array(n, kNull); }

Array listed(const Object& object, const char* key) {
    const Value* value = get(object, key);
    return value ? items(*value) : Array{};
}

}  // namespace

bool supports(const Object& block, double x, double z, const std::string& kind) {
    auto overlaps = [&](const Array& b) {
        return x + kHalfWidth > num(b[0]).value && x - kHalfWidth < num(b[1]).value &&
               z + kHalfWidth > num(b[2]).value && z - kHalfWidth < num(b[3]).value;
    };
    if (kind == "run")
        if (const Value* standing = get(block, "standingBounds")) return overlaps(items(*standing));
    if (const Value* center = get(block, "centerBounds")) {
        const Array& b = items(*center);
        return num(b[0]).value < x && x < num(b[1]).value && num(b[2]).value < z && z < num(b[3]).value;
    }
    return overlaps(items(at(block, "bounds")));
}

std::array<Num, 4> center_bounds(const Object& block, const std::string& kind) {
    auto copied = [](const Value& value) {
        const Array& b = items(value);
        return std::array<Num, 4>{num(b[0]), num(b[1]), num(b[2]), num(b[3])};
    };
    auto widened = [](const Value& value) {
        const Array& b = items(value);
        Num half = flt(kHalfWidth);
        return std::array<Num, 4>{sub(num(b[0]), half), add(num(b[1]), half), sub(num(b[2]), half), add(num(b[3]), half)};
    };
    if (kind == "run")
        if (const Value* derived = get(block, "_derivedStandingFootprint")) return copied(*derived);
    if (kind == "run")
        if (const Value* standing = get(block, "standingBounds")) return widened(*standing);
    if (const Value* derived = get(block, "_derivedFootprint")) return copied(*derived);
    if (const Value* center = get(block, "centerBounds")) return copied(*center);
    return widened(at(block, "bounds"));
}

Value prepare_route(const Value& request) {
    if (!is_object(request)) fail("Expected a version 1 block route");
    Object route = fields(request);
    const auto* version = std::get_if<std::int64_t>(&get_or_null(route, "version").data);
    if (!version || *version != 1) fail("Expected a version 1 block route");
    put(route, "objective", objective_spec(get(route, "objective")));
    if (const Value* required = get(route, "terminalSupportRequired"); required && !is_bool(*required))
        fail("terminalSupportRequired must be boolean");
    const Value* threshold = get(route, "physicsThreshold");
    if (number(threshold ? *threshold : real(.005), "physicsThreshold").value < 0)
        fail("physicsThreshold must be nonnegative");
    const Value& blocks_value = get_or_null(route, "blocks");
    if (!is_list(blocks_value) || items(blocks_value).size() < 2)
        fail("Supply a start block and at least one landing block");
    Array blocks;
    for (std::size_t i = 0; i < items(blocks_value).size(); ++i) {
        const Value& block_value = items(blocks_value)[i];
        if (!is_object(block_value)) fail("Invalid block");
        Object block = fields(block_value);
        const Value& id = setdefault(block, "id", text(std::to_string(i)));
        auto* id_text = std::get_if<std::string>(&id.data);
        if (!id_text || id_text->empty()) fail("Block IDs must be nonempty strings");
        if ((get(block, "bounds") != nullptr) == (get(block, "centerBounds") != nullptr))
            fail("Supply either collision bounds or an explicit center region");
        const Value* bounds = get(block, "bounds");
        check_rectangle(bounds ? bounds : get(block, "centerBounds"), "Block bounds must be [minX, maxX, minZ, maxZ]",
                        "block bound", "Block bounds are empty");
        if (const Value* standing = get(block, "standingBounds"))
            check_rectangle(standing, "Standing support needs four bounds", "standing bound", "Standing support is empty");
        number(get_or_null(block, "top"), "block top");
        blocks.push_back(object(std::move(block)));
    }
    put(route, "blocks", array(blocks));
    const Value& airtimes_value = get_or_null(route, "airtimes");
    if (!is_list(airtimes_value) || items(airtimes_value).size() != blocks.size() - 1)
        fail("Supply one valid airtime per transition");
    const Array airtimes = items(airtimes_value);
    const std::size_t legs = airtimes.size();
    const Value kinds_value = setdefault(route, "kinds", array(Array(legs, text("jump"))));
    if (!is_list(kinds_value) || items(kinds_value).size() != legs ||
        std::any_of(items(kinds_value).begin(), items(kinds_value).end(),
                    [](const Value& k) { return !is_text(k, {"jump", "walk_off", "step", "movement"}); }))
        fail("Transition kinds must be jump, walk_off, step or movement");
    const Array& kinds = items(kinds_value);
    std::vector<std::int64_t> durations;
    for (std::size_t i = 0; i < legs; ++i) {
        const std::string& kind = as_text(kinds[i]);
        durations.push_back(integer(airtimes[i], "airtime", kind == "jump" ? 2 : 1, 200));
        if (kind == "step" && durations.back() != 1) fail("A prescribed step takes one grounded movement input");
    }
    Array default_phases;
    for (const auto& k : kinds) default_phases.push_back(text(as_text(k) == "step" ? "post" : "pre"));
    const Value phases = setdefault(route, "landingPhases", array(default_phases));
    if (!is_list(phases) || items(phases).size() != legs ||
        std::any_of(items(phases).begin(), items(phases).end(), [](const Value& p) { return !is_text(p, {"pre", "post"}); }))
        fail("Landing phases must be pre or post");
    const Value entries = setdefault(route, "contactEntries", array(nulls(legs)));
    if (!is_list(entries) || items(entries).size() != legs) fail("Supply one optional contact entry per transition");
    for (std::size_t i = 0; i < legs; ++i) {
        const Value& entry_value = items(entries)[i];
        if (is_null(entry_value)) continue;
        if (!is_object(entry_value)) fail("Invalid contact entry face");
        const Object& entry = fields(entry_value);
        const auto* direction = std::get_if<std::int64_t>(&get_or_null(entry, "direction").data);
        if (!is_text(get_or_null(entry, "axis"), {"X", "Z"}) || !direction || (*direction != -1 && *direction != 1))
            fail("Invalid contact entry face");
        std::int64_t from = integer(get_or_null(entry, "outsideFrom"), "outsideFrom", 0, durations[i] - 1);
        std::int64_t through = integer(get_or_null(entry, "outsideThrough"), "outsideThrough", from, durations[i] - 1);
        integer(get_or_null(entry, "enter"), "entry state", through + 1, durations[i]);
    }
    const Value exits = setdefault(route, "walkOffExits", array(nulls(legs)));
    if (!is_list(exits) || items(exits).size() != legs) fail("Supply one optional run-off exit per transition");
    for (std::size_t i = 0; i < legs; ++i) {
        const Value& exit_value = items(exits)[i];
        if (is_null(exit_value)) continue;
        if (as_text(kinds[i]) != "walk_off" || !is_object(exit_value) ||
            !is_text(get_or_null(fields(exit_value), "axis"), {"X", "Z"}))
            fail("Invalid run-off exit");
        const auto* direction = std::get_if<std::int64_t>(&get_or_null(fields(exit_value), "direction").data);
        if (!direction || (*direction != -1 && *direction != 1)) fail("Run-off direction must be -1 or 1");
    }
    const Value& initial_value = get_or_null(route, "initial");
    if (!is_object(initial_value)) fail("Supply an initial state");
    Object initial = fields(initial_value);
    for (const char* key : {"position", "velocity"}) {
        const Value& values = get_or_null(initial, key);
        if (!is_list(values) || items(values).size() != 3) fail(std::string("Initial ") + key + " must be [X,Y,Z]");
        for (const auto& v : items(values)) number(v, std::string("initial ") + key);
    }
    number(setdefault(initial, "yaw", real(0.0)), "initial yaw");
    const Array& position = items(at(initial, "position"));
    const Object& start = fields(blocks[0]);
    if (std::abs(sub(num(position[1]), num(get_or_null(start, "top"))).value) > 1e-7)
        fail("The initial feet height must equal the start block top");
    if (!supports(start, num(position[0]).value, num(position[2]).value))
        fail("The initial player must overlap the start block");
    put(route, "initial", object(std::move(initial)));
    const Value& inputs_value = setdefault(route, "inputs", object({}));
    if (!is_object(inputs_value)) fail("Invalid input policy");
    Object inputs = fields(inputs_value);
    for (auto [key, fallback] : {std::pair{"forward", f32(0.98)}, std::pair{"strafe", 0.0}})
        if (std::abs(number(setdefault(inputs, key, real(fallback)), key).value) > 1)
            fail("Movement inputs must be at the scaled moveFlying range");
    for (auto [key, fallback] : {std::pair{"sprint", true}, std::pair{"incomingSprint", false}, std::pair{"force45", false}})
        if (!is_bool(setdefault(inputs, key, boolean(fallback)))) fail(std::string(key) + " must be boolean");
    if (std::abs(num(at(inputs, "forward")).value - f32(0.98)) > 1e-7 || num(at(inputs, "strafe")).value != 0)
        fail("The first route family uses forward movement without strafe");
    put(route, "inputs", object(std::move(inputs)));
    const Value total = setdefault(route, "maxTotalRunTicks", integer_value(40));
    integer(total, "maxTotalRunTicks", 0, 1000);
    Array default_bounds;
    for (std::size_t i = 0; i < legs; ++i) default_bounds.push_back(array({integer_value(0), total}));
    const Value run_bounds = setdefault(route, "runBounds", array(default_bounds));
    if (!is_list(run_bounds) || items(run_bounds).size() != legs) fail("Supply one run bound pair per transition");
    for (const auto& bounds : items(run_bounds)) {
        if (!is_list(bounds) || items(bounds).size() != 2) fail("Run bounds must be [min,max]");
        std::int64_t lo = integer(items(bounds)[0], "minimum run ticks", 0, 1000);
        integer(items(bounds)[1], "maximum run ticks", lo, 1000);
    }
    if (number(setdefault(route, "supportClearance", real(1e-6)), "supportClearance").value < 0)
        fail("Support clearance must be nonnegative");
    if (py_truthy(get_or_null(route, "obstacles"))) fail("Obstacle scenes require the integration replay adapter");
    for (const char* field : {"ladderTicks", "ladderPattern", "contacts", "vertical"})
        if (get(route, field))
            fail(std::string(field) + " needs a retiming adapter; it cannot be omitted from the compiled request");
    const Value constraints_value = setdefault(route, "constraints", array({}));
    if (!is_list(constraints_value)) fail("Constraints must be a list");
    Array constraints;
    for (const auto& row_value : items(constraints_value)) {
        if (!is_object(row_value)) fail("Invalid route constraint");
        Object row = fields(row_value);
        integer(get_or_null(row, "leg"), "constraint leg", 0, legs - 1);
        integer(setdefault(row, "offset", integer_value(0)), "constraint offset", 0, 1000);
        const Value* phase = get(row, "phase");
        if ((phase && !is_text(*phase, {"departure", "run"})) || !is_text(get_or_null(row, "mode"), {"X", "Z"}))
            fail("Only departure/run X/Z constraints are supported");
        if (!is_text(get_or_null(row, "cmp"), {"GE", "LE", "EQ"})) fail("Invalid route comparison");
        number(get_or_null(row, "rhs"), "constraint rhs");
        constraints.push_back(object(std::move(row)));
    }
    put(route, "constraints", array(std::move(constraints)));
    validate_passages(route);
    validate_hits(route);
    validate_ladder_events(route);
    validate_contact_events(route);
    return object(std::move(route));
}

Value schedule(const Value& route_value, const std::vector<std::int64_t>& allocation, const std::string& stage) {
    const Object& route = fields(route_value);
    const Array& airtimes = items(at(route, "airtimes"));
    const Array& kinds = items(at(route, "kinds"));
    const Array& blocks = items(at(route, "blocks"));
    if ((stage != "jump" && stage != "run") || allocation.empty() || allocation.size() > airtimes.size())
        fail("Invalid allocation or stage");
    const Array profiles = listed(route, "verticalProfiles");
    Array grounded, jump, events;
    std::vector<std::tuple<std::int64_t, std::int64_t, std::string>> supports_at = {{0, 0, "initial"}};
    std::vector<std::pair<std::int64_t, std::int64_t>> exit_checks;
    std::int64_t state = 0;
    const Value* required = get(route, "terminalSupportRequired");
    const bool terminal = !required || py_truthy(*required);
    for (std::size_t i = 0; i < allocation.size(); ++i) {
        const Array& bounds = items(items(at(route, "runBounds"))[i]);
        std::int64_t extra = integer(integer_value(allocation[i]), "run ticks", as_int(bounds[0]), as_int(bounds[1]));
        std::int64_t landing = state;
        for (std::int64_t k = 0; k < extra; ++k) {
            supports_at.emplace_back(state, i, "run");
            grounded.push_back(boolean(true));
            jump.push_back(boolean(false));
            ++state;
        }
        std::int64_t fire = state;
        const std::string& kind = as_text(kinds[i]);
        auto event = [&](const Value& landed_at) {
            return object({{"leg", integer_value(i)}, {"landed", integer_value(landing)}, {"jump", integer_value(fire)},
                           {"landing", landed_at}, {"runTicks", integer_value(extra)}, {"kind", kinds[i]}});
        };
        if (stage == "run" && i == allocation.size() - 1) {
            events.push_back(event(kNull));
            break;
        }
        std::int64_t airtime = as_int(airtimes[i]);
        const Object empty;
        const Object& profile = i < profiles.size() ? fields(profiles[i]) : empty;
        if (const Value* listed_grounded = get(profile, "grounded")) {
            const Array& values = items(*listed_grounded);
            grounded.insert(grounded.end(), values.begin(), values.end() - (values.empty() ? 0 : 1));
        } else {
            grounded.push_back(boolean(true));
            for (std::int64_t k = 1; k < airtime; ++k) grounded.push_back(boolean(false));
        }
        if (const Value* listed_jump = get(profile, "jump")) {
            const Array& values = items(*listed_jump);
            jump.insert(jump.end(), values.begin(), values.end());
        } else {
            jump.push_back(boolean(kind == "jump"));
            for (std::int64_t k = 1; k < airtime; ++k) jump.push_back(boolean(false));
        }
        if (kind == "walk_off") {
            if (fire > 0) supports_at.emplace_back(fire - 1, i, "run");
            exit_checks.emplace_back(fire, i);
        }
        state += airtime;
        if (terminal || i + 1 < airtimes.size()) {
            bool post = as_text(items(at(route, "landingPhases"))[i]) == "post";
            supports_at.emplace_back(post ? state : state - 1, i + 1, "landing");
        }
        events.push_back(event(integer_value(state)));
    }
    std::int64_t allocated = 0;
    for (auto a : allocation) allocated += a;
    if (allocated > as_int(at(route, "maxTotalRunTicks"))) fail("Allocation exceeds the total run-tick limit");
    Array walls;
    const Num clearance = num(at(route, "supportClearance"));
    std::set<std::tuple<std::int64_t, std::int64_t, std::string>> checked(supports_at.begin(), supports_at.end());
    for (const auto& [tick, index, kind] : checked) {
        auto bounds = center_bounds(fields(blocks.at(index)), kind);
        for (int a = 0; a < 2; ++a) {
            const char* axis = a == 0 ? "X" : "Z";
            Num lower = add(flt(std::nextafter(bounds[2 * a].value, INFINITY)), clearance);
            Num upper = sub(flt(std::nextafter(bounds[2 * a + 1].value, -INFINITY)), clearance);
            std::string name = kind + ":" + std::to_string(index) + ":" + axis;
            walls.push_back(object({{"mode", text(axis)}, {"t1", integer_value(tick)}, {"cmp", text("GE")},
                                    {"rhs", value(lower)}, {"name", text(name + ":lo")}}));
            walls.push_back(object({{"mode", text(axis)}, {"t1", integer_value(tick)}, {"cmp", text("LE")},
                                    {"rhs", value(upper)}, {"name", text(name + ":hi")}}));
        }
    }
    for (const auto& [tick, index] : exit_checks) {
        auto source = center_bounds(fields(blocks.at(index)), "run");
        auto target = center_bounds(fields(blocks.at(index + 1)));
        const Value& exit_value = items(at(route, "walkOffExits"))[index];
        std::string axis;
        std::int64_t direction;
        if (is_null(exit_value)) {
            Num dx = sub(sub(add(target[0], target[1]), source[0]), source[1]);
            Num dz = sub(sub(add(target[2], target[3]), source[2]), source[3]);
            bool along_x = std::abs(dx.value) > std::abs(dz.value);
            axis = along_x ? "X" : "Z";
            direction = (along_x ? dx : dz).value >= 0 ? 1 : -1;
        } else {
            axis = as_text(at(fields(exit_value), "axis"));
            direction = as_int(at(fields(exit_value), "direction"));
        }
        int k = axis == "X" ? 0 : 2;
        Num edge = direction > 0 ? source[k + 1] : source[k];
        walls.push_back(object({{"mode", text(axis)}, {"t1", integer_value(tick)},
                                {"cmp", text(direction > 0 ? "GE" : "LE")},
                                {"rhs", value(add(edge, mul(whole(direction), clearance)))},
                                {"name", text("walk_off:" + std::to_string(index))}}));
    }
    for (const auto& event_value : events) {
        const Object& event = fields(event_value);
        if (is_null(get_or_null(event, "landing"))) continue;
        std::int64_t leg = as_int(at(event, "leg")), fire = as_int(at(event, "jump"));
        const Value& entry_value = items(at(route, "contactEntries"))[leg];
        if (is_null(entry_value)) continue;
        const Object& entry = fields(entry_value);
        auto target = center_bounds(fields(blocks.at(leg + 1)));
        const std::string& axis = as_text(at(entry, "axis"));
        std::int64_t direction = as_int(at(entry, "direction"));
        int k = axis == "X" ? 0 : 2;
        Num edge = direction > 0 ? target[k + 1] : target[k];
        std::string outside = "contact_outside:" + std::to_string(leg);
        for (std::int64_t offset = as_int(at(entry, "outsideFrom")); offset <= as_int(at(entry, "outsideThrough")); ++offset)
            walls.push_back(object({{"mode", text(axis)}, {"t1", integer_value(fire + offset)},
                                    {"cmp", text(direction > 0 ? "GE" : "LE")},
                                    {"rhs", value(add(edge, mul(whole(direction), clearance)))}, {"name", text(outside)}}));
        std::int64_t tick = fire + as_int(at(entry, "enter"));
        supports_at.emplace_back(tick, leg + 1, "contact");
        std::string enter = "contact_enter:" + std::to_string(leg);
        for (int a = 0; a < 2; ++a) {
            const char* name = a == 0 ? "X" : "Z";
            walls.push_back(object({{"mode", text(name)}, {"t1", integer_value(tick)}, {"cmp", text("GE")},
                                    {"rhs", value(add(target[2 * a], clearance))}, {"name", text(enter)}}));
            walls.push_back(object({{"mode", text(name)}, {"t1", integer_value(tick)}, {"cmp", text("LE")},
                                    {"rhs", value(sub(target[2 * a + 1], clearance))}, {"name", text(enter)}}));
        }
    }
    for (const auto& row_value : items(at(route, "constraints"))) {
        const Object& row = fields(row_value);
        std::int64_t leg = as_int(at(row, "leg"));
        if (leg >= static_cast<std::int64_t>(events.size())) continue;
        const Object& event = fields(events[leg]);
        bool run = py_equal(get_or_null(row, "phase"), text("run"));
        std::int64_t offset = as_int(at(row, "offset"));
        if (run) {
            if (offset >= as_int(at(event, "runTicks"))) continue;
        } else if (is_null(get_or_null(event, "landing"))) {
            continue;
        }
        std::int64_t tick = (run ? as_int(at(event, "landed")) : as_int(at(event, "jump"))) + offset;
        if (!run && tick > as_int(at(event, "landing"))) fail("Constraint lies beyond its transition");
        const Value* name = get(row, "name");
        walls.push_back(object({{"mode", at(row, "mode")}, {"t1", integer_value(tick)}, {"cmp", at(row, "cmp")},
                                {"rhs", at(row, "rhs")},
                                {"name", name ? *name : text("route:" + std::to_string(leg))}}));
    }
    auto [passage_rows, passage_failures] = compile_passages(route, events);
    walls.insert(walls.end(), passage_rows.begin(), passage_rows.end());
    CompiledHits hits = compile_hits(route, events);
    walls.insert(walls.end(), hits.rows.begin(), hits.rows.end());
    Array contacts = hits.contacts, scene_contacts = hits.scene_contacts;
    if (py_truthy(get_or_null(route, "ladderEvents"))) throw Unsupported("ladder events are scheduled in Python");
    Object extra;
    std::vector<bool> ladder, sneak;
    bool masked = false;  // extra.setdefault('ladderTicks' / 'sneak') ran
    const Array route_events = listed(route, "contactEvents");
    for (std::size_t i = 0; i < std::min(events.size(), profiles.size()); ++i) {
        const Object& event = fields(events[i]);
        if (is_null(get_or_null(event, "landing"))) continue;
        const Object& profile = fields(profiles[i]);
        std::int64_t leg = as_int(at(event, "leg")), offset = as_int(at(event, "jump"));
        std::int64_t duration = as_int(airtimes[leg]);
        for (const auto& row : listed(profile, "positionConstraints"))
            walls.push_back(object(with(fields(row), "t1", integer_value(offset + as_int(at(fields(row), "t1"))))));
        for (const auto& case_value : listed(profile, "caseEvents")) {
            const Object& c = fields(case_value);
            const Value& kind = at(c, "case");
            if (is_text(kind, {"normal_step", "slab_boost"})) {
                bool owned = std::any_of(route_events.begin(), route_events.end(), [&](const Value& e_value) {
                    const Object& e = fields(e_value);
                    if (as_int(at(e, "leg")) != leg || !py_equal(get_or_null(e, "role"), text("step")) ||
                        !py_equal(at(e, "offset"), at(c, "input")) || !py_equal(at(e, "plane"), at(c, "plane")))
                        return false;
                    const Array& event_boxes = items(at(e, "boxes"));
                    for (const auto& b : items(at(c, "boxes")))
                        if (contains(event_boxes, b)) return true;
                    return false;
                });
                if (owned) continue;
            }
            scene_contacts.push_back(object({{"state", integer_value(offset + as_int(at(c, "input")) + 1)},
                                             {"axis", integer_value(1)}, {"direction", integer_value(-1)},
                                             {"plane", at(c, "plane")}, {"boxes", at(c, "boxes")},
                                             {"movementCase", kind}}));
        }
        std::vector<bool> occupied(duration + 1, false), selected(duration, false);
        if (const Value* flags = get(profile, "ladderTicks")) {
            occupied.clear();
            for (const auto& v : items(*flags)) occupied.push_back(py_truthy(v));
        }
        if (const Value* flags = get(profile, "sneak")) {
            selected.clear();
            for (const auto& v : items(*flags)) selected.push_back(py_truthy(v));
        }
        if (static_cast<std::int64_t>(occupied.size()) != duration + 1 ||
            static_cast<std::int64_t>(selected.size()) != duration)
            fail("Profile medium/input arrays have inconsistent lengths");
        auto any = [](const std::vector<bool>& flags) { return std::find(flags.begin(), flags.end(), true) != flags.end(); };
        if (any(occupied) || any(selected)) {
            if (!masked) {
                ladder.assign(state + 1, false);
                sneak.assign(state, false);
                masked = true;
            }
            for (std::size_t t = 0; t < occupied.size(); ++t) ladder.at(offset + t) = ladder.at(offset + t) || occupied[t];
            for (std::size_t t = 0; t < selected.size(); ++t) sneak.at(offset + t) = sneak.at(offset + t) || selected[t];
        }
    }
    if (masked) {
        Array l, s;
        for (bool v : ladder) l.push_back(boolean(v));
        for (bool v : sneak) s.push_back(boolean(v));
        put(extra, "ladderTicks", array(std::move(l)));
        put(extra, "sneak", array(std::move(s)));
    }
    CompiledContacts compiled = compile_contact_events(route, events, state);
    walls.insert(walls.end(), compiled.rows.begin(), compiled.rows.end());
    contacts.insert(contacts.end(), compiled.horizontal.begin(), compiled.horizontal.end());
    scene_contacts.insert(scene_contacts.end(), compiled.scene.begin(), compiled.scene.end());
    Array merged_contacts = merge_contacts(contacts, false);
    Array merged_scene = merge_contacts(scene_contacts, true);
    if (!compiled.vertical.empty()) {
        Array vertical;
        for (const auto& c : compiled.vertical) vertical.push_back(object(with(fields(c), "axis", integer_value(1))));
        put(extra, "verticalContacts", array(merge_contacts(vertical, false)));
    }
    Array relative;
    if (const Value* units_value = get(route, "movementUnits"); units_value && !is_null(*units_value)) {
        relative = items(*units_value);
    } else {
        for (const auto& duration : airtimes) relative.push_back(array(movement_program(as_int(duration))));
    }
    Array units;
    for (const auto& event_value : events) {
        const Object& event = fields(event_value);
        std::int64_t landed = as_int(at(event, "landed")), fire = as_int(at(event, "jump"));
        if (as_int(at(event, "runTicks")))
            units.push_back(object({{"type", text("normal")}, {"start", integer_value(landed)},
                                    {"end", integer_value(fire)}, {"grounded", boolean(true)}}));
        if (!is_null(get_or_null(event, "landing")))
            for (const auto& unit : items(relative.at(as_int(at(event, "leg"))))) units.push_back(shifted_unit(unit, fire));
    }
    put(extra, "movementUnits", array(std::move(units)));
    if (std::any_of(profiles.begin(), profiles.end(), [](const Value& p) {
            return py_equal(get_or_null(fields(p), "composition"), text("movement_program"));
        }))
        put(extra, "movementComposition", boolean(true));
    Array support_checks, exits;
    for (const auto& [tick, index, kind] : supports_at)
        support_checks.push_back(array({integer_value(tick), integer_value(index), text(kind)}));
    for (const auto& [tick, index] : exit_checks) exits.push_back(array({integer_value(tick), integer_value(index)}));
    Array allocated_list;
    for (auto a : allocation) allocated_list.push_back(integer_value(a));
    Object plan = {{"ticks", integer_value(state)},
                   {"grounded", array(std::move(grounded))},
                   {"jump", array(std::move(jump))},
                   {"events", array(std::move(events))},
                   {"supportChecks", array(std::move(support_checks))},
                   {"exitChecks", array(std::move(exits))},
                   {"walls", array(std::move(walls))},
                   {"allocation", array(std::move(allocated_list))},
                   {"stage", text(stage)},
                   {"passageFailures", array(std::move(passage_failures))},
                   {"contacts", array(std::move(merged_contacts))},
                   {"sceneContacts", array(std::move(merged_scene))}};
    for (auto& field : extra) plan.push_back(std::move(field));
    return object(std::move(plan));
}

Value initial_guess(const Value& route_value, const Value& plan_value, const Value* parent) {
    const Object& route = fields(route_value);
    const Object& plan = fields(plan_value);
    const Array& blocks = items(at(route, "blocks"));
    Value yaw = at(fields(at(route, "initial")), "yaw");
    const std::int64_t ticks = as_int(at(plan, "ticks"));
    Array result(ticks, yaw);
    const bool has_parent = parent && py_truthy(*parent);
    const Array* prior = has_parent ? &items(at(fields(at(fields(*parent), "candidate")), "yaw")) : nullptr;
    if (prior)
        for (std::size_t t = 0; t < std::min(prior->size(), result.size()); ++t) result[t] = (*prior)[t];
    for (const auto& event_value : items(at(plan, "events"))) {
        const Object& event = fields(event_value);
        std::int64_t leg = as_int(at(event, "leg"));
        auto target = center_bounds(fields(blocks.at(leg + 1)));
        auto source = center_bounds(fields(blocks.at(leg)));
        double dx = sub(sub(add(target[0], target[1]), source[0]), source[1]).value / 2;
        double dz = sub(sub(add(target[2], target[3]), source[2]), source[3]).value / 2;
        Value facing = dx != 0 || dz != 0 ? real(kRadToDeg * std::atan2(-dx, dz)) : yaw;
        std::int64_t start = as_int(at(event, "landed"));
        const Value& landing = get_or_null(event, "landing");
        std::int64_t end = is_null(landing) ? as_int(at(event, "jump")) : as_int(landing);
        if (prior) start = std::max<std::int64_t>(start, prior->size());
        for (std::int64_t t = start; t < end && t < ticks; ++t) result[t] = facing;
        yaw = facing;
    }
    if (py_truthy(at(fields(at(route, "inputs")), "force45"))) {
        const Array& jump = items(at(plan, "jump"));
        for (std::size_t t = 0; t < result.size(); ++t)
            if ((!prior || t >= prior->size()) && !py_truthy(jump[t])) result[t] = value(add(num(result[t]), whole(45)));
    }
    if (!parent || is_null(*parent))
        if (const Value* warm = get(route, "warmYaw"); warm && py_truthy(*warm))
            for (std::size_t t = 0; t < std::min(items(*warm).size(), result.size()); ++t) result[t] = items(*warm)[t];
    return array(std::move(result));
}

}  // namespace pkc
