# Why j13 inertia recovery fails despite a nearby feasible path

2026-10-24. Follow-up to the [corrected90/103 probes](../post-turn-clear-cap-2026-10-24/README.md). Investigation only; production code unchanged. Same exact state55, Z/MIN@90, all56 positional constraints, margin .0003, EPS2=1e-24, max10000. Same verified costate-preserving initializer. Only the opt-in focused class ran in `/tmp/pkc-endpoints-fixed`, production HEAD b98f87f. Main HEAD before this investigation167f1fb. User j127 modification untouched.

## What was missing from the previous probe

Previous probes validated only the dual solver's returned iterate. We now audit every iteration using its existing observer and test-side reflection of costates and derivative controls. The shared validator runs at every iterate. Snapshot audits at5/20/50 include full constraint margins, all inertia velocities and paths. Reflection reads arrays; production solver behavior/stopping is unchanged. Final solve outcomes, iteration counts and costates reproduce the previous run. Instrumented solve timing includes diagnostics and is not a performance benchmark.

## Actual sequence

Initial no-zeroing solve converges167 iterations, residual1.44e-8, and satisfies its assumed model. Actual inertia zeros X63, Z82 and X84 through89. At X79 the assumed model has .0003000002 clearance, but physical continuous replay misses by .0093914246. This small physical perturbation is the expected initial miss.

Compensated initialization preserves the old directions within2.77e-12 degrees. Its physical violation remains .0093914246. Eight zeroing checks are fixed, with opposing velocity rows. At the same initial angles those fixed zeroings match the actual threshold checks.

| Patterned iteration | Continuous violation | Committed exact replay violation | Decoder fallback ticks | Interpretation |
|---:|---:|---:|---:|---|
| 0 | 0.009391424562 | 0 | 0 | Same starting path |
| 1 | 0.001713709129 | 0.001747054267 | 0 | Repairing the initial miss |
| 2 | 0.001096166381 | 0.001111548822 | 0 | Repairing the initial miss |
| 3 | 0.000625058925 | 0.0006882494843 | 0 | Repairing the initial miss |
| 4 | 0.0002522035476 | 0.0002791494252 | 0 | Repairing the initial miss |
| 5 | 0 | 0 | 0 | First accepted intermediate path |
| 6 | 0.5241266489 | 0 | 0 | Dual decreases but primal feasibility is lost |
| 20 | 0 | 0 | 0 | Accepted intermediate path |
| 50 | 0 | 0 | 0 | Accepted intermediate path |
| 213 | 3.381886654 | 2.577214181 | 1 | First costate direction replaced by decoder |
| 227 final | 16.91573908 | 3.893346499 | 27 | NO_DESCENT; final returned path rejected |

**29 intermediate iterates pass the shared acceptance rule.** Iterations: 5, 18, 19, 20, 21, 50, 51, 52, 78, 79, 80, 81, 82, 83, 84, 114, 115, 116, 117, 118, 119, 120, 121, 128, 129, 130, 131, 132, 133. The best observed accepted Z/MIN candidate is iteration114, final Z90=-1090.631257180407. The code does not retain them. `ClosedFormSolve.runLadder` waits for `solver.solve` to return, then expands/validates only its returned direction. This test-side reproduction does the same for its outcome, while the audit exposes the discarded valid intermediate paths.

At iteration5 every assumed inertia check agrees with actual continuous motion, with zero other mismatches. All eight zeroed velocities have magnitude below .0047, the inward-tightened .005 threshold. The path also passes FULL Minecraft replay as an additional audit, beyond the required committed prefix.

| Snapshot | Minimum full continuous clearance | Minimum committed exact clearance | Minimum full exact clearance |
|---|---:|---:|---:|
| 5 | 3.59975363153e-05 | 2.12205575281e-05 | 2.12205575281e-05 |
| 20 | 0.000334046889407 | 0.000273369854995 | 0.000176831652539 |
| 50 | 0.000280902831719 | 0.000227773369147 | 0.000159847276905 |

Acceptance checks original constraints, not achievement of .0003 on every row. In particular iteration5's tightest continuous clearance is .0000359975 at X58. Iteration20 has at least .0003340469 continuous clearance. Iteration5 differs from the initial actual continuous path by at most .0110907727 X and .0059857449 Z over the saved input-state rows55..89; maximum yaw adjustment7.3383043 degrees. See [side-by-side state and yaw CSV](initial-vs-valid-iteration5.csv).

## What happens to the inertia checks

| Check | Original actual velocity | Iteration5 velocity |
|---|---:|---:|
| X63 | 0.00209820559781 | -0.00138133631568 |
| Z82 | -0.00019860774641 | 0.000395276278882 |
| X84 | -0.00403945302823 | -0.00417064021926 |
| X85 | 0.0015078396282 | 0.000823303471665 |
| X86 | 0.00110503828064 | 0.000531877501072 |
| X87 | 0.000763041818324 | 0.000278509018929 |
| X88 | 0.000470432474638 | 0.000121592409764 |
| X89 | 0.00165000804396 | 0.0009190272014 |

