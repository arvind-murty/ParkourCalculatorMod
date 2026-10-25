# Local IPOPT production integration, 2026-10-25

## Scope and isolation

The user selected an optional IPOPT solver alongside the existing pipeline and deferred native distribution. Work lives in `/Users/arvindmurty/CS/ParkourCalculatorMod-ipopt-local`, branch `feature/ipopt-local`. The starting commit is `9bc4d24479056edb010053ea32bee311b064b4a9`. This clone has no dev ref, so the separate worktree starts from the current available HEAD. The shared original checkout, its other agent's handoff, solver experiment tools and existing modified capture are not changed by the production implementation.

The earlier experiments and full-route follow-up are recorded in the original checkout's `docs/research/real11-generic-solvers-2026-10-25.md`. The 808-tick result remains a rejected small authored/model mismatch; no tolerances were widened to accept it. Production targets ordinary selected spans, not unbroken 600-plus-tick routes or automatic splicing.

## Implemented boundary

The Angle Solver now has Pipeline and IPOPT (local) choices. Pipeline remains the default. Backend selection, inertia and recorded-contact settings survive save/load and undo; older saves select Pipeline. IPOPT uses a configured local Python executable, with CasADi 3.8.1 and NumPy 2.5.4, tested under Python 3.12.2. No installation, download, native packaging or existing experiment environment modification occurs automatically.

The worker is a packaged resource, independent of the research scripts. The Java adapter exports an immutable problem snapshot and uses JSON lines over a dedicated child process. There is no shell interpolation. It bounds startup, construction and solving together, drains stderr, terminates on timeout/cancellation, validates every incumbent in Java, and retains a valid incumbent on timeout. The request writer is a daemon thread so a worker that never reads stdin cannot block cancellation, including large requests. Worker status alone cannot certify feasibility.

The continuous replay independently reconstructs controls from facings with StrictMath, applies automatic per-axis thresholding, and conditionally clips prescribed wall hits while zeroing only normal velocity. A missed wall cannot trigger a reset even if the shortfall is below the positional tolerance. Every compiled constraint is checked at 1e-7 tolerance. Native Pipeline's zero-tolerance game-arithmetic gate remains unchanged.

All supported position and facing rows are exported, including relative rows and cross-axis displacement comparisons. Continuous facing equalities use the ordinary 1e-4 corridor around the typed value, without the native solver's wider sine-table-cell equivalence. The worker uses linear facing comparisons in a local angular chart; final validation uses the existing wrapped constraint evaluator. Relative references outside the span, displacement at its initial state, and facing constraints on its final state produce explicit configuration errors rather than being omitted. Start position and velocity remain fixed. Legal record mode, nonzero smooth lambda, and combined horizontal inertia produce configuration errors in this first local backend.

Results and live trajectories are labelled continuous. Stop keeps a validated incumbent. Apply uses the existing yaw/key writing and simulation callback; realized game differences get a continuous-versus-game message instead of incorrectly asserting that the solver cannot see walls. Continuous plans cannot use the native Smooth action. IPOPT recordings identify the continuous model and tolerance without reporting unexecuted native graph nodes.

## Integration findings

A direct packaged-worker probe reproduced the positional-only 400-tick wall/inertia solve. Adding the original facing rows exposed recovery sensitivity: immediately freezing every inferred zero/survival branch after the carry solve sometimes ended in restoration failure or a failed fixed pattern. This was a search failure, not a reason to drop the facing rows.

Direct probes from the authored start and accepted earlier paths showed that the facing-constrained carry problem is feasible, and that some complete fixed patterns solve while another inferred pattern does not. Enforcing inferred zero events first, without survivor sign walls, provides an effective intermediate continuation. The local worker now uses carry, zero-event continuation, then complete fixed patterns, with bounded refresh attempts inside one total budget. Java always checks the full automatic-threshold model before accepting a candidate, including during intermediate phases.

The complete Java engine and Apply test first passed with all 341 rows and 34 contacts in 6.431 s, X@400 approximately 691.4017521813. After changing facing equalities to the typed continuous corridor, the same engine path passed in 3.693 s, X@400 approximately 691.4017522784. These are separate local runs, not a speedup claim. Sixteen targeted tests then passed together, including the known-optimum worker problem, independent replay, strict wall-shortfall rejection, process cancellation/time limits, malformed output, rejected fake success, retained incumbent, and save/load settings.

The archived fixture is copied into the backend's own `core/src/test/resources/ipopt/real11.json.gz`; original files are untouched. SHA-256: `ce8b765c0f8b1e1d0add83d2a690230e1df0898d4bc4c8919d7a72b358bd27be`. Optional `PKC_IPOPT_CAPTURE` overrides it. Runtime tests require explicit `PKC_IPOPT_PYTHON`; the default Java tests do not need CasADi installed.

## Verification in progress

A full `:core:test -PslowTests -PverySlowTests` run is in progress. Existing native failures observed so far include `CertBnbDfFoldTest.p2sFreeStartDfChainSolves`, `CrossJvmDeterminismTest.searchPathIsBitIdenticalAcrossJvms`, `First30NormalMarginTraceTest.traceNormalMarginSevenWindowCommitOne`, and `FoldDriverEngineTest.j1150InnerThoroughReachesTheWitnessValue`.

For baseline comparison, all five modified pre-existing production classes were recompiled directly from unchanged HEAD into a separate temporary directory, ahead of the remaining unchanged compiled classes. The first two failures reproduce with those original versions. The baseline cross-JVM test has the expected linear hash 6680509507868675287 but closed-form hash 7622806883939910482 instead of its golden 7100074979304643135. The baseline free-start dF case fails at 37/44 constraints with the receding-horizon solver. Other failures and final suite results will be recorded after completion.

No Minecraft client is launched: one is already running, and the repository prohibits a second client sharing its run directory. In-game QA remains outstanding. Setup and supported semantics are documented in `docs/ipopt-local-solver.md`.

## Scope correction from the user

The user clarified that mod integration is unnecessary and a purely standalone solver is preferable if simpler. The optional mod integration is preserved on `feature/ipopt-local`; the deliverable is moving to a separate standalone branch without changes to the mod's UI, save schema or engine. The in-progress expensive native suite was stopped because its gate no longer applies to that standalone scope. It was not a passing full-suite run.

Before that correction, all 17 targeted tests passed on JDK 21: real11 200 ticks in 0.627 s and 400 ticks in 3.935 s. The same 17 tests passed on Java 8 once the ARM worker architecture was selected: 200 ticks in 0.627 s and 400 ticks in 3.660 s. An Intel Java 8 runtime under Rosetta initially launched universal Python as Intel despite ARM NumPy/CasADi packages. An optional macOS `PKC_IPOPT_ARCH` / `pkc.ipopt.arch` setting fixes that process architecture issue. The standalone ARM Python process does not need the Java-side workaround.

The four native failures above all reproduced with the original production classes. Additional native failures were observed in two GraphPathObjectiveGateTest methods, InertiaFoldingTest and J008VelocityFieldGoldenTest before stopping the suite; those additional cases were not individually baseline-rerun. Existing native solver tests and golden values were not edited.
