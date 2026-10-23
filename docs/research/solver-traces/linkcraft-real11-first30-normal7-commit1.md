# LinkcraftII real/11: first 30 jumps, normal-margin 7 ladder

**Result:** solved all 30 jumps (250 ticks, 229 constraints) with commit=1 and window ladder `7,5,3,2,1`. The full solve took about 7.1 s in this trace run. Nonterminal windows use the standard robust margin ladder `0.05, 0.02, 0.01, 0.005, 0.0012, 0.0003, 0.0`; each closed-form objective stops at its first certified result. The final window instead enters `RecoveryLadder`, whose closed-form stage uses its ascending margin sequence before SLP recovery. Its rung-by-rung results are in the terminal row of the CSV and raw trace. Jump indices and tick spans below are zero-based, half-open.

This rerun has explicit objective start/end logs, fixed-margin solver rung output, fallback SLP result logs, and a terminal recovery-chain label. The CSV has one row per objective attempt with every closed-form inertia pattern and margin rung, including the exact replay violation. The raw trace retains the solver and per-tick path logs.

| Attempt | Jump window | Tick window | Objective attempts in order | Window result / time |
|---:|---|---|---|---|
| 1 | [0,7) | [0,68) | `Z/MAX@68` solved @ pass0 free 5.00e-02 | **solved**, 54.1 ms |
| 2 | [1,8) | [12,71) | `Z/MAX@59` solved @ pass0 free 5.00e-02 | **solved**, 8.1 ms |
| 3 | [2,9) | [21,75) | `Z/MAX@54` solved @ pass0 free 2.00e-02 | **solved**, 10.1 ms |
| 4 | [3,10) | [30,77) | `Z/MAX@47` solved @ pass0 free 2.00e-02 | **solved**, 6.5 ms |
| 5 | [4,11) | [40,80) | `Z/MAX@40` solved @ pass0 free 2.00e-02 | **solved**, 7.2 ms |
| 6 | [5,12) | [55,88) | `Z/MAX@33` miss; `Z/MIN@33` solved @ pass1 x@8,@26 3.00e-04 | **solved**, 119.0 ms |
| 7 | [6,13) | [64,90) | `Z/MAX@26` miss; `Z/MIN@26` solved @ pass0 free 1.00e-02 | **solved**, 12.8 ms |
| 8 | [7,14) | [68,99) | `Z/MAX@31` miss; `Z/MIN@31` solved @ pass1 x@16-22+z@14 3.00e-04 | **solved**, 23.4 ms |
| 9 | [8,15) | [71,113) | `Z/MAX@42` solved @ pass1 x@13-19+z@11,@30 3.00e-04 | **solved**, 16.1 ms |
| 10 | [9,16) | [75,117) | `Z/MAX@42` solved @ pass1 x@9-15+z@7 1.20e-03 | **solved**, 18.6 ms |
| 11 | [10,17) | [77,128) | `Z/MAX@51` miss; `Z/MIN@51` miss; `X/MAX@51` solved @ pass1 x@7-13+z@5 1.20e-03 | **solved**, 129.7 ms |
| 12 | [11,18) | [80,130) | `Z/MAX@50` miss; `Z/MIN@50` miss; `X/MAX@50` miss; `X/MIN@50` miss; `fallback SLP Z/MAX@50` miss | **miss**, 340.1 ms |
| 13 | [11,16) | [80,117) | `Z/MAX@37` miss; `Z/MIN@37` solved @ pass1 x@4-10+z@2 1.20e-03 | **solved**, 23.7 ms |
| 14 | [12,19) | [88,132) | `Z/MAX@44` miss; `Z/MIN@44` miss; `X/MAX@44` miss; `X/MIN@44` solved @ pass1 x@0-2 3.00e-04 | **solved**, 60.5 ms |
| 15 | [13,20) | [90,155) | `Z/MAX@65` miss; `Z/MIN@65` miss; `X/MAX@65` miss; `X/MIN@65` miss; `fallback SLP Z/MAX@65` miss | **miss**, 455.5 ms |
| 16 | [13,18) | [90,130) | `Z/MAX@40` miss; `Z/MIN@40` miss; `X/MAX@40` miss; `X/MIN@40` miss; `fallback SLP Z/MAX@40` solved (SLP) | **solved**, 97.2 ms |
| 17 | [14,21) | [99,167) | `Z/MAX@68` miss; `Z/MIN@68` miss; `X/MAX@68` miss; `X/MIN@68` miss; `fallback SLP Z/MAX@68` miss | **miss**, 656.7 ms |
| 18 | [14,19) | [99,132) | `Z/MAX@33` miss; `Z/MIN@33` solved @ pass0 free 1.00e-02 | **solved**, 8.4 ms |
| 19 | [15,22) | [113,176) | `Z/MAX@63` solved @ pass1 x@3-4,@32-62+z@24 5.00e-02 | **solved**, 32.4 ms |
| 20 | [16,23) | [117,187) | `Z/MAX@70` miss; `Z/MIN@70` miss; `X/MAX@70` miss; `X/MIN@70` miss; `fallback SLP Z/MAX@70` miss | **miss**, 390.0 ms |
| 21 | [16,21) | [117,167) | `Z/MAX@50` miss; `Z/MIN@50` solved @ pass0 free 1.00e-02 | **solved**, 217.2 ms |
| 22 | [17,24) | [128,196) | `Z/MAX@68` miss; `Z/MIN@68` miss; `X/MAX@68` miss; `X/MIN@68` miss; `fallback SLP Z/MAX@68` miss | **miss**, 546.0 ms |
| 23 | [17,22) | [128,176) | `Z/MAX@48` solved @ pass0 free 2.00e-02 | **solved**, 1.4 ms |
| 24 | [18,25) | [130,205) | `Z/MAX@75` miss; `Z/MIN@75` miss; `X/MAX@75` miss; `X/MIN@75` miss; `fallback SLP Z/MAX@75` miss | **miss**, 1024.4 ms |
| 25 | [18,23) | [130,187) | `Z/MAX@57` miss; `Z/MIN@57` miss; `X/MAX@57` miss; `X/MIN@57` miss; `fallback SLP Z/MAX@57` miss | **miss**, 343.7 ms |
| 26 | [18,21) | [130,167) | `Z/MAX@37` solved @ pass0 free 5.00e-02 | **solved**, 0.3 ms |
| 27 | [19,26) | [132,207) | `Z/MAX@75` miss; `Z/MIN@75` miss; `X/MAX@75` miss; `X/MIN@75` miss; `fallback SLP Z/MAX@75` miss | **miss**, 896.4 ms |
| 28 | [19,24) | [132,196) | `Z/MAX@64` miss; `Z/MIN@64` miss; `X/MAX@64` miss; `X/MIN@64` miss; `fallback SLP Z/MAX@64` miss | **miss**, 459.5 ms |
| 29 | [19,22) | [132,176) | `Z/MAX@44` solved @ pass0 free 2.00e-02 | **solved**, 1.0 ms |
| 30 | [20,27) | [155,209) | `Z/MAX@54` miss; `Z/MIN@54` miss; `X/MAX@54` miss; `X/MIN@54` miss; `fallback SLP Z/MAX@54` miss | **miss**, 297.5 ms |
| 31 | [20,25) | [155,205) | `Z/MAX@50` miss; `Z/MIN@50` miss; `X/MAX@50` miss; `X/MIN@50` miss; `fallback SLP Z/MAX@50` miss | **miss**, 203.9 ms |
| 32 | [20,23) | [155,187) | `Z/MAX@32` miss; `Z/MIN@32` miss; `X/MAX@32` miss; `X/MIN@32` miss; `fallback SLP Z/MAX@32` miss | **miss**, 41.6 ms |
| 33 | [20,22) | [155,176) | `Z/MAX@21` solved @ pass0 free 5.00e-02 | **solved**, 0.3 ms |
| 34 | [21,28) | [167,222) | `Z/MAX@55` miss; `Z/MIN@55` miss; `X/MAX@55` miss; `X/MIN@55` miss; `fallback SLP Z/MAX@55` solved (SLP) | **solved**, 220.2 ms |
| 35 | [22,29) | [176,241) | `Z/MAX@65` miss; `Z/MIN@65` miss; `X/MAX@65` miss; `X/MIN@65` miss; `fallback SLP Z/MAX@65` solved (SLP) | **solved**, 217.3 ms |
| 36 | [23,30) | [187,250) | `terminal recovery X/MAX@63` solved (recovery chain; see trace) | **solved**, 141.5 ms |

## Detailed records

- [Objective-by-objective CSV](linkcraft-real11-first30-normal7-commit1-objectives.csv): each row includes objective result and runtime; the final column lists every closed-form pattern and margin rung, with certification status or replay violation.
- [Full raw trace](linkcraft-real11-first30-normal7-commit1.txt): includes all solver output, SLP activity, window seeds/constraints, and every committed tick position, velocity, and yaw.
- Reproducer: `First30NormalMarginTraceTest`, run with `./gradlew -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home :core:test -PslowTests --tests de.legoshi.parkourcalc.anglesolver.First30NormalMarginTraceTest`. Override the TAS path with `PKC_TRACE_INPUT` if needed.
- Bookkeeping branch: `codex/first30-margin-trace-bookkeeping`. Commit `5b632de` pins margin to `0.0003`; `79bb653` restores the normal ladder. Check out `5b632de` to reproduce the fixed-margin experiment; the normal-margin trace is at the current branch tip.
