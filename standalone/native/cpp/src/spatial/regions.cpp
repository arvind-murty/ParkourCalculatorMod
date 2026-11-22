// Port of spatial/regions.geometry_key and canonical_cover.
#include "pkc/spatial/regions.hpp"

#include <stdexcept>
#include <tuple>

#include "../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "py_order.hpp"

namespace pkc {
namespace {

const double kMinFreeWidth = .012;  // scene/geometry.MIN_FREE_WIDTH
bool thin(double width) { return width < kMinFreeWidth / 2; }

}  // namespace

std::string geometry_key(const Value& value) { return sha256_hex(dumps(value, true, true)); }

std::vector<NumBox> canonical_cover(const std::vector<NumBox>& input) {
    const std::vector<NumBox> boxes = sorted_set(input);
    if (boxes.empty()) return {};
    const Num height = boxes[0][1];
    for (const auto& b : boxes)
        if (b[1].value != height.value || b[4].value != height.value)
            fail("A support cover must lie on one horizontal plane");
    std::vector<NumBox> areas;
    for (const auto& b : boxes)
        if (b[0].value < b[3].value && b[2].value < b[5].value) areas.push_back(b);
    std::vector<Num> edge_values;
    for (const auto& b : areas) {
        edge_values.push_back(b[0]);
        edge_values.push_back(b[3]);
    }
    const std::vector<Num> edges = sorted_set(edge_values);
    using Interval = std::array<Num, 2>;
    struct Strip {
        Num x, xx;
        std::vector<Interval> coverage;
    };
    auto same = [](const std::vector<Interval>& a, const std::vector<Interval>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i][0].value != b[i][0].value || a[i][1].value != b[i][1].value) return false;
        return true;
    };
    std::vector<Strip> strips;
    for (std::size_t k = 0; k + 1 < edges.size(); ++k) {
        const Num x = edges[k], xx = edges[k + 1];
        std::vector<Interval> intervals;
        for (const auto& b : areas)
            if (b[0].value <= x.value && xx.value <= b[3].value) intervals.push_back({b[2], b[5]});
        std::stable_sort(intervals.begin(), intervals.end(), value_less<2>);
        std::vector<Interval> merged;
        for (const auto& [lo, hi] : intervals) {
            if (!merged.empty() && lo.value <= merged.back()[1].value) {
                const Num prior = merged.back()[1];
                merged.back()[1] = prior.value > hi.value ? prior : hi;  // max(hi, prior)
            } else {
                merged.push_back({lo, hi});
            }
        }
        if (!strips.empty() && strips.back().xx.value == x.value && same(strips.back().coverage, merged)) {
            strips.back().xx = xx;
        } else if (!merged.empty()) {
            strips.push_back({x, xx, merged});
        }
    }
    std::vector<NumBox> result;
    for (const auto& strip : strips)
        for (const auto& [z, zz] : strip.coverage)
            if (!thin(strip.xx.value - strip.x.value) && !thin(zz.value - z.value))
                result.push_back({strip.x, height, z, strip.xx, height, zz});
    return sorted_set(result);
}

}  // namespace pkc
