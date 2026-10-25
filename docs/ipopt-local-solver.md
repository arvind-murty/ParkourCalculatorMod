# Local IPOPT solver

Choose **IPOPT (local)** in the Angle Solver's Solver row. Pipeline remains the default. This backend solves the horizontal continuous model with sparse state and facing variables, using CasADi derivatives and IPOPT. It does not certify game arithmetic or global optimality.

## Local setup

The pinned runtime was tested with Python 3.12. From the repository root:

```sh
python3.12 -m venv .venv-ipopt
.venv-ipopt/bin/python -m pip install -r core/src/main/resources/de/legoshi/parkourcalc/core/anglesolver/ipopt/requirements.txt
export PKC_IPOPT_PYTHON="$PWD/.venv-ipopt/bin/python"
```

Start the Minecraft client or its launcher from an environment containing `PKC_IPOPT_PYTHON`. Alternatively, set the Minecraft JVM argument `-Dpkc.ipopt.python=/absolute/path/to/python`. The JVM property takes precedence over the environment variable; otherwise the backend tries `python3` on PATH. Configure an executable path, without shell arguments. Nothing downloads or installs automatically.

On Apple Silicon, an Intel Java 8 runtime under Rosetta may launch a universal Python binary as x86_64 even when its packages are ARM. Set `PKC_IPOPT_ARCH=arm64` or the Minecraft JVM argument `-Dpkc.ipopt.arch=arm64` to launch the worker explicitly as ARM. `x86_64` is also supported for an Intel Python environment. Leave this option unset on other operating systems. The worker architecture is independent of the JVM because they communicate over JSON.

The Java module packages the Python worker as a resource and extracts a temporary copy for each solve. The Python runtime and native CasADi/IPOPT libraries stay local. Every worker runs in its own process with one BLAS/OpenMP thread. Java enforces the total time limit, including Python imports and model construction, and terminates the worker on cancellation or timeout.

## Solving

Select the tick range, input policy, sprint policy, constraints and objective as usual. Set a budget such as `5s` or `10s`. A budget of zero gives IPOPT a 10-second limit and returns the first independently validated feasible solution. Budgets below one second retain the usual Fast effort; larger budgets optimize the objective. Stop keeps the best validated candidate; cancellation discards the in-flight result.

**Inertia zeroing** applies per-axis automatic thresholding at every tick. With it off, every component carries forward normally, including small initial velocities. With it on, the threshold comes from the selected game model, for example .005 on 1.8.9. The worker first solves with unrestricted carry, then constrains inferred zero events, and finally freezes a complete zero/survival pattern. It can refresh that pattern within the remaining time. Java independently replays the actual automatic threshold before accepting any candidate. A fixed pattern failing does not prove the full problem infeasible.

**Recorded wall hits** prescribes the collision schedule from the current simulated trajectory. A hit must reach its recorded player-center plane from the allowed side at its recorded tick. The model pins that coordinate to the plane and zeroes the stored normal velocity. Tangential motion continues normally. The NLP uses a 1e-7 reach guard; Java rejects a missed contact even if its shortfall is below the positional tolerance. Wall direction is inferred from movement into that plane, including history before a selected range. An unresolvable collision produces a configuration error. These are scheduled contacts, not a general collision detector or ladder speed-cap model. A path's unrecorded collisions are discovered by simulation after Apply.

The initial position and velocity are fixed to the selected start state. Start-position constraints remain enforced. Existing input and sprint policies determine acceleration coefficients. Position constraints, relative positions, displacements, cross-axis displacement constraints, facing pins and relative-facing rows are exported. Facing equalities use the normal 1e-4 equality corridor around the typed target, rather than a sine-table cell. Facing comparisons use a local angular chart around the seed; final Java validation uses the wrapped constraint semantics. Relative position references outside the selected range, displacement constraints at the initial state, and facing constraints on the final state produce explicit configuration errors.

The first local version requires zero smooth lambda and Legal record mode off. Combined horizontal inertia used by newer game versions is unsupported when inertia zeroing is enabled. These choices produce explicit configuration errors rather than falling back to a different model.

## Results and validation

Java reconstructs the trajectory from the returned facings using `StrictMath`, continuous acceleration and friction, automatic inertia, and conditional prescribed wall clipping. It checks every compiled constraint at tolerance 1e-7 and rejects non-finite or malformed candidates. IPOPT's success status alone cannot produce an accepted result. A worker timeout can still return a previously validated incumbent. A failed local search is reported as **No feasible solution found**, not as a proof of infeasibility.

Accepted results are labelled **Continuous solution**. Apply writes the solved facings and any Force 45 keys through the existing input and simulation callback. The game simulator then checks the realized path against the planned path; float facing accumulation, sine-table arithmetic, actual collisions and surface behavior can cause a difference. Use the resulting simulation to judge game validity. The existing Pipeline backend retains its original byte-exact acceptance rules.

## Reproduce integration tests

Pure Java validation, settings and process-lifecycle tests need no CasADi installation:

```sh
./gradlew :core:test --tests '*Ipopt*'
```

The slow integration class runs when `PKC_IPOPT_PYTHON` is set. Its analytic problem has a known optimum and includes facing and relative-position rows. The same class runs the archived real11 400-tick engine and Apply test from its separately copied compressed fixture. `PKC_IPOPT_CAPTURE` can override that fixture with another gzipped copy:

```sh
./gradlew :core:test -PslowTests --tests '*LocalIpoptIntegrationTest'
```

Optional `PKC_IPOPT_ARTIFACT_DIR` exports the exact real11 request from that test. Integration tests skip when the explicit runtime configuration is absent. No tests install native packages or modify another agent's experiment environment.
