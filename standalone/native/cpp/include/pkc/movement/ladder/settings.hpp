// Declared ladder search domain: movement/ladder/settings.py.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

Value ladder_settings(const Object& scene);  // scene['ladderSearch'] checked, defaults filled in

}  // namespace pkc
