# Objective on the first unconstrained airborne tick

2026-10-24. Supersedes temporary endpoint-cap removal and axis/sense-specific endpoint selection. User explicitly wants a tick with NO constraint, not merely no matching cap, and it must be airborne rather than a landing/ground state. [Metadata](metadata.json) pins implementation and recorded production baseline.

## Implemented rule

For each PROVISIONAL window, start at its nominal jump endpoint and scan forward to the first tick satisfying both:

1. No original route constraint refers to this tick as t1 OR t2, regardless of axis, comparator, mode or objective direction.
2. The authored scenario marks it airborne: slipAt(tick) is NaN, exactly the contact convention already used by JumpLinearModel and ExactJumpModel.

The helper takes no objective argument. A constraint referring to the tick still excludes it even if the row's other reference lies beyond the current window. Both references of relative/facing rows count. The lookahead scenario extends through the selected state and includes ALL original rows wholly within that horizon. All alternate objectives and centered SLP use this same expanded window. The nominal jump count still chooses the commit boundary; adding objective ticks does not add committed jumps. Constraints are no longer deleted in either provisional or terminal window solves. The last window preserves the original route objective/end tick, even if constrained. If there is no eligible tick before the authored route end, that provisional rung is skipped and the existing ladder tries another window. No ticks/inputs are invented past the route.

The cache key is namespaced for this placement policy and uses the selected horizon/commit exit. LRS-AIR logs nominal and selected endpoints. LRS-WIN's jump range remains the nominal jump window, while its tick range includes any extension. The removed withObjectiveBoundRemoval probe knob and deletion helper no longer apply.

## Does this avoid turnarounds?

There is still NO automatic recognition/exclusion of an entire turnaround section. The prior manually banned jump ranges and after-turn endpoint experiments remain test-only. This implementation avoids constrained/ground endpoints. A free airborne state may still lie inside a turnaround. It is a local tick-placement rule, not a turn detector or a guarantee of nonzero costates/global optimality.

## Real input selections

[Complete sampled endpoints](selected-ticks.csv) from the original real/11 scenario:

| Nominal endpoint | Selected tick | Extension |
|---:|---:|---:|
|64 |69 |5 |
|68 |69 |1 |
|75 |91 |16 |
|77 |91 |14 |
|88 |91 |3 |
|90 |91 |1 |
|99 |104 |5 |
|113 |121 |8 |

Unlike the earlier Z/MAX103 test,103 is still excluded by this objective-independent rule because a route constraint references it. Tick104 is the first eligible airborne tick after nominal99. Different nominal window rungs may extend to the same eligible state; caching can reuse equivalent horizon/seed/commit combinations.

## Focused checks

20 tests pass: FreeAirObjective6 (including the explicit real-route selection probe), LongRunConfig5, ReplayPrefixValidation4, WindowCandidateValidation5. Covers all row kinds/both references, ground skipping, already-free ticks, axis/sense independence, no invented route-end tick, normal ladders and validation behavior. These tests verify selection and existing validation; the full808-tick route has NOT been rerun after this policy change. Earlier ten-ladder47/161s reports used temporary cap removal, not this new rule.

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.freeAirObjectiveProbe=true :core:test --tests de.legoshi.parkourcalc.core.anglesolver.solver.FreeAirObjectiveTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.LongRunConfigTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.ReplayPrefixValidationTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.WindowCandidateValidationTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/free-air-objective-2026-10-24/focused.log 2>&1
```

Execution in the existing isolated core-only worktree, with main source copied. No full suite. Decoder, seeding criterion, retention, margins, dual iteration cap and physical inputs unchanged. User j127 fixture untouched. Source snapshots and JUnit XMLs archived.
