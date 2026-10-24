# Jump6–12 Z/MIN replay endpoints

Sequentially reconstruct exact committed states for windows1–5, then evaluate Z/MIN for6–12 at state88. Normal code aac6527; fixed b98f87f. Same isolated worktrees/core-only Gradle settings as the [first-five endpoint reproduction](../first-five-window-endpoints-2026-10-23/metadata.txt). Only focused `JumpSixWindowEndpointsProbeTest` runs; production unchanged. Source variants archived here; copy the selected variant to its class filename in core/src/test/.../solver before running. JVM property pkc.endpointRun=normal or fixed.

| Z/MIN result | Replay X88 | Replay Z88 |
|---|---:|---:|
| Normal, accepted |732.655541654916|-1090.148753136121|
| Fixed, rejected |732.687406696540|-1091.807886997006|

Normal exactly reproduces the recorded pattern X63/X81 and accepted0.0003 rung with71 iterations, residual3.109e-15, dual-5.062496823. Fixed reproduces the recorded1264-iteration input-stall, residual0.03940, dual-5.134714974, committed replay violation0.0113403853090, continuous violation1.50530499057, empty effective inferred pattern. Fixed endpoint belongs to that failed candidate; no accepted seven-jump fixed result exists.

An initial reconstruction used the saved seeds rounded to12 decimal places. It did not reproduce jump6: normal took a different pattern and accepted0.02; fixed unexpectedly passed on its fourth pattern. Those exploratory logs are explicitly named rounded-seed and are NOT the reported endpoint comparison. Sequential exact replay from the original state recovered both original solve traces. This demonstrates sensitivity to seed rounding here; previous first-five endpoint probes had prefix position differences under2.3e-12 and remain accurate to their reported decimal precision. Final sequential first-five prefix position discrepancies are at most5e-13 against logged coordinates.

Endpoint CSVs contain both continuous and Minecraft replay states for all six windows; last row is6–12. max_prefix_* fields are only checked for windows1–5; zeros on row6 mean not measured there.
