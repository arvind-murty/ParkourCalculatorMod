# Standalone IPOPT findings

## Scope and provenance

The standalone package supplies a local Python CLI/API, a separate CasADi/IPOPT worker, independent scalar validation, time limits/cancellation, warm and fallback starts, inertia continuation, prescribed wall contacts, ladder component caps and vertical replay. Jump inputs are taps. It does not change the mod, native solver or save schema. See [README.md](README.md) for setup, request fields and usage.

On 2026-10-26 the user requested separating this package from inherited solver experiments. The clean branch `feature/ipopt-clean` starts at upstream main `485c0a8cd700183b59fa6bf7b9028e3ee4938ab1` and imports the final 24 standalone files from research head `5ef088b5f9d413b70a386a3ca645506ee665bff5`, plus this findings record. No existing commits are amended or rewritten. The original `feature/ipopt-standalone` branch remains intact on the user's fork as the research archive.

The archived branch contains the earlier generic-solver comparison and feasibility harnesses, their benchmark outputs, native Java solver experiments and detailed research traces. Neither tools directory is a standalone dependency. The optional Java exporter uses the existing core compiler and test fixture reader. Worker dependencies are external, pinned in requirements.txt; no virtual environment is distributed.

Archived implementation and detailed evidence: [ladder implementation record](https://github.com/arvind-murty/ParkourCalculatorMod/blob/5ef088b5f9d413b70a386a3ca645506ee665bff5/docs/research/ipopt-ladder-implementation-2026-10-26.md), [result provenance](https://github.com/arvind-murty/ParkourCalculatorMod/blob/5ef088b5f9d413b70a386a3ca645506ee665bff5/docs/research/solver-traces/ipopt-ladder-2026-10-26/provenance.json).

## Modeling findings

Independent scalar replay determines acceptance from returned facings; worker success and returned state values are not trusted. Automatic inertia and conditional wall clipping must satisfy every requested constraint at 1e-7 tolerance. A missed plane cannot reset velocity even if the miss is below that tolerance. Unsuccessful local search is unknown, not certified infeasibility.

Retaining the best valid incumbent and polishing its actual inertia pattern fixes a material warm-start regression. A previous no-inertia path can seed a subsequent inertial/ladder solve, but it must pass the changed model before becoming an incumbent. A distinct fallback shares the total budget.

Ladder caps apply independently to X/Z after acceleration and inertia, before collision reach and friction. The smooth NLP fixes free/positive/negative cap branches and tries bounded neighboring regimes. An analytic regression confirms that branch changes recover an optimum missed by the initial fixed branch. Climbing uses ladder occupancy and the previous movement's horizontal collision; its upward movement occurs on the following tick. Stored vertical carry and collision state survive span seams.

Real11 ladder occupancy is prescribed from captured climb signatures and climb-proven cells, including states 189 and 361 without a climb signature. Through 400 inputs the ranges are 136-152, 189-193, 356-362 and 395-399; final state 400 is also on a ladder. Vertical replay includes prescribed floor, ceiling and step events. Step-ups during jumps and landings explain the initial Y mismatch. General geometry discovery, ladder membership checks against a changed path and full world collision validation remain outside this model.

The copied 200/400 fixtures retain all original compiled constraints and horizontal contacts. Their ladder/Y copies have 22/34 ladder input ticks, 50/81 vertical contacts and 8/9 steps respectively. Captured Y positions, stored vertical velocities and ground states replay exactly. The source capture file SHA-256 is af5d2d1bc6e0ab466f1b6cf309f3a26e76e4a63765ac8b15ff02fb4ad55d28bf. Local optimization does not certify global objective quality; shorter spans remain preferred. The original long route has a recorded constraint/model mismatch near states 651/652.

## Clean-branch verification, 2026-10-26

All 47 standalone tests passed in 24.005 seconds with Python 3.12.2, CasADi 3.8.1 and NumPy 2.5.4. The ladder cases solved with zero row violation: 200 inputs in .666 s at X 737.514258802979, 400 in 3.671 s at X 691.581020917188. Tests also cover the original no-ladder fixtures, warm-start recovery, actual CLI capture attachment, cap optimization, vertical timing and resume state. These are local single-route measurements, not a general reliability benchmark.

The clean-base Java fast gate `:core:test` passed: 849 tests, zero failures/errors, two skipped. The optional exporter compiled with Java 8 compatibility and exported all 341 constraint rows for 400 inputs from the original capture. Its normalized physical hash exactly matches the saved original 400-input fixture: 295b76ca7ee3f215fcd9714660119c0552ae951ac0e9f2f57b152da7ea97f0b3. No exporter or native-code adaptation was required. The fresh export also solved with captured ladder/Y attachment and passed independent validation with exact recorded Y and stored vertical velocity.

Only the 25 files under standalone/ipopt are added relative to upstream main. No tools benchmarks, chronological research traces or inherited native solver changes are included. The previous branch remains available with its complete history. Publication uses the user's fork and the new branch, without force pushing or amending any commit.
