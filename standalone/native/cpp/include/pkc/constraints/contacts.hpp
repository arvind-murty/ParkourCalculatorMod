// Floor requirements of a run-off: constraints/contacts.py.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pkc/program.hpp"
#include "pkc/spatial/surfaces.hpp"
#include "pkc/value.hpp"

namespace pkc {

// exposed_tops(scene, block) for a scene with a collisionScene: [[x0, z0, x1, z1], ...].
Array exposed_tops(const SceneGeometry& geometry, const Object& block);

// floor_checks(scene, block_ids, profiles, envelopes=envelopes); a null `envelopes` is None.
Array floor_checks(const Value& scene, const std::vector<std::string>& block_ids, const Array& profiles,
                   const Value* envelopes);

// avoid_contact_rows(conflict, clearance) for a conflict's leg, offset, reason, block id, region and position.
Array avoid_contact_rows(std::int64_t leg, std::int64_t offset, const std::string& reason, const std::string& block_id,
                         const Array& region, double x, double z, double clearance);

// walk_off_faces(scene, program, cut, envelopes, landing_boxes): nullopt is None, else
// {solids, region, diagonal, options}. A null `landing_boxes` is None.
std::optional<Value> walk_off_faces(const Value& scene, const SceneGeometry& geometry, const Program& program,
                                    std::int64_t cut, const Value& envelopes, const Value& landing_boxes);

}  // namespace pkc
