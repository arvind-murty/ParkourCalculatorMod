// Typed search programs: the native form of movement/program.py's program and
// movement-choice dicts. Conversion is strict: an unknown key or a wrong type
// in a typed record throws, so schema drift fails loudly instead of being
// dropped. Python's aliasing (profile.position is trace.position) is not kept;
// each struct owns its data, so copying a program is a value copy.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

// A JSON number that remembers whether Python held it as int or float, because
// canonical identity encodes 1 and 1.0 differently. Index fields use int64.
struct Num {
    double value = 0.;
    bool integral = false;
};

// A key that is present but may be JSON null; std::optional fields mark keys
// that may be absent.
template <class T>
struct Nullable {
    std::optional<T> value;
};

using Box = std::array<Num, 6>;     // min x, y, z, max x, y, z
using Region = std::array<Num, 4>;  // tangent rectangle
using Anchors = std::map<std::string, std::int64_t>;

struct ScalarState {  // program initial state, boundary_state()
    bool on_ground = false;
    Num position;
    bool previous_horizontal_collision = false;
    Num velocity;
};

struct PositionConstraint {  // profile.positionConstraints rows, addressed by state
    std::string cmp, mode;
    std::optional<std::string> name;
    Num rhs;
    std::int64_t t1 = 0;
    std::optional<std::int64_t> t2;
};

struct LegConstraint {  // walk-off and region-choice options, addressed by leg and input offset
    std::string cmp, mode, name;
    Num rhs;
    std::int64_t leg = 0, offset = 0;
};

struct VerticalContact {  // profile.contacts, trace.vertical.contacts
    std::int64_t state = 0, direction = 0;
    Num plane;
    std::optional<std::string> movement_case;  // key "case"
    std::optional<Num> clearance, lower_height, higher_height;
};

struct ContactEvent {  // profile.contactEvents
    std::string axis;
    std::int64_t direction = 0, offset = 0;
    std::optional<std::int64_t> leg, state, solid_index;  // state: ladder-ceiling contacts
    std::optional<Num> plane;
    std::optional<Box> box;
    std::optional<std::vector<Box>> boxes;
    std::optional<std::vector<Region>> regions;
    std::optional<Nullable<std::string>> surface;
    std::optional<std::string> role, block_id, effect, owner_region, owner_type, successor_type;
};

struct CaseEvent {  // profile.caseEvents
    std::string type, movement_case;  // key "case"
    std::int64_t input = 0;
    Num plane;
    Anchors anchors;
    std::vector<Box> boxes;
    std::optional<std::vector<Box>> lower_boxes;  // the lower face's boxes
    std::optional<Num> clearance, lower_height, higher_height;
};

// movementUnits / generatedUnits entries. Ladder units add region and state
// windows; a unit's effects are the case units it overlaps, shaped like units
// plus their contact geometry.
struct MovementUnit {
    std::string type;
    std::int64_t start = 0, end = 0;
    std::optional<std::vector<std::int64_t>> grounded_inputs, jump_inputs, sneak_inputs, hit_inputs, hit_window,
        occupied_states, stop_inputs, additional_occupied_states;
    std::optional<std::string> movement_case, region, successor_type;  // movement_case: key "case"
    std::optional<Anchors> anchors;
    std::optional<Num> plane;
    std::optional<std::vector<Box>> boxes;
    std::optional<std::vector<Region>> regions;
    std::optional<Nullable<std::string>> surface;
    std::optional<std::vector<MovementUnit>> effects;
};

struct RegionChoice {
    std::int64_t offset = 0;
    std::string owner, surface;
    std::vector<std::vector<LegConstraint>> options;
};

struct WalkOff {
    std::int64_t offset = 0;
    std::vector<LegConstraint> options;
    std::vector<std::int64_t> solids;
};

struct Profile {
    std::int64_t duration = 0;
    std::vector<Num> position, velocity;
    std::vector<bool> grounded, jump, sneak, ladder_ticks;
    std::vector<VerticalContact> contacts;
    std::vector<ContactEvent> contact_events;
    std::vector<CaseEvent> case_events;
    std::vector<PositionConstraint> position_constraints;
    std::optional<std::vector<std::int64_t>> constraint_owners;
    std::vector<MovementUnit> generated_units;
    std::optional<std::vector<RegionChoice>> region_choices;
    std::string composition, landing_phase;
    std::optional<Num> threshold;
};

