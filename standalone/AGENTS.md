# Standalone development scope

The user's current work is entirely in `standalone/`. Success means a valid IPOPT solution with the correct inputs, movement, inertia, geometry, contacts and constraints, checked by the standalone scalar model and shared scene acceptance.

Use focused standalone tests for the code being changed. Run the wider standalone suites when the affected scope justifies them. Do not run the mod's Gradle Java core test suites on each iteration. The consolidated branch includes local replay and TAS export; final actual-world validation remains a separate mod step.

The existing Java coefficient compiler and scene-check bridge may still be built and used as standalone physics helpers. Building those helpers is separate from running the mod's Java test gate.

Use `feature/ipopt-consolidated` for current standalone work, in `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-consolidated` or a new isolated worktree branched from it. Earlier orchestration/integration branches are historical snapshots. Commit each completed feature. Preserve the full original X/Z position and velocity as the numerical starting state; never freeze an optimizer's intermediate endpoint during composition.
