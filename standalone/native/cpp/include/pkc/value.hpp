// JSON-shaped value exchanged with Python and used for canonical identity.
// Integers and floats stay distinct because canonical digests encode 1 and 1.0
// differently (see parkour_solver/canonical.py).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace pkc {

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;  // insertion order; the encoder sorts keys

struct Value {
    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string,
                 std::shared_ptr<const Array>, std::shared_ptr<const Object>> data;
};

}  // namespace pkc
