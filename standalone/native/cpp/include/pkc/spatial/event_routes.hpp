// The event route domain: spatial/event_routes.py's builders (surface_groups, build_route_domain, build_event_graph)
// and its program readers and cursor (support_episodes, initial_cursor, advance_cursor, observed_route), on the
// domain's dict (`data`) and event graph.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

// surface_groups(support_components(...)) on support_components' rows, as SurfaceGroup dicts sorted by id.
Value surface_groups(const Value& supports);

// build_route_domain(scene, task, deadline=..., max_cells=...).to_dict(): the domain's dict as its canonical JSON
// reads back (sorted keys). `seconds` is the deadline's remaining budget; `registered` names the movement types.
Value build_route_domain(const Value& scene, const Value& task, std::optional<double> seconds, std::int64_t max_cells,
                         const std::vector<std::string>& registered);

// build_event_graph(data).
Value build_event_graph(const Value& data);

// support_episodes(data, profile, required_owners=...): the profile's landings and named contacts as episodes.
Value support_episodes(const Value& data, const Value& profile, const std::vector<std::string>& required_owners = {});

// initial_cursor(data, roots): the cursor a frame's roots start from.
Value initial_cursor(const Value& data, const Array& roots);

// advance_cursor(data, graph, cursor, request, observed): the cursor after the request's event.
Value advance_cursor(const Value& data, const Value& graph, const Value& cursor, const Value& request,
                     const Value& observed);

// observed_route(data, profile, witness, after=after): a witness's physical evidence.
Value observed_route(const Value& data, const Value& profile, const Value& witness, const Value& after);

}  // namespace pkc
