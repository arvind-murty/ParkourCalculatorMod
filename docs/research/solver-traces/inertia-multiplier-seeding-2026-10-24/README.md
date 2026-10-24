# Seeding the inertia-aware dual from the preceding solution

Date: 2026-10-24. User asks whether inertia-aware retries can reuse multiplier hints instead of starting cold. Test-only experiment; production solver unchanged. [Previous endpoint comparison](../manual-coupled-window-2026-10-24/README.md) is the matched cold reference.

## Setup and row mapping

The probe reconstructs the exact saved fixed jump6 X/Z entry from its hexadecimal values, builds the same slice/spec and temporarily removes matching objective endpoint caps. Vertical state is computed independently from the fixed input schedule; Y does not affect the horizontal result. Initial no-zeroing passes reproduce all four archived continuous violations within1e-9, and the cold patterned retries reproduce the prior iteration counts/violations. These positional-only specs have identity facing prefolds. No facing/order/general cross-axis seed mapping is claimed.

For each new pattern, original position rows retain their order and physical identity; the probe checks row name, axis and equality kind before carrying their multipliers. Duplicate names therefore do not collapse into a map entry. New velocity-band rows are appended, with two opposing normals per zeroing. The direct loop reproduces the single-margin CF pattern loop with four passes maximum and stop-on-no-improvement. It uses the production `CostateDualSolver`, `FacingPrefold.expand`, pattern inference and shared validation. It does not modify or hook the production CF implementation.

Fixed margin .0003, EPS2=1e-24, cap10000, original early exits, same exact entry for every independent attempt. Acceptance requires full continuous actual-inertia feasibility plus retained-row replay through state64. No SLP or other recovery stages.

## Four seed variants

`seed_scale` in the CSVs is a test-harness encoding, not a production parameter:

- `0`: cold, all multipliers zero.
- `1`: copy the preceding position-row multipliers; new band multipliers zero.
- `.25`: copy one quarter of those weights; new band multipliers zero.
- `-1`: **preserve the previous costates**. Copy full position-row weights, then choose nonnegative weights on the new opposing inertia rows to compensate for the changed coefficients. This is not multiplying weights by minus one.

The relevant algebra is

    g_old = c_old - A_old^T lambda_old
    g_new = c_new - A_new^T lambda_old - B^T lambda_band.

Choose the new band weights so `B^T lambda_band` equals the difference between the uncorrected new costates and the old ones. Each band has normals +v and -v, so either sign of compensation is representable with nonnegative weights. For the measured free-to-fixed patterns, the carry-cut coefficient change lies in these velocity-row spans. Under the new folded model, successive zeroing velocity rows on the same axis have disjoint control support; the probe projects the costate difference onto each pair. The measured angle differences verify the preservation, rather than assuming it.

This preserves the old directional response without fixing those angles for later iterations. The dual remains free to improve all multipliers and controls. No extra physical constraint is added beyond the normal pattern-band rows.

## Results: the shorter inertia-aware solves now succeed

Patterned retry only; the preceding initial solve is identical across variants.

| Window/objective | Cold retry | Copy position weights | Preserve costates |
|---|---|---|---|
| Jumps6–9, Z/MAX |10000 iterations, miss .318900702 |741, miss .278055018 | **12, accepted** |
| Jumps6–10, Z/MAX |914, miss .583956613 |332, miss .242159748 | **12, accepted** |
| Jumps6–14, Z/MIN |272, miss16.915740158 |10000, miss2.862356649 |10000, miss16.915720525 |
| Jumps6–15, Z/MIN |1455, miss3.125772893 |3298, miss3.162926101 |89, miss9.341131046 |

Quarter-strength seeds do not land any case. Full outcome/iteration/residual data is in outcomes.csv and passes.csv. “Miss” in this table is the actual continuous violation of the retry, not the initial candidate's smaller violation. Every nonlanding attempt retains the same initial best as before.

For the accepted costate-preserving retries:

| Metric | End jump9 | End jump10 |
|---|---:|---:|
| Initial actual continuous violation |.010383205522 |.008981865497 |
| Initial projected residual |.010683205522 |.009281865497 |
| Max initial yaw change from preceding candidate |5.15e-14 degrees |1.53e-13 degrees |
| Final projected residual |1.61e-9 |1.48e-9 |
| Minimum final costate |.224552 |.294938 |
| Costate-direction fallback ticks |0 |0 |
| Failed inertia assumptions |0 |0 |
| Full continuous violation |0 |0 |
| Committed-prefix replay violation |0 |0 |
| Minimum full retained-row replay clearance |.000256461 |.000238673 |

Neither shorter endpoint has a matching Z cap to omit, so those full replay margins cover all original rows in each local window as well. This is not a full-route result or an optimality certificate.

For comparison, simply copying old weights changes initial recovered angles by up to87.25 degrees on end9 and74.94 on end10, even though the physical same-angle perturbation was only about .01 blocks. Thus naive multiplier reuse is not trajectory reuse. Correctly compensating the changed adjoint coefficients preserves the good trajectory and makes the small repair possible in these cases.

On end9 the seeded lower X63-band multiplier is1.113485219381 (upper0); it finishes at .561550054735. On end10 it starts1.364222227875 and finishes .716742082138. The new constraint's initial multiplier therefore matters; setting both band multipliers to zero lost the old costate directions.

## Interpretation and scope

This establishes a specific mechanism and fix for these two short-window failures: the cold/new-dual initialization was avoidable, and preserving costates across the coefficient change leads to a clean converged, inertia-consistent solution. It is stronger evidence than an arbitrary cap or damping win because the initialization explicitly retains the preceding angle response.

It does not resolve the longer multi-zeroing retries. Their corrected seeds also preserve angles to about1e-12 degrees and start at residual about .00976, but then enter tiny-costate/fallback regimes and fail. Investigation of those needs separate numerical/recovery analysis. It is not proven that seeding alone should fix every pattern.

The demonstrated construction handles added zeroing rows with an unchanged positional row set and identity facing fold. Removed/replaced patterns, ties/order changes, general two-axis rows, free-start support terms, cross-margin seeds and numerical row rescaling require explicit mapping and verification before a production implementation. It should remain scoped as research until those contracts are addressed. The test uses new variable names and adds no production flags/default changes.

## Reproduction and artifacts

Code revision b98f87f in `/tmp/pkc-endpoints-fixed`; production diff there is empty, settings.gradle includes core only. Main branch adds the opt-in test and records. Source archived beside this README.

    ./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > focused.log 2>&1

Only this focused probe ran. One JUnit method passes; experimental misses are records, not failed assertions. Timings are single-run solve timings with JIT/order effects, not a performance benchmark. Observers sample every100 iterations and on exit. Per-case/pass files include initial metrics, iterations, all multipliers, initial/final costates by tick, constraint margins and yaw/position/velocity paths. Raw source/input hashes and entry evidence are in the previous manual-coupled-window archive. No input fixture was modified.

## Relation to the margin ladder

Both mechanisms warm-start a related dual solve. Changing only the margin modifies inequality RHS values, not the objective vector or constraint normals. Consequently the same lambda automatically gives the same costates `c-A^T lambda` and the same initial angles; the updated RHS changes the gradient and subsequent steps. Changing an inertia pattern modifies those coefficients and introduces rows, so raw lambda carry loses that property. The compensated band prices restore the previous costates for the cases tested. This extends the useful initialization behavior of the margin ladder across a structural model change; neither mechanism alone guarantees a successful solve.
