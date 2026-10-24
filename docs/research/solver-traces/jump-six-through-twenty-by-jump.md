# Constraints grouped by solver jump

Internal state ticks; UI rows are one higher. Jump j covers input ticks `[bounds[j-1], bounds[j])`. State constraints exactly at the end boundary are assigned to the following jump for this table; a solve ending at that boundary includes those positional rows too, so neighboring window specs can share them. Ground/air boundaries come from `LongRunSolver.jumpBoundaries`. Values rounded to three decimals.

## Jump6: inputs55–63, end state64

| State tick | Constraints |
|---:|---|
| 55 | Z ≤ -1095.3 |
| 56 | Z ≤ -1095.3 |
| 57 | Z ≤ -1095.3 |
| 58 | X ≤ 729.7 |
| 63 | X ∈ [728.7, 729.7]; Z ∈ [-1095.3, -1093.7] |

## Jump7: inputs64–67, end state68

| State tick | Constraints |
|---:|---|
| 64 | X ≤ 729.7; Z ≥ -1093.7 |
| 65 | X ≤ 729.7 |
| 66 | X ≤ 729.7 |
| 67 | X ∈ [729.7, 731.3]; Z ∈ [-1093.3, -1091.7] |

## Jump8: inputs68–70, end state71

| State tick | Constraints |
|---:|---|
| 68 | Z ≤ -1092.7 |
| 70 | X ∈ [729.7, 731.3]; Z ∈ [-1093.3, -1091.7] |

## Jump9: inputs71–74, end state75

| State tick | Constraints |
|---:|---|
| 74 | X ∈ [729.7, 731.3]; Z ∈ [-1090.8, -1090.3] |

## Jump10: inputs75–76, end state77

| State tick | Constraints |
|---:|---|
| 75 | X ∈ [731.3, 731.7] |
| 76 | X ∈ [731.7, 732.7]; Z ∈ [-1090.3, -1088.7] |

## Jump11: inputs77–79, end state80

| State tick | Constraints |
|---:|---|
| 77 | X ≤ 732.7 |
| 78 | X ≤ 732.7 |
| 79 | X ∈ [732.7, 734.3]; Z ∈ [-1090.3, -1088.7] |

## Jump12: inputs80–87, end state88

| State tick | Constraints |
|---:|---|
| 80 | X ∈ [732.7, 734.3] |
| 83 | Z ≥ -1088.7 |
| 84 | X ≤ 732.7; Z ≤ -1088.7 |
| 85 | X ≤ 732.7 |
| 86 | X ≤ 732.7 |
| 87 | X ∈ [731.7, 732.7]; Z ∈ [-1090.3, -1088.7] |

## Jump13: inputs88–89, end state90

| State tick | Constraints |
|---:|---|
| 88 | X ∈ [731.7, 732.7]; Z ∈ [-1090.3, -1088.7] |
| 89 | X ∈ [732.7, 734.3]; Z ∈ [-1090.3, -1088.7] |

## Jump14: inputs90–98, end state99

| State tick | Constraints |
|---:|---|
| 94 | X ≤ 732.575 |
| 95 | Z ≤ -1090.3 |
| 98 | X ∈ [731.7, 733.3]; Z ∈ [-1095.3, -1093.7] |

## Jump15: inputs99–112, end state113

| State tick | Constraints |
|---:|---|
| 99 | Z ∈ [-1095.3, -1093.7] |
| 100 | Z ∈ [-1095.3, -1093.7] |
| 101 | Z ∈ [-1095.3, -1093.7] |
| 102 | Z ∈ [-1095.3, -1093.7] |
| 111 | X ≥ 731.3; Z ∈ [-1091.7, -1091.3] |
| 112 | X ∈ [729.7, 731.3]; Z ≤ -1091.3 |

## Jump16: inputs113–116, end state117

| State tick | Constraints |
|---:|---|
| 113 | X ∈ [729.7, 731.3]; Z ∈ [-1091.3, -1090.3] |
| 114 | X ∈ [729.7, 731.3]; Z ∈ [-1091.3, -1090.3] |
| 116 | X ∈ [729.7, 731.3]; Z ∈ [-1092.7, -1091.7] |

## Jump17: inputs117–127, end state128

| State tick | Constraints |
|---:|---|
| 117 | X ∈ [729.7, 731.3]; Z ∈ [-1092.7, -1091.7] |
| 119 | X ∈ [729.7, 731.3]; Z ∈ [-1093.7, -1092.7] |
| 120 | X ≥ 731.3 |
| 122 | X ∈ [732, 732.575]; Z ∈ [-1093, -1092] |
| 127 | X ∈ [732.7, 734.3]; Z ∈ [-1093.3, -1091.7] |

## Jump18: inputs128–129, end state130

| State tick | Constraints |
|---:|---|
| 128 | Z ≥ -1093.7 |
| 129 | X ∈ [733.3, 733.7]; Z ∈ [-1095.3, -1093.7] |

## Jump19: inputs130–131, end state132

| State tick | Constraints |
|---:|---|
| 130 | X ∈ [733.3, 733.7]; Z ∈ [-1095.3, -1093.7] |
| 131 | X ∈ [732.3, 733.3]; Z ∈ [-1095.3, -1093.7] |

## Jump20: inputs132–154, end state155

| State tick | Constraints |
|---:|---|
| 132 | X ∈ [732.3, 733.3]; Z ∈ [-1095.3, -1093.7] |
| 133 | Z ≤ -1095.3 |
| 134 | X ≤ 733.7; Z ≤ -1095.425 |
| 135 | Z ≤ -1095.425 |
| 136 | X ∈ [734, 735]; Z ≥ -1095.425 |
| 154 | X ∈ [734.3, 734.7] |

State155 has no positional row. Original duplicate X87 upper rows are shown once. See [full-precision scalar rows](jump-six-through-twenty-constraints.csv) and [the full route table](jump-six-through-twenty-constraints.md). No solver run.
