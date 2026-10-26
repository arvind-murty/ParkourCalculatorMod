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

## Inventory and remaining work, 2026-10-26

The current delivery is a working continuous optimizer for a compiled fixed-input problem with prescribed events. The autonomous route-solving pipeline still needs problem construction, environment consistency, reliable orchestration and output realization. The user's priority remains the main solving/feasibility path before optimizing how many calls the outer search makes.

| Component | Implemented | Remaining |
| --- | --- | --- |
| Continuous numerical solve | Sparse X/Z/yaw NLP; position, relative and facing rows; endpoint/motion objectives; fixed initial horizontal state. | Reliability/objective measurements on more than two overlapping spans of Real11. |
| Inertia and ladder caps | Automatic scalar replay, inertia continuation, fixed branch inequalities, bounded cap-neighbor trials and incumbent polishing. | Broader near-boundary coverage; better restoration/pattern recovery if measurements show a gap. |
| Contacts and vertical motion | Conditional prescribed wall clipping, normal velocity reset, ladder timing, vertical gravity/jumps/sneak and prescribed floor/ceiling/step events. | Verify finite contact extents and ladder occupancy against candidate geometry; derive and refresh events when they change. |
| Acceptance | Separate scalar replay, all requested rows, branch/contact failures, structured unknown/error/cancelled outcomes. | World/schedule consistency. Continuous acceptance currently certifies only the supplied model. |
| Setup | Explicit compiled JSON requests; Java exporter from a captured TAS; captured ladder/Y attachment. | A convenient compiler from input rows, explicit start state, constraints and supplied environment/event data, without needing a successful recorded route. |
| Runtime | CLI/API, bounded worker, cancellation, atomic output, callbacks, warm/fallback starts, diagnostics. | Automated standalone CI and a broader known-feasible/known-impossible/near-boundary acceptance corpus. |
| Route composition | Returned X/Z trajectories and velocities; Y/ground/collision resume fields; a scalar ladder seam regression. | General slicing/reindexing, start-state injection, seam constraints and overlap repair for optimized multi-window routes. |
| Autonomous search | Numerical solve can evaluate externally supplied fixed-input coefficients. | Input candidate generation/ranking, conservative rejection, selected candidate retention and persistence. |
| Usable TAS output | Returned absolute facings and trajectories. | Convert results back into TAS rows with the effective keys used to compile the coefficients, respecting KEEP/FORCE_45, yaw representation and the selected span. |

The most significant model limitation is that known climbable cells are metadata used to infer a schedule from the recorded path. Scalar validation never queries those cells against the new candidate. Declared wall contacts are planes without block extents, and vertical contacts/steps have no supporting geometry checks. A candidate can therefore satisfy the declared equations without establishing that the same events occur in a world replay. An initial supplied schedule remains a useful approach; validating membership/reach/support and refreshing inconsistent events is sufficient without putting block collision detection inside IPOPT.

Captured referencePosition/referenceVelocity are currently hard acceptance rows, appropriate for the Real11 regression. Production problem construction must distinguish these recorded regression oracles from actual user constraints. Y should be replayed from the current state and current event schedule. If grounded/ladder state changes, re-evaluate tapped jump eligibility and any affected acceleration/friction coefficients rather than retaining the original route's values silently. Fixed grounded jump impulses are supported now; general jump key intent is not yet compiled from a changing replay state.

Recommended order: (1) make one short-window request self-contained and physically consistent, with geometry supplied explicitly or captured once; (2) establish feasibility and objective reliability on a broader corpus with identical reference physics, measuring unknown outcomes and first-feasible/optimization times separately; (3) add general window slicing, state handoff, seam validation and overlap re-solves; (4) add input-pattern search and TAS realization. A small set of alternative exit states is useful at difficult seams; it need not complicate every easy splice.

Feasibility-only mode already exists. Failed IPOPT or fixed-pattern attempts must continue to mean unknown. Simple sound contradiction/reachability bounds or a convex relaxation could certify some rejections later; a local positive-slack minimum alone is not a certificate. Faster feasibility recovery should be guided by the corpus rather than replacing the current model preemptively.

Byte-exact game certification is separate from the user's continuous-model acceptance target. It can be an optional final check for actual TAS use, rather than a prerequisite for finishing this numerical pipeline. Mod UI integration, distributed runtime packaging, additional game profiles and global optimization certificates remain deferred. This inventory follows inspection of current implementation/tests and makes no solver change; the previous clean-branch verification remains applicable.

## Ownership split and integration worktree, 2026-10-26

The user assigned autonomous input/route discovery to another agent and Minecraft/mod integration to this agent. Created `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-mod-integration` on `feature/ipopt-mod-integration`, based on clean solver `498ecc1`. Other worktrees and branches remain untouched. Minecraft/mod integration is now an active responsibility; distributed runtime packaging and extra game profiles remain deferred. No integration adapter or mod behavior is implemented in this handoff turn.

[AUTONOMOUS_AGENT_HANDOFF.md](AUTONOMOUS_AGENT_HANDOFF.md) records the ownership boundary, isolated worktree/import instructions, public API/CLI examples, request compilation requirements, tick and seam conventions, inertia/cap continuation, captured-schedule limits, fixtures, test commands and optional headless export. It explicitly distinguishes compiled fixed-input continuous feasibility from world replay and autonomous key selection. This handoff is intended to let the other agent develop/test independently while integration work proceeds separately.

The next integration work is to expose the existing compiled-problem contract through a mod adapter, obtain start/world/surface state through the existing ports and Minecraft simulator, run the worker off the game thread with cancellation and stale-result protection, and apply effective input/yaw rows through the normal simulation wiring. The initial integration should remain selectable alongside the existing solver and use a local configured runtime, as previously requested. World replay feedback must distinguish continuous-model acceptance from realized game validation. An older mod spike exists at feature/ipopt-local commit 97f545a; it predates current continuation and ladder support and must not be merged wholesale or duplicate the new worker/model.

Smoke-checked the documented Python API on the 200-input ladder fixture from the new worktree: feasible, X 737.514258802979, independent replay accepted. Also checked the documented CLI flow on the 400-input ladder fixture: feasible, X 691.581020917188, zero row violation and no vertical failures. Used the existing worker interpreter read-only. The prior 47-test standalone and clean-base Java results remain the complete suite verification; they were not rerun for a documentation-only handoff.
