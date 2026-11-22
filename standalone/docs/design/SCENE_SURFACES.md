# Scene surfaces

[`spatial/surfaces.py`](../../src/parkour_solver/spatial/surfaces.py) keeps one memoized `SceneGeometry` per collision
scene. `scene_geometry(collision_scene)` returns it for `scene['collisionScene']` (solids and optional bounds); it never
rides inside the scene dict. It serves the merged contact faces below and the obstacle clusters and sides of
[avoidance](AVOIDANCE_PASSAGES.md).

## Contact options

`contact_options(axis, sign, *, clearance, members)` gives finite contact domains, one
`ContactOption(surface, plane, members, boxes, regions)` per connected surface. `sign` is the expanded face's outward
normal, minus the motion direction (a landing uses the top, +1; a ceiling hit the bottom, -1). Only `members`
contribute, every solid of the scene clips (coverage bounds do not), and the union is eroded by the clearance along its
horizontal tangent axes (the union of the shrunk maximal rectangles), so the seam between touching members stays inside
one region; a clipping neighbour's edge stays exact, because touching it is allowed.
`movement.contact_combinations.scene_contact_options` asks with every offered block as a member.

Every generator asks with the offered blocks' solids, so landings, steps, flight cases and ceilings are one choice per
merged face (`contact_combinations.top_faces`, named by `face_name`: its blocks' owners joined). Coplanar tops merge
when their player-space rectangles touch, i.e. less than a player's width apart, and a face's region excludes positions
a taller neighbour blocks. Stepping cases test sides and corners per member block (a seam against a flush member is
neither) and drop a member's choice that repeats another's; corner tangent rows span the members' shared side. Unwanted-floor checks use clearance 0, one check per exact exposed rectangle.

## Members, not a merged box

Movement events and full-scene checks carry the member `boxes` of a contact; a merged face is never passed as a
fictitious AABB or through an arbitrary first member. Plane-only dedup does not preserve coverage, so combination keys
include the surface and regions. A point-player physical face and its player-expanded contact domain are not
interchangeable: body expansion can bridge a physical gap between two faces, and nearby solids can clip the
player-valid region further.
