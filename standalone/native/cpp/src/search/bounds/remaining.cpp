// Port of search/bounds/remaining.py with bounds/flight.state_covers.
#include "pkc/search/bounds/remaining.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../py_math.hpp"
#include "../../schema.hpp"
#include "pkc/vertical.hpp"

namespace pkc {
namespace {

using Cover = std::vector<double>;

double n(const Value& v) { return num(v).value; }

std::vector<Cover> state_covers(const Value& guides_value, const std::string& name, const Value& motion) {
    const Object& guides = fields(guides_value);
    const Value* covers = get(fields(at(guides, "covers")), name);
    if (!covers || items(*covers).empty()) return {};
    double pad = 0.;
    const Value* phase = get(guides, "coverPhase");
    if (phase && as_text(*phase) != "post") {
        const Value* m = get(fields(motion), "movementAfterSupportCheck");
        pad = m ? n(*m) : 0.;
        if (!std::isfinite(pad) || pad <= 0) return {};
    }
    std::vector<Cover> out;
    for (const auto& bv : items(*covers)) {
        const Array& b = items(bv);
        out.push_back({n(b[0]) - pad, n(b[1]), n(b[2]) - pad, n(b[3]) + pad, n(b[4]), n(b[5]) + pad});
    }
    return out;
}

double cover_distance(const std::vector<Cover>& left, const std::vector<Cover>& right) {
    double best = INFINITY;
    for (const auto& b : left)
        for (const auto& c : right) {
            const double dx = std::max({0., b[0] - c[3], c[0] - b[3]}), dz = std::max({0., b[2] - c[5], c[2] - b[5]});
            best = std::min(best, python_hypot(dx, dz));
        }
    return best;
}

bool same_box(const Value& a, const Value& b) {
    const Array &x = items(a), &y = items(b);
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (n(x[i]) != n(y[i])) return false;
    return true;
}

}  // namespace

Value remaining_bound(const Value& guides_value, std::int64_t span, const Program& program, const Value& motion,
                      const Value& transfers_value, const Value& hard_latches_value) {
    const Object& guides = fields(guides_value);
    const Array& names = items(at(guides, "anchors"));
    const Profile& p = program.profile;
    const std::int64_t target = span + 1, count = static_cast<std::int64_t>(names.size());
    auto supported_by = [&](std::int64_t i) {
        return program.terminal_support.value && *program.terminal_support.value == as_text(names.at(i));
    };
    const bool reached = supported_by(target) && p.grounded.back();
    std::int64_t first = reached ? target : target - 1;
    if (!reached && (!p.grounded.back() || !supported_by(first))) first = target;
    Array segments;
    bool unknown = false;
    Num gap = whole(0);
    for (std::int64_t i = first; i < count - 1; ++i) {
        const std::string a = as_text(names[i]), b = as_text(names[i + 1]);
        const auto left = state_covers(guides_value, a, motion), right = state_covers(guides_value, b, motion);
        if (left.empty() || right.empty()) {
            unknown = true;
            continue;
        }
        const double distance = cover_distance(left, right);
        gap = add(gap, flt(distance));
        segments.push_back(object({{"source", text(a)}, {"target", text(b)}, {"distance", real(distance)}}));
    }
    const Value* speed_value = get(fields(motion), "movementAfterSupportCheck");
    const Value speed = speed_value ? *speed_value : real(0.);
    const double s = n(speed);
    const std::int64_t horizontal =
        std::isfinite(s) && s > 0 ? std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil((gap.value - 1e-7) / s))) : 0;
    // _vertical_remaining
    const Object transfers = is_null(transfers_value) ? Object{} : fields(transfers_value);
    const Object latches = is_null(hard_latches_value) ? Object{} : fields(hard_latches_value);
    const std::int64_t n_ticks = p.duration;
    const bool vertical_reached = reached;
    Array terms;
    std::int64_t vertical = 0;
    for (std::int64_t i = span + (vertical_reached ? 1 : 0); i < count - 1; ++i) {
        const std::string key = as_text(names[i]) + "→" + as_text(names[i + 1]);
        const Value* record = get(transfers, key);
        if (!get(latches, key) || !record || !py_truthy(get_or_null(fields(*record), "ticks"))) continue;
        const Object& r = fields(*record);
        std::int64_t ticks = as_int(at(r, "ticks"));
        std::string reason = "future_transfer";
        if (i == span && !vertical_reached) {
            if (p.position.empty() || p.velocity.empty()) continue;
            if (p.grounded.back() && supported_by(i)) {
                if (std::fabs(p.position.back().value - n(at(r, "sourceTop"))) > 1e-7 ||
                    std::fabs(p.velocity.back().value - (-kGravity * kYDrag)) > 1e-7)
                    continue;
                reason = "grounded_source";
            } else if (!p.grounded.back()) {
                std::int64_t boundary = 0;
                const Value& source_box = get_or_null(r, "sourceBox");
                for (const auto& e : p.contact_events)
                    if (e.axis == "Y" && e.direction == -1 && e.boxes)
                        for (const auto& box : *e.boxes) {
                            Array b;
                            for (const Num& v : box) b.push_back(value(v));
                            if (!is_null(source_box) && same_box(array(b), source_box)) boundary = std::max(boundary, e.offset + 1);
                        }
                std::vector<std::int64_t> launches;
                for (std::int64_t t = boundary; t < n_ticks; ++t)
                    if (p.grounded[t] && p.jump[t]) launches.push_back(t);
                if (launches.size() != 1) continue;
                const std::int64_t launch = launches[0];
                if (std::fabs(p.position[launch].value - n(at(r, "sourceTop"))) > 1e-7) continue;
                if (std::any_of(p.case_events.begin(), p.case_events.end(), [&](const CaseEvent& e) { return e.input >= launch; }))
                    continue;
                if (std::any_of(p.contact_events.begin(), p.contact_events.end(), [&](const ContactEvent& e) {
                        return e.offset >= launch && e.axis == "Y" && e.direction == 1;
                    }))
                    continue;
                ticks = std::max<std::int64_t>(0, ticks - (n_ticks - launch));
                reason = "paid_transfer_inputs";
            } else {
                continue;
            }
        }
        vertical += ticks;
        terms.push_back(object({{"source", names[i]}, {"target", names[i + 1]}, {"ticks", integer_value(ticks)},
                                {"reason", text(reason)}, {"proof", get_or_null(r, "id")}}));
    }
    Array proofs;
    for (const auto& t : terms)
        if (py_truthy(at(fields(t), "proof"))) proofs.push_back(at(fields(t), "proof"));
    Num estimate = whole(0);
    const Array& points = items(at(guides, "points"));
    for (std::int64_t i = first; i < count - 1; ++i) {
        const Array &a = items(points.at(i)), &b = items(points.at(i + 1));
        estimate = add(estimate, flt(python_hypot(n(b[0]) - n(a[0]), n(b[2]) - n(a[2]))));
    }
    return object({{"remainingTicks", integer_value(std::max(horizontal, vertical))},
                   {"horizontalTicks", integer_value(horizontal)},
                   {"verticalTicks", integer_value(vertical)},
                   {"transferTerms", array(std::move(terms))},
                   {"proofIds", array(std::move(proofs))},
                   {"reason", text(unknown ? "partial_unknown_landing_enclosure" : "relaxed_remaining_time")},
                   {"distanceLowerBound", value(gap)},
                   {"movementUpperBound", speed},
                   {"segments", array(std::move(segments))},
                   {"guideDistanceEstimate", value(estimate)}});
}

}  // namespace pkc
