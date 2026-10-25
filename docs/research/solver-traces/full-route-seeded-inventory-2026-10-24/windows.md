# Every attempted window, current seeded full-route run

Human jump ranges are one-based and inclusive. `commit_end` is the exact state committed when a window succeeds. A miss commits nothing. Objective-by-objective data is in [objectives.csv](objectives.csv); every dual rung is in [cf-rungs.csv](cf-rungs.csv). Source line numbers refer to the corresponding raw trace.

## Commit3

| Jumps | Input states | Accepted method | Committed through state | Window ms |
|---|---|---|---:|---:|
| 1–7 | 0–68 | Z/MAX | 30 | 64.6 |
| 4–10 | 30–77 | Z/MAX | 64 | 11.2 |
| 7–13 | 64–90 | Z/MIN | 75 | 129.3 |
| 10–16 | 75–117 | miss | - | 499.0 |
| 10–14 | 75–99 | Z/MIN | 88 | 56.8 |
| 13–19 | 88–132 | Z/MIN | 113 | 431.0 |
| 16–22 | 113–176 | Z/MAX | 130 | 137.2 |
| 19–25 | 130–205 | miss | - | 3326.3 |
| 19–23 | 130–187 | miss | - | 2309.8 |
| 19–21 | 130–167 | Z/MAX | 167 | 3.9 |
| 22–28 | 167–222 | miss | - | 1892.8 |
| 22–26 | 167–207 | miss | - | 1728.9 |
| 22–24 | 167–196 | SLP | 196 | 147.6 |
| 25–31 | 196–259 | miss | - | 1265.2 |
| 25–29 | 196–241 | X/MAX | 209 | 199.5 |
| 28–34 | 209–292 | SLP | 250 | 2132.4 |
| 31–37 | 250–333 | miss | - | 242.7 |
| 31–35 | 250–304 | X/MAX | 280 | 101.2 |
| 34–40 | 280–381 | miss | - | 600.9 |
| 34–38 | 280–351 | Z/MIN | 319 | 4.2 |
| 37–43 | 319–438 | miss | - | 5337.5 |
| 37–41 | 319–406 | miss | - | 1362.6 |
| 37–39 | 319–365 | miss | - | 63.0 |
| 37–38 | 319–351 | Z/MIN | 351 | 0.8 |
| 39–45 | 351–449 | miss | - | 8147.3 |
| 39–43 | 351–438 | miss | - | 5329.7 |
| 39–41 | 351–406 | miss | - | 898.0 |
| 39–40 | 351–381 | miss | - | 200.6 |
| 39–39 | 351–365 | miss | - | 0.4 |
## Commit1

| Jumps | Input states | Accepted method | Committed through state | Window ms |
|---|---|---|---:|---:|
| 1–7 | 0–68 | Z/MAX | 12 | 0.8 |
| 2–8 | 12–71 | Z/MAX | 21 | 1.5 |
| 3–9 | 21–75 | Z/MAX | 30 | 1.4 |
| 4–10 | 30–77 | Z/MAX | 40 | 1.4 |
| 5–11 | 40–80 | Z/MAX | 55 | 1.5 |
| 6–12 | 55–88 | miss | - | 437.0 |
| 6–10 | 55–77 | Z/MAX | 64 | 0.5 |
| 7–13 | 64–90 | Z/MIN | 68 | 118.1 |
| 8–14 | 68–99 | Z/MIN | 71 | 129.6 |
| 9–15 | 71–113 | miss | - | 593.2 |
| 9–13 | 71–90 | SLP | 75 | 351.1 |
| 10–16 | 75–117 | miss | - | 501.0 |
| 10–14 | 75–99 | miss | - | 2.1 |
| 10–12 | 75–88 | Z/MAX | 77 | 37.8 |
| 11–17 | 77–128 | miss | - | 172.4 |
| 11–15 | 77–113 | miss | - | 174.5 |
| 11–13 | 77–90 | Z/MIN | 80 | 44.1 |
| 12–18 | 80–130 | miss | - | 247.1 |
| 12–16 | 80–117 | miss | - | 108.8 |
| 12–14 | 80–99 | miss | - | 1.0 |
| 12–13 | 80–90 | Z/MIN | 88 | 19.4 |
| 13–19 | 88–132 | miss | - | 545.4 |
| 13–17 | 88–128 | miss | - | 176.8 |
| 13–15 | 88–113 | miss | - | 48.6 |
| 13–14 | 88–99 | miss | - | 0.6 |
| 13–13 | 88–90 | Z/MAX | 90 | 0.2 |
| 14–20 | 90–155 | miss | - | 5992.4 |
| 14–18 | 90–130 | miss | - | 734.3 |
| 14–16 | 90–117 | miss | - | 2.3 |
| 14–15 | 90–113 | miss | - | 0.5 |
| 14–14 | 90–99 | miss | - | 0.2 |
