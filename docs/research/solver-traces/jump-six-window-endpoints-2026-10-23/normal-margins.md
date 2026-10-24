# Normal accepted6–12 Z/MIN: constraint clearance

Same exact sequential normal reproduction as the endpoint probe; accepted margin0.0003, X63/X81 pattern,71iterations. Positive margin is distance inside the original bound (before inward margin). All53 authored window rows pass the full replay as well as the continuous path; smallest full replay margin0.000170328230 at Z83.

| Tick | Constraint | Normal continuous value | Continuous clearance | Normal replay value | Replay clearance |
|---:|---|---:|---:|---:|---:|
| 58 | X ≤ 729.699999988 | 729.699699988 | 0.000300000 | 729.699704721 | 0.000295267 |
| 66 | X ≤ 729.699999988 | 729.679327376 | 0.020672612 | 729.679321193 | 0.020678795 |
| 68 | Z ≤ -1092.699999988 | -1092.700299988 | 0.000300000 | -1092.700287928 | 0.000287940 |
| 74 | X ≤ 731.300000012 | 731.299700012 | 0.000300000 | 731.299670315 | 0.000329697 |
| 79 | X ≥ 732.699999988 | 732.700299988 | 0.000300000 | 732.700237247 | 0.000237259 |
| 83 | Z ≥ -1088.699999988 | -1088.699699987 | 0.000300001 | -1088.699829660 | 0.000170328 |
| 84 | X ≤ 732.699999988 | 732.699699988 | 0.000300000 | 732.699592398 | 0.000407590 |
| 85 | X ≤ 732.699999988 | 732.687101220 | 0.012898768 | 732.686976450 | 0.013023539 |
| 86 | X ≤ 732.699999988 | 732.675636342 | 0.024363646 | 732.675495936 | 0.024504052 |
| 87 | Z ≥ -1090.300000012 | -1089.886367310 | 0.413632702 | -1089.886494386 | 0.413505625 |

This includes the nine rows violated by fixed plus the other tight normal row Z68. Six continuous rows sit at the inward margin0.0003: X58 upper,Z68 upper,X74 upper,X79 lower,Z83 lower,X84 upper. This describes the optimized candidate and does not establish intrinsic problem difficulty or infeasibility from the fixed entry state. No inference that replay rounding caused the fixed miss is warranted: its candidate already violates the continuous rows.

[Every normal constraint and margin](normal-constraint-margins.csv), [focused log](normal-constraint-audit-focused.log). Production unchanged; only the focused normal probe ran.
