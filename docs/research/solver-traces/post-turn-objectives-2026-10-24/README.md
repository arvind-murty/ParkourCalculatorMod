# One state after landing constraints: jump13 Z/MIN and jump14 Z/MAX

Date: 2026-10-24. Same exact fixed jump6 entry and .0003 margin as the preceding seeding experiment. Only the opt-in focused probe ran, using b98f87f production code in /tmp/pkc-endpoints-fixed with the updated test harness. Production unchanged.

Interpretation of +1: jump13's landing constraints are at89; objective at90 (35 inputs from55). Jump14's landing constraints are at98; objective at99 (44 inputs). These are already the existing end-state boundaries; +1 does not mean91/100. All original constraints within each window were RETAINED, including89 and98. State99 has another Z band, repeated100–102, so the jump14 +1 Z/MAX objective is still directly capped by Z99<=-1093.7. No endpoint wall removal in these requested comparisons.

## Seeding verification added before reuse

After computing the proposed band weights, reconstruct every new costate and compare it to the old one. Relative per-tick reconstruction tolerance is1e-8 with norm floor1e-12. Reject a nonfinite or negative multiplier; if reconstruction fails, discard the compensation and retain mapped position weights only, tracing that preservation declined. On verified preservation, also assert max recovered-angle change<1e-6deg. This is a guard for the existing positional identity-fold spike, not a general facing/changed-basis implementation. Duplicates remain separate by original compiled order; name/axis/equality checks precede copying. New rows are checked to be exact opposing pairs. The code remains test-side and does not automatically switch production retry behavior.

## Results

| Objective | Initial actual continuous miss | Seeded retry iterations / stop | Seeded retry residual | Seeded retry actual miss | Accepted? |
|---|---:|---|---:|---:|---|
| Z/MIN@90, jump13 |.009391424562 |227 / NO_DESCENT |.009030052 |16.915739075 |no |
| Z/MAX@99, jump14 |.009350175810 |10000 / ITERATION_LIMIT |.002735290 |3.616524037 |no |

Costate-preserving seeds keep the preceding angles within2.8e-12/3.3e-12deg and start at residual .00975/.00977. Both verified successfully. Later iterations develop tiny costates and27 fallback ticks; the pattern retry is worse, so the loop stops for no improvement. Cold, direct-carry and quarter-carry controls also miss. No SLP/fold/B&B or full route was attempted, and these are not infeasibility certificates.

The first jump13 affine solve stops NO_DESCENT just above tolerance (residual1.44e-8); its assumed positional rows pass. Jump14's first solve stops input-stall at residual .0004248; its assumed positional rows also pass. Their remaining initial actual continuous misses include inertia-pattern disagreement. Do not describe both initial solves as fully stationarity-converged.

Reproduce:

    ./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=post-turn :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > focused.log 2>&1

The final archived log/data is the all-constraints-retained run. An exploratory first run still used the older endpoint-cap omission policy and was superseded before archiving. Timings are single-run, ordered and JIT-sensitive. Seed_scale -1 means compensation preserving old costates, not negative weights. See the preceding seeding archive for formulation and complete diagnostic file descriptions. The focused test passed; experimental misses are recorded results.
