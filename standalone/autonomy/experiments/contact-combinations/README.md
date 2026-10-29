# Coupled contact and ladder evidence

The snapshots contain the exact compiled continuous problem, selected route/event schedule, accepted candidate and independent shared finite-scene acceptance. They are witnesses, not minimum-time or objective certificates.

| Snapshot | Inputs | Evidence |
| --- | --- | --- |
| `corner-result.json` | 3 | Cold geometry search: ceiling plus Z hit on input 0, X hit on input 1 |
| `contact-blockage.json` | 12 | Z contact at state 4 plus a wide-neo X-to-Z gate at state 4 and Z-to-X handoff at states 8/9 |
| `ladder-ceiling.json` | 12 | Cold geometry search: required ceiling cancellation composed with entry and climbing |
| `two-ladders.json` | 42 | Cold geometry search: start, ladder A top support, ladder B top support; joint prefix solve |
| `airborne-transfer.json` | 28 | Prescribed episode-contract solve: two regions, one launch, no intermediate landing |
| `automatic-airborne-transfer.json` | 28 | Cold geometry search independently discovers the two-region airborne transfer |

The automatic transfer snapshot records the geometry-only fixture SHA-256. No entry time, hit schedule or facing is supplied by that fixture. Its accepted episodes occupy states 1 through 5 and 11 through 24, exit at states 6 and 25, and land at state 28. There is only one jump impulse and no intermediate grounded state. Local measured cold time was about 88 seconds under the documented work budgets.

The contact/blockage snapshot is derived from a successful geometry-only `contact-wide-neo.json` search. Its accepted contact is input 3 against the back wall, while the path avoids the central wall. The explicit wide-neo gates were reconstructed from that witness, added as constraints and revalidated with a feasible-seed IPOPT call plus scalar/shared scene acceptance. This verifies their simultaneous constraint composition; it does not claim that the original cold search selected explicit passage rows. The raw geometry-only search found an incumbent in its first successful branch after about 9.5 seconds and continued under a 50-second total budget.

Unit/NLP checks additionally cover all 63 nonempty subsets of X/Y/Z opportunities across two consecutive inputs; full ladder-window combinations; signed/finite face alternatives; every same-input X/Z subset with ceiling or landing; actual step plus two retained wall contacts; ceiling plus both wall contacts while occupied, followed by a ceiling-only tick and an occupied landing; and rejection of missing/extra/wrong-timed contacts. Step diagnostics now preserve contacts from the selected step path while preserving movement arithmetic.

Revalidate saved contracts without a new search:

```sh
PKC_RUN_TICKS_COMPILER="$PWD/standalone/autonomy/build/compiler.json" \
python3 -m unittest standalone.autonomy.tests.test_ladder_composition.CompositionEvidenceTests -v
```

The numerical integration gate requires both `PKC_RUN_TICKS_COMPILER` and `PKC_IPOPT_PYTHON`. Tests without the worker skip the cold searches. See [CONTACT_COMBINATIONS.md](../../CONTACT_COMBINATIONS.md) and [LADDER_COMPOSITION_PLAN.md](../../LADDER_COMPOSITION_PLAN.md) for interfaces, limits and remaining work.
