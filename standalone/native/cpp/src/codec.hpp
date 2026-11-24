// Strict conversion between typed records and JSON values. Each record lists its fields once in a describe() overload
// in namespace pkc::codec, found by argument-dependent lookup; Reader and Writer walk that list to decode and encode,
// so the two directions cannot disagree. A record marks itself with PKC_RECORD; one with an `annotations` member keeps
// the keys no field claims there instead of failing on them.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "pkc/ordered_map.hpp"
#include "pkc/program.hpp"
#include "pkc/value.hpp"

namespace pkc::codec {

// Largest magnitude where a double holds every integer exactly.
inline constexpr std::int64_t kExactInteger = std::int64_t{1} << 53;

// Where a decode is in the input, rendered only when it fails: building the
// string for every field cost more than the decode itself.
struct Path {
    const Path* parent = nullptr;
    std::string_view key;
    std::size_t index = 0;
    bool is_index = false;

    std::string str() const {
        if (!parent) return std::string(key);
        return parent->str() + (is_index ? "[" + std::to_string(index) + "]" : "." + std::string(key));
    }
};

inline Path child(const Path& parent, std::string_view key) { return {&parent, key}; }
inline Path element(const Path& parent, std::size_t index) { return {&parent, {}, index, true}; }

[[noreturn]] inline void fail(const Path& path, const std::string& problem) {
    throw std::invalid_argument(path.str() + ": " + problem);
}

template <class T>
const T& as(const Value& value, const Path& path, const char* expected) {
    if (auto* held = std::get_if<T>(&value.data)) return *held;
    fail(path, std::string("expected ") + expected);
}

template <class T>
struct IsRecord : std::false_type {};
#define PKC_RECORD(T) \
    template <>       \
    struct IsRecord<T> : std::true_type {}

template <class T, class = void>
struct HasAnnotations : std::false_type {};
template <class T>
struct HasAnnotations<T, std::void_t<decltype(std::declval<T&>().annotations)>> : std::true_type {};

// Declared up front because records and containers nest in each other.
template <class T>
std::enable_if_t<IsRecord<T>::value> decode(const Value& value, T& out, const Path& path);
template <class T>
void decode(const Value& value, Nullable<T>& out, const Path& path);
template <class T>
void decode(const Value& value, std::vector<T>& out, const Path& path);
template <class T, std::size_t N>
void decode(const Value& value, std::array<T, N>& out, const Path& path);
template <class T>
void decode(const Value& value, std::shared_ptr<T>& out, const Path& path);
template <class T>
void decode(const Value& value, OrderedMap<T>& out, const Path& path);
template <class T>
std::enable_if_t<IsRecord<T>::value, Value> encode(const T& record);
template <class T>
Value encode(const Nullable<T>& value);
template <class T>
Value encode(const std::vector<T>& values);
template <class T, std::size_t N>
Value encode(const std::array<T, N>& values);
template <class T>
Value encode(const std::shared_ptr<T>& value);
template <class T>
Value encode(const OrderedMap<T>& map);

inline void decode(const Value& value, bool& out, const Path& path) { out = as<bool>(value, path, "bool"); }

inline void decode(const Value& value, std::int64_t& out, const Path& path) {
    out = as<std::int64_t>(value, path, "int");
}

inline void decode(const Value& value, Num& out, const Path& path) {
    if (auto* integer = std::get_if<std::int64_t>(&value.data)) {
        if (*integer > kExactInteger || *integer < -kExactInteger) fail(path, "int outside the exact double range");
        out = {static_cast<double>(*integer), true};
    } else {
        out = {as<double>(value, path, "number"), false};
    }
}

inline void decode(const Value& value, std::string& out, const Path& path) {
    out = as<std::string>(value, path, "str");
}

inline void decode(const Value& value, Value& out, const Path&) { out = value; }

inline void decode(const Value& value, Anchors& out, const Path& path) {
    for (const auto& [key, item] : *as<std::shared_ptr<const Object>>(value, path, "object"))
        out[key] = as<std::int64_t>(item, child(path, key), "int");
}

template <class T>
void decode(const Value& value, Nullable<T>& out, const Path& path) {
    if (std::holds_alternative<std::nullptr_t>(value.data)) {
        out.value.reset();
    } else {
        decode(value, out.value.emplace(), path);
    }
}

template <class T>
void decode(const Value& value, std::vector<T>& out, const Path& path) {
    const auto& items = *as<std::shared_ptr<const Array>>(value, path, "list");
    out.clear();
    out.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        T item{};
        decode(items[i], item, element(path, i));
        out.push_back(std::move(item));
    }
}

template <class T, std::size_t N>
void decode(const Value& value, std::array<T, N>& out, const Path& path) {
    const auto& items = *as<std::shared_ptr<const Array>>(value, path, "list");
    if (items.size() != N) fail(path, "expected " + std::to_string(N) + " items");
    for (std::size_t i = 0; i < N; ++i) decode(items[i], out[i], element(path, i));
}

// A shared object decodes fresh, as Python objects come back from JSON unaliased.
template <class T>
void decode(const Value& value, std::shared_ptr<T>& out, const Path& path) {
    auto decoded = std::make_shared<T>();
    decode(value, *decoded, path);
    out = std::move(decoded);
}

