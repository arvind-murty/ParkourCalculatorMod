# Seeded seven versus ten window ladders

Only ladder changes; committed states can diverge. These are policy comparisons, not identical-entry controls. A number before a method is the window size; × means that window failed.

## Commit3

| Starting jump | Seven ladder | Ten ladder |
|---:|---|---|
| 1 | 7: Z/MAX | 10: Z/MAX |
| 4 | 7: Z/MAX | 10: Z/MIN |
| 7 | 7: Z/MIN | 10×<br>7: Z/MIN |
| 10 | 7×<br>5: Z/MIN | 10×<br>7×<br>5: Z/MIN |
| 13 | 7: Z/MIN | 10×<br>7: Z/MIN |
| 16 | 7: Z/MAX | 10×<br>7: Z/MAX |
| 19 | 7×<br>5×<br>3: Z/MAX | 10×<br>7×<br>5×<br>3: Z/MAX |
| 22 | 7×<br>5×<br>3: SLP | 10×<br>7: SLP |
| 25 | 7×<br>5: X/MAX | 10×<br>7×<br>5×<br>3×<br>2×<br>1: Z/MAX |
| 26 | - | 10×<br>7×<br>5×<br>3×<br>2×<br>1× |
| 28 | 7: SLP | - |
| 31 | 7×<br>5: X/MAX | - |
| 34 | 7×<br>5: Z/MIN | - |
| 37 | 7×<br>5×<br>3×<br>2: Z/MIN | - |
| 39 | 7×<br>5×<br>3×<br>2×<br>1× | - |
## Commit1

| Starting jump | Seven ladder | Ten ladder |
|---:|---|---|
| 1 | 7: Z/MAX | 10: Z/MAX |
| 2 | 7: Z/MAX | 10: Z/MAX |
| 3 | 7: Z/MAX | 10: X/MIN |
| 4 | 7: Z/MAX | 10: Z/MIN |
| 5 | 7: Z/MAX | 10: Z/MIN |
| 6 | 7×<br>5: Z/MAX | 10: SLP |
| 7 | 7: Z/MIN | 10: Z/MIN |
| 8 | 7: Z/MIN | 10: X/MAX |
| 9 | 7×<br>5: SLP | 10: X/MIN |
| 10 | 7×<br>5×<br>3: Z/MAX | 10: Z/MIN |
| 11 | 7×<br>5×<br>3: Z/MIN | 10: SLP |
| 12 | 7×<br>5×<br>3×<br>2: Z/MIN | 10: SLP |
| 13 | 7×<br>5×<br>3×<br>2×<br>1: Z/MAX | 10: SLP |
| 14 | 7×<br>5×<br>3×<br>2×<br>1× | 10×<br>7: Z/MAX |
| 15 | - | 10×<br>7: Z/MAX |
| 16 | - | 10×<br>7: Z/MAX |
| 17 | - | 10×<br>7×<br>5×<br>3: Z/MIN |
| 18 | - | 10×<br>7×<br>5: SLP |
| 19 | - | 10×<br>7×<br>5×<br>3: Z/MAX |
| 20 | - | 10×<br>7×<br>5×<br>3: Z/MIN |
| 21 | - | 10×<br>7×<br>5×<br>3: Z/MIN |
| 22 | - | 10×<br>7×<br>5×<br>3×<br>2: Z/MIN |
| 23 | - | 10×<br>7×<br>5×<br>3×<br>2: SLP |
| 24 | - | 10: SLP |
| 25 | - | 10×<br>7×<br>5×<br>3×<br>2×<br>1× |
