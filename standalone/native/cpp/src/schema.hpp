// schema.py validators on JSON values, keeping Python's int/float kinds.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "value_ops.hpp"

namespace pkc {

inline constexpr double kTolerance = 1e-7;  // schema.TOLERANCE

inline Num number(const Value& value, const std::string& name) {  // schema.number
    if (auto* integer = std::get_if<std::int64_t>(&value.data)) return {static_cast<double>(*integer), true};
    if (auto* real_value = std::get_if<double>(&value.data))
        if (std::isfinite(*real_value)) return {*real_value, false};
    fail(name + " must be a finite number");
}

inline std::int64_t integer(const Value& value, const std::string& name, std::int64_t lower, std::int64_t upper) {
    auto* held = std::get_if<std::int64_t>(&value.data);
    if (!held || *held < lower || *held > upper) fail(name + " is out of range");
    return *held;
}

inline std::int64_t as_int(const Value& value) {  // an index Python reads as an int
    auto* held = std::get_if<std::int64_t>(&value.data);
    if (!held) fail("expected an int");
    return *held;
}

inline Num num(const Value& value) { return number(value, "value"); }

}  // namespace pkc
