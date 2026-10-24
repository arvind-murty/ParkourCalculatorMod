# Manual coupled-section endpoint spike

Date: 2026-10-24. User proposal: mark turnaround sections manually; provisional objective endpoints may not fall inside them. Commit size1 does not require the objective to advance to the next jump on every iteration. This is a test-only endpoint planner and matched-window experiment, not a production adaptive-window implementation.

Manual ranges use human jump numbers, inclusive. `[10,14]` maps nominal endpoint12 to forward15 or backward9; `[11,13]` gives forward14 or backward10. Prefer forward in the planner. No automatic detector. Endpoint directions are manually selected: incoming Z/MAX, outgoing Z/MIN. This does not claim those choices are universally right across later route turns.

## Matched setup

Every attempt starts at the SAME exact fixed jump6 entry at state55, reconstructed through the first5 accepted windows. X730.296913235570,Z-1096.123854818772,VX-.233829877256,VZ+.159910783797,yaw93.870376587deg. Commitment would cover only jump6 through state64. Margin0.0003, cap10000, EPS2=1e-24, same original early exits. Endpoint-cap removal and shared validation match the previous fixed run. No new committed route was assembled.

## Results

| Ending jump | End state | Objective | Accepted? | CF ms | Best continuous violation | Committed replay violation |
|---:|---:|---|---|---:|---:|---:|
| 12 | 88 | Z/MAX | no | 232.050 | 0.589759980783 | 0.131773224621 |
| 12 | 88 | Z/MIN | no | 9.493 | 1.50530499057 | 0.011340385309 |
| 15 | 113 | Z/MIN | no | 41.313 | 0.00940227256149 | 0 |
| 9 | 75 | Z/MAX | no | 29.126 | 0.0103832055224 | 0 |
| 14 | 99 | Z/MIN | no | 12.667 | 0.00940227171907 | 0 |
| 10 | 77 | Z/MAX | no | 4.937 | 0.00898186549693 | 0 |

All four shifted endpoints miss, but their best committed-prefix replay passes. Do not commit them under the current policy: full continuous lookahead still fails. Their reported exit states are proposed states, not accepted commits. Full-original-row replay over the whole window is in summary.csv; it includes rows temporarily omitted during the solve, so it differs from the continuous acceptance check.

## The failure changed

| Ending jump / objective | Initial unpatterned dual iterations | Initial residual | Minimum costate | Actual-model miss |
|---|---:|---:|---:|---:|
| 9 / Z/MAX | 46 | 4.11e-10 | 0.11127 | 0.0103832055224 |
| 10 / Z/MAX | 46 | 1.21e-09 | 0.212598 | 0.00898186549693 |
| 14 / Z/MIN | 420 | 4.98e-09 | 0.0122932 | 0.00940227171907 |
| 15 / Z/MIN | 225 | 6.66e-09 | 0.0338588 | 0.00940227256149 |

All shifted initial dual solves stop on PROJECTED_GRADIENT with comfortably nonzero costates and no direction fallback. Their active affine rows are satisfied to about the0.0003 target margin. The remaining actual continuous miss is inertia-pattern mismatch, unlike the original Z/MIN@88 stall.

- Backward end9: X63 pre-check velocity0.002396562811 is below0.005 although the local model assumes it survives. Actual X75 misses its lower wall by0.010383205522.
- Backward end10: X63 velocity0.002050165161; actual X76 misses its lower wall by0.008981865497.
- Forward end14: X63 velocity0.002100553983, plus Z82 and X84–90 zeroing. Actual X79 misses its lower wall by0.009402271719.
- Forward end15: nearly identical early candidate, X63 velocity0.002100554032; actual X79 miss0.009402272561, with additional later violations.

CF infers those patterns and retries. The backward retries fix X63 and fail numerical/recovery convergence; one reaches cap10000, the other stops NO_DESCENT. Forward retries fix X63/X84–90 and Z82 and also fail. Exact retry values and stop reasons are in solver-trace.txt and the per-case candidates/iterations CSVs. None is a proof that the corresponding physical window is infeasible.

The original Z/MIN baseline reproduces1264 iterations, residual0.0394, continuous violation1.505304990575 and prefix replay0.011340385309. Changing endpoint/objective therefore removes the measured INITIAL dual-convergence failure in these four comparisons, without rescuing the full patterned solve. It supports separating endpoint geometry from inertia recovery, rather than declaring the manual policy a complete solution or dismissing it because all attempts were rejected.

## Artifacts and limits

`summary.csv`: results, single-run timings, proposed jump6 exit position/velocity/yaw. `*-path.csv`: every tick position/velocity/yaw of the best candidate. `*-margins.csv` and `*-inertia.csv`: original retained constraint margins and threshold velocities for that candidate. `*-candidates.csv`: every validation event, assumed pattern, violations and failed assumption count. `*-iterations.csv`: sampled and final dual diagnostics for every pattern solve. solver-trace.txt preserves stage/rung timings and exits. entry.txt records hexadecimal exact entry bits; boundaries.txt records jump partition.

Focused test has2 passing methods: the manual endpoint-selection assertions and opt-in capture experiment. An experimental miss is recorded data, not a failed test. Production solver files unchanged. No full suite or route run. See metadata.txt for command, code revision and capture hash. Timings are single measurements with JVM/order/observer effects, not performance regression estimates.
