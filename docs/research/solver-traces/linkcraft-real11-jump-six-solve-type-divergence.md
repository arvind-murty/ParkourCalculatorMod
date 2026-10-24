# Jump6: fixed cap1000 versus successful normal-margin reference

## Exact scope

This inspects the saved commit1 solve starting at jump6/tick55. Reference: the successful normal-margin30-jump run under shared actual-inertia validation. Fixed: the latest full808-tick forward-jump-preferred run, margin0.0003, default dual cap1000, EPS2=1e-24. Reference used cap100 and EPS2=1e-14. This is a comparison of actual solve attempts; no same-entry, margin-policy or endpoint controls were run here.

The focused audit only reproduces the **existing seven closed-form calls** in the fixed trace, with identical entry state, input window, objective, cap and margin, to expose their already-produced candidates through the shared diagnostic helper. It reconstructs the exact tick55 state by forwarding the archived committed float game facings0..54 and checks every X/Z seed component against the rounded original log within1e-10. It asserts each method reproduces the archived pass/fail outcome. The probe passes; no full suite ran. SLP outcomes below are read from the original trace, not rerun.

## What each run actually solves

| Run | Actual input interval / end state | Original constraints | Outcome |
|---|---|---:|---|
| Normal reference, nominal7 jumps | [55,88), end88 | 53 | CF Z/MAX fails; CF Z/MIN succeeds at0.0003 after inertia retry; no SLP call |
| Fixed cap1000, nominal7 jumps extended forward | [55,90), end90 | 57 | CF Z/MAX, Z/MIN, X/MAX, X/MIN all fail; ordinary and inertia-aware centered SLP fail |
| Fixed cap1000, fallback5 jumps | [55,77), end77 | 32 | CF Z/MAX fails; CF Z/MIN fails; CF X/MAX succeeds at0.0003; X/MIN and SLP not called |

Both use9 committed inputs, through state64. The forward policy sees the +Z cap at nominal end88, moves the fixed endpoint to90, and adds the X/Z box at89 (four rows). This is an actual extra jump segment, not simply moving the objective while keeping the optimization problem otherwise identical.

The entry state also differs:

| Run | X55 | Z55 | vX55 | vZ55 | Incoming yaw |
|---|---:|---:|---:|---:|---:|
| Normal reference | 730.338594178782 | -1096.094697424725 | -0.226905272350 | +0.168494581577 | 86.486671448 |
| Fixed cap1000 | 730.296788310471 | -1096.123960468676 | -0.233858250699 | +0.159863722150 | 93.823913574 |

## Normal-margin reference: successful Z/MIN sequence

Normal Z/MAX tries its robust margin ladder across unpatterned, zero-X-at62 and zero-X-at63 models and fails. Its best actual continuous violation is0.108092104889.

Normal Z/MIN's unpatterned ladder ends with best violation0.042182597371 at margin0. Its inferred retry pattern is X zeroing at absolute ticks63 and81. On that fixed pattern, it tries margins0.05,0.02,0.01,0.005,0.0012 and0.0003; the0.0003 rung is accepted after71 iterations. Both full continuous and committed replay violations are0. The earlier rungs warm-start the next rung's dual multipliers. This is not a cold0.0003 solve, despite the final accepted margin being0.0003.

## Fixed seven-jump window: every CF candidate

The pattern column below uses the code's **window-relative ticks**; add55 for absolute ticks. `free` means the affine optimization model does not zero carry. Continuous validation always runs actual inertia threshold checks. Every row here is rejected except the final end77 X/MAX row.

