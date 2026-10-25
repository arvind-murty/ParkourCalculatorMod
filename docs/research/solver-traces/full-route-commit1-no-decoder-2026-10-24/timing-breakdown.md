# Full commit1 run: timing breakdown

All times below are instrumented wall time from the saved trace. No rerun. Full solve317.605258s; measured window calls317.5802s. Source implementation and configuration are in [the run report](README.md).

## Where the time went

| Work | Count | Seconds |
|---|---:|---:|
| Failed window attempts |59 |283.079 |
| Successful windows, including earlier failed objectives |38 |34.501 |
| Closed-form margin/pattern runs, overlapping those windows |795 |291.581 |
| Of those, runs hitting10000 iterations |438 |249.300 |
| All SLP calls |70 |24.490 |
| Accepted objective/SLP calls alone |38 |3.574 |

These rows overlap; do not add them. Successful WINDOW timing includes earlier failed coordinate objectives, whereas successful OBJECTIVE timing counts only the call that returned an accepted candidate.59 of97 nominal windows fail; one failed rung is cached and costs no newly timed solve.391 objective/SLP calls contain353 misses.

The dual executes4,987,427 iterations across795 runs. Stop counts:438 ITERATION_LIMIT,145 DIVERGENCE_STALL,134 NO_DESCENT,53 RECOVERED_INPUT_STALL,25 PROJECTED_GRADIENT,2 LAMBDA_LIMIT. Many difficult long windows try four objectives, each possibly several fixed inertia patterns, before SLP and then a shorter window. The10000 cap is per dual run, not per window or whole route. Fixed margin.0003 means this cost is not a sweep of the normal margin ladder.

Retention audits total30.438s, already included in closed-form time. This is a useful cost component but does not explain the majority of317.605s. Heavy trace output and validation are included in the instrumented timings; this was not an uninstrumented performance benchmark. No per-iteration CPU or logging-only attribution is available.

## Every starting jump and window attempt

Seconds; **bold** marks the accepted window. Dash means not attempted; cache means a reused miss. The objective column identifies the accepted method, with coordinate objectives solved by closed form. Each numeric cell includes all objective attempts and SLP performed for that window.

| Starting jump |10 jumps |7 jumps |5 jumps |3 jumps |2 jumps |1 jump |Total s |Accepted method |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | **0.826** | - | - | - | - | - |0.826 |Z/MIN |
| 2 | **1.464** | - | - | - | - | - |1.464 |Z/MIN |
| 3 | **0.656** | - | - | - | - | - |0.656 |Z/MIN |
| 4 | **0.704** | - | - | - | - | - |0.704 |Z/MIN |
| 5 | **0.206** | - | - | - | - | - |0.206 |Z/MIN |
| 6 | 3.983 | **0.333** | - | - | - | - |4.316 |Z/MIN |
| 7 | 1.269 | **0.145** | - | - | - | - |1.414 |Z/MIN |
| 8 | 2.327 | **0.012** | - | - | - | - |2.339 |Z/MAX |
| 9 | 2.601 | 1.548 | **0.014** | - | - | - |4.163 |Z/MAX |
| 10 | **2.751** | - | - | - | - | - |2.751 |X/MAX |
| 11 | 5.261 | **0.808** | - | - | - | - |6.069 |Z/MAX |
| 12 | 7.095 | **0.885** | - | - | - | - |7.981 |X/MAX |
| 13 | **4.285** | - | - | - | - | - |4.285 |SLP |
| 14 | 5.782 | **0.581** | - | - | - | - |6.363 |Z/MIN |
| 15 | 8.331 | **0.170** | - | - | - | - |8.501 |Z/MAX |
| 16 | 4.499 | **1.119** | - | - | - | - |5.618 |SLP |
| 17 | 7.748 | 3.938 | **0.005** | - | - | - |11.691 |Z/MAX |
| 18 | 10.259 | 4.659 | **1.455** | - | - | - |16.373 |SLP |
| 19 | 3.057 | 7.058 | 2.663 | **0.003** | - | - |12.781 |Z/MAX |
| 20 | 6.141 | 5.622 | 3.384 | 0.463 | **0.026** | - |15.637 |Z/MAX |
| 21 | 5.922 | 2.927 | cache | 0.344 | **<0.001** | - |9.193 |Z/MAX |
| 22 | 4.120 | **1.831** | - | - | - | - |5.951 |SLP |
| 23 | 1.884 | 2.055 | **1.486** | - | - | - |5.426 |SLP |
| 24 | **3.402** | - | - | - | - | - |3.402 |SLP |
| 25 | **3.482** | - | - | - | - | - |3.482 |SLP |
| 26 | **0.888** | - | - | - | - | - |0.888 |SLP |
| 27 | **2.438** | - | - | - | - | - |2.438 |SLP |
| 28 | 2.254 | **1.138** | - | - | - | - |3.393 |SLP |
| 29 | 3.040 | **1.986** | - | - | - | - |5.026 |SLP |
| 30 | 13.245 | **1.171** | - | - | - | - |14.416 |SLP |
| 31 | 6.194 | **0.002** | - | - | - | - |6.195 |Z/MAX |
| 32 | 10.616 | **0.129** | - | - | - | - |10.745 |Z/MIN |
| 33 | 8.091 | 3.740 | **0.001** | - | - | - |11.832 |Z/MAX |
| 34 | 22.901 | 3.737 | 0.605 | **<0.001** | - | - |27.244 |Z/MAX |
| 35 | 11.694 | 5.072 | 1.084 | **<0.001** | - | - |17.851 |Z/MAX |
| 36 | 7.307 | 2.138 | 2.265 | **0.047** | - | - |11.757 |Z/MAX |
| 37 | 15.248 | 6.779 | 2.801 | 1.029 | **0.032** | - |25.890 |Z/MIN |
| 38 | 5.912 | 7.127 | 1.450 | 1.122 | 0.564 | **0.019** |16.194 |Z/MIN |
| 39 | 6.017 | 5.957 | 6.899 | 2.123 | 0.901 | 0.223 |22.120 |Miss |

[Every objective/SLP call and its timing](objectives.csv), [every pattern/margin dual run](cf-rungs.csv), [every window including actual endpoint](windows.csv), [per-start timing CSV](timing-by-start.csv).

The slowest window starts at jump34: the10-jump attempt takes22.901s and fails. Within it X/min takes6.074s, X/max5.466s and Z/max5.371s. The longest individual coordinate call is X/min on the10-jump window starting32, taking9.218s. At39 all six failed windows together take22.120s.
