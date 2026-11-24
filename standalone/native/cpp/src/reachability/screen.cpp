#include "pkc/reachability/screen.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "pkc/envelopes.hpp"
#include "../value_ops.hpp"

namespace pkc {

namespace {

double bound(const Value& v) {
    double out = 0.;
    if (!numeric(v, out)) fail("expected a number");
    return out;
}

}  // namespace

Value condition_envelopes(const Value& envelopes_value, const Array& extra_rows, bool refine, const Value* objective) {
    const Object& envelopes = fields(envelopes_value);
    Array rows;
    for (const auto& r : extra_rows) {
        const Object& row = fields(r);
        const Value* t1 = get(row, "t1");
        rows.push_back(object(with(row, "t1", t1 ? *t1 : get_or_null(row, "offset"))));
    }
    const Value proof = interaction_reachability(envelopes_value, array(rows));
    const Value& model = get_or_null(envelopes, "model");
    Value tightened = envelopes_value;
    if (!rows.empty() && !is_null(model)) {
        Object conditioned = fields(model);
        const Value* walls = get(conditioned, "walls");
        Array all = walls ? items(*walls) : Array{};
        all.insert(all.end(), rows.begin(), rows.end());
        put(conditioned, "walls", array(std::move(all)));
        const Array& states = items(at(envelopes, "states"));
        const bool known = std::all_of(states.begin(), states.end(), [](const Value& s) {
            const Value* y = get(fields(s), "Y");
            return y && bound(items(*y)[0]) == bound(items(*y)[1]);
        });
        Array y;
        if (known)
            for (const auto& s : states) y.push_back(items(at(fields(s), "Y"))[0]);
        const Value problem = object(conditioned);
        Object rebuilt = fields(reachable_envelopes(problem, known ? array(std::move(y)) : Value{nullptr}, std::nullopt));
        rebuilt.emplace_back("model", problem);
        tightened = object(std::move(rebuilt));
    } else if (!rows.empty()) {
        // The old phase boxes remain a conservative superset without a motion model.
        Object copy = envelopes;
        Array states = items(at(copy, "states"));
        for (const auto& r : rows) {
            const Object& row = fields(r);
            const std::string& mode = as_text(at(row, "mode"));
            if ((mode != "X" && mode != "Z") || !is_null(get_or_null(row, "t2"))) continue;
            const auto t = static_cast<std::size_t>(std::get<std::int64_t>(at(row, "t1").data));
            Object state = fields(states.at(t));
            const Array& range = items(at(state, mode));
            Value lo = range[0], hi = range[1];
            const std::string& cmp = as_text(at(row, "cmp"));
            const double rhs = bound(at(row, "rhs"));
            // max/min keep the first argument on ties, as Python's do.
            if (cmp == "GE" || cmp == "EQ")
                if (rhs - 1e-7 > bound(lo)) lo = real(rhs - 1e-7);
            if (cmp == "LE" || cmp == "EQ")
                if (rhs + 1e-7 < bound(hi)) hi = real(rhs + 1e-7);
            put(state, mode, array({lo, hi}));
            states[t] = object(std::move(state));
        }
        put(copy, "states", array(std::move(states)));
        tightened = object(std::move(copy));
    }
    const Value checked = py_truthy(at(fields(proof), "ruledOut"))
                              ? proof
                              : interaction_reachability(tightened, object({}), refine, objective);
    return object({{"envelopes", tightened}, {"proof", checked}});
}

}  // namespace pkc