These are velocities just before the check, not after zeroing. All iteration5 checks agree with the chosen pattern. Large final violations are not a direct physical consequence of removing .002 velocity at X63.

## Why small physical zeroing causes a large numerical change

The new Z82 zeroing erases earlier Z *velocity* carry. Earlier inputs still affect position; positions are not reset. For every control55..81, its coefficients in Z89 and Z90 become exactly identical. Therefore minimizing Z90 against the retained lower bound on Z89 gives no preference among those early controls when the Z89 multiplier equals1 and other relevant multipliers vanish. Moving the objective one tick past the bound does not prevent this cancellation after an intervening inertia reset.

The [coefficient CSV](j13-seed-1.0-pass1-coefficients.csv) verifies objective Z coefficient equals the Z89 lower-wall normal exactly for all55..81. For example, tick55 is -5.241889072196846 in both; tick81 is -.9999999999998107 in both. In the no-zeroing model the corresponding differences were -.0000881500055 at55 and -.06094472758 at81. Before zeroing there was a small remaining preference; zeroing makes that preference exactly vanish in this part of the Lagrangian.

Measured final multiplier on Z89lo is1.000000000064674. All27 costates55..81 fall below1e-9. Their directions are then weakly determined by tiny residual prices and cancellation. This is a concrete degeneracy mechanism, not proof that the returned dual has converged or that this is a positive duality gap.

`FacingPrefold.expand` replaces a costate whose norm is below1e-9 with the objective direction. Here that points the replacement controls toward -Z. It does not retain the previous feasible yaw or solve for a feasible choice among these nearly undetermined directions.

At iteration213 the first replacement changes a yaw by166.18 degrees. Raw directions miss .00972649, while decoded directions miss3.38188665. At the final227 sample,27 directions are replaced, the largest difference is173.21 degrees, raw physical miss is .03317165 and decoded physical miss16.91573908. The fallback accounts for the huge terminal error. Removing it alone does not solve convergence: the final raw directions are still invalid.

The dual objective decreases from -4.65414039567 at feasible iteration5 to -4.65422218660 at invalid iteration6. A dual descent is not a primal-feasibility-preserving step. There is no incumbent retention inside this dual solve. It continues optimizing beyond valid paths, eventually stalls, and returns only the final candidate.

## Consequences supported by this experiment

- The inertia-aware problem has nearby feasible paths; this retry finds them quickly. The failure is not evidence that the motion must change drastically or that this pattern is infeasible.
- Small physical trajectory differences can create exact derivative cancellation once velocity carry is reset. This is an endpoint/objective degeneracy even though90 itself has no direct bound.
- A practical recovery direction is retaining validated feasible intermediate iterates while continuing optimization. Iteration20 even meets the intended continuous inward margin. This is not implemented here, and each-iteration exact validation would need a performance-conscious policy.
- Another recovery direction is choosing feasible controls for near-zero costates instead of replacing them all with the objective direction. This requires validation/repair, not simply assuming the last directions remain feasible.
- This does not establish global optimality, a duality-gap claim, a general rate guarantee, or a complete production recovery fix.

## Sources and reproduction

