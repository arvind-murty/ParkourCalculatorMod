# User-supplied blocks and mod-derived landing bounds

2026-11-02. This supersedes the initial JSON-only footprint hypotheses in INVENTORY.md. The user supplied seven geometries and asked for bounds incorporating the player hitbox and neighboring obstacles, using the mod's existing derivation.

## Coordinate interpretation for this report

Horizontal integer coordinates identify unit block cells. B1's ranges include both endpoint block coordinates, so X742 through746 means physical X=[742,747]; Z-1104 through-1105 means physical Z=[-1105,-1103]. Explicit Y=[0,h] is a physical vertical extent. B7's shorthand7 is provisionally read as topY7 with Y=[0,7]. If the user intended a single block at block-coordinate Y7, its Y range would instead be[7,8], and its top8. These conventions are recorded in user-blocks-provisional.json and must remain explicit until verified. The literal geometric-X-endpoint interpretation of B1 would exclude the saved start X746.5 from support, which is why the inclusive-cell interpretation is used here.

| ID | Physical X | Physical Z | Physical Y |
| --- | --- | --- | --- |
| b1 | [742,747] | [-1105,-1103] | [0,5] |
| b2 | [741,742] | [-1104,-1103] | [0,6] |
| b3 | [738,739] | [-1101,-1100] | [0,7] |
| b4 | [734,735] | [-1100,-1099] | [0,7.5] |
| b5 | [730,731] | [-1096,-1095] | [0,6] |
| b6 | [730,731] | [-1095,-1094] | [0,20] |
| b7 | [729,730] | [-1095,-1094] | [0,7], provisional shorthand interpretation |

## Derived player-center landing regions

Executed the existing Java ConstraintDeriver.deriveFootprint and SupportOverlap code, with Minecraft1.8.9 collision mode and all seven supplied boxes as neighboring obstacles. Reference point is the center of each support's top. Checked facing yaw0 and-90; both produce identical rectangles for these references. The existing helper uses HALF=0.30000001192092896 and body-height1.8. Display rounded to0.1; exact doubles are saved in derived-player-bounds.json.

| ID | Player-center X | Player-center Z | Feet Y on top |
| --- | --- | --- | ---: |
| b1 | [742.3,747.3] | [-1105.3,-1102.7] | 5 |
| b2 | [740.7,742.3] | [-1104.3,-1102.7] | 6 |
| b3 | [737.7,739.3] | [-1101.3,-1099.7] | 7 |
| b4 | [733.7,735.3] | [-1100.3,-1098.7] | 7.5 |
| b5 | [729.7,731.3] | [-1096.3,-1095.3] | 6 |
| b6 | [729.7,731.3] | [-1095.3,-1093.7] | 20 |
| b7 | [728.7,729.7] | [-1095.3,-1093.7] | 7 under the shorthand interpretation |

B2 clips B1's lower X limit from its unblocked741.7 to742.3. Tall B6 clips B5's upper Z to-1095.3 and B7's upper X to729.7. These reproduce the narrower B5 and B7 authored player-center constraints without inventing narrow physical blocks. The earlier hypothesis that B5's physical Z footprint might be0.4 wide is superseded by the explicit user-supplied full footprint and neighboring obstruction.

These are top-surface standing/landing rectangles. They do not describe a complete swept movement between surfaces or prove reachability of a top. The mod's corner derivation can select a rectangle based on click/reference and facing; a selected rectangle is not a proof that no alternative collision-free corner region exists. Future orchestration must preserve full collision boxes and existing passage/case/scene validation rather than freeze one GUI rectangle as the universal space for every movement regime.

## Source and reproduction

The UI path is ConstraintKeyController.onKey -> ConstraintDeriver.deriveFootprint, with selected support, nearby collision boxes, click point and facing. SupportOverlap handles precise legal support endpoints. No player-box or collision-constraint logic was reimplemented.

DeriveSelectedFootprints.java is a source-mode invocation adapter for those existing Java methods. Input, exact outputs, invocation and hashes are stored beside this document. The standalone convenience footprints action currently calls deriveFootprint with obstacles=null; this report intentionally calls the shared Java method with the actual neighboring boxes instead. Existing full-scene collision checks remain separate. No mod code, solver model, limits or runtime footprint endpoint was changed, and no route solve has run in this bounds-verification step.
