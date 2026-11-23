// The event route domain's program readers and cursor: spatial/event_routes.py's support_episodes, initial_cursor,
// advance_cursor and observed_route, on the domain's dict (`data`) and event graph.
#pragma once

#include <string>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

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