- [Production dual loop](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/CostateDualSolver.java): `solve`, `newtonStep`, `gradientStep`, `grad`.
- [Direction decoder](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/FacingPrefold.java): `expand`.
- [Movement coefficients](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/JumpLinearModel.java): `coefAxis`, `objectiveVectors`, `velocityWalls`.
- [Production validation placement](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/ClosedFormSolve.java): `runLadder`.
- [Shared validator](../../../../core/src/main/java/de/legoshi/parkourcalc/core/anglesolver/solver/WindowCandidateValidation.java).
- [Per-iteration summary](j13-seed-1.0-pass1-detailed.csv), [per-tick costates/directions](j13-seed-1.0-pass1-tick-detail.csv), [final prices](j13-seed-1.0-pass1-multipliers.csv), [JUnit result](focused-result.xml), [source snapshot](InertiaMultiplierSeedProbeTest.java).

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=j13-inertia-detail :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/j13-inertia-detail-2026-10-24/focused.log 2>&1
```

Run from the isolated core-only worktree. Gradle completed successfully; one focused test, no failures. Detailed auditing runs inside the iteration observer and adds overhead. Old90/103 artifacts are preserved. The diagnostic final sample can differ from the loop-entry sample with the same iteration number; the CSV records both in order. The terminal decision uses the final sample. Only `iterationObserver` diagnostics and test-side reflection were added; solver production behavior was not changed.


## Clarifications: Z82, exact cancellation, and useful receding-horizon candidates

Z82 is inferred from the initial angles, not selected arbitrarily by the optimizer or specified by the user. The actual continuous incoming Z velocity at global82 is -.0001986077464100692, inside the strict physical band abs(v)<.005. The 1.8 rule therefore zeros it before tick82 acceleration. The next fixed-pattern model assumes this check zeros and adds the opposing velocity rows; after inward margin .0003 their band is abs(v)<=.0047. At valid iteration5 the incoming velocity is +.00039527627888175044, still inside. The assumption can be reconsidered in another pattern pass; it is not a universal claim about tick82 for all possible paths.

The cancellation is structural within this fixed pattern, not small-number rounding. A perturbation to a control before82 can change position at82 but cannot change outgoing Z velocity after the fixed reset. Its resulting position offset persists unchanged through89 and90. Thus its derivative in Z90-Z89 is exactly0. When minimizing Z90 with Z89's lower wall active at multiplier1, its objective coefficient cancels that wall's coefficient. Other earlier constraints still exist and must be satisfied. This does not mean early motion is physically free or that every early yaw is feasible. In the measured terminal dual, other early prices tend to0, leaving early costates tiny. Floating-point sensitivity then makes choosing their directions unreliable; the mathematical cancellation is the cause, rather than a numerical approximation to a small nonzero velocity influence. With fixed Z82 zeroing, those early controls have exactly no influence on this final displacement, although they remain relevant to feasibility and the actual receding-horizon commitment.

Keeping a validated incumbent inside each dual solve would directly prevent this experiment's false failure:29 valid intermediate candidates already exist, and iteration114 has the best observed accepted Z90=-1090.631257180407. Continue optimizing, replace the incumbent only with a validated better candidate, and return it if the terminal decode fails. That preserves the best OBSERVED terminal score, not a proven optimum. Save angles and enough path/state data to retain the uncommitted continuation, with validation appropriate to the eventual seam. Here iteration5/20/50 full-window exact replay passes as an extra audit, but the general shared rule only promises exact replay on the committed prefix and continuous validity over the whole window. A candidate from a different pattern must be checked under actual inertia, never accepted just because its fixed model is feasible.

This solves candidate loss, not the full quality-selection problem. A flat terminal objective can leave many feasible early paths with different seam positions, velocities and future usefulness. Retaining incumbents cannot prove which of these is best for future windows, and keeping the first feasible path alone could commit a weak one. A proposed hierarchy is feasibility first, the declared primary objective second, and explicit route-aligned early progress/velocity targets to select among near-equal primary candidates, followed by continuity with a known good path where helpful. The early targets must reflect route geometry; arbitrary summed forward motion or maximum wall clearance is not a generic substitute. A second optimization stage within a declared primary tolerance, or an explicit lexicographic formulation, makes this choice reviewable; a tiny weighted blend still risks losing influence numerically. No such quality policy or production incumbent mechanism is implemented by this investigation.

The decoder fallback is `FacingPrefold.expand`'s rule norm(g)<1e-9: replace the costate vector by the objective direction, then compute the yaw that realizes that input vector using `lin.recoverYawDeg`. For Z/MIN the replacement vector is(0,-1). This is not setting camera yaw to0 or -90, not velocity zeroing, and not Minecraft angle quantization. It ignores per-path feasibility when choosing that replacement, which is why replacing27 early controls can break the path. Validated incumbent retention and constrained recovery of underdetermined directions are separate remedies: the former supplies a usable candidate already found; the latter tries to recover a feasible path at the current terminal dual and may enable further objective improvements. Simply reusing old angles for tiny costates still requires revalidating the combined path.

No additional solver run was needed for these clarifications; source and archived measurements were rechecked.


## Preserve the path during inertia repair; meaning of the decoder vector

User emphasizes that inertia retries should repair the existing decent path rather than discard it while chasing a weak terminal preference. The Z82 reset is the trigger that removes pre82 velocity influence; the active Z89 wall also matters because it pins the remaining shared position contribution to Z89/Z90. Reset alone does not erase early position dependence. Under both conditions the terminal objective cannot distinguish those early controls.

Saving validated intermediate candidates directly helps: use the previous path as a warm repair seed (it is not an accepted incumbent if actual inertia causes a constraint violation), retain the first valid repaired path, then continue optimizing while preserving the best valid candidate. Our j13 retry reaches this point at5 and visits29 valid candidates. A feasibility-first repair stage could stop there when requested, whereas an optimization stage can continue with incumbent retention. The data does not justify blindly limiting every inertia retry to5 iterations or accepting the old invalid path. Incumbent retention protects feasibility and the best observed objective; route-aware secondary preferences remain a separate quality question for receding-horizon commitments.

The decoder's vector is the per-control costate g_t=(gX_t,gZ_t)=c_t-sum_j(lambda_j*A_jt), not velocity and not acceleration. Its direction selects the acceleration/input direction; acceleration magnitude is independently fixed by the physics. Small norm means the Lagrangian weakly distinguishes directions, not that the player is moving slowly. At g=0 every direction gives the same g dot u, but constraints can still rule out most choices. The decoder's norm<1e-9 objective-direction replacement is a default for this underdetermination, not constrained recovery. The code comment in JumpLinearModel.recoverYawDeg calls vanishing costates an undetermined direction left to the caller's default. Blame attributes FacingPrefold's threshold and objective-default behavior to d80d3e24, `feat: dF constraint with closed-form and free-start solving`; no detailed threshold rationale is stated in that commit message. This checked history supports identifying it as a longstanding default, not claiming a documented feasibility guarantee or a deliberate repair strategy. Replacing directions wholesale explains the drastic path change; saving a validated whole candidate avoids relying on that terminal decode.

No additional solve or production change for this clarification.