| Window end | Objective | Candidate | Assumed zeroing pattern (local ticks from55) | Iterations | Worst continuous row | Continuous violation | Committed replay violation | Below1e-9 recovery count |
|---:|---|---:|---|---:|---|---:|---:|---:|
| 90 | ZMAX | 0 | free | 1000 | X@89lo | 1.477023009 | 0.249236902 | 0 |
| 90 | ZMAX | 1 | x@32-34 | 1000 | Z@89hi | 1.684271192 | 0.013329484 | 0 |
| 90 | ZMIN | 0 | free | 319 | X@79lo | 0.009376966 | 0.000000000 | 0 |
| 90 | ZMIN | 1 | x@8,@29-34+z@27 | 625 | Z@89lo | 16.916091610 | 3.893650457 | 27 |
| 90 | XMAX | 0 | free | 1000 | Z@83 | 0.194206671 | 0.000000000 | 0 |
| 90 | XMAX | 1 | x@8 | 1000 | X@88hi | 2.107532724 | 1.495125098 | 1 |
| 90 | XMIN | 0 | free | 1000 | X@89lo | 1.904762846 | 0.082053163 | 1 |
| 90 | XMIN | 1 | x@7 | 1000 | X@88hi | 0.559398226 | 0.056276122 | 0 |
| 90 | XMIN | 2 | x@26+z@34 | 1000 | X@89lo | 1.233392510 | 0.000000000 | 0 |
| 77 | ZMAX | 0 | free | 46 | X@76lo | 0.008968264 | 0.000000000 | 0 |
| 77 | ZMAX | 1 | x@8 | 1000 | X@76lo | 0.418204862 | 0.151921075 | 1 |
| 77 | ZMIN | 0 | free | 62 | X@76lo | 0.012540837 | 0.000000000 | 0 |
| 77 | ZMIN | 1 | x@8+z@21 | 174 | Z@76lo | 11.665143185 | 3.893650457 | 21 |
| 77 | XMAX | 0 | free | 80 | X@58 | 0.000000000 | 0.000000000 | 0 |

### Closest candidate: initial Z/MIN, end90

This is distinct from jump2's iteration-cap failure:

- The unpatterned dual reaches PROJECTED_GRADIENT after319 iterations, final residual2.669e-9. All original affine positional rows pass, minimum signed clearance about0.000299997331 at X58.
- The model keeps a small positive X carry at tick63: vx=+0.002095075262. Actual continuous physics zeros it because its magnitude is below0.005. That removes positive-X displacement in subsequent ticks.
- At X79 lower wall732.699999988079, the affine model has clearance+0.000300000021, while the actual continuous path has clearance-0.009376966022. The accumulated difference is0.009676966042 blocks.
- The committed replay through64 passes. The X79 failure is in provisional lookahead, so full continuous checking is what rejects this candidate.
- Actual threshold discovery produces X zeroing at63 and84–89, plus Z zeroing at82.

### Z/MIN inertia retry, end90

The next affine model fixes that whole discovered pattern and adds the corresponding velocity-band rows. It does not just fix tick63.

- Stops after625 iterations on NO_DESCENT, final projected residual0.02538. This is an unfinished fixed-pattern solve, not evidence that its pattern is infeasible.
- Minimum costate norm5.64927e-12;27 active controls fall below the1e-9 recovery cutoff. `JumpLinearModel.recoverAlongCostate` substitutes the objective direction for each such control. For this Z/MIN objective that direction is -Z. This substitution is a heuristic; it does not preserve satisfaction of the dual wall rows.
- Recovered angles violate the X63 zeroing requirement: assumed-model vx63=-0.065983460097, well outside the allowed magnitude below0.005. They also violate the Z82 zeroing requirement: vz82=-0.203522537854. The X84–89 zeroing assumptions do satisfy the actual threshold in the recovered path.
- Worst original positional violation is Z89 lower wall:16.347987768163 in the assumed affine path and16.916091609578 with actual inertia. Committed replay violation is3.893650457131.
- The retry is worse than the initial0.009376966022 violation, so the inertia loop stops and returns no feasible Z/MIN candidate.

