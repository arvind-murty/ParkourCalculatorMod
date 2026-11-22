// Ladder columns, regions and climb constraints: scene/ladders.py, on JSON values.
#pragma once

#include "pkc/value.hpp"

namespace pkc {

Value ladder_columns(const Value& cells);
Value ladder_regions(const Value& cells);
Value region_rectangles(const Value& column, const Value& y);
bool region_contains(const Value& column, const Value& position);
Value region_axis(const Value& column, const std::string& axis);  // [lo, hi]
Value climb_walls(const Value& column, const Value& solids);
Value movement_contexts(const Value& column, const Value& solids, bool include_fall);
Value entry_channels(const Value& column, const Value& wall);
Value outside_row(const Value& face, const Value& state, const Value& margin, const Value& name);
// schedule_constraints(...) as [rows, contacts, scene_contacts].
Value schedule_constraints(const Value& schedule, const Value& column, const Value& wall, const Value& entry_face,
                           const Value& exit_face, const Value& start, const Value& margin);
Value exit_faces(const Value& column, const Value& target);

}  // namespace pkc
