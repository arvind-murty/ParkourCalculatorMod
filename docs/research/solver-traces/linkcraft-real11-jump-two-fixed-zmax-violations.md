# Jump2: why the fixed Z/MAX proposals violate continuous constraints

Same production jump2 window: input ticks12..70, end state71,37 original constraints,9 committed input ticks. The failed proposals below reproduce the archived forward-jump and forward-tick runs exactly. Current EPS2=1e-24, fixed margin0.0003, default dual cap100.

## Immediate cause

All three failed fixed Z/MAX proposals stop at ITERATION_LIMIT100 with large returned projected-gradient residuals. The dual algorithm does not maintain a feasible angle path at every iteration: it updates constraint multipliers, recovers angles from the returned costates, then validates the candidate. Hitting the finite cap does not imply those recovered angles satisfy the fixed-pattern model's original walls.

| Proposal | Worst row | Violation already in the assumed-pattern affine model | Violation with actual continuous inertia checks | Final dual projected residual |
|---|---|---:|---:|---:|
| Fixed forward-jump initial | X67 >=729.6999999880792 | 0.076033005589 | 0.085501454763 | 0.1244 |
| Fixed forward-jump inertia retry | Z68 <=-1092.699999988079 | 0.211319277857 | 0.211319277857 | 0.2116 |
| Fixed forward-tick initial | Z68 <=-1092.699999988079 | 0.496625813556 | 0.496625813556 | 0.4969 |

The first forward-jump proposal's assumed model keeps X carry at tick63, but actual continuous physics zeros vx=+0.003311801526. That adds0.009468449174 to the X67 violation. Most of its violation (0.076033005589) already exists before that inertia mismatch.

The retry explicitly assumes X zeroing at tick63 and satisfies that threshold: assumed vx=+0.000656474047, strictly inside0.005. It nevertheless has an unfinished positional solve and violates Z68 by0.211319277857 in both models. Thus enforcing that zeroing decision does not make the other constraints feasible.

The forward-tick proposal has no actual continuous X/Z zeroing event inside this window, so the pass loop has no new inertia pattern to try. Its0.496625813556 violation is already in its unpatterned model.

Minimum returned active costate magnitudes are approximately0.1351,0.1280,0.1716, respectively. None invokes the1e-9 direction fallback. The earlier near-zero-costate norm-smoothing example is not the explanation for these candidates.

## Reference solve and resulting method choices

The saved successful normal-margin run accepts jump2 Z/MAX at margin0.05. The focused reproduction from its exact reconstructed tick12 state also accepts it under current EPS2. It reaches the100-iteration cap too, but original constraints pass: minimum continuous clearance is0.049884194318, with final projected residual0.0001158. Its requested inward buffer and actual residual differ from the failed fixed proposals; iteration count alone does not decide acceptance.

In the original fixed runs, forward-jump Z/MAX's retry worsens the violation and the inertia loop stops; forward-tick Z/MAX has no new zeroing pattern and stops. Both proceed to Z/MIN, whose initial proposal misses by about0.00926, then whose X-zero-at64 retry passes. Neither run invokes X/MAX, X/MIN or SLP for this window.

## Evidence and bookkeeping

- [Each candidate](jump-two-margin-2026-10-23/candidates.csv), [every row's assumed/continuous/replay value](jump-two-margin-2026-10-23/constraint-margins.csv), [inertia velocities](jump-two-margin-2026-10-23/inertia-checks.csv).
- [Trace with actual stopping reasons/residuals](jump-two-margin-2026-10-23/solver-trace.txt), [all per-iteration observations](jump-two-margin-2026-10-23/iterations.csv), [full tick paths](jump-two-margin-2026-10-23/paths.csv).
- [Focused probe source](../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/JumpTwoMarginProbeTest.java), [settings](jump-two-margin-2026-10-23/metadata.txt), [focused test log](jump-two-margin-2026-10-23/focused-test.log).
- [Outcome CSV](jump-two-margin-2026-10-23/outcomes.csv) also preserves same-entry and larger-cap controls already completed before the user's request to stop running controls. No additional controls were run after that request; this note explains the original failed proposals only.
- Implementation: ClosedFormSolve.runLadder calls CostateDualSolver.solve, then pre.expand, then WindowCandidateValidation.evaluate; CostateDualSolver.solve defaults to MAX_ITER100 and returns its current iterate at that limit. This inspection changes no production code or solve budget.
