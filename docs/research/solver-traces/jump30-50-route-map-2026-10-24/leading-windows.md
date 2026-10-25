# Accepted windows leading into39, current staged baseline

Every accepted row uses cap1000 and commits one nominal jump. All larger windows listed as failed below were tried first from the same row entry. Their objective calls and inertia attempts are in the original run. Actual inputs are [start,end); objective is evaluated at end.

| Starting jump |Failed nominal window sizes first |Accepted nominal window |Actual input horizon |Accepted method/objective |Committed exit state |
|---:|---|---|---|---|---:|
|30 |10 |30–36 |241–320 |SLP |250 |
|31 |10 |31–37 |250–338 |Z/MAX |259 |
|32 |10 |32–38 |259–352 |Z/MIN |271 |
|33 |10, 7 |33–37 |271–338 |Z/MAX |280 |
|34 |10, 7 |34–38 |280–352 |Z/MIN |292 |
|35 |10, 7, 5 |35–37 |292–338 |Z/MAX |304 |
|36 |10, 7, 5, 3 |36–37 |304–338 |Z/MAX |319 |
|37 |10, 7, 5, 3 |37–38 |319–352 |Z/MIN |333 |
|38 |10, 7, 5, 3, 2 |38–38 |333–352 |Z/MIN |351 |

At39, both cap1000 and10000 try window sizes10,7,5,3,2,1; all fail. Actual endpoints493,450,441,412,388,373. Proposed commit exit365, no inputs committed.
