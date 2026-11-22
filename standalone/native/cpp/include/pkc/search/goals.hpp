// Landing prefixes of a program on a goal block: search/goals.py.
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

// _landing_prefixes(scene, program, block_id, reuse_full=...): each prefix ending on the block's top.
std::vector<LandingPrefix> landing_prefixes(const Value& scene, const Program& program, const std::string& block_id,
                                            bool reuse_full);

}  // namespace pkc
