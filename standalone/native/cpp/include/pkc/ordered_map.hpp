// A string-keyed dict that keeps Python's insertion order, for state whose iteration order decides what runs next.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pkc {

template <class T>
struct OrderedMap {
    std::vector<std::pair<std::string, T>> items;

    T* find(std::string_view key) {
        for (auto& [name, value] : items)
            if (name == key) return &value;
        return nullptr;
    }
    const T* find(std::string_view key) const { return const_cast<OrderedMap*>(this)->find(key); }
    bool contains(std::string_view key) const { return find(key) != nullptr; }

    // d[key]: the existing entry, or a default one appended.
    T& operator[](std::string_view key) {
        if (T* found = find(key)) return *found;
        items.emplace_back(std::string(key), T{});
        return items.back().second;
    }
};

}  // namespace pkc
