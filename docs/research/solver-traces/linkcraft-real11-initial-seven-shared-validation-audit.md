# Initial seven-jump window: shared validation audit of normal versus fixed margins

## Scope and reproduction

Same initial state, inputs, objective tick and 33 constraints, sliced from the full Linkcraft real/11 spec to global ticks `[0,68)` (jumps 1–7). Compare actual production closed-form calls: Z/MAX at only 0.0003, Z/MIN at only 0.0003 (the next accepted objective), and Z/MAX at only 0.05 (the first rung accepted by the normal ladder). Each starts with cold dual multipliers. The audit observes production calls; it does not copy the solve or validation algorithm into the probe.

Both commit horizons were tested: commit=3 checks replay through tick 30, while commit=1 checks through tick 12. The selected candidate angles are bit-identical across these two horizons for each objective/margin. **There are no original constraint rows at or before tick 12 in this initial window**, so committed replay validation for commit=1 is empty. Commit=3 includes the eight rows at ticks 20 and 29. The continuous check always includes all 33 rows through tick 68.

`WindowCandidateValidation` now exposes an optional per-thread `DiagnosticObserver`. It reports each original constraint's assumed-model, actual-continuous and Minecraft replay value and signed margin; committed-prefix membership; every axis's pre-threshold velocity and outcome; and the supplied assumed model's velocity and consistency with its pattern. Closed form and SLP both supply their assumed/local model to this common hook. Diagnostic detail is allocated only when an observer is installed, and the assumed model is used only for explanation, never for acceptance. The observer supports other callers of the helper too. Unit tests cover observation without changing acceptance and the same hook receiving SLP diagnostics.

`JumpLinearModel.forwardFixed` uses the existing affine position coefficients. `velocityBeforeCheck` uses the existing friction/zero-pattern velocity coefficients. Actual continuous positions/velocities reuse the original `zeroingPattern` recurrence via `forwardContinuous`. No duplicate threshold simulator was added to the test.

## Result for every candidate

A = normal Z/MAX 0.05, first proposal. B = fixed Z/MAX 0.0003, first proposal. C = fixed Z/MAX 0.0003, inertia-pattern retry. D = fixed Z/MIN 0.0003, first proposal.

| Candidate | Assumed pattern | Dual iterations | Projected-gradient residual | Continuous max violation | Replay max violation, commit=3 | Replay max violation, commit=1 | Decision |
|---|---|---:|---:|---:|---:|---:|---|
| A | No zeroing | 100 | 0.2090 | 0 | 0 | 0 | Accept |
| B | No zeroing | 100 | 0.03623 | 0.011663350626 | 0 | 0 | Reject |
| C | X/Z tick 0, X tick 63 zeroed | 83 | 0.004700 | 0.071773792746 | 0.002686312704 | 0 | Reject; worse than B, stop inertia loop |
| D | No zeroing | 66 | 9.113e-10 | 0 | 0 | 0 | Accept |

## Exact reasons fixed Z/MAX fails

B violates only the upper Z wall at tick 68. The wall is `Z <= -1092.699999988079`. Its assumed affine model gives `Z=-1092.688336637455`, already 0.011663350624 beyond the wall; the continuous simulation with actual inertia gives `Z=-1092.688336637453`, beyond by 0.011663350626. This failure is present before yaw mapping, and before correcting the assumed inertia pattern has any effect on that Z value.

The continuous simulation of B finds X below threshold at tick 63. Closed form retries with X63 zeroed (plus the ineffective initial X/Z zero checks). C obeys that zeroing band's velocity requirement but violates three positional rows: Z@20hi by 0.002399056917, X@67lo by 0.019091860804, and Z@68 by 0.071773792746. On commit=3, Minecraft replay also violates Z@20hi by 0.002686312704. On commit=1, that replay row is outside the prefix; full continuous rejection is unchanged. C has a worse maximum violation than B, so `solveWithPrefold` stops after this retry and returns B as its best miss. LongRun advances to Z/MIN, which accepts D.

## Every full-window continuous signed constraint margin

Positive = clearance inside the original wall; negative = violation. These are original walls, not the margin-tightened optimization walls. Labels retain global state indices from the saved constraint names. Units are blocks.

