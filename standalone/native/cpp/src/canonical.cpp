#include "pkc/canonical.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>

namespace pkc {
namespace {

void append_string(std::string& out, std::string_view text) {
    // json.dumps ensure_ascii: escape quote, backslash and every character
    // outside ' '..'~'; code points above U+FFFF become surrogate pairs.
    static const char* hex = "0123456789abcdef";
    auto escape = [&](unsigned unit) {
        out += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4) out += hex[(unit >> shift) & 0xF];
    };
    out += '"';
    for (std::size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++i;
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20 || c == 0x7F) escape(c);
                    else out += static_cast<char>(c);
            }
            continue;
        }
        int length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        if (i + length > text.size()) throw std::invalid_argument("invalid UTF-8 in string");
        unsigned code = c & (0xFF >> (length + 1));
        for (int k = 1; k < length; ++k) code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        i += length;
        if (code > 0xFFFF) {
            code -= 0x10000;
            escape(0xD800 | (code >> 10));
            escape(0xDC00 | (code & 0x3FF));
        } else {
            escape(code);
        }
    }
    out += '"';
}

void append(std::string& out, const Value& value) {
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            out += "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            out += v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            out += std::to_string(v);
        } else if constexpr (std::is_same_v<T, double>) {
            out += float_repr(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            append_string(out, v);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const Array>>) {
            out += '[';
            for (std::size_t i = 0; i < v->size(); ++i) {
                if (i) out += ", ";
                append(out, (*v)[i]);
            }
            out += ']';
        } else {
            // sort_keys orders str keys by code point, which is UTF-8 byte order.
            std::vector<const std::pair<std::string, Value>*> items;
            items.reserve(v->size());
            for (const auto& item : *v) items.push_back(&item);
            std::sort(items.begin(), items.end(), [](auto* a, auto* b) { return a->first < b->first; });
            out += '{';
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out += ", ";
                append_string(out, items[i]->first);
                out += ": ";
                append(out, items[i]->second);
            }
            out += '}';
        }
    }, value.data);
}

}  // namespace

std::string float_repr(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Out of range float values are not JSON compliant");
    // Shortest round-trip digits, then CPython's repr layout: scientific when
    // the decimal point position is <= -4 or > 16, otherwise fixed with ".0".
    char buffer[32];
    auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    if (result.ec != std::errc()) throw std::runtime_error("to_chars failed");
    std::string_view text(buffer, result.ptr - buffer);
    std::string out;
    if (text.front() == '-') {
        out += '-';
        text.remove_prefix(1);
    }
    std::size_t e = text.find('e');
    std::string digits;
    for (char c : text.substr(0, e)) if (c != '.') digits += c;
    int exponent = std::stoi(std::string(text.substr(e + 1)));
    int point = exponent + 1;
    int n = static_cast<int>(digits.size());
    if (point <= -4 || point > 16) {
        out += digits[0];
        if (n > 1) out += '.' + digits.substr(1);
        out += exponent < 0 ? "e-" : "e+";
        int magnitude = exponent < 0 ? -exponent : exponent;
        if (magnitude < 10) out += '0';
        out += std::to_string(magnitude);
    } else if (point <= 0) {
        out += "0." + std::string(-point, '0') + digits;
    } else if (point >= n) {
        out += digits + std::string(point - n, '0') + ".0";
    } else {
        out += digits.substr(0, point) + '.' + digits.substr(point);
    }
    return out;
}

std::string canonical_json(const Value& value) {
    std::string out;
    append(out, value);
    return out;
}

std::string digest(const Value& value) {
    static const char* hex = "0123456789abcdef";
    auto hash = sha256(canonical_json(value));
    std::string out;
    out.reserve(64);
    for (auto byte : hash) {
        out += hex[byte >> 4];
        out += hex[byte & 0xF];
    }
    return out;
}

}  // namespace pkc
