# Jump6 fixed-entry margin controls

All use the exact same fixed entry reconstructed sequentially through windows1–5, code b98f87f, EPS2=1e-24, cap10000, 52 retained rows (Z88 lower wall omitted), Z/MIN@88, committed replay through64 and continuous validation through88. Changes confined to /tmp/pkc-endpoints-fixed isolated worktree; main workspace production unchanged.

| Control | Outcome | Best continuous violation |
|---|---|---:|
| Saved single cold0.0003 | rejected |1.50530499057|
| Ordinary margin ladder, reuse multipliers | accepts FIRST rung0.05 |0|
| Ordinary ladder, reset multipliers each rung | accepts FIRST rung0.05 |0|
| Force continuation to0.0003, reuse multipliers | rejected after two patterns |1.60064021868|
| Force continuation to0.0003, reset multipliers | rejected, same as single cold0.0003 |1.50530499057|

The ordinary0.05 successes are identical:267 iterations, input-stall, no assumed zeroing. They occur before any multiplier reuse. Actual continuous clearance over all52 retained rows is at least0.034316913340; full Minecraft replay clearance at least0.034189192121. Endpoint replay X732.503789792640,Z-1090.008210829941 also satisfies omitted original Z88 lower bound-1090.300000011921 by0.291789181980. Therefore this provides a candidate that satisfies ALL53 original rows, including full replay, from the EXACT fixed entry. The fixed entry is not infeasible for6–12. Solver acceptance is still based on prefix replay plus full continuous validation; the full replay margins here were audited additionally.

Forced continuation uses margins0.05,0.02,0.01,0.005,0.0012,0.0003; skips acceptance AND best-candidate/pattern selection from larger rungs. Each new pattern starts a fresh ladder as usual. Trace lines still say certified when a larger-margin candidate has zero violation, but the probe explicitly continues. Warm pass0 at0.0003 misses1.60064021868, discovers X63; pass1 at0.0003 misses1.60909195804 and stops for no improvement. Cold pass0 at0.0003 reproduces1264 iterations, replay0.011340385309, model1.50530499057, no effective inertia pattern. Thus multiplier continuation is not sufficient to rescue fixed0.0003 in this control; it changes the path but does not produce a feasible target-margin candidate.

This does not isolate why historical normal's different entry/constraints/epsilon/cap succeeded at0.0003. It disproves attributing the current fixed failure to an impossible start state and shows normal's multiplier reuse is not by itself a demonstrated explanation. Larger margin produces a different candidate and is already sufficient here without reuse.

## Reproduction

Existing isolated fixed worktree at b98f87f with settings.gradle including core only. Archived probe source reconstructs the first5 fixed windows then modifies only jump6's margin schedule. Run:

```
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.endpointRun=fixed :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.JumpSixWindowEndpointsProbeTest
```

Production patch archived is forced/cold: skip larger-margin return/selection and set warm=null. For forced/warm retain the skip but restore warm=r.lambda. For ordinary/warm remove both probe changes. For ordinary/cold only set warm=null. Source adds the explicit six-rung schedule excluding0, used for forced controls; ordinary trials used the unchanged normal default schedule (stop at0.05 anyway). The observer captures the LAST evaluated candidate on a miss, not the internally selected best. Therefore forced-warm-last-evaluated-* files describe pass1's1.60909 failure, whereas best reported violation1.60064 is from pass0.

Raw per-rung traces, endpoint/margin CSVs, focused logs and JUnit results are preserved for all four controls. They are experimental outcomes, not feasibility assertions masquerading as green test guarantees. Only these four focused probes ran.
