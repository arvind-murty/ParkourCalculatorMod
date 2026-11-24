// Scene normalization: scene/normalize.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

// prepare_scene(request). Scenes with climbable cells throw Unsupported (ladder settings stay in Python).
Value prepare_scene(const Value& request);

}  // namespace pkc
