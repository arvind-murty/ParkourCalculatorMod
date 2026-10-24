# Objectives used by LongRun SLP and fallbacks

Scope: the LongRun path currently used for Linkcraft real/11. This is source inspection, not a new solve. The intermediate window's surrogate objective and the actual LP objective are distinct in centered SLP.

## Intermediate windows

| Call or fallback | Objective actually optimized |
|---|---|
| Primary CF | Maximize Z at that attempt's selected endpoint. |
| Alternate CF | Minimize Z, maximize X, minimize X, in that order; each now independently chooses its endpoint from the nominal window. |
| Centered SLP (`optimizeCentered`) | Minimize the largest constraint violation / deepen the weakest clearance. No position-objective phase2. Runs on the nominal window. |
| Centered SLP inertia-aware retry | Same feasibility/clearance objective, with inertia-aware local derivatives; it does not switch to another coordinate objective. |
| Smaller-window fallback | Restart the full sequence on the next nominal window size: Z/MAX, Z/MIN, X/MAX, X/MIN, centered SLP. |
| Commit1 restart after commit3 fails | Same objective sequence and policies, rerun from the original route start with a shorter committed prefix. |

Centered SLP still builds its initial dual seed from `spec.objective`, which LongRun passes as nominal-window Z/MAX. During SLP phase1, however, `objRow` is null and `TrustRegionLp` minimizes its shared worst-violation variable. The logged position objective is a diagnostic; it is not the criterion for accepting a phase1 improvement. Ordinary and inertia-aware retries use the same targetClearance/hugObjective settings.

`centerClearance` defaults to0.02 blocks, independently of LongRun's fixed CF margin0.0003. It is a target for stopping the centering phase, not a verified minimum clearance guaranteed for every accepted candidate. Existing acceptance may retain a lower-clearance feasible result (including the shared prefix/full-continuous acceptance path). `CLEARANCE` is1e-6. The centered method skips phase2 entirely because `hugObjective=false`.

## Terminal window

| Call or fallback | Objective actually optimized |
|---|---|
| Terminal RecoveryLadder CF | The actual requested route objective, X/MAX at state808 for this input. |
| Terminal ordinary SLP / inertia-aware retry | Phase1 restores feasibility; phase2 improves the actual requested objective while preserving feasibility. |
| Relaxation recovery | The same requested objective in its relaxed seed construction; its SLP repair has feasibility phase1 then objective phase2. |
| Alternate CF recovery seeds | Other axis/sense objectives generate seeds; subsequent SLP runs on the original requested objective again. |
| Level-set top-up | Improve the original requested objective through repeated feasibility solves. |
| `hugBestObjective` / `hugObjective` | Generate seeds using other objectives, then SLP and level-set ascent improve the original requested objective; keep the best feasible result by that original objective. |

The actual goal endpoint is not forward-adjusted. Terminal behavior is different from intermediate centered SLP: terminal SLP can genuinely maximize X or minimize/maximize whichever final objective was requested.

## Default iteration cap update

The user requested a cap of10000. `CostateDualSolver.MAX_ITER` and `ClosedFormSolve.Config.dualMaxIterations` now default to10000, including direct dual seeds used by SLP/recovery. Early stopping rules remain active. SLP's separate outer budgets (phase1Calls40, totalCalls60, inertia-aware retry) and inner LP cap2000 were not changed. No tests or solve probes ran for this edit; no new runtime/route outcome is claimed.

## Sources

- `LongRunSolver.solveWindow`, `alternates`, `hugBestObjective`, `hugObjective`.
- `SlpSolve.optimizeCentered`, `optimize`, phase1/phase2 loop, objective-row construction, acceptance test, ordinary-to-inertia-aware retry.
- `TrustRegionLp.run`: phase1 costs only the shared violation variable; phase2 switches to the supplied objective coefficients.
- `RecoveryLadder.solve`, `alternateObjectives`, `levelSetTopUp`.
- `RelaxationRecovery.seedAtMargin`, relaxed objective construction and `SlpSolve.optimizeBestEffort` calls.