struct TraceResume {
    bool on_ground = false, on_ladder = false, previous_horizontal_collision = false;
    Num position, velocity;
};

struct VerticalTrace {
    std::vector<VerticalContact> contacts;
    std::vector<Nullable<Num>> jump_velocity;
    std::vector<bool> sneak;
    bool on_ground = false, previous_horizontal_collision = false;
    Num position, velocity, threshold;
    std::string profile;
};

struct Trace {  // trace_ladder_schedule output
    std::string status;
    std::int64_t ticks = 0;
    std::vector<Num> position, velocity, movement;
    std::vector<bool> on_ground, ladder_ticks, hit, sneak, climb_refreshed;
    std::vector<Value> failures;
    TraceResume resume;
    VerticalTrace vertical;
};

struct Program {
    ScalarState initial;
    Profile profile;
    std::vector<MovementUnit> movement_units;
    std::vector<Value> ladder_events;  // ladder schedules stay opaque until a unit computes on them
    std::string terminal;
    Nullable<std::string> terminal_support;
    std::int64_t interactions = 0;
    Nullable<Trace> trace;
    // Keys append_choice carries into a child.
    std::optional<Num> threshold;
    std::optional<std::vector<Num>> guide_seed_yaw;
    std::optional<std::int64_t> committed_through, settled_through;
    std::optional<std::vector<WalkOff>> walk_offs;
    std::optional<Value> repair_state, solve_scope;
    // Keys append_choice sets: the parent's duration and its prefix chain ([state, hex], or null).
    std::optional<std::int64_t> parent_ticks;
    std::optional<Value> prefix_chain;
    // Search-layer keys (decision, comparison and repair bookkeeping) that
    // search/ adds to programs; carried unchanged until search/ is native.
    Object annotations;
};

struct MovementChoice {  // movement_choice() output, consumed by append_choice
    MovementUnit unit;
    std::vector<MovementUnit> units;
    ScalarState initial;
    Profile profile;
    std::vector<Value> ladder_events;
    Nullable<std::string> terminal_support;
    bool interaction = false;
};

// Throw std::invalid_argument naming the offending path.
Program program_from_value(const Value& value);
MovementChoice choice_from_value(const Value& value);
Value to_value(const Program& program);
Value to_value(const MovementChoice& choice);
Value to_value(const Profile& profile);

// Typed records from their dict lists (strict, as program_from_value), for builders that assemble them as dicts.
std::vector<PositionConstraint> position_constraints_from_value(const Value& value);
std::vector<ContactEvent> contact_events_from_value(const Value& value);
std::vector<CaseEvent> case_events_from_value(const Value& value);
std::vector<RegionChoice> region_choices_from_value(const Value& value);

// Thrown for inputs a native unit leaves to Python (a refined interaction screen
// with a model, a ceiling a ladder climb owns outside a ladder append); the
// caller runs the Python.
struct Unsupported : std::logic_error {
    using std::logic_error::logic_error;
};

// movement/program.py append_choice: the child, or nullopt when the choice does
// not compose. Errors throw std::invalid_argument with the Python's message.
std::optional<Program> append_choice(const Program& program, const MovementChoice& choice,
                                     std::int64_t max_inputs = 200);

// movement/program.py program_physics_key (hex).
std::string program_physics_key(const Program& program);

// movement/program.py boundary_state.
ScalarState boundary_state(const Program& program, bool on_ladder = false);

// movement/units.profile_program on a typed profile.
std::vector<MovementUnit> profile_units(const Profile& profile);

// movement/program.py movement_choice without given units or ladder events (the open generators'
// form): nullopt for an invalid trace.
std::optional<MovementChoice> movement_choice(const std::string& kind, const ScalarState& initial, const Trace& trace,
                                              std::vector<ContactEvent> contacts, std::vector<CaseEvent> cases,
                                              std::vector<PositionConstraint> constraints,
                                              std::optional<std::string> support, const std::string& landing_phase,
                                              std::vector<RegionChoice> region_choices);

}  // namespace pkc
