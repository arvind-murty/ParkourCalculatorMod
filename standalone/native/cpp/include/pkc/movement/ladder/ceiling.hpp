// Ladder ceiling ownership: movement/ladder/ceiling.py, on JSON values.
#pragma once

#include <utility>

#include "pkc/value.hpp"

namespace pkc {

// own_ceiling_contacts(contacts, units) -> (contacts, units).
std::pair<Array, Array> own_ceiling_contacts(const Array& contacts, const Array& units);

}  // namespace pkc
