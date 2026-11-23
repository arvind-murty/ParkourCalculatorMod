// Task goals and landing prefixes of a program on a goal block: search/goals.py.
#pragma once

#include <string>
#include <vector>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

struct LandingPrefix {
    Program program;
    bool reused = false;  // reuse_full gave the program itself back (the Python yields the same object)
};

// goal_spec(scene, spec): the landing task (null spec: the scene's goal block), or std::invalid_argument with the
// Python's message.
Value goal_spec(const Value& scene, const Value& spec);

// goal_key(scene, spec, corridor): `corridor` is corridor.to_dict(), or null for None.
std::string goal_key(const Value& scene, const Value& spec, const Value& corridor);

// landing_satisfied(scene, block_id, vertical).
bool landing_satisfied(const Value& scene, const std::string& block_id, const Value& vertical);

// _landing_prefixes(scene, program, block_id, reuse_full=...): each prefix ending on the block's top.
std::vector<LandingPrefix> landing_prefixes(const Value& scene, const Program& program, const std::string& block_id,
                                            bool reuse_full);

}  // namespace pkc
