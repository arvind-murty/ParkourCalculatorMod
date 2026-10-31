# Standalone IPOPT workspace

Canonical branch: `feature/ipopt-consolidated`. Canonical worktree on this machine: `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-consolidated`.

This branch contains the continuous optimizer, automatic route/input/schedule orchestration, shared movement compilation, finite-scene collision checks, local executable-angle replay and TAS export. Use this branch for future standalone work. Earlier branches remain historical snapshots.

| Area | Entry point |
| --- | --- |
| Continuous optimizer and request schema | [ipopt/README.md](ipopt/README.md) |
| Automatic discovery and generic movement composition | [autonomy/GENERIC_COMPOSITION.md](autonomy/GENERIC_COMPOSITION.md) |
| Current pruning research task | [autonomy/PRUNING_RESEARCH_HANDOFF.md](autonomy/PRUNING_RESEARCH_HANDOFF.md) |
| Local replay and TAS export | [ipopt/REPLAY.md](ipopt/REPLAY.md) |
| Shared movement assumptions and limits | [ipopt/MOVEMENT_PORT.md](ipopt/MOVEMENT_PORT.md) |
| Consolidation provenance and checks | [CONSOLIDATION.md](CONSOLIDATION.md) |

The orchestration compiler and local replay now use the same checked-in core/Forge movement classes and extended `StaticScene`. The old historical-source materialization step is removed. Rebuild the helper after changing those classes.

Prepare both Java helpers without running mod tests or Minecraft:

```sh
./gradlew --configure-on-demand \
  -Dorg.gradle.java.home=/Library/Java/JavaVirtualMachines/jdk-21.jdk/Contents/Home \
  -I standalone/autonomy/compiler.gradle -I standalone/ipopt/export.gradle \
  :core:prepareRunTicksCompiler :core:compileStandaloneIpoptExport
```

See the optimizer README for creating `.venv-ipopt` and installing the pinned worker dependencies. Keep its executable path as `.venv-ipopt/bin/python`. Then run standalone validation:

```sh
PKC_IPOPT_PYTHON="$PWD/.venv-ipopt/bin/python" \
python3 -m unittest discover -s standalone/ipopt/tests -v

PKC_RUN_TICKS_COMPILER="$PWD/standalone/autonomy/build/compiler.json" \
PKC_IPOPT_PYTHON="$PWD/.venv-ipopt/bin/python" \
python3 -m unittest standalone.autonomy.tests.test_ladder_ceiling \
  standalone.autonomy.tests.test_generic_composition -v
```

The current priority is pruning/search efficiency; water is deferred. Standalone continuous acceptance and exact local replay acceptance remain separate results. Final actual-world validation belongs to the mod after importing the TAS.
