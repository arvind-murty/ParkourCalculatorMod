# Ten-jump ladder: cap removal versus free-air endpoint

Only endpoint/constraint policy changes; committed states can diverge. These are policy comparisons, not identical-entry controls. A number before a method is the window size; × means that window failed.

## Commit3

| Starting jump | Previous cap removal | Free-air endpoint |
|---:|---|---|
| 1 | 10: Z/MAX | 10: Z/MIN |
| 4 | 10: Z/MIN | 10: Z/MIN |
| 7 | 10×<br>7: Z/MIN | 10×<br>7: Z/MIN |
| 10 | 10×<br>7×<br>5: Z/MIN | 10×<br>7×<br>5: Z/MAX |
| 13 | 10×<br>7: Z/MIN | 10×<br>7: Z/MAX |
| 16 | 10×<br>7: Z/MAX | 10×<br>7: Z/MAX |
| 19 | 10×<br>7×<br>5×<br>3: Z/MAX | 10×<br>7×<br>5×<br>3: Z/MAX |
| 22 | 10×<br>7: SLP | 10×<br>7×<br>5×<br>3: SLP |
| 25 | 10×<br>7×<br>5×<br>3×<br>2×<br>1: Z/MAX | 10×<br>7×<br>5×<br>3×<br>2×<br>1× |
| 26 | 10×<br>7×<br>5×<br>3×<br>2×<br>1× | - |
## Commit1

| Starting jump | Previous cap removal | Free-air endpoint |
|---:|---|---|
| 1 | 10: Z/MAX | 10: Z/MIN |
| 2 | 10: Z/MAX | 10: Z/MIN |
| 3 | 10: X/MIN | 10: Z/MIN |
| 4 | 10: Z/MIN | 10: Z/MIN |
| 5 | 10: Z/MIN | 10: Z/MAX |
| 6 | 10: SLP | 10×<br>7: Z/MIN |
| 7 | 10: Z/MIN | 10×<br>7: Z/MIN |
| 8 | 10: X/MAX | 10×<br>7×<br>5: Z/MIN |
| 9 | 10: X/MIN | 10: Z/MAX |
| 10 | 10: Z/MIN | 10: Z/MAX |
| 11 | 10: SLP | 10×<br>7×<br>5×<br>3×<br>2×<br>1× |
| 12 | 10: SLP | - |
| 13 | 10: SLP | - |
| 14 | 10×<br>7: Z/MAX | - |
| 15 | 10×<br>7: Z/MAX | - |
| 16 | 10×<br>7: Z/MAX | - |
| 17 | 10×<br>7×<br>5×<br>3: Z/MIN | - |
| 18 | 10×<br>7×<br>5: SLP | - |
| 19 | 10×<br>7×<br>5×<br>3: Z/MAX | - |
| 20 | 10×<br>7×<br>5×<br>3: Z/MIN | - |
| 21 | 10×<br>7×<br>5×<br>3: Z/MIN | - |
| 22 | 10×<br>7×<br>5×<br>3×<br>2: Z/MIN | - |
| 23 | 10×<br>7×<br>5×<br>3×<br>2: SLP | - |
| 24 | 10: SLP | - |
| 25 | 10×<br>7×<br>5×<br>3×<br>2×<br>1× | - |
