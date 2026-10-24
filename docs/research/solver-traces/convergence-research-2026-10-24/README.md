# Convergence research sources — 2026-10-24

Research-only artifacts supporting [the plan and expanded review](../../angle-solver-convergence-investigation-plan.md). No new solver execution.

- `pr387.json`, `pr389.json`, `pr390.json`: upstream `Leg0shii/ParkourCalculatorMod` PR metadata/descriptions fetched using `gh`. PR387 includes the opposing-pair margin-cap history; PR389 illustrates witness-specific recovery; PR390 records an ordinary implementation failure rather than mathematical infeasibility.
- `margin-cap-blame.txt`: provenance of the free-start opposing-pair margin cap.
- `numerical-step-blame.txt`: provenance of the dual numerical-step implementation.
- `saved-artifact-audit.txt`: read-only re-analysis of previously archived CSV rows. Confirms duplicate X87 upper bounds and distinguishes nominal margin, affine clearance and physical clearance of the accepted .05 candidate.

Older experiment/PR artifacts are linked individually in the plan. The audit did not create a new trajectory or substitute rounded seeds for the original runs.
