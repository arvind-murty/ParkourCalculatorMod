// Port of numerics/constraints.py. Group keys compare with Python == (1 == 1.0),
// in first-seen order like the Python dict.
#include "pkc/numerics/constraints.hpp"

#include <vector>

#include "../schema.hpp"

namespace pkc {

Value reduce_walls(const Value& walls_value) {
    const Array& walls = items(walls_value);
    struct Group {
        Array key;
        std::size_t representative;
        Array sources;
    };
    std::vector<Group> groups;
    for (std::size_t index = 0; index < walls.size(); ++index) {
        const Object& wall = fields(walls[index]);
        const Value& mode = at(wall, "mode");
        const Value& t2 = get_or_null(wall, "t2");
        const std::string& name = as_text(mode);
        const std::string& cmp = as_text(at(wall, "cmp"));
        const bool simple = (name == "X" || name == "Z") && is_null(t2) && (cmp == "GE" || cmp == "LE");
        // Facing residuals wrap, so different facing bounds are not ordered like
        // scalar X/Z bounds. Relative and cross-axis rows only deduplicate exact
        // expressions; no coordinate rounding or algebraic relaxation occurs.
        const bool relative = !is_null(t2) && name != "DXZ" && name != "DZX";
        const Value* op = get(wall, "op");
        Array key{mode, at(wall, "t1"), t2, relative ? (op ? *op : text("PLUS")) : Value{nullptr}, at(wall, "cmp"),
                  simple ? Value{nullptr} : at(wall, "rhs")};
        Group* group = nullptr;
        for (auto& candidate : groups)
            if (py_equal(candidate.key, key)) group = &candidate;
        if (!group) group = &groups.emplace_back(Group{std::move(key), index, {}});
        group->sources.push_back(integer_value(static_cast<std::int64_t>(index)));
        if (simple) {
            double old = num(at(fields(walls[group->representative]), "rhs")).value;
            double rhs = num(at(wall, "rhs")).value;
            if (cmp == "GE" ? rhs > old : rhs < old) group->representative = index;
        }
    }
    Array selected, representatives, sources;
    for (const auto& group : groups) {
        selected.push_back(walls[group.representative]);
        representatives.push_back(integer_value(static_cast<std::int64_t>(group.representative)));
        sources.push_back(array(group.sources));
    }
    const auto retained = static_cast<std::int64_t>(groups.size());
    return object({{"walls", array(std::move(selected))},
                   {"audit", object({{"original", integer_value(static_cast<std::int64_t>(walls.size()))},
                                     {"retained", integer_value(retained)},
                                     {"representatives", array(std::move(representatives))},
                                     {"sourceIndices", array(std::move(sources))}})}});
}

}  // namespace pkc
