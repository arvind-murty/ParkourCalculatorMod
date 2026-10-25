# Per-start solve-type comparison

Same fixed seven-jump ladder, .0003 margin and temporary endpoint-bound removal; previous code lacks retention and conditional seeding. States change after the first differing accepted candidate, so later rows are policy comparisons, not identical-entry controls. `×` means every objective and SLP missed that window. A number before a method is the window size.

## Commit3

| Starting jump | Previous fixed run | Current seeded/retained run |
|---:|---|---|
| 1 | 7: Z/MAX | 7: Z/MAX |
| 4 | 7: Z/MAX | 7: Z/MAX |
| 7 | 7: SLP | 7: Z/MIN |
| 10 | 7×<br>5×<br>3: Z/MIN | 7×<br>5: Z/MIN |
| 13 | 7×<br>5×<br>3×<br>2×<br>1: Z/MAX | 7: Z/MIN |
| 14 | 7×<br>5×<br>3×<br>2×<br>1× | - |
| 16 | - | 7: Z/MAX |
| 19 | - | 7×<br>5×<br>3: Z/MAX |
| 22 | - | 7×<br>5×<br>3: SLP |
| 25 | - | 7×<br>5: X/MAX |
| 28 | - | 7: SLP |
| 31 | - | 7×<br>5: X/MAX |
| 34 | - | 7×<br>5: Z/MIN |
| 37 | - | 7×<br>5×<br>3×<br>2: Z/MIN |
| 39 | - | 7×<br>5×<br>3×<br>2×<br>1× |
## Commit1

| Starting jump | Previous fixed run | Current seeded/retained run |
|---:|---|---|
| 1 | 7: Z/MAX | 7: Z/MAX |
| 2 | 7: Z/MAX | 7: Z/MAX |
| 3 | 7: Z/MAX | 7: Z/MAX |
| 4 | 7: Z/MAX | 7: Z/MAX |
| 5 | 7: Z/MAX | 7: Z/MAX |
| 6 | 7×<br>5: X/MAX | 7×<br>5: Z/MAX |
| 7 | 7: Z/MIN | 7: Z/MIN |
| 8 | 7×<br>5: Z/MIN | 7: Z/MIN |
| 9 | 7×<br>5×<br>3: Z/MAX | 7×<br>5: SLP |
| 10 | 7: X/MAX | 7×<br>5×<br>3: Z/MAX |
| 11 | 7: Z/MAX | 7×<br>5×<br>3: Z/MIN |
| 12 | 7: Z/MAX | 7×<br>5×<br>3×<br>2: Z/MIN |
| 13 | 7: Z/MIN | 7×<br>5×<br>3×<br>2×<br>1: Z/MAX |
| 14 | 7×<br>5: Z/MAX | 7×<br>5×<br>3×<br>2×<br>1× |
| 15 | 7: SLP | - |
| 16 | 7×<br>5: Z/MAX | - |
| 17 | 7×<br>5×<br>3: Z/MIN | - |
| 18 | 7×<br>5: SLP | - |
| 19 | 7×<br>5×<br>3×<br>2: SLP | - |
| 20 | 7: SLP | - |
| 21 | 7×<br>5: SLP | - |
| 22 | 7: SLP | - |
| 23 | 7: SLP | - |
| 24 | 7: SLP | - |
| 25 | 7×<br>5×<br>3×<br>2×<br>1× | - |
