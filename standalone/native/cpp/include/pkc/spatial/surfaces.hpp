// Player-space contact faces and obstacle clusters: spatial/surfaces.py. The Python
// SceneGeometry keeps its caches and dataclasses and asks this one on a miss.
#pragma once

#include <cstdint>
#include <vector>

#include "pkc/spatial/cells.hpp"
#include "pkc/value.hpp"

namespace pkc {

// free_rectangles(boxes): maximal closed X/Z rectangles disjoint from the boxes' open rectangles.
std::vector<Rect> free_rectangles(const std::vector<NumBox>& boxes);
Value free_rectangles(const Value& boxes);  // the same on JSON boxes, rows as lists

class SceneGeometry {
public:
    explicit SceneGeometry(const Value& solids);  // collision solids; boxes are their expanded boxes

    // _cluster_map(indices) as its groups: sorted member lists, keyed in Python by their minimum.
    std::vector<std::vector<std::int64_t>> cluster_groups(std::vector<std::int64_t> indices) const;

    // sides(cluster, active) for the cluster with these member solids, as
    // [[bounds [[axis, direction, plane], ...], owners [[solid, ...], ...], region], ...].
    Value sides(const std::vector<std::int64_t>& members, const std::vector<std::int64_t>& active) const;

    // contact_options(axis, sign, clearance=..., members=...) as [[surface, plane, members, regions], ...].
    Value contact_options(int axis, std::int64_t sign, const Value& clearance, std::vector<std::int64_t> members) const;

    // near(rectangle): the sorted indices of the expanded boxes meeting the closed X/Z rectangle.
    std::vector<std::int64_t> near(double x0, double z0, double x1, double z1) const;
    const std::vector<NumBox>& boxes() const { return boxes_; }

private:
    std::vector<std::int64_t> touching(std::int64_t index, const std::vector<std::int64_t>& indices) const;
    std::vector<NumBox> boxes_;
};

}  // namespace pkc
