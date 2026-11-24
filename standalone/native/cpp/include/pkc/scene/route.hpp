// Routes and their schedules: scene/route.py.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc {

// prepare_route(request). Routes with ladder events throw Unsupported.
Value prepare_route(const Value& request);
// schedule(route, allocation, stage)
Value schedule(const Value& route, const std::vector<std::int64_t>& allocation, const std::string& stage = "jump");
bool supports(const Object& block, double x, double z, const std::string& kind = "landing");
std::array<Num, 4> center_bounds(const Object& block, const std::string& kind = "landing");
// initial_guess(route, plan, parent); parent may be null.
Value initial_guess(const Value& route, const Value& plan, const Value* parent);

}  // namespace pkc
