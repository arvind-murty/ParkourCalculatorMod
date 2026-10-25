# Commit1: decoder fallback enabled versus disabled

Only decoder fallback changes; committed states can diverge. These are policy comparisons, not identical-entry controls. A number before a method is the window size; × means that window failed.

## Commit1

| Starting jump | Fallback enabled | Fallback disabled |
|---:|---|---|
| 1 | 10: Z/MIN | 10: Z/MIN |
| 2 | 10: Z/MIN | 10: Z/MIN |
| 3 | 10: Z/MIN | 10: Z/MIN |
| 4 | 10: Z/MIN | 10: Z/MIN |
| 5 | 10: Z/MAX | 10: Z/MIN |
| 6 | 10×<br>7: Z/MIN | 10×<br>7: Z/MIN |
| 7 | 10×<br>7: Z/MIN | 10×<br>7: Z/MIN |
| 8 | 10×<br>7×<br>5: Z/MIN | 10×<br>7: Z/MAX |
| 9 | 10: Z/MAX | 10×<br>7×<br>5: Z/MAX |
| 10 | 10: Z/MAX | 10: X/MAX |
| 11 | 10×<br>7×<br>5×<br>3×<br>2×<br>1× | 10×<br>7: Z/MAX |
| 12 | - | 10×<br>7: X/MAX |
| 13 | - | 10: SLP |
| 14 | - | 10×<br>7: Z/MIN |
| 15 | - | 10×<br>7: Z/MAX |
| 16 | - | 10×<br>7: SLP |
| 17 | - | 10×<br>7×<br>5: Z/MAX |
| 18 | - | 10×<br>7×<br>5: SLP |
| 19 | - | 10×<br>7×<br>5×<br>3: Z/MAX |
| 20 | - | 10×<br>7×<br>5×<br>3×<br>2: Z/MAX |
| 21 | - | 10×<br>7×<br>5×<br>3×<br>2: Z/MAX |
| 22 | - | 10×<br>7: SLP |
| 23 | - | 10×<br>7×<br>5: SLP |
| 24 | - | 10: SLP |
| 25 | - | 10: SLP |
| 26 | - | 10: SLP |
| 27 | - | 10: SLP |
| 28 | - | 10×<br>7: SLP |
| 29 | - | 10×<br>7: SLP |
| 30 | - | 10×<br>7: SLP |
| 31 | - | 10×<br>7: Z/MAX |
| 32 | - | 10×<br>7: Z/MIN |
| 33 | - | 10×<br>7×<br>5: Z/MAX |
| 34 | - | 10×<br>7×<br>5×<br>3: Z/MAX |
| 35 | - | 10×<br>7×<br>5×<br>3: Z/MAX |
| 36 | - | 10×<br>7×<br>5×<br>3: Z/MAX |
| 37 | - | 10×<br>7×<br>5×<br>3×<br>2: Z/MIN |
| 38 | - | 10×<br>7×<br>5×<br>3×<br>2×<br>1: Z/MIN |
| 39 | - | 10×<br>7×<br>5×<br>3×<br>2×<br>1× |
