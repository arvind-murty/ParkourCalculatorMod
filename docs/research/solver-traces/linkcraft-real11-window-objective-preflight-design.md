# Avoiding objective/wall cancellation before solving: window-objective design

This is a proposed design, not an implemented policy or a measured comparison. Fixed margin 0.0003 and shared continuous/replay acceptance remain the intended experimental settings. No tests or new solves ran for this discussion.

## Existing small-costate fallback

`FacingPrefold.expand` and `JumpLinearModel.recoverAlongCostate` replace a costate when `gx*gx+gz*gz < 1e-18`, i.e. magnitude below 1e-9. They substitute the raw objective direction, then compute the yaw required to accelerate along it with that tick's keys. For +Z this is direction `(0,1)`, not necessarily facing zero. It is a heuristic direction choice, not a feasible-primal recovery procedure; validation still decides acceptance. Our measured problematic final costate (~9.82477e-8) exceeds the fallback threshold, so that particular failure is due to smoothing/recovery, not this fallback.

## Tick shifts versus motion objectives

The identity is `Z[T+1] = Z[T] + displacementZ[T]`. Thus extending a positional objective by one tick adds one tick's motion to its score. It is equivalent to maximizing that displacement only if Z[T] is fixed; a one-sided bound does not fix it. If the extra tick's input is optimized too, it also contributes acceleration. For a backward shift, `Z[T-1] = Z[T] - displacementZ[T-1]`: at fixed Z[T], maximizing the previous position would minimize that final displacement, not maximize it. Do not treat both signs as equivalent velocity maximization.

The user clarified that the main alternatives are a **whole-jump** shift (change which jump ends the lookahead) and a terminal-motion objective; a one-tick shift is largely a short lookahead position/motion mixture.

## Concrete initial-window alternatives

The actual jump boundaries in the imported scenario give:

| Window | End state | Direct matching constraint for MAX Z there |
|---|---:|---|
| Jumps 1–6 | 64 | None: Z64 has a lower bound, which does not directly cap MAX Z |
| Jumps 1–7 | 68 | Z68 <= -1092.699999988079: exact objective/wall match |
| Jumps 1–8 | 71 | No direct Z71 constraint |

For this map, extending to 1–8 is the first whole-jump alternative to probe. It retains the troublesome Z68 wall and adds the next jump's constraints (the X/Z landing box at tick 70), while moving the objective beyond that wall. Shortening to 1–6 removes jump 7 from lookahead, including its Z68 requirement; it is a less informative fallback. This recommendation is a structural inference, not an observed solve result or proof that 8 jumps will be stable.

Terminal +Z motion at the existing end is another candidate. Its affine objective is proportional to `Z68-Z67`, rather than `Z68`. A position cap alone can no longer cancel its entire coefficient vector. But this window also contains a lower bound Z67 >= -1093.3000000119207, so the pair implies `Z68-Z67 <= 0.6000000238417`. If both bounds are active, their normals can cancel the motion objective too. That implied cap may be far above reachable terminal motion and thus inactive; structural representability alone is not proof of a numerical problem.

For ordinary authored 1.8 ground/air movement, terminal displacement and post-friction carry differ by the fixed positive final friction factor, so their maxima agree at a fixed end tick. This statement does not generalize to every special surface. A velocity alternative means a signed component or projection in the intended onward travel direction, not unconstrained speed magnitude. It should not default blindly to +Z on every map segment.

## Preflight detection and proposed choice

1. Use the existing jump boundaries to enumerate nominal W, W+1, and W-1 whole-jump ends, with the start and commit span unchanged. Never truncate inside the committed prefix or past the route endpoint.
2. Before solving, flag a direct objective cap: MAX Z with an upper Z bound at that state, MIN Z with a lower Z bound, and the analogous X cases. Equality pins cap either direction. The opposite-sided bound alone is not this direct cancellation risk.
3. For a more general detector, compile the same affine objective and wall coefficients used by `JumpLinearModel`. Flag a wall positively proportional to the objective vector (or either sign for an equality). Facing folds should be applied before this comparison where they eliminate variables. This also detects paired rows that happen to become the objective after folding.
4. Treat objective vectors expressible as a nonnegative combination of inequality normals (plus equality span) as a broader structural risk, not an automatic reject. Multipliers can cancel the objective only when the relevant constraints support that optimum. Feasible movement ranges and constraint geometry can make an implied cap unreachable and therefore inactive.
5. Proposed policy for the direct-cap class: prefer W+1 when it removes the direct cap and is within the allowed lookahead; consider a directional terminal-motion objective without shortening the original lookahead; use W-1 as a fallback when the reduction in lookahead is acceptable. No option guarantees good conditioning or a feasible continuation. Keep the shared validator and fallback solves.
6. Log the preflight decision, e.g. `nominal=7 end=68 MAX_Z capped by Z@68; selected=8 end=71 no matching endpoint wall`. Store the actual endpoint, objective kind/direction and policy in cache identity so results for different surrogate objectives cannot be mixed. Terminal windows retain the user's true final objective.

This can avoid an obvious unstable attempt using only constraints, without first running the dual or knowing the actual costates. It predicts risk; it does not establish which walls are active or guarantee all future costates remain nonzero.

## Current implementation details relevant to a future prototype

`LongRunSolver.runHorizon` currently uses a positional +Z objective for intermediate windows and takes each end from jump boundaries; this preflight does not exist yet. `Objective.Type.MOTION` is currently evaluated as a position difference only in the custom-direction branch. `JumpLinearModel.objectiveVectors` checks `isMotion` even for scalar-axis objectives, but `Objective.evaluate`'s scalar branch returns position. A prototype must avoid that inconsistency (use the supported custom-direction motion form, or make scalar motion evaluation consistent), and make logs/alternate objectives/cache keys preserve objective type and direction. No fixes for those details are implemented in this note.

Sources: `LongRunSolver.jumpBoundaries`/`runHorizon`, `Objective.evaluate`, `JumpLinearModel.objectiveVectors`/`compileWall`/`recoverAlongCostate`, `FacingPrefold.expand`, the saved initial-window constraint CSV, and the original TAS `~/Downloads/LinkcraftII_Competition_01/real/11.json` at states 64, 67, 68, 70 and 71.


## Direct detector implemented

The [direct objective-bound detector](linkcraft-real11-objective-bound-detection.md) now implements the common absolute X/Z case and reports preflight findings before LongRun objective calls. Six focused unit tests and the fixed 7-ladder capture probe pass. Whole-jump selection remains proposed work; detection is recorded in a commit before the independent EPS2 change.