These logs establish that direction fallback is invoked on27 controls and that the returned angles violate velocity-band and position rows. No ablation was run, so they do not isolate fallback from the preceding dual failure as an independent cause.

### Other CF objectives and SLP, end90

Z/MAX reaches cap1000 on both passes; its positional violations already exist in the affine model. X/MAX also reaches cap1000 in both passes. X/MIN tries unpatterned, X62, then X81/Z89 patterns, reaches1000 each time, and returns no feasible result. The table above lists their exact validation residuals and worst rows.

Centered SLP uses its unchanged phase1 budget, separate from the dual iteration cap:

| SLP pass | LP calls | Outcome |
|---|---:|---|
| Ordinary | 40 | Phase1 budget exhausted; violation0.122258610, current worst X88 upper wall |
| Inertia-aware retry | 31 | Final trust region1.12e-7 degrees, negligible rejected step; violation0.122253746, worst Z83 |

The original trace then records end90 as a window miss and falls back to end77.

## Fixed five-jump fallback, end77

Z/MAX unpatterned converges after46 iterations but actual X zeroing produces an X76 lower-wall miss0.008968263933. Its X63 retry reaches1000 iterations and worsens to0.418204861828.

Z/MIN unpatterned converges after62 iterations but actual X zeroing produces an X76 lower-wall miss0.012540837376. Its X63/Z76 retry stops after174 iterations on NO_DESCENT, with21 controls invoking the tiny-costate recovery fallback; violation11.665143184557.

X/MAX's unpatterned solve converges after80 iterations and passes both actual continuous and committed replay checks. It supplies the committed jump6 prefix. X/MIN and SLP are not attempted on this smaller window.

## What this establishes

The normal reference and fixed run diverge at jump6 in both the selected problem and the numerical path: normal retains end88 and finds Z/MIN via its margin continuation/inertia pattern, while fixed extends to90 and finds no candidate, then shrinks to77 and accepts X/MAX. The fixed Z/MIN initial candidate is already affine-feasible and loses actual feasibility through inertia zeroing; its inertia retry becomes a different, numerically unsuccessful solve with many fallback directions. Increasing the1000 cap alone would not extend that retry because it exits on NO_DESCENT at625.

The current data do not tell whether restoring end88, reusing normal's entry state, reusing its margin continuation, or improving recovery is sufficient to reproduce normal's successful Z/MIN result. Those are future controls, not results of this audit.

## Sources

- [Successful normal trace](shared-inertia-validation-2026-10-23/normal7-commit1.txt), starting at `LRS-WIN commit=1 jumps=[5,12) ticks=[55,88)`.
- [Original fixed cap1000 trace](fixed-seven-forward-jump-cap1000-2026-10-23/solver-trace.txt), commit1 windows55–90 and55–77; includes both centeredSLP passes.
- [Candidate audit trace](jump-six-fixed-cap1000-2026-10-23/solver-trace.txt), [candidate summary](jump-six-fixed-cap1000-2026-10-23/candidates.csv), [every constraint margin](jump-six-fixed-cap1000-2026-10-23/constraint-margins.csv), [all inertia checks](jump-six-fixed-cap1000-2026-10-23/inertia-checks.csv), [all tick paths and yaws](jump-six-fixed-cap1000-2026-10-23/paths.csv).
- [Reproducer](../../../core/src/test/java/de/legoshi/parkourcalc/anglesolver/JumpSixCandidateAuditTest.java), [settings](jump-six-fixed-cap1000-2026-10-23/metadata.txt), [focused test log](jump-six-fixed-cap1000-2026-10-23/focused-test.log).
- Implementation: `LongRunSolver.adjustedEnd/solveWindow`; `ClosedFormSolve.solveWithPrefold/runLadder`; `JumpLinearModel.zeroingPattern/forwardContinuous/recoverAlongCostate`; `WindowCandidateValidation.evaluate`; `SlpSolve` phase1 stopping/ordinary-to-inertia-aware retry.
