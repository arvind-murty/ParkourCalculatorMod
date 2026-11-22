#include "pkc/canonical.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <memory>
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

void append(std::string& out, const Value& value, bool sort_keys, bool compact) {
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
                if (i) out += compact ? "," : ", ";
                append(out, (*v)[i], sort_keys, compact);
            }
            out += ']';
        } else {
            // sort_keys orders str keys by code point, which is UTF-8 byte order.
            std::vector<const std::pair<std::string, Value>*> items;
            items.reserve(v->size());
            for (const auto& item : *v) items.push_back(&item);
            if (sort_keys)
                std::sort(items.begin(), items.end(), [](auto* a, auto* b) { return a->first < b->first; });
            out += '{';
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out += compact ? "," : ", ";
                append_string(out, items[i]->first);
                out += compact ? ":" : ": ";
                append(out, items[i]->second, sort_keys, compact);
            }
            out += '}';
        }
    }, value.data);
}


// json.loads for the JSON json.dumps writes: insertion-ordered objects (a repeated
// key keeps its first position and last value, like a dict), int literals as int.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value document() {
        Value result = parse();
        space();
        if (at_ != text_.size()) error("Extra data");
        return result;
    }

private:
    std::string_view text_;
    std::size_t at_ = 0;

    [[noreturn]] void error(const char* message) {
        throw std::invalid_argument(std::string(message) + ": char " + std::to_string(at_));
    }
    void space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }
    bool take(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) return false;
        at_ += word.size();
        return true;
    }
    Value parse() {
        space();
        if (at_ >= text_.size()) error("Expecting value");
        char c = text_[at_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return {parse_string()};
        if (take("null")) return {nullptr};
        if (take("true")) return {true};
        if (take("false")) return {false};
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        error("Expecting value");
    }
    Value parse_object() {
        ++at_;
        auto fields = std::make_shared<Object>();
        space();
        if (take("}")) return {std::shared_ptr<const Object>(std::move(fields))};
        while (true) {
            space();
            if (at_ >= text_.size() || text_[at_] != '"') error("Expecting property name enclosed in double quotes");
            std::string key = parse_string();
            space();
            if (!take(":")) error("Expecting ':' delimiter");
            Value item = parse();
            auto found = std::find_if(fields->begin(), fields->end(), [&](const auto& f) { return f.first == key; });
            if (found != fields->end()) {
                found->second = std::move(item);
            } else {
                fields->emplace_back(std::move(key), std::move(item));
            }
            space();
            if (take("}")) break;
            if (!take(",")) error("Expecting ',' delimiter");
        }
        return {std::shared_ptr<const Object>(std::move(fields))};
    }
    Value parse_array() {
        ++at_;
        auto values = std::make_shared<Array>();
        space();
        if (take("]")) return {std::shared_ptr<const Array>(std::move(values))};
        while (true) {
            values->push_back(parse());
            space();
            if (take("]")) break;
            if (!take(",")) error("Expecting ',' delimiter");
        }
        return {std::shared_ptr<const Array>(std::move(values))};
    }
    unsigned hex4() {
        if (at_ + 4 > text_.size()) error("Invalid \\uXXXX escape");
        unsigned code = 0;
        for (int k = 0; k < 4; ++k) {
            char h = text_[at_++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= h - '0';
            else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
            else error("Invalid \\uXXXX escape");
        }
        return code;
    }
    std::string parse_string() {
        ++at_;
        std::string out;
        while (true) {
            if (at_ >= text_.size()) error("Unterminated string starting at");
            char c = text_[at_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) error("Invalid control character at");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) error("Unterminated string starting at");
            char e = text_[at_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned code = hex4();
                    if (code >= 0xD800 && code < 0xDC00 && text_.substr(at_, 2) == "\\u") {
                        std::size_t mark = at_;
                        at_ += 2;
                        unsigned low = hex4();
                        if (low >= 0xDC00 && low < 0xE000) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            at_ = mark;
                        }
                    }
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code < 0x10000) {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xF0 | (code >> 18));
                        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: error("Invalid \\escape");
            }
        }
    }
    Value parse_number() {
        std::size_t start = at_;
        bool real = false;
        if (text_[at_] == '-') ++at_;
        auto digits = [&] {
            std::size_t first = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            return at_ > first;
        };
        if (!digits()) error("Expecting value");
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            real = true;
            if (!digits()) error("Expecting value");
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            real = true;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) ++at_;
            if (!digits()) error("Expecting value");
        }
        const char* first = text_.data() + start;
        const char* last = text_.data() + at_;
        if (real) return {std::strtod(std::string(first, last).c_str(), nullptr)};  // the grammar checked it; 1e999 is inf
        std::int64_t value;
        auto result = std::from_chars(first, last, value);
        if (result.ec != std::errc() || result.ptr != last) throw std::invalid_argument("integer outside the 64-bit range");
        return {value};
    }
};

}  // namespace

std::string float_repr(double value) {
    if (!std::isfinite(value))
        throw std::invalid_argument(std::string("Out of range float values are not JSON compliant: ") +
                                    (std::isnan(value) ? "nan" : value > 0 ? "inf" : "-inf"));
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

std::string dumps(const Value& value, bool sort_keys, bool compact) {
    std::string out;
    append(out, value, sort_keys, compact);
    return out;
}

std::string canonical_json(const Value& value) { return dumps(value, true, false); }

std::string digest(const Value& value) { return sha256_hex(canonical_json(value)); }

std::string sha256_hex(std::string_view data) {
    static const char* hex = "0123456789abcdef";
    auto hash = sha256(data);
    std::string out;
    out.reserve(64);
    for (auto byte : hash) {
        out += hex[byte >> 4];
        out += hex[byte & 0xF];
    }
    return out;
}

Value loads(std::string_view text) { return Parser(text).document(); }

}  // namespace pkc
