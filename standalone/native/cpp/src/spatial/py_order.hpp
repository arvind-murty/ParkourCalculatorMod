// Python's sorted(set(...)) on number tuples: value-equal items (1 == 1.0) keep the
// first one seen, then the rest sort by value.
#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "pkc/program.hpp"

namespace pkc {

template <std::size_t N>
bool value_less(const std::array<Num, N>& a, const std::array<Num, N>& b) {
    for (std::size_t i = 0; i < N; ++i) {
        if (a[i].value < b[i].value) return true;
        if (b[i].value < a[i].value) return false;
    }
    return false;
}

template <std::size_t N>
bool value_equal(const std::array<Num, N>& a, const std::array<Num, N>& b) {
    for (std::size_t i = 0; i < N; ++i)
        if (a[i].value != b[i].value) return false;
    return true;
}

template <std::size_t N>
std::vector<std::array<Num, N>> sorted_set(const std::vector<std::array<Num, N>>& items) {
    std::vector<std::array<Num, N>> out;
    for (const auto& item : items)
        if (std::none_of(out.begin(), out.end(), [&](const auto& seen) { return value_equal(seen, item); }))
            out.push_back(item);
    std::stable_sort(out.begin(), out.end(), value_less<N>);
    return out;
}

inline std::vector<Num> sorted_set(const std::vector<Num>& items) {
    std::vector<Num> out;
    for (const auto& item : items)
        if (std::none_of(out.begin(), out.end(), [&](const Num& seen) { return seen.value == item.value; }))
            out.push_back(item);
    std::stable_sort(out.begin(), out.end(), [](const Num& a, const Num& b) { return a.value < b.value; });
    return out;
}

}  // namespace pkc
