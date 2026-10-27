# Block-route selection prototype

2026-10-26. Implemented as separate orchestration features after the run-ticks and ordinary-profile checkpoints, now extended to different heights and short staircase profiles. Only the start and goal support IDs are fixed. The intermediate supports, their order and the transition profile are search variables. Inputs are fixed to sprint-jump on takeoff and sprint 45 otherwise, with free facings on every input.

## Supported scene

`sceneModel: support_planes` declares rectangular floor supports at arbitrary heights, normal 1.8.9 movement and a fixed initial state. `coplanar_support_planes` remains accepted for the original equal-height fixtures. The model includes ordinary jumps and a prescribed short stair-jump family, but no ceilings, ladders, special surfaces or general finite side-collision events. This is a floor/contact route family, not a general world solver.

The existing Y simulation derives an ordinary profile independently for every directed height pair: flat, +.5, +1 and -1 produce 12, 10, 9 and 14 inputs. A missing profile is a declared-family exclusion, not a proof against other contact mechanics. At a landing or run input, strict overlap with the selected actual support is still required. Supports are never merged into a bounding rectangle over gaps.

Descending past another support's top creates an earlier-floor exclusion at that transition-relative input. Every accepted candidate is checked against those actual support footprints. If it overlaps, orchestration branches over four outside-face positional constraints and re-solves within a bounded contact-search budget. This includes avoiding the departure floor before landing on a lower target. The rows retime with added runs; a limit or numerical failure remains unknown. Independent strict checks prevent row tolerance from accepting the unwanted contact.

For a valid half-block stair rise, the existing Y step model supplies a 2-input profile: grounded takeoff, then one air input. The first movement steps onto the higher surface and cancels the upward carry. The candidate must be outside a selected target face at takeoff and overlap the target at the next state; floor support is checked there before the landing-producing air input. The graph keeps all four approach-face alternatives and the ordinary longer jump as distinct profiles for the same block pair. The step model's promoted-float .6 maximum applies; a .25 rise is not automatically assigned this jump-cancel profile.

`stairBoost` defaults to true. Setting it false removes short profiles from enumeration; ordinary candidates must still avoid an unplanned first-movement stair cancellation. Those contact checks also generate retimed outside-face rows if needed. Every accepted complete prefix now passes the existing scalar Y validator with its selected floor/step contacts and inserted ground inputs, and its ground flags must match the compiled horizontal regime.

Example input, [fixtures/blocks-route-choice.json](fixtures/blocks-route-choice.json):

```json
{
  "version": 1,
  "sceneModel": "coplanar_support_planes",
  "initial": {
    "position": [0.5, 1, 0.5],
    "velocity": [0, -0.0784000015258789, 0]
  },
  "startBlock": "start",
  "goalBlock": "goal",
  "blocks": [
    {"id": "goal", "bounds": [7, 8, 0, 1], "top": 1},
    {"id": "start", "bounds": [0, 1, 0, 1], "top": 1},
    {"id": "middle", "bounds": [3, 4, 0, 1], "top": 1}
  ],
  "maxJumps": 3,
  "maxRunTicksPerJump": 2,
  "maxTotalRunTicks": 3
}
```

The array is an unordered set of available supports, not a required route. Air times, jump ticks, facing constraints, intermediate waypoints and recorded inputs are absent. Support IDs must be unique. Repeated visits during search are allowed, including jumping again on the same support to gain momentum. Each occurrence receives a distinct ID when compiling the ordered inner route.

Other limits are `maxPrefixes` (default 200), `maxQueuedPrefixes` (4000) and `maxFlightInputs` (200). `maxJumps` defaults to 4. These are explicit bounds on the prototype's candidate family/work, not claims that further routes are impossible.

## Optimistic transition graph

The existing vertical recurrence derives each edge's ordinary profile. The existing Java model compiler supplies movement magnitudes and friction, cached by duration; `ConstraintDeriver` supplies support footprints. A norm bound covers incoming horizontal momentum at every future input:

```text
V = max(initial stored speed norm, max_t friction[t] * magnitude[t] / (1 - friction[t]))
```

Per-axis inertia zeroing can only reduce the norm. Since movement has norm at most `V + magnitude[t]`, multiplying it by that input's friction preserves the bound. A small numerical allowance covers arithmetic rounding.

The graph overestimates each transition's reach by starting with this global speed bound and adding every flight input's full magnitude in a useful direction. Reach stops at the pre-horizontal landing-check state, not the later first-grounded state. It also adds a full input's movement as a run-off allowance: a jump may begin one position beyond the supporting footprint while retaining the preceding grounded flag.

If the minimum distance between two expanded footprints exceeds that optimistic radius, their edge is excluded. Otherwise the edge remains a possibility, not a feasibility result. Reverse graph reachability removes supports with no possible continuation to the goal. A shortest-hop calculation also discards prefixes that cannot reach the goal within `maxJumps`.

