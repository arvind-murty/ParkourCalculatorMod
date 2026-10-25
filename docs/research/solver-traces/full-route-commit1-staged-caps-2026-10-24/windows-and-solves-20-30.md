# Every window and solve from starts20–30

Current staged baseline: commit1, cap1000 for every attempt below, fixed margin.0003, decoder fallback disabled, retention/seeding enabled. No10000 retry was needed at these entries. Input horizons are[start,end); all coordinate objectives evaluate position at end. Coordinate calls are ClosedFormSolve.optimizeRobust; SLP is centered feasibility restoration with ordinary/aware retry when needed.

✓ accepted; × missed; dash not attempted. A cached miss runs no new solves. Timings are full window-call ms. Only successful windows commit, exactly one jump.

| Window |Input horizon |Z/max |Z/min |X/max |X/min |SLP |Window ms |Committed exit |
|---|---|---|---|---|---|---|---:|
| 20–29 |132–244 |× |× |× |× |× |867.8 |— |
| 20–26 |132–210 |× |× |× |× |× |1560.9 |— |
| 20–24 |132–199 |× |× |× |× |× |759.0 |— |
| 20–22 |132–178 |× |× |× |× |× |145.1 |— |
| 20–21 |132–169 |× |✓ |— |— |— |16.4 |155 |
| 21–30 |155–253 |× |× |× |× |× |1042.0 |— |
| 21–27 |155–210 |× |× |× |× |× |579.1 |— |
| 21–25 |155–210 |— |— |— |— |— |Cached miss |— |
| 21–23 |155–188 |× |× |× |× |× |66.3 |— |
| 21–22 |155–178 |✓ |— |— |— |— |0.1 |167 |
| 22–31 |167–260 |× |× |× |× |× |459.2 |— |
| 22–28 |167–223 |× |× |× |× |× |455.6 |— |
| 22–26 |167–210 |× |× |× |× |× |294.3 |— |
| 22–24 |167–199 |× |× |× |× |✓ |72.5 |176 |
| 23–32 |176–272 |× |× |× |× |✓ |460.4 |187 |
| 24–33 |187–283 |× |× |× |× |✓ |802.5 |196 |
| 25–34 |196–293 |× |× |× |× |× |913.0 |— |
| 25–31 |196–260 |× |× |× |× |× |226.7 |— |
| 25–29 |196–244 |× |× |× |× |✓ |67.7 |205 |
| 26–35 |205–305 |× |× |× |× |✓ |227.3 |207 |
| 27–36 |207–320 |× |× |× |× |✓ |576.7 |209 |
| 28–37 |209–338 |× |× |× |× |× |1425.6 |— |
| 28–34 |209–293 |× |× |× |× |✓ |282.9 |222 |
| 29–38 |222–352 |× |× |× |× |× |836.4 |— |
| 29–35 |222–305 |× |× |× |× |✓ |217.2 |241 |
| 30–39 |241–373 |× |× |× |× |× |1957.3 |— |
| 30–36 |241–320 |× |× |× |× |✓ |280.7 |250 |

All four closed-form objectives miss before every SLP invocation. Starts22–30 accept through SLP only. Starts23,24,26,27 accept the first10-jump window; starts28,29,30 reject it and fall back to7. Starts20 and21 accept closed form on2-jump windows.

At start21, nominal21–27 and21–25 both expand to the same actual horizon155..210 with the same committed prefix and seed, so21–25 reuses the cached miss. There is no separate repeated set of objective calls.

[Every individual objective/SLP call with ms](objective-attempts-20-30.csv), [all original windows](windows.csv), [full run report](README.md). This is extracted from existing logs, not a new solve.
