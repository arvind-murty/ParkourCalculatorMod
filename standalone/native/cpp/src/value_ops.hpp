// JSON-shaped values with Python dict/list semantics, shared by the native
// units that port Python code operating on dicts and lists.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "pkc/value.hpp"

namespace pkc {

[[noreturn]] inline void fail(const std::string& message) { throw std::invalid_argument(message); }

inline Value object(Object fields) { return {std::make_shared<const Object>(std::move(fields))}; }
inline Value array(Array items) { return {std::make_shared<const Array>(std::move(items))}; }
inline Value text(std::string value) { return {std::move(value)}; }
inline Value integer_value(std::int64_t value) { return {value}; }
inline Value real(double value) { return {value}; }
inline Value boolean(bool value) { return {value}; }
inline const Value kNull{nullptr};

inline const Object& fields(const Value& value) {
    auto* held = std::get_if<std::shared_ptr<const Object>>(&value.data);
    if (!held) fail("expected an object");
    return **held;
}

inline const Array& items(const Value& value) {
    auto* held = std::get_if<std::shared_ptr<const Array>>(&value.data);
    if (!held) fail("expected a list");
    return **held;
}

inline bool is_list(const Value& value) { return std::holds_alternative<std::shared_ptr<const Array>>(value.data); }
inline bool is_object(const Value& value) { return std::holds_alternative<std::shared_ptr<const Object>>(value.data); }
inline bool is_null(const Value& value) { return std::holds_alternative<std::nullptr_t>(value.data); }

inline const Value* get(const Object& object, std::string_view key) {
    for (const auto& [name, value] : object)
        if (name == key) return &value;
    return nullptr;
}

// dict.get(key) where an absent key reads as None.
inline const Value& get_or_null(const Object& object, std::string_view key) {
    const Value* value = get(object, key);
    return value ? *value : kNull;
}

inline const Value& at(const Object& object, std::string_view key) {
    if (const Value* value = get(object, key)) return *value;
    fail("KeyError: '" + std::string(key) + "'");
}

inline void put(Object& object, std::string_view key, Value value) {
    for (auto& [name, held] : object) {
        if (name == key) {
            held = std::move(value);
            return;
        }
    }
    object.emplace_back(std::string(key), std::move(value));
}

inline const Value& setdefault(Object& object, std::string_view key, Value value) {
    if (!get(object, key)) object.emplace_back(std::string(key), std::move(value));
    return at(object, key);
}

inline void erase(Object& object, std::string_view key) {
    object.erase(std::remove_if(object.begin(), object.end(), [&](const auto& field) { return field.first == key; }),
                 object.end());
}

inline Object with(Object object, std::string_view key, Value value) {  // dict(row, key=value)
    put(object, key, std::move(value));
    return object;
}

inline const std::string& as_text(const Value& value) {
    auto* held = std::get_if<std::string>(&value.data);
    if (!held) fail("expected a str");
    return *held;
}

inline bool numeric(const Value& value, double& out) {
    if (auto* integer = std::get_if<std::int64_t>(&value.data)) {
        out = static_cast<double>(*integer);
    } else if (auto* real_value = std::get_if<double>(&value.data)) {
        out = *real_value;
    } else if (auto* flag = std::get_if<bool>(&value.data)) {
        out = *flag;
    } else {
        return false;
    }
    return true;
}

// Python ==: numbers compare by value (1 == 1.0 == True), containers elementwise.
inline bool py_equal(const Array& left, const Array& right);

inline bool py_equal(const Value& a, const Value& b) {
    double x, y;
    if (numeric(a, x) && numeric(b, y)) return x == y;
    if (a.data.index() != b.data.index()) return false;
    if (is_null(a)) return true;
    if (auto* s = std::get_if<std::string>(&a.data)) return *s == std::get<std::string>(b.data);
    if (is_list(a)) return py_equal(items(a), items(b));
    const auto &left = fields(a), &right = fields(b);
    if (left.size() != right.size()) return false;
    for (const auto& [key, value] : left) {
        const Value* other = get(right, key);
        if (!other || !py_equal(value, *other)) return false;
    }
    return true;
}

inline bool py_equal(const Array& left, const Array& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (!py_equal(left[i], right[i])) return false;
    return true;
}

inline bool contains(const Array& values, const Value& value) {
    return std::any_of(values.begin(), values.end(), [&](const Value& item) { return py_equal(item, value); });
}

// Python truthiness: None, False, 0, 0.0 and empty str/list/dict are false.
inline bool py_truthy(const Value& value) {
    double number;
    if (numeric(value, number)) return number != 0;
    if (is_null(value)) return false;
    if (auto* text_value = std::get_if<std::string>(&value.data)) return !text_value->empty();
    if (is_list(value)) return !items(value).empty();
    return !fields(value).empty();
}

}  // namespace pkc