template <class T>
void decode(const Value& value, OrderedMap<T>& out, const Path& path) {
    const auto& fields = *as<std::shared_ptr<const Object>>(value, path, "object");
    out.items.clear();
    out.items.reserve(fields.size());
    for (const auto& [key, item] : fields) {
        T decoded{};
        decode(item, decoded, child(path, key));
        out.items.emplace_back(key, std::move(decoded));
    }
}

class Reader {
public:
    Reader(const Value& value, const Path& path)
        : fields_(*as<std::shared_ptr<const Object>>(value, path, "object")),
          path_(path),
          used_(fields_.size()) {}

    template <class T>
    void req(const char* key, T& out) {
        const Value* value = find(key);
        if (!value) fail(path_, std::string("missing key ") + key);
        decode(*value, out, child(path_, key));
    }

    template <class T>
    void opt(const char* key, std::optional<T>& out) {
        out.reset();
        if (const Value* value = find(key)) decode(*value, out.emplace(), child(path_, key));
    }

    // Keys no field claimed: an error, or a record's annotations.
    Object rest() const {
        Object rest;
        for (std::size_t i = 0; i < fields_.size(); ++i)
            if (!used_[i]) rest.push_back(fields_[i]);
        return rest;
    }

    void finish() const {
        for (std::size_t i = 0; i < fields_.size(); ++i)
            if (!used_[i]) fail(path_, "unknown key " + fields_[i].first);
    }

private:
    const Value* find(const char* key) {
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i].first == key) {
                used_[i] = true;
                return &fields_[i].second;
            }
        }
        return nullptr;
    }

    const Object& fields_;
    const Path& path_;
    std::vector<bool> used_;
};

inline Value encode(bool value) { return {value}; }
inline Value encode(std::int64_t value) { return {value}; }
inline Value encode(const Num& value) {
    if (value.integral) return {static_cast<std::int64_t>(value.value)};
    return {value.value};
}
inline Value encode(const std::string& value) { return {value}; }
inline Value encode(const Value& value) { return value; }

inline Value encode(const Anchors& anchors) {
    auto object = std::make_shared<Object>();
    for (const auto& [key, value] : anchors) object->emplace_back(key, Value{value});
    return {std::shared_ptr<const Object>(std::move(object))};
}

template <class T>
Value encode(const Nullable<T>& value) {
    if (!value.value) return {nullptr};
    return encode(*value.value);
}

template <class T>
Value encode(const std::vector<T>& values) {
    auto array = std::make_shared<Array>();
    array->reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) array->push_back(encode(static_cast<const T&>(values[i])));
    return {std::shared_ptr<const Array>(std::move(array))};
}

inline Value encode(const std::vector<bool>& values) {
    auto array = std::make_shared<Array>();
    array->reserve(values.size());
    for (bool value : values) array->push_back({value});
    return {std::shared_ptr<const Array>(std::move(array))};
}

template <class T, std::size_t N>
Value encode(const std::array<T, N>& values) {
    auto array = std::make_shared<Array>();
    array->reserve(N);
    for (const auto& value : values) array->push_back(encode(value));
    return {std::shared_ptr<const Array>(std::move(array))};
}

template <class T>
Value encode(const std::shared_ptr<T>& value) {
    return encode(*value);
}

template <class T>
Value encode(const OrderedMap<T>& map) {
    auto object = std::make_shared<Object>();
    object->reserve(map.items.size());
    for (const auto& [key, value] : map.items) object->emplace_back(key, encode(value));
    return {std::shared_ptr<const Object>(std::move(object))};
}

// Counts the fields a record writes so Writer allocates its object once.
class FieldCount {
public:
    template <class T>
    void req(const char*, const T&) {
        ++count;
    }

    template <class T>
    void opt(const char*, const std::optional<T>& value) {
        count += value.has_value();
    }

    std::size_t count = 0;
};

class Writer {
public:
    explicit Writer(std::size_t capacity) { fields_->reserve(capacity); }

    template <class T>
    void req(const char* key, const T& value) {
        fields_->emplace_back(key, encode(value));
    }

    template <class T>
    void opt(const char* key, const std::optional<T>& value) {
        if (value) fields_->emplace_back(key, encode(*value));
    }

    void extend(const Object& rest) { fields_->insert(fields_->end(), rest.begin(), rest.end()); }

    Value done() { return {std::shared_ptr<const Object>(std::move(fields_))}; }

private:
    std::shared_ptr<Object> fields_ = std::make_shared<Object>();
};

template <class T>
std::enable_if_t<IsRecord<T>::value> decode(const Value& value, T& out, const Path& path) {
    Reader reader(value, path);
    describe(out, reader, static_cast<T*>(nullptr));
    if constexpr (HasAnnotations<T>::value) {
        out.annotations = reader.rest();
    } else {
        reader.finish();
    }
}

template <class T>
std::enable_if_t<IsRecord<T>::value, Value> encode(const T& record) {
    FieldCount fields;
    describe(record, fields, static_cast<T*>(nullptr));
    if constexpr (HasAnnotations<T>::value) fields.count += record.annotations.size();
    Writer writer(fields.count);
    describe(record, writer, static_cast<T*>(nullptr));
    if constexpr (HasAnnotations<T>::value) writer.extend(record.annotations);
    return writer.done();
}

// A record decoded from a value at the root of `name`'s path.
template <class T>
T from_value(const Value& value, const char* name) {
    T out{};
    decode(value, out, Path{nullptr, name});
    return out;
}

}  // namespace pkc::codec
