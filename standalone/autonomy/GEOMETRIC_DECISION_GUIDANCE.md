# Use the existing cell route as the first decision guide

2026-10-31. User asked why another trajectory initializer is needed when the cell route already supplies the rough spatial path. Proposed simply connecting route endpoints and using that geometry to define local progress objectives. Recommendation: test this geometric proxy first. Mixed-input and provisional full-route solves are optional later improvements, not prerequisites. No new runtime code or benchmark was added in this explanatory turn.

## Existing representation already supplies a guide

`cell_routes/routes.py` constructs `Corridor.points` from the original start, successive finite portal midpoints and a point in the goal landing region. It also stores ordered cell-stage paths, route signatures and corridor projections. The generator builds consecutive line segments through convex cells. A prescribed point route can supply the same kind of geometric guide.

Thus a cheap untimed polyline already exists. It supplies the broad direction and upcoming turns that motivated cell decomposition. It does not need to satisfy gravity, acceleration or an exact tick schedule to serve as an objective guide. Its numerical midpoint choices are references; the solver must retain freedom to cross elsewhere in the portal or land elsewhere in the allowed goal region.

Connecting endpoints means connecting the successive cell/portal anchors within the selected corridor. A single global start-goal chord could erase the intended obstacle side or cross a blocked region. Optional shortcutting must preserve corridor membership and ordered progress. Straight segments through the existing points are sufficient for the first baseline; no new path-smoothing algorithm is required.

## Minimum local evaluator

For a pending decision, select a common guide point beyond that interaction, such as the next useful exit or a little farther along the polyline. Determine a common horizontal forward direction d for its alternatives. Do not choose each candidate's direction after seeing its optimized result.

For tick 4 versus tick 5 alternatives, pick a common later absolute state h. Solve each candidate's original-state prefix through h with its actual discrete movement/contact constraints, all earlier facings variable, and objective:

```text
maximize d dot p[h]
```

The guide supplies where to make progress. The existing continuous physical model determines how far each candidate can get while satisfying its selected input/contact effects. This needs neither a mixed future-contact model nor a solved full-route reference.

The standalone objective already supports an arbitrary horizontal direction through `customYaw`; both `standalone/ipopt/model.py` and `worker.py` use the corresponding signed X/Z projection. This is enough for a direction-only prototype. Adding a velocity term is a separate change, not an existing combined objective silently assumed available.

Use the full selected prefix from the original initial position and velocity. A prior solved position/velocity at the decision is a seed, never a fixed endpoint. The evaluated candidate includes real step/ceiling effects; the geometric guide itself need not choose a method or encode those dynamics.

Where alternative methods have different durations, do not compare progress at their separate endpoints without accounting for time. Prefer a common absolute lookahead horizon with a well-defined local continuation. An alternative comparison can use arrival time at a common downstream cross-section and its outgoing state. Both are heuristic local objectives, and the experiment must state which it uses. Extending the lookahead past another unresolved physical interaction may require including that interaction in the local window rather than pretending its effect is known.

## Small extensions only if measurements require them

Position-only progress is the first baseline. A candidate farther forward may have poor outgoing momentum, so a subsequent ablation can test:

```text
d dot (p[h] + ell*v[h])
```

Here ell is a short lookahead measured in ticks, not a claimed exact suffix value. A turn-aware guide point or next-segment direction may help before corners. Do not introduce several weights, terminal models or a dynamics initializer before establishing whether the simplest score fails.

Use the ordered active route segment and recorded portal progress. Global nearest-point projection can jump ahead at a self-intersection or on a revisited cell. A polyline progress score should include accumulated segment distance and preserve its route stage. A fixed tangent score is only locally meaningful and must not be compared blindly across unrelated stages.

The geometric Y path is not a prescribed flight trajectory. Use Y/contact conditions from the actual candidate and the corridor/opportunity representation. For horizontal facing guidance, use the horizontal direction toward a useful later route point. A vertical-only guide segment must not produce an undefined direction or reward height simply for being higher; use a subsequent horizontal direction or a relevant local contact/progress criterion.

## What can fail, and why it is still the right baseline

The path does not predict useful braking before a turn, a ceiling shortcut's earlier landing, the value of optional support contact, or a pair of coupled method changes. A short horizon may miss those downstream effects. These are possible ranking errors to measure; they do not establish that a more expensive whole-route initializer is necessary.

Retain a small configurable set of promising alternatives, apply genuine local feasibility checks, and validate complete routes with the actual configured final objective. A lower geometric score is not proof of infeasibility or dominance. The score is not a bound. No inertia thresholds are needed in the decision phase; carry ordinary momentum and defer threshold-pattern handling as already agreed.

## Next experiment and escalation rule

The current generator uses cell routes mainly for filtering and constraint propagation. The untested addition is using their existing polyline to rank and optimize local candidates.

Compare current ordering against this direction/progress-guided ordering on the same selected corridors and local decision opportunities. Include passage timing, normal step versus jump/slab boost, ceiling hit versus avoid/grind, optional landing versus overflight, a turn and the known multi-support discovery control. Keep the same generation/work budgets, original-state solves and physical acceptance. Ladder and intentional wall-hit tests remain excluded.

Measure top-k retention of useful methods, first accepted route, complete-proposal discovery, total physical/numerical work, elapsed ticks and final objective. Count the cost of local scoring. Retain generation unknowns and dropped alternatives explicitly. If direction-only ordering works, there is no demonstrated need for another initializer.

If it fails, identify the missing information and test a small targeted addition: more lookahead, outgoing momentum, coupled decisions, or an updated geometric guide. Only then compare provisional physical drafts or a mixed-method reference for that measured deficiency. This supersedes the previous recommendation to build the full provisional-draft initializer next.

## Source context

Repository source checks: `Corridor.points`, the portal midpoint extension and goal-region handling in `cell_routes/routes.py`; ordered-stage propagation and candidate auditing in `cell_routes/local.py`; direction objectives in `standalone/ipopt/model.py` and `worker.py`. These show that the basic route geometry and directional numerical objective already exist. They do not constitute a measured implementation of this candidate-ranking strategy.

Geometric path following is an established source of direction guidance; [Coulter's CMU report](https://publications.ri.cmu.edu/implementation-of-the-pure-pursuit-path-tracking-algorithm) is background for that idea. This proposal uses geometric targets to score discrete parkour decisions, not the report's vehicle steering controller or its performance claims.
