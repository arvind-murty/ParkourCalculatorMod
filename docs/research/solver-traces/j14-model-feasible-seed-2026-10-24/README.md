# Seed from feasible assumed-model solutions, without an actual-miss cutoff

2026-10-24. User replaces the provisional .01-block actual-miss condition with validity under the assumed model. Implemented as a new test-only policy mode, retaining the older modes for historical comparison. Production retention unchanged; production multiplier seeding/policy not yet integrated. Decoder unchanged, compensation-preservation rejection still commented out, finite/nonnegative seed assertion retained. User j127 capture edit untouched.

## Policy

Seed a changed-pattern retry when the previous candidate's assumed-model violation is <=1e-8. No actual continuous/replay violation size limit and no mismatch-count condition. The outer loop already requires a failed candidate and a changed inferred pattern before retrying.

Assumed-model violation combines all original spec constraints evaluated on the fixed-pattern path with violation of each ASSUMED ZEROING velocity band, abs(v)<=.005. The .0003 inward optimization margin is not required for seeding; the check is against the original assumed-model constraints with floating-point tolerance. Surviving checks that were never constrained by that model are not treated as assumed-model constraint rows. If the new actual checks differ, final candidate validation still uses actual continuous inertia and exact replay over the committed prefix. Assumed feasibility licenses initialization; it does not accept the path.

## j14 rerun

Same exact state55, Z/MAX@103, all70 positional constraints, fixed .0003, EPS24, cap10000. No pass number hardcoding.

| Before solve | Previous actual miss | Previous assumed-model miss | Seed |
|---|---:|---:|---|
|0 |No previous candidate |No previous candidate |No |
|1 |.883040338417 |.879105563646 |No |
|2 |.332352548739 |.236166363409 |No |
|3 |.006556921486 |0 |Yes |

This reproduces the successful delayed repair and close-only policy: valid saved candidates at77,78,82; selected82 has Z103=-1093.6526875545678. Whole continuous and committed exact violations0, no failed assumed inertia checks. Full exact replay passes as an additional audit. The terminal373 iterate still fails, and retention returns the earlier valid candidate. The criterion is now whether the model solve produced a feasible path, not whether the physical discrepancy happened to be below .01.

## Reproduction

One focused opt-in test, zero failures/errors; no full suite/graph/route. Run from isolated core-only `/tmp/pkc-endpoints-fixed`, b98f87f+c392645 production files, with archived probe:

```sh
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.inertiaSeedProbe=true -Dpkc.inertiaSeedCases=j14-model-feasible-seed :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.InertiaMultiplierSeedProbeTest > /Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/j14-model-feasible-seed-2026-10-24/focused.log 2>&1
```

[Explicit policy decisions](seed-policy.csv), [every solve](passes.csv), [saved candidate](j14-seed-1.0-pass3-retention.csv), [trace](solver-trace.txt), [JUnit result](focused-result.xml), [source snapshot](InertiaMultiplierSeedProbeTest.java). All per-pass paths, yaw, inertia velocities, costate/price and constraint margin diagnostics preserved. Terminal solver diagnostics and selected intermediate path diagnostics remain distinguished as in the preceding archives. Previous [close-only policy](../j14-close-seed-2026-10-24/README.md) is superseded for the proposed selection rule.


## Continuing optimization and selecting the returned iteration

Saving a feasible candidate does not stop the inner dual solve. It continues to its existing projected-gradient, recovered-input-stall, no-descent, divergence-stall, multiplier-limit or iteration-limit stop. The candidate observer audits decoded paths at loop-entry/final samples without changing the optimization steps. Retention screens actual continuous feasibility, then checks promising candidates through shared continuous/replay validation. It retains the feasible candidate with the best continuous objective score, including any configured smoothing penalty. Here Z/MAX with no smoothing means largest actual continuous Z103. It stores the angles and that iteration's multiplier snapshot, not only the number.

Latest j14 first saves77, improves78 then82, and still runs to373 NO_DESCENT. Terminal physical miss3.61652403689 is rejected, so it returns the saved82 candidate with Z103=-1093.6526875545678. If a valid terminal candidate scores better, the caller uses it instead. Saving82 does not establish it is globally optimal. Once this fixed-pattern/margin solve completes and supplies an accepted candidate, the surrounding closed-form routine returns without advancing to another margin or inertia pattern. This statement is about this solve, not a claim that the outer Optimize graph cannot run later stages. No new run for this clarification.
