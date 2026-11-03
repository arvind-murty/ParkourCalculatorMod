# Results, terminology and debugging

Start with [architecture](ARCHITECTURE.md) for call flow, [runbook](RUNBOOK.md) for execution, and [Real11 guide](REAL11_GUIDE.md) for the existing corpus. Do not launch a new long solve just to answer a question already logged.

## Vocabulary and units of work

| Term | Meaning |
| --- | --- |
| Cell / portal / corridor | Spatial decomposition region / opening / selected broad route through regions. No intrinsic tick cost or fixed dynamic state. |
| Prefix inputs | Number and identities of already modeled **input ticks**, not estimated arrival at a common spatial goal. Old priority incorrectly favored shorter prefixes. |
| Program | Concrete discrete movement/input/contact history, scalar vertical trace, constraints and provenance; horizontal facings are still variables. |
| Passage description | Signed face chain and switch/gate ticks used to build obstacle clearance rows. Many descriptions can yield one effective row set. |
| Constraint variant | One concrete hard row/contact alternative for a fixed program. A schedule can require many such trials. |
| Branch | One handled constraint alternative. It may be screened or stopped before worker dispatch. |
| Worker call | A subprocess numerical solve; main calls and spatial-comparison probe calls are counted separately. One call can have multiple internal phases/iterations/callbacks. |
| Repair search/state | Inner fixed-program enumeration/refinement and its resumable queues/logs. It is not an IPOPT mixed-integer mechanism. |
| Spatial probe | Auxiliary arrival solve used to compare alternatives at a common spatial goal; does not by itself establish a complete route. |
| Accepted prefix | Numerically and physically accepted for its recorded scope/horizon. Not a complete terminal incumbent. |
| Unknown / suspended / exhausted | No accepted witness yet / unfinished saved work / declared domain drained. None alone certifies physical impossibility. |

## Result layers

1. **Numerical:** IPOPT/parent scalar validation satisfies the fixed continuous request. Read `status`, `termination`, `best` or `candidate`, violations, worker phase diagnostics. Worker “feasible” is not global optimality or scene discovery.
2. **Vertical/event:** Selected run/jump/landing/stepping/ceiling schedule agrees with scalar motion, impulses and ground timing.
3. **Scene:** Swept Y→X→Z collision/body checks agree with supplied solids and scope. Omitted external blocks are not magically validated.
4. **Corridor:** The accepted physical trajectory also meets the selected broad route/projection/signature. A corridor rejection can be physically valid on a different signature; inspect rather than label IPOPT infeasible.
5. **Executable replay/export:** Float yaw, input/sprint/cooldown, sine-table and finite-scene replay plus JSON round trip pass the replay report. Actual-world mod validation remains separate.

Inspect `solveScope`, `solveIdentity`, `best.validationScope`, `fullSceneValidated`, and `candidateFilter.corridorAudit`. Preliminary scope omits future interactions and matching corridor requirements; it cannot become a complete incumbent. `validation_prefix` ensures candidate arrays and checks use the same horizon. A skipped check is neither PASS nor FAIL.

## Report layout and safe inspection

Fixed-clock reports contain `source`, `fixture`, `windows`, progress `searchState`, and final `result`/`validation`. A live progress file may not yet contain a final `result`; final files put continuation under `result.searchState`. Window candidate rows contain programs/scores and retained/deferred classifications. Terminal attempts and spatial probes have separate records. Inner `repairState` has `attemptLog`, `contactLog`, constraint generation and pending/inFlight/completed/unresolved state. Read the producing module for the exact schema/version; don't assume all historical reports share it.

Read large files selectively:

```sh
python3 - <<'PY'
import gzip, json
from pathlib import Path
p = Path('/tmp/real11-cold-new.json.gz')
with (gzip.open(p, 'rt') if p.suffix == '.gz' else p.open()) as f:
    report = json.load(f)
r = report.get('result', report)
print('keys:', sorted(r))
print('status:', r.get('status'), 'termination:', r.get('termination'))
print('work:', r.get('work'))
s = r.get('searchState', report.get('searchState', {}))
print('continuation keys:', sorted(s))
PY
```

Do not dump complete multi-MB reports to the conversation. Use tables/counters and link exact provenance. Resume snapshots repeat history; count identity deltas, not list lengths summed across files. Logs do not always align one-to-one: the saved Real11 corpus has two missing post-validation records with ambiguous possible branch placements. Preserve that ambiguity.

## Failure investigation workflow

1. Fix the claim and context: cold discovery, seeded control, prefix, terminal objective or actual replay? Record fixture revision, allowed choices, scope and source hash.
2. Trace one candidate from generator choice → append/truncate → profile/schedule → compiled coefficients → exact rows/contact alternatives → screen → dispatched request → scalar result → physical/corridor checks → retention/child program → final/resume state.
3. If no numerical request occurred, inspect source cursor, deadlines, duplicate counters and pre-dispatch proof. Raising IPOPT time cannot fix symbolic starvation.
4. If the worker succeeded but the candidate was rejected, identify the exact check, phase and residual. Compare full physical replay with corridor signature separately. For an omitted future obstacle, both preliminary constraints and checks must omit its interactions.
5. For a suspicious block/tick, ask the **shared interaction query** with the actual compiled context. Inspect swept Y extent, X/Z envelopes and selected contacts. Endpoint height or a soft guide is not a reachability proof. Impossible required contact rejects the proposal; impossible unwanted contact creates no avoidance.
6. For orphan constraints, retain the full proposal and owner event before projection. Compare untruncated and truncated checks. A discarded future event must not leave accidental hard approach rows.
7. For repeated work, compare physical/context keys and exact effective rows; distinguish raw descriptions, unique models, dispatched calls and worker callbacks. Never infer that many duplicate descriptions mean every numerical call was duplicated.
8. For pruning/ordering, compare the same goal, absolute tick, phase/schedule and witness provenance. Directional position, forward velocity and speed must come from the **same** accepted witness. Retention caps must not erase protected saved work.
9. Add a minimal generic regression and a fixture-specific one. Then run only the checks needed to validate the hypothesis, before broadening.

## Profiling and evidence

Use existing `generation_diagnostic`, `audit_generation`, `CountingCompiler` and `SearchWork` counters. Offline `cProfile` can isolate generation/refinement. Avoid timed `faulthandler` stack-dump timers around the macOS native numerical runtime; prior diagnostics found that setup unsafe. Keep BLAS/OpenMP threads bounded, record overlapping jobs, and do not benchmark compiler rebuilds concurrent with solves.

Preserve plain/gzip JSON, exact constraints, source hashes, command/settings, wall and worker time, and any replay checks. Gzip evidence should use deterministic `mtime=0`; JSON must reject NaN/Infinity. Never round coordinates before re-evaluating a tight constraint. New outputs belong in new paths, with a manifest when archived. Historical source hashes are not updated just because explanatory documentation changes.