| Constraint | A normal Z/MAX | B fixed Z/MAX | C fixed retry | D fixed Z/MIN |
|---|---:|---:|---:|---:|
| X@20lo | +0.368553982 | +0.379362501 | +0.382693599 | +0.381214294 |
| X@20hi | +1.231446042 | +1.220637523 | +1.217306425 | +1.218785730 |
| Z@20lo | +1.552213413 | +1.591718288 | +1.602399081 | +1.599700024 |
| Z@20hi | +0.047786611 | +0.008281736 | **-0.002399057** | +0.000300000 |
| X@29lo | +0.478891601 | +0.463887717 | +0.466209219 | +0.452916423 |
| X@29hi | +1.121108422 | +1.136112307 | +1.133790805 | +1.147083601 |
| Z@29lo | +0.153196975 | +0.153315391 | +0.162964170 | +0.140255083 |
| Z@29hi | +1.446803049 | +1.446684632 | +1.437035853 | +1.459744941 |
| X@39lo | +1.343664380 | +1.293605877 | +1.294085056 | +1.267561248 |
| X@39hi | +0.256335644 | +0.306394147 | +0.305914968 | +0.332438776 |
| Z@39lo | +1.134824426 | +1.080747597 | +1.087629406 | +1.043440124 |
| Z@39hi | +0.465175598 | +0.519252427 | +0.512370618 | +0.556559900 |
| X@54lo | +0.895046834 | +0.861731822 | +0.864126272 | +0.864321495 |
| X@54hi | +0.704953190 | +0.738268202 | +0.735873752 | +0.735678529 |
| Z@54lo | +0.035552290 | +0.002426645 | +0.011655952 | +0.000562125 |
| Z@54hi | +0.964447710 | +0.997573355 | +0.988344048 | +0.999437875 |
| X@58 | +0.042899558 | +0.033532230 | +0.028093790 | +0.000299999 |
| Z@57 | +0.057231325 | +0.042305106 | +0.030172205 | +0.017855794 |
| Z@56 | +0.332466453 | +0.329604527 | +0.318266451 | +0.311762262 |
| X@63lo | +0.695626082 | +0.702962558 | +0.709191499 | +0.722755786 |
| X@63hi | +0.304373918 | +0.297037442 | +0.290808501 | +0.277244214 |
| Z@63lo | +1.421008651 | +1.516975183 | +1.532536630 | +1.599700023 |
| Z@63hi | +0.178991373 | +0.083024840 | +0.067463394 | +0.000300000 |
| Z@64 | +0.041175209 | +0.152216667 | +0.168602280 | +0.228448271 |
| X@64 | +0.282619058 | +0.276662854 | +0.272832299 | +0.251742151 |
| Z@55 | +0.794299427 | +0.809838016 | +0.799694697 | +0.802750654 |
| X@67lo | +0.034659743 | +0.014827236 | **-0.019091861** | +0.006052049 |
| X@67hi | +1.565340281 | +1.585172788 | +1.619091885 | +1.593947975 |
| Z@67lo | +0.248881304 | +0.416847242 | +0.467235287 | +0.000300000 |
| Z@67hi | +1.351118720 | +1.183152782 | +1.132764736 | +1.599700024 |
| X@65 | +0.155103246 | +0.158893002 | +0.169705856 | +0.132657063 |
| X@66 | +0.065107994 | +0.076145684 | +0.097619297 | +0.061286354 |
| Z@68 | +0.171062818 | **-0.011663351** | **-0.071773793** | +0.615714225 |

## Every committed Minecraft replay margin, commit=3

These are all eight checked rows through tick 30. Commit=1 checks none of them because its exit is tick 12. The CSV also preserves replay margins for uncommitted rows as diagnostic information, without treating them as acceptance conditions.

| Constraint | A normal Z/MAX | B fixed Z/MAX | C fixed retry | D fixed Z/MIN |
|---|---:|---:|---:|---:|
| X@20lo | +0.368608622 | +0.379434282 | +0.382791240 | +0.381310873 |
| X@20hi | +1.231391402 | +1.220565742 | +1.217208783 | +1.218689150 |
| Z@20lo | +1.552391212 | +1.591941813 | +1.602686337 | +1.599990787 |
| Z@20hi | +0.047608812 | +0.008058211 | **-0.002686313** | +0.000009237 |
| X@29lo | +0.479007056 | +0.464003618 | +0.466359003 | +0.453064215 |
| X@29hi | +1.120992968 | +1.135996406 | +1.133641021 | +1.146935809 |
| Z@29lo | +0.153480467 | +0.153625749 | +0.163360844 | +0.140653267 |
| Z@29hi | +1.446519556 | +1.446374275 | +1.436639180 | +1.459346757 |

## Inertia threshold checks and velocities

The only nontrivial zeroing in these candidates is X at tick 63. All actual continuous and Minecraft replays zero X there. None has a nonzero-velocity Z threshold crossing. All also zero the already-zero initial X/Z velocity, which has no effect on movement. “No zeroing” is an unconstrained assumption here; it does not mean the solver added keep-alive inequalities.