The bound is deliberately broad. It includes any free facing, carried velocity, repeated visits and allowed run count in this normal-input family. It does not use a fixed four-block radius, a sampled trajectory, a no-turn condition or a local IPOPT failure as a proof. The randomized witness audit checks that valid continuous free-facing trajectories survive the edge filter.

## Route-prefix pool and numerical evaluation

A shared priority queue holds full support-ID sequences plus their transition IDs. Different profiles or approach faces for the same support sequence are not deduplicated. The priority favors a lower possible flight-input count, fewer jumps, feasible parents, lower observed parent run counts and proximity to the goal. Reverse shortest flight-time and hop counts come from the optimistic graph. These choose evaluation order; no elapsed-time optimum is claimed. A parent's best observed run count is not a lower bound on a child: later constraints may cause the inner search to change earlier allocations and facings.

Each prefix is compiled into an ordinary-profile route and evaluated by the existing first-failure run-ticks/IPOPT path, starting from the original initial state. All earlier facings and run counts can be searched again. One previously solved exit position/velocity is never frozen as the only arrival at a block.

There is no visited-block deduplication. Different paths reaching the same block remain distinct. An unresolved prefix can still be extended: a more complete target and fresh guesses may recover a local search miss. Numerical worker errors stop the search rather than masquerading as route infeasibility.

Default termination is the first accepted start-to-goal route. `--improve` keeps searching for lower-run or equal-run shorter accepted routes until a limit or a zero-run witness is reached. Zero certifies the nonnegative lower bound for added run ticks within the model family; no elapsed-time optimality is claimed. Positive counts are best found, not certified minima.

Pool truncation is explicitly reported as `heuristicPoolDrops`, separate from graph-reach and hop-horizon exclusions. Cancellation retains an accepted incumbent. A graph compilation timeout and budget/search exhaustion return unknown. Results retain the input scene, graph, attempts, unresolved prefixes, chosen block IDs, allocation, compiled inner route, profile traces and accepted trajectory.

## Run

Prepare the Java compiler and Python runtime as in [README.md](README.md), then:

```sh
PKC_IPOPT_PYTHON="$PWD/.venv-ipopt/bin/python" \
python3 -m standalone.autonomy.solve_blocks \
  --input standalone/autonomy/fixtures/blocks-route-choice.json \
  --output standalone/autonomy/build/blocks-route-choice-result.json \
  --seconds 45 --route-seconds 4 --candidate-seconds 1 --retries 0
```

`find_blocks(scene, compiler, ...)` is the Python API. `route_seconds` bounds each full-prefix run-ticks search; `candidate_seconds` bounds each individual numerical attempt. The outer deadline includes graph compilation and all prefix searches. The CLI writes its result atomically and uses the same status exit codes as the existing solver CLI.

## Verification

- `blocks-route-choice.json`: chooses `start -> middle -> goal` from an unordered set including an off-axis alternative and a disconnected remote support. No route order or air times are supplied. The accepted allocation is `[0,0]`.
- `blocks-momentum-choice.json`: an isolated narrow intermediate support is reachable, while the observed two-jump continuation through it remains unknown at the stated budget; the pool finds `start -> around -> goal`. Local unknown is not labeled impossible. The pool's accepted result is compared with exhaustive evaluation of every route in this small one/two-jump family.
- `blocks-required-run.json`: the chosen direct route requires one added ground input, producing a 13-input accepted schedule. This checks that route selection composes with run-ticks search rather than assuming all edges are immediate jumps.
- `blocks-height-route.json`: chooses an intermediate +1 support to reach a goal +2 above the start, deriving `[9,9]` air times and an 18-input accepted trajectory. The reverse -1 graph edge derives 14 inputs.
- `blocks-lower-target.json`: a 14-input descending route passes the earlier-floor and combined X/Z/Y model checks.
- `blocks-staircase.json`: chooses two `[2,2]` short profiles and one inserted run input, producing a 5-input accepted trajectory. A separate integration case retains the ordinary 10-input jump when all short approaches fail to reach.
- An earlier-floor control case cannot exit a wide upper support before its Y crossing. A horizontal-only lower-target witness is rejected and all four outside-face repairs fail within the declared family; no route is accepted through the earlier floor.
- Unit tests retain different paths to the same support, extend unknown prefixes, permit repeated-support jumps, compile distinct repeated visits, reject composite/gap unions and unsupported geometry, distinguish compiler errors from timeouts, retain an incumbent on cancellation and expose candidate/pool limits.
- A seeded audit forwards 100 free-facing, automatic-inertia witnesses through the existing scalar model and checks that the optimistic graph retains their landing edge, including varied initial momentum.

The combined orchestration suite passes 67 tests with the actual Java compiler and IPOPT runtime. [Height/stair artifacts](experiments/height-and-stairs/README.md) include self-contained full numerical requests with generated Y contacts. Native core code and the numerical worker are unchanged. Ceilings remain the next pass; [BLOCK_SOLVER_PLAN.md](BLOCK_SOLVER_PLAN.md) tracks the remaining profile and geometry families.
