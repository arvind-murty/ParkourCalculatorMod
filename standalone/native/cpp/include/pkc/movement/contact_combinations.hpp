// Contact events on JSON values: movement/contact_combinations.py.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pkc/spatial/surfaces.hpp"
#include "pkc/value.hpp"

namespace pkc {

// merge_contacts(contacts, scene=scene) on (state, axis) keys; `axis` stands in for every contact's own.
Array merge_contacts(const Array& contacts, bool scene, std::optional<std::int64_t> axis = std::nullopt);
void validate_contact_events(Object& route);  // edits the route in place, as the Python does
Value bounding_region(const Array& regions);
// finite_rows(axis, region, **at): `at` holds the trailing keys (t1, or leg and offset).
Array finite_rows(int axis, const Array& region, const Object& at);
struct CompiledContacts {
    Array rows, horizontal, scene, vertical;
};
CompiledContacts compile_contact_events(const Object& route, const Array& events, std::int64_t ticks);

// spatial/surfaces.ContactOption: one connected face of the member solids.
struct FaceOption {
    std::string surface;
    Value plane;
    std::vector<std::int64_t> members;
    Array boxes;    // the members' collision solids
    Array regions;  // player-space rectangles [u0, v0, u1, v1]
};

using SolidOwners = std::map<std::int64_t, std::set<std::string>>;

// scene_contact_options(scene, axis, sign, members) on the scene's geometry (None members: the offered blocks').
std::vector<FaceOption> scene_contact_options(const Value& scene, const SceneGeometry& geometry, int axis,
                                              std::int64_t sign,
                                              std::optional<std::vector<std::int64_t>> members = std::nullopt);
SolidOwners solid_owners(const Value& scene);
std::string face_name(const FaceOption& option, const SolidOwners& owners);
// face_owners(scene, name) over the scene's top faces; nullopt `name` is None.
std::set<std::string> face_owners(const std::vector<FaceOption>& top_faces, const SolidOwners& owners,
                                  const std::optional<std::string>& name);

}  // namespace pkc
