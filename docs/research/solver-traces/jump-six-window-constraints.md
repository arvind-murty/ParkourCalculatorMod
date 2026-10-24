# Exact fixed jump6 window: entry and positional constraints

The failing seven-jump window covers jumps6–12, state ticks55–88, with inputs55–87. The commit=1 attempt would commit through state64. All tick numbers below are internal trace indices; UI row numbers are one greater. Positions are measured before that tick's inputs.

Entry: X730.296913235570, Z-1096.123854818772; VX-0.233829877256, VZ+0.159910783797; carried yaw93.870376587 degrees. These are the sequentially replayed fixed-run entry, not the capture's original debug state or the normal run's different entry.

All original positional rows are shown below, grouped into intervals and rounded to one decimal for readability. Stored boundaries differ by about1.2e-8; the linked CSV retains precision. A dash means no row on that axis at that tick.

| Tick | X requirement | Z requirement |
|---:|---|---|
| 55 | - | ≤ -1095.3 |
| 56 | - | ≤ -1095.3 |
| 57 | - | ≤ -1095.3 |
| 58 | ≤ 729.7 | - |
| 63 | [728.7, 729.7] | [-1095.3, -1093.7] |
| 64 | ≤ 729.7 | ≥ -1093.7 |
| 65 | ≤ 729.7 | - |
| 66 | ≤ 729.7 | - |
| 67 | [729.7, 731.3] | [-1093.3, -1091.7] |
| 68 | - | ≤ -1092.7 |
| 70 | [729.7, 731.3] | [-1093.3, -1091.7] |
| 74 | [729.7, 731.3] | [-1090.8, -1090.3] |
| 75 | [731.3, 731.7] | - |
| 76 | [731.7, 732.7] | [-1090.3, -1088.7] |
| 77 | ≤ 732.7 | - |
| 78 | ≤ 732.7 | - |
| 79 | [732.7, 734.3] | [-1090.3, -1088.7] |
| 80 | [732.7, 734.3] | - |
| 83 | - | ≥ -1088.7 |
| 84 | ≤ 732.7 | ≤ -1088.7 |
| 85 | ≤ 732.7 | - |
| 86 | ≤ 732.7 | - |
| 87 | [731.7, 732.7] | [-1090.3, -1088.7] |
| 88 | [731.7, 732.7] | [-1090.3, -1088.7] |

There are53 original scalar positional rows. X87<=732.699999988079 is present twice, as `X@87` and `X@87hi`; it appears once in the grouped table.

For the specific rejected Z/MIN attempt, the experimental endpoint-cap policy omitted ONLY the Z88 lower row (`Z@88lo`), leaving52 rows. The table restores it to show the full authored constraint set. Other objective attempts omit their own matching endpoint wall instead. All rows return for final full-route validation. The fixed inward optimization margin was0.0003; these are the original boundaries, before inward tightening.

Sources: [exact-entry reproduction](jump-six-window-endpoints-2026-10-23/README.md), [retained rows with full-precision values](jump-six-window-endpoints-2026-10-23/fixed-constraint-margins.csv), [window trace summary](fixed-seven-window-local-bounds-2026-10-23/windows.csv), original `~/Downloads/LinkcraftII_Competition_01/real/11.json` angleSolver ticks. Original Z88 row rechecked against the capture. No rerun performed.
