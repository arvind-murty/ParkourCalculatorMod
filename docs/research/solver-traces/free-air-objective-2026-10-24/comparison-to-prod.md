# Current solver changes versus recorded production

Baseline is local main/upstream-main02844f4 (same repository ancestor of the investigation). This compares against that recorded release, not an unverified assertion about a newer upstream deployment. [Earlier exhaustive inventory](../full-route-seeded-inventory-2026-10-24/INVENTORY.md) has the commit/file recovery map; endpoint-cap removal in that historical snapshot is now superseded.

| Area | Recorded production | Current branch |
|---|---|---|
|Window size/commit defaults |10,7,5,3,2,1; commit3 then1 |Same |
|Turnaround avoidance |No section detector |Still no section detector; manual research only |
|Provisional objective endpoint |Nominal jump boundary |Advance to first unconstrained AIRBORNE tick, independently of axis/sense |
|Constraint deletion |No experimental cap removal |Earlier experiment removed caps; now removed that behavior, retain all rows |
|Window acceptance |Whole-window exact replay |Full continuous motion with actual inertia over entire horizon, plus committed-prefix exact replay |
|Final route acceptance |All original rows, exact replay |Still all original rows, exact replay |
|Robust margin policy |Normal ladder |Normal ladder remains default; probes support/choose fixed .0003 |
|Dual smoothing |EPS2=1e-14 |EPS2=1e-24 |
|Dual cap |100 iterations |10000 iterations, existing early stop rules still active |
|Intermediate iterates |Decode/check terminal result |Retain improving validated candidates while continuing, prefer saved valid result if terminal fails/worsens |
|Inertia retry initialization |Cold new pattern solve |Compensated multiplier seed when previous assumed model, including its zeroing bands, is feasible within1e-8; supported positional identity-fold cases |
|Decoder tiny-costate default |Objective direction below1e-9 |Unchanged; flagged for later |
|Diagnostics/probes |Original traces/tests |Extensive per-constraint/inertia/dual/SLP/window/committed-state diagnostics and opt-in experiments |
|BnB in receding windows |No new integration |Still none; cap regression coverage added, whole-map graph guards unchanged |

The actual byte-exact physics constants and player input rows are not redefined by these experiments. The continuous fixed/actual-inertia forward helper was added for validation, not as a replacement for the game's physics. Source scope is primarily ten solver files, focused tests, and research records. The current user j127 file modification is pre-existing and unrelated; the earlier committed j147 debug fixture edit is separately identified in the exhaustive inventory.
