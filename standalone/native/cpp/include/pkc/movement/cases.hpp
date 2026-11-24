// Stepping and ceiling case geometry: movement/cases.py and the generator
// helpers of movement/contact_combinations.py. Rows, events and region choices
// are built as dicts, as the Python builds them; choices decode them.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pkc/movement/contact_combinations.hpp"
#include "pkc/movement/generation.hpp"
#include "pkc/value.hpp"

namespace pkc {

Value outside_row(const Array& box, const std::string& axis, std::int64_t sign, std::int64_t state,
                  const std::string& name = "case:outside");
bool exposed_side(const GenerationContext& context, const Object& block, const std::string& axis, std::int64_t sign,
                  double y);
bool convex_corner(const GenerationContext& context, const Object& block, std::int64_t x_direction,
                   std::int64_t z_escape, double y);
Array corner_rows(const Array& box, std::int64_t first, std::int64_t second, std::int64_t x_direction,
                  std::int64_t z_escape, const Array& boxes);

// top_choices: zero or one (rows, regionChoices).
std::vector<std::pair<Array, Array>> top_choices(const GenerationContext& context, const FaceOption& face,
                                                 const std::string& owner, std::int64_t state,
                                                 const std::string& name, bool jumping);
// ceiling_clearances(context, feet, top, jumping=...): row sets keeping a step's, slab boost's or corner jump cancel's
// take-off from `feet` out from under every low ceiling below `top` (one outside row per exposed side per ceiling).
std::vector<Array> ceiling_clearances(const GenerationContext& context, double feet, double top, bool jumping);
// corner_ledges(scene, box, x_direction, z_escape, feet, boxes): the planes, ascending, of the top faces at or below
// `feet` under a corner jump cancel's jump tick.
std::vector<double> corner_ledges(const GenerationContext& context, const Array& box, std::int64_t x_direction,
                                  std::int64_t z_escape, double feet, const Array& boxes);

Value contact_event(const FaceOption& option, const std::string& axis, std::int64_t direction, std::int64_t offset,
                    const std::string& role, Object fields = {});
Array region_choices(const GenerationContext& context, const Value& event, const std::string& owner);
std::optional<FaceOption> reachable_option(const GenerationContext& context, const FaceOption& option,
                                           std::int64_t tick, bool jumping, const Object& label);

}  // namespace pkc