| Candidate | Solver assumes X63 zeroed? | Assumed-model VX before check | Actual continuous VX before check | Minecraft VX before check | Actual outcome |
|---|---|---:|---:|---:|---|
| A | false | +0.000656447611 | +0.000656447611 | +0.000660287801 | Zero, magnitude below 0.005 |
| B | false | -0.001695983953 | -0.001695983953 | -0.001693941967 | Zero, magnitude below 0.005 |
| C | true | -0.000074975099 | -0.000074975099 | -0.000069839377 | Zero, magnitude below 0.005 |
| D | false | -0.002011907545 | -0.002011907545 | -0.002009075622 | Zero, magnitude below 0.005 |

Only C has explicit X63 zeroing-band rows in this closed-form solve. Its assumed velocity is -0.000074975099, safely within the original strict band `abs(VX) < 0.005` and the solver's margin-tightened band `abs(VX) <= 0.0047` (the same 0.0003 inward margin tightens both velocity inequalities). The raw band clearance is 0.004925024901, and tightened clearance is 0.004625024901. Its tick-0 zeroing rows are omitted by `velocityWalls` because no inputs reach tick 0; the initial velocities are already zero. Thus this retry's rejection is positional, not a failure to satisfy its explicit inertia-band rows.

A, B and D assume no zeroing but actual X63 zeroing still occurs. Acceptance deliberately uses the actual continuous simulation: this pattern difference is allowed when every resulting positional/facing constraint passes, as with A and D. It does not allow fictitious physics.

## Why a larger margin passes while the smaller one fails

The smaller-margin problem is not infeasible. A is a witness: its minimum actual continuous wall clearance is 0.034659743251, and even its unpatterned affine-model clearance is at least 0.035552289783, both well above 0.0003. There exists a suitable candidate for the smaller margin; that cold finite-iteration solve did not find one.

Changing the margin changes every inequality RHS seen by the dual solver, hence its multiplier trajectory and recovered angles. A and B both hit 100 iterations. Neither has a small projected-gradient residual; A also fails to achieve the full requested 0.05 inward clearance on some walls, but its remaining clearance is positive against every original wall. B's residual positional error crosses the original Z68 wall. Acceptance checks original feasibility, not dual convergence or achievement of the requested inward margin. A's success therefore does not demonstrate a converged Z/MAX optimum. C has a smaller dual residual but worse positional feasibility; that residual alone is not the acceptance test. The audit does not claim to isolate the lower-level numerical cause of the recovered-angle error.

## Artifacts and focused tests

All ten selected tests passed (one capture audit, five shared-helper tests, four prefix tests). No full suite ran. No acceptance behavior was intentionally changed; these changes add optional observation.

- [Candidate summaries](initial-seven-shared-audit-2026-10-23/summary.csv)
- [Every constraint, all three trajectories](initial-seven-shared-audit-2026-10-23/constraint-margins.csv)
- [Every inertia check, both axes](initial-seven-shared-audit-2026-10-23/inertia-checks.csv)
- [Every tick's yaw, position and velocity](initial-seven-shared-audit-2026-10-23/paths.csv)
- [Solver trace with iteration counts and reasons for stopping the inertia loop](initial-seven-shared-audit-2026-10-23/solver-trace.txt)
- [Focused test command log](initial-seven-shared-audit-2026-10-23/focused-tests.log)

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.marginProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.InitialSevenSharedValidationAuditTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.WindowCandidateValidationTest --tests de.legoshi.parkourcalc.core.anglesolver.solver.ReplayPrefixValidationTest
```

Background the command and redirect output to a file. Keep fixed margin 0.0003 for subsequent route experiments; margin 0.05 here is an explicit comparison only.


## Larger dual iteration caps and recovery discrepancy

The [iteration-cap/convergence audit](linkcraft-real11-initial-seven-dual-iteration-convergence.md) tests fixed 0.0003 caps 100 through 20000. More iterations do not fix Z/MAX: at cap >=200 the first pass stops at 125 on no descent, and the retry at 83 on recovered-input stall. The returned smoothed dual derivative uses shorter-than-required movement vectors; restoring full magnitude in angle recovery adds 0.07244946 to final Z and causes the measured violation. Per-iteration curves, per-tick recovery vectors, constraint margins and timings are preserved. Seven focused tests pass; default cap stays 100. The older `pg` log at a cap is one iterate behind the returned state; the new `finalPg` reports the returned-state residual.
