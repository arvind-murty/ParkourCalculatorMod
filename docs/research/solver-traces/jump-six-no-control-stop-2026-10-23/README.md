# Jump6: disable control-change early stop

Code b98f87f in isolated /tmp/pkc-endpoints-fixed. Sequentially reconstruct the exact first5 fixed windows, then fixed Z/MIN6–12, margin0.0003, cap10000, EPS2=1e-24, Z88 lower wall omitted, committed replay through64. Only change to production in the isolated worktree: remove the RECOVERED_INPUT_STALL break. Projected-gradient convergence check and all other stops unchanged. Main workspace production unchanged.

| Candidate | Iterations | Stop | Final projected residual | Continuous violation | Prefix replay violation |
|---|---:|---|---:|---:|---:|
| Saved baseline, no fixed zeroing |1264|input-change stall|0.03940|1.50530499057|0.0113403853090|
| No input-change exit, no fixed zeroing |10000|iteration limit|0.2070|1.51511722273|0.00536855730968|
| No input-change exit, retry with X63 zeroing |547|no descent|0.5352|1.19039721828|0.0541514121645|

The longer initial attempt discovers X63 zeroing, so an additional pattern solve runs and improves worst continuous violation. Neither reaches gradient tolerance1e-8 or produces an accepted candidate. Simply disabling the unsafe early stop is insufficient for this case. The last-evaluated artifacts contain the X63 retry, which is also the best candidate in this run. Position/yaw assertions through first5 commits still match original traces. Only this focused probe ran.

## Multiplier stopping criterion

We iterate on lambda, but a small change in lambda is not generally a convergence certificate either: damping/line search can make updates small while the gradient remains nonzero, and nonunique multipliers can move without changing the primal solution. The existing projected-gradient residual, max_j |Pi(lambda_j-gradient_j)-lambda_j|, is the appropriate stationarity check for nonnegative inequality multipliers (free equality coordinates use the ordinary gradient). It already runs before the input-change heuristic. At lambda_j=0, positive gradient is optimal for an inactive inequality, hence use projection rather than requiring every raw gradient component to vanish.

Reproduce with archived source in core/src/test/.../solver and the archived production patch, core-only settings, then:

```
./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home -Dpkc.endpointRun=fixed :core:test -PslowTests --tests de.legoshi.parkourcalc.core.anglesolver.solver.JumpSixWindowEndpointsProbeTest
```

Production isolated source restored after the probe. This experiment changes a stopping rule, not a proof of convergence for the remaining numerical algorithm.
