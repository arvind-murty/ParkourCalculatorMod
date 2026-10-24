# Route constraints: jumps6–20 and candidate objective endpoints

Date: 2026-10-24. Read from original real/11.json, without running a solve. Human jump numbers; internal state ticks, with UI row=tick+1. Positions are before inputs. Window6–20 starts55 and ends155. Original rows shown; no endpoint caps removed and no inward margin applied. Values rounded to three decimals below; precise scalar rows are in [CSV](jump-six-through-twenty-constraints.csv). A dash means no bound on that axis at that state.

## Jump partition

| Jump | Start state | End state |
|---:|---:|---:|
| 6 | 55 | 64 |
| 7 | 64 | 68 |
| 8 | 68 | 71 |
| 9 | 71 | 75 |
| 10 | 75 | 77 |
| 11 | 77 | 80 |
| 12 | 80 | 88 |
| 13 | 88 | 90 |
| 14 | 90 | 99 |
| 15 | 99 | 113 |
| 16 | 113 | 117 |
| 17 | 117 | 128 |
| 18 | 128 | 130 |
| 19 | 130 | 132 |
| 20 | 132 | 155 |

## All positional constraints

| State tick | X | Z |
|---:|---|---|
| 55 | - | ≤ -1095.300 |
| 56 | - | ≤ -1095.300 |
| 57 | - | ≤ -1095.300 |
| 58 | ≤ 729.700 | - |
| 63 | [728.700, 729.700] | [-1095.300, -1093.700] |
| 64 | ≤ 729.700 | ≥ -1093.700 |
| 65 | ≤ 729.700 | - |
| 66 | ≤ 729.700 | - |
| 67 | [729.700, 731.300] | [-1093.300, -1091.700] |
| 68 | - | ≤ -1092.700 |
| 70 | [729.700, 731.300] | [-1093.300, -1091.700] |
| 74 | [729.700, 731.300] | [-1090.800, -1090.300] |
| 75 | [731.300, 731.700] | - |
| 76 | [731.700, 732.700] | [-1090.300, -1088.700] |
| 77 | ≤ 732.700 | - |
| 78 | ≤ 732.700 | - |
| 79 | [732.700, 734.300] | [-1090.300, -1088.700] |
| 80 | [732.700, 734.300] | - |
| 83 | - | ≥ -1088.700 |
| 84 | ≤ 732.700 | ≤ -1088.700 |
| 85 | ≤ 732.700 | - |
| 86 | ≤ 732.700 | - |
| 87 | [731.700, 732.700] | [-1090.300, -1088.700] |
| 88 | [731.700, 732.700] | [-1090.300, -1088.700] |
| 89 | [732.700, 734.300] | [-1090.300, -1088.700] |
| 94 | ≤ 732.575 | - |
| 95 | - | ≤ -1090.300 |
| 98 | [731.700, 733.300] | [-1095.300, -1093.700] |
| 99 | - | [-1095.300, -1093.700] |
| 100 | - | [-1095.300, -1093.700] |
| 101 | - | [-1095.300, -1093.700] |
| 102 | - | [-1095.300, -1093.700] |
| 111 | ≥ 731.300 | [-1091.700, -1091.300] |
| 112 | [729.700, 731.300] | ≤ -1091.300 |
| 113 | [729.700, 731.300] | [-1091.300, -1090.300] |
| 114 | [729.700, 731.300] | [-1091.300, -1090.300] |
| 116 | [729.700, 731.300] | [-1092.700, -1091.700] |
| 117 | [729.700, 731.300] | [-1092.700, -1091.700] |
| 119 | [729.700, 731.300] | [-1093.700, -1092.700] |
| 120 | ≥ 731.300 | - |
| 122 | [732.000, 732.575] | [-1093.000, -1092.000] |
| 127 | [732.700, 734.300] | [-1093.300, -1091.700] |
| 128 | - | ≥ -1093.700 |
| 129 | [733.300, 733.700] | [-1095.300, -1093.700] |
| 130 | [733.300, 733.700] | [-1095.300, -1093.700] |
| 131 | [732.300, 733.300] | [-1095.300, -1093.700] |
| 132 | [732.300, 733.300] | [-1095.300, -1093.700] |
| 133 | - | ≤ -1095.300 |
| 134 | ≤ 733.700 | ≤ -1095.425 |
| 135 | - | ≤ -1095.425 |
| 136 | [734.000, 735.000] | ≥ -1095.425 |
| 154 | [734.300, 734.700] | - |

There is no positional row at state155. The X87 upper wall occurs twice in the original scalar rows and is shown once in the interval table.

## What the route actually requires

| Region | Constraint evidence | Implication |
|---|---|---|
| Jump12 | Z83≥-1088.7, Z84≤-1088.7 | Crossing/return plane at the forward end of the route |
| Jump14 into15 | Z98–102≤-1093.7, then Z111≥-1091.7 | A separate return toward +Z is required, not a sustained -Z leg through15 |
| Jump15 into16 | Z112≤-1091.3, Z113–114≥-1091.3, Z116≤-1091.7 | Another reversal region; maximum Z must be approached then the route moves back |
| Jumps16–18 | Z114≥-1091.3, Z129≤-1093.7 | Net -Z progress of at least2.4 blocks |
| Jump20 | Z135≤-1095.425, Z136≥-1095.425 | Another negative-Z crossing/return plane |

The source does not determine exact velocity reversal times. “Turnaround region” here labels geometric constraints, not a proved zero-velocity tick. It is more precise to say endpoints14/15 sit among additional nearby reversals than to treat the whole segment as one turn at12.

## Candidate endpoints to test next

1. **Jump17, Z/MIN@128**, or **jump18, Z/MIN@130**: these lie on the net -Z movement after the15/16 peak and before the20 crossing. They are geometry-based candidates, not validated good endpoints. Jump18 also sits near an X return, so17 is the cleaner first comparison if avoiding both axes' coupling. The intervals do not prove monotonic velocity or guarantee numerical convergence.
2. **Jump20, Z/MAX@155** is a possible outgoing objective after the135/136 crossing, but has no final Z row. Original Z156 is still constrained to[-1095.3,-1093.7], so unconstrained progress at155 may be a poor continuation surrogate. Do not select it solely because it is farther ahead.
3. If extending beyond20, **jump21, Z/MAX@167** includes Z166 in[-1092.3,-1090.7]; jump22's Z175 band[-1089.3,-1087.7] supports continued +Z progress. Those future constraints make21 a better-motivated larger-window candidate than20, though it is longer (112 inputs from55) and remains untested.

Endpoint-bound detection remains separate:17 has a matching Z lower row at128,18 has one at130, and21 has no row at167 but a nearby bounded landing at166. Existing temporary cap omission does not remove nearby earlier walls. A directionally sensible endpoint can still expose numerical cancellation; do not label this a proven policy.

The observed end14/end15 failed retries remain measured numerical/recovery failures. Route geometry suggests alternate endpoints, but does not establish that the geometry caused those failures. No new solve/tests ran for this table.
