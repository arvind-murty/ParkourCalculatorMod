# Why EPS2 is inside the square root: blame and upstream PR investigation

## Provenance, checked with gh CLI

- `git blame` attributes `private static final double EPS2 = 1.0e-14` and the original objective/gradient/Hessian smoothing formulas to squash commit `9a22bdbe`, merged via [PR #122](https://github.com/Leg0shii/ParkourCalculatorMod/pull/122) on 2026-06-10. The numeric constant has not changed in the inspected local history. The later StrictMath commit changes the square-root implementation, not the formula or epsilon.
- `gh pr view 122 --repo Leg0shii/ParkourCalculatorMod --json body,comments,commits,...` reports an empty PR body, zero comments and 70 commits. The detailed introduction is its original commit [8263d65c](https://github.com/Leg0shii/ParkourCalculatorMod/commit/8263d65c20464d73ed574ad5d38c1a9d7a3f12ed), retrieved with `gh api repos/Leg0shii/ParkourCalculatorMod/commits/8263d65c...`.
- That original patch already has EPS2=1e-14 inside all three norm calculations. Its file comment calls this tiny norm smoothing, says recovered angles require only modest dual accuracy, and relies on inward margins plus exact verification and fallback. Its commit description describes fast dual optimization, quantization margins and fallback; **neither supplies a derivation, sensitivity study or numerical justification for choosing exactly 1e-14**. Do not attribute the following mathematical explanation to an explicit author statement beyond those comments.
- [PR #387](https://github.com/Leg0shii/ParkourCalculatorMod/pull/387) later explicitly narrows the modest-accuracy assumption to the pinned fast path. It documents smoothing bias versus conditioning for `FreeP0.smooth`, a separate start-position support parameter. It does not justify or change EPS2. PRs #389 and #390 were also read because their combined squash updated the surrounding precision comments; they do not explain the EPS2 value.
- The existing [dual iteration audit](../dual-newton-iteration-audit.md), associated with [issue #384](https://github.com/Leg0shii/ParkourCalculatorMod/issues/384), documents cap sensitivity and separate FreeP0 sharpening experiments. Those are useful historical context but must not be presented as an EPS2 experiment.

## Mathematical distinction between the proposed changes

The current smooth norm is `sqrt(dot(g,g) + delta^2)` with `delta=1e-7`. Its gradient is `g/sqrt(dot(g,g)+delta^2)` and Hessian is `I/r - g*g^T/r^3` where `r=sqrt(dot(g,g)+delta^2)`. At the origin its gradient is zero and its curvature is finite, proportional to 1/delta. This makes the zero-costate kink differentiable for the Newton machinery; it does not guarantee a well-conditioned or nonsingular full multiplier Hessian.

1. **Move the additive term outside the objective norm:** `sqrt(dot(g,g)) + 1e-14`. The added term is constant in the multipliers, so it does not change the gradient or Hessian at nonzero costates and does not smooth the origin. This does not supply the regularization the current algorithm uses.
2. **Change only the gradient denominator:** `g/(sqrt(dot(g,g))+1e-14)`. At the same measured costate its length bias is much smaller, but it is not the gradient of the current smoothed objective, nor of `norm(g)+constant`. Changing only this calculation would make the objective, gradient and Hessian inconsistent. A mathematically consistent alternative is possible (for example a radial function with derivative r/(r+delta)), but it requires a different objective and Hessian, not just moving a constant.
3. **Keep the current family but use a smoothing length of 1e-14:** set EPS2=1e-28 consistently in objective, gradient and Hessian. That is a sharper version of the present model. It is a sensible separate numerical probe, not a proven fix: curvature at zero grows from about 1e7 to 1e14 before other factors, cancellation becomes harder to resolve, and a degenerate optimum can move its costates down along with epsilon, retaining the relative length bias.

There is also a separate yaw-recovery fallback: `FacingPrefold.expand` treats `dot(g,g)<1e-18` (norm below 1e-9) as zero and substitutes the objective direction. Sharpening EPS2 below that scale interacts with this fallback. Lowering only EPS2 therefore does not isolate all small-costate behavior.

For the measured last-tick costate, the near-zero condition is structural: objective Z68 and upper wall Z68 cancel. A small epsilon alone need not resolve the undetermined primal direction. The previous [iteration/recovery probe](linkcraft-real11-initial-seven-dual-iteration-convergence.md) is the actual evidence for this route; no new epsilon run was made here.

## Persisted evidence

[Blame and numeric-constant history](smoothing-history-2026-10-23/blame-and-history.txt), [PR #122 metadata including original commit messages](smoothing-history-2026-10-23/pr122.json), [original creation patch and message](smoothing-history-2026-10-23/original-closed-form-commit.json), and later PR/issue snapshots are stored alongside them. All GitHub reads used gh CLI. Only research notes changed; no solver edits or tests were run for this question.


## Separate EPS2=1e-24 experiment

The [sharper-smoothing experiment](linkcraft-real11-eps24-experiment.md) changes EPS2 to 1e-24 independently of direct-bound detection. Above the 1e-9 recovery threshold, relative length shrink is bounded by about 0.00005%. Initial fixed Z/MAX still fails; its inertia retry now hits the arbitrary-direction fallback on all 68 controls and has a much larger violation. Fixed Z/MIN still passes, and the full fixed 7-ladder route still stalls at tick 90 before jump 14. Five focused tests pass; no full suite ran. The original fallback comment describes objective/wall cancellation, not a documented smoothing-error calibration.
