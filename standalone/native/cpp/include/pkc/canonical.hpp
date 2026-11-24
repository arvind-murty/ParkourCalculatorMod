// Byte-for-byte equivalent of parkour_solver.canonical: json.dumps(value,
// sort_keys=True, allow_nan=False) and its SHA-256 hex digest.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "pkc/value.hpp"

namespace pkc {

std::string canonical_json(const Value& value);  // throws std::invalid_argument on NaN/infinity
// json.dumps(value, sort_keys=sort_keys, allow_nan=False), with separators=(',', ':') when compact.
std::string dumps(const Value& value, bool sort_keys, bool compact);
// json.loads(text); malformed text throws std::invalid_argument.
Value loads(std::string_view text);
std::string float_repr(double value);            // Python repr(float) for finite values
std::array<std::uint8_t, 32> sha256(std::string_view data);
std::string sha256_hex(std::string_view data);  // lowercase hex
std::string digest(const Value& value);          // lowercase hex of sha256(canonical_json(value))

}  // namespace pkc
