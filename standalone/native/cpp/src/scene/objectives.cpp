// Port of scene/objectives.py.
#include "pkc/scene/objectives.hpp"

#include "../value_ops.hpp"

namespace pkc {

Value objective_spec(const Value* value) {
    Object spec;
    if (value == nullptr || is_null(*value)) {
        spec = {{"axis", text("X")}, {"sense", text("MAX")}};
    } else if (is_object(*value)) {
        spec = fields(*value);
    } else {
        fail("Objective must contain only axis and sense");
    }
    for (const auto& field : spec)
        if (field.first != "axis" && field.first != "sense") fail("Objective must contain only axis and sense");
    auto one_of = [&](const char* key, const char* a, const char* b) {
        auto* s = std::get_if<std::string>(&get_or_null(spec, key).data);
        return s && (*s == a || *s == b);
    };
    if (!one_of("axis", "X", "Z") || !one_of("sense", "MAX", "MIN"))
        fail("Objective requires axis X/Z and sense MAX/MIN");
    return object(std::move(spec));
}

}  // namespace pkc
