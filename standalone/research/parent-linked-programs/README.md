# Parent-linked programs

2026-11-21, `perf/prefix-reuse-keys`. Children extend their parent instead of rebuilding it: design in
[generic composition](../../docs/design/GENERIC_COMPOSITION.md#boundary-and-composition), freeze invariant in
[performance](../../docs/PERFORMANCE.md#cache-and-identity-invariants). This is also the Python reference for the
native `Program`'s parent-linked extension in the C++ migration design.

## Why

Fixed-work extended input ([input](../prefix-reuse-keys/2026-11-21/extended-fixedwork100.json), call cap 100,
`--profile-mode none`, not durable) on `996c1a44`. Per-call fits against program length
([`append_scaling.py`](append_scaling.py), [`gate_scaling.py`](gate_scaling.py)):

| Stage | Seconds | Per tick | Search s of that run |
| --- | ---: | --- | ---: |
| `append_choice` | 6.276 | 24.7 µs per parent tick, nothing measurable per new tick | 40.70 |
| `program_physics_key` | 1.780 | 6.1 µs (88.2% of its cost) | 40.70 |
| `InteractionGate.prepare` | 6.644 | 49.1 µs | 40.95 |
| `InteractionGate.analyze_model` | 2.369 | 19.9 µs | 40.95 |

Parents have a median 69 ticks; a child adds a median 12 ([before](2026-11-21/append-scaling-before.json),
[gate](2026-11-21/gate-scaling-before.json)).

## What makes sharing safe

- In all 3,951 appends every parent field is an exact prefix of the child's ([`prefix_stability.py`](prefix_stability.py),
  [result](2026-11-21/prefix-stability.json)).
- No container inside a built program changes after creation: 0 of 1,863,596 on the fixed-work run and 0 of 80,239
  on the short case ([`mutation_audit.py`](mutation_audit.py), [extended](2026-11-21/mutation-ext100.json),
  [short](2026-11-21/mutation-short.json)).

## Phase A: children share the parent's content

`append_choice` traces only the new ticks from `boundary_state` and concatenates the parent's per-tick lists, sharing
their elements; only the choice's contacts, cases, rows, region choices and units are shifted into new objects.
Appends with ladder ticks or ladder events, and the first append, keep the whole-program rebuild (`_rebuild`, deferred
as C3). The unit test compares every extension with `_rebuild` on generated chains, including truncated parents.

Every `append_choice` call of a real run was also computed by the previous commit's implementation
([`append_parity.py`](append_parity.py)): equal in all 3,946 extensions and 5 rebuilds of the fixed-work run and all
280 extensions and 5 rebuilds of the short case ([extended](2026-11-21/append-parity-ext100.json),
[short](2026-11-21/append-parity-short.json)).

Equal work, old `996c1a44` and new alternating, load average 5.6 to 6.9:

| Run | Search s | Expanded | Calls | Preparations | Sequence checks |
| --- | ---: | ---: | ---: | ---: | ---: |
| old 1 | 43.93 | 28 | 100 | 1,412 | 1,015 |
| new 1 | 36.07 | 28 | 100 | 1,412 | 1,015 |
| old 2 | 42.34 | 28 | 100 | 1,412 | 1,015 |
| new 2 | 36.21 | 28 | 100 | 1,412 | 1,015 |

Mean search time falls 16.2%; the parity harness (`tools/profiling/parity.py` at `364f5699`) finds identical reports,
digests included, for both pairs (19 transitions) and the short case (T64, objective -1093.2381026602418, 62
transitions, 4.94 s old and 4.72 s new) ([summaries](2026-11-21/)). `append_choice` now takes 0.408 s over the same
3,951 calls: 0.20 µs per parent tick and 2.29 µs per new tick ([after](2026-11-21/append-scaling-after.json)). The
native gate passed 830 tests with no failures, errors or skips ([report](native-gate/report.json); run on `996c1a44`
with this change uncommitted).

The dedicated ladder, climbable, vine and movement-entry modules, which the gate excludes, ran 99 tests on old and new
with the same 14 failures and errors on both ([list](2026-11-21/ladder-modules-failures.txt)), none from this change.
