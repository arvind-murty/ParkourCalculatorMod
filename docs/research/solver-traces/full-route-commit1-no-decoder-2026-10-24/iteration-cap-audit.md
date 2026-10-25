# Iteration caps and intermediate candidates

Audit of the existing saved trace, no rerun and no production changes. [Retention records](retained-iteration-audit.csv), [timing accounting](timing-breakdown.md).

Jump39 accounts for22.120s,6.96% of317.605s. All preceding starts account for295.460s in window calls. The run does not establish that39 is physically unsolvable.

## Where a smaller cap could save work

438 runs hit10000 iterations.433 of these never logged a valid incumbent and consume248.404s. The other5 hit-cap runs did retain valid candidates and together cost.895s. All26 runs with valid retained candidates, regardless of stop type, total only1.063s of291.581s in measured closed-form rung time. Returning earlier only AFTER a valid incumbent is found would therefore have a small direct timing benefit on this particular recorded route; most time is spent before finding any valid candidate in failed runs. That does not quantify downstream changes in route choice.

## Recorded first-valid iterations

The table truncates each independently recorded dual trajectory without changing its initial multipliers. Counts are successful closed-form runs with a logged valid candidate by the cap. It does not simulate the shorter-cap pipeline: earlier termination can change inferred inertia patterns, warm starts, objective selection, accepted angles and all later committed entry states. It is not a forecast of how many route jumps a new run would solve.

| Cap |Recorded successful CF runs with a valid retained candidate by cap |
|---:|---:|
|100 |18/26 |
|500 |23/26 |
|1000 |23/26 |
|2000 |24/26 |
|5000 |25/26 |
|10000 |26/26 |

Late first-feasible cases: start9 at3047, start11 at7194, start36 at1745. A1000 cap truncates those successful trajectories before their first accepted path. Other strategies might still solve after such truncation; that requires a rerun.

## Quality of early feasible candidates

| Starting jump |First valid |Best retained |Later score improvement |Terminal iteration |
|---:|---:|---:|---:|---:|
|9 |3047 |3054 |0.000310150 |3074 |
|10 |348 |5706 |0.000011128 |10000 |
|11 |7194 |7199 |0.000090360 |10000 |
|12 |170 |1718 |0.000070888 |10000 |
|14 |50 |2055 |0.028918970 |2270 |
|15 |257 |273 |0.030852080 |10000 |
|17 |183 |185 |0.051779260 |361 |
|19 |21 |171 |0.046466800 |504 |
|20 |75 |87 |0.036787660 |1288 |
|36 |1745 |1807 |0.137547440 |10000 |
|38 |1 |5 |0.207101280 |7 |

Differences are from logged scores rounded to12 significant digits. These provisional coordinate objectives have no smoothing penalty, so differences are endpoint coordinate improvements in blocks. Objective quality at the end of a window is not a bound on committed-prefix position or final-route success. No optimality-gap guarantee follows from a first valid candidate.

Current retention already selects the best VALID logged intermediate candidate if the terminal iterate is invalid or worse. It does not stop the dual immediately when it saves that candidate. An arbitrary intermediate iteration may violate constraints; a lower cap would return the retained feasible candidate if there is one, otherwise ordinary failure handling continues.

For this trace, sensible separate experiments would be: cap failed runs sooner while keeping a larger-budget retry; or stop after a feasible incumbent has not improved enough over a chosen span of iterations. These have different goals. The former targets the measured248.404s of unsuccessful hit-cap work, but can discard late feasibility. The latter trades objective quality for much smaller direct savings here. No thresholds or stopping changes were implemented by this audit.
