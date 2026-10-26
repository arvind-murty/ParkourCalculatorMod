# Replay and movement handoff, 2026-10-26

Implementation: `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-mod-integration`, branch `feature/ipopt-mod-integration`. Relevant commits: `133b48c` (local replay), `af12801` (correct movement compilation), `264f6b9` (limitations/source map). The latest commits are local, so do not assume the remote has them. Inspect/import changes into your own worktree; do not edit this integration checkout. No pushes without an explicit user request.

**Important fix:** required wall hits cancel sprint. Legacy air acceleration carries the previous tick's sprint factor, so its decrease/recovery lags one tick. Old real11 benchmark arrays assume sprint through these hits. Recompile them before game replay; their old feasibility/objective claims do not describe the corrected physics.

Your pipeline should be: **choose rows/timing/contact schedule → compile movement → Python solve → exact local angle repair/replay → accepted TAS**. Recompile after changing keys, run ticks, ground/contact timing, effects or resume state. Reindex all tick-based data when inserting rows. Prefer `KEEP` when you have already chosen the exact keys; FORCE_45 deliberately edits them.

To correct an existing compiled request:

```sh
./gradlew -I standalone/ipopt/export.gradle :core:compileStandaloneIpoptPhysics \
  -PipoptCapture=/path/to/template.json \
  -PipoptInput=/path/to/problem.json \
  -PipoptOutput=/tmp/corrected-problem.json
```

Then use the existing Python `solve()`/CLI with that corrected problem. The 1.8.9 exporter now performs movement compilation automatically. A local Java-home override may be needed, as for other Gradle tasks.

- `FixedMovementCompiler` and `LocalReplayOracle` are in `forge-core/.../replay/`. They share `ReplayInputState`, the mod's existing `PlayerSprintMachine`, and the exact movement kernel. Avoid cloning these mechanics in the planner.
- The compiled `movement` object records collision/ground/input assumptions. Scalar validation checks collision timing and checks ground when vertical replay is supplied. A changed event schedule needs recompilation/re-solving; it does not prove infeasibility.
- Exact replay needs no running Minecraft. Declared-contact mode supports source.startTick = 0. Scene mode discovers collisions from supplied complete inert collision boxes, ladder cells and friction cells; see [REPLAY.md](REPLAY.md) for the API, command and scene schema. Real11 has no complete scene snapshot, so its current tests use declared contacts.
- Corrected real11 **200 ticks**: solve ~0.94 s, repair/export ~0.25 s. **400 ticks**: solve ~2.42 s, repair/export ~3.80 s. Both pass all original scalar rows and fresh JSON-round-trip replay. The previous state-190 failure is resolved. Preserve the explicit objective give-back, currently 1e-4.
- This is the 1.8.9 static-block/ladder profile. Fluids, webs, soulsand/slime callbacks, damage, interactions and other versions still need ports. Bounds must cover all scene queries. Continuous feasibility is separate from replay acceptance, and `unknown` is not infeasibility.

Detailed limits and source pointers: [MOVEMENT_PORT.md](MOVEMENT_PORT.md). Reproduction/results: [FINDINGS.md](FINDINGS.md). Ownership stays unchanged: you own input/run-tick/route search; this integration agent owns movement compilation, replay, angle realization and TAS export.
