"""Frame-end policies per request from push_then_ipopt.py rows: IPOPT alone (from the relaxed facings, fallbackYaw, 5 s),
push alone (full strength), IPOPT then push when IPOPT fails, and push then an IPOPT polish from the push answer keeping
the better (IPOPT from the relaxed facings only when the push finds nothing). Reference: the best of all of them.
Usage: frame_end_score.py <rows.jsonl> ..."""
import json, sys
for path in sys.argv[1:]:
    R = [json.loads(l) for l in open(path)]
    def best(r, values):
        s = 1 if r['sense'] == 'MAX' else -1
        values = [v for v in values if v is not None]
        return max(values, key=lambda v: s*v) if values else None
    policies = {
        'IPOPT alone': lambda r: (r['ipoptFromRelaxed'], 1),
        'push alone': lambda r: (r['push'], 0),
        'IPOPT, then push if it fails': lambda r: (r['ipoptFromRelaxed'] if r['ipoptFromRelaxed'] is not None else r['push'], 1),
        'push + IPOPT polish (IPOPT from relaxed if no push)': lambda r: ((best(r, [r['push'], r['polish']]), 1) if r['push'] is not None
                                                                         else (r['ipoptFromRelaxed'], 1)),
    }
    ref = {r['fingerprint']: best(r, [r['ipoptFromRelaxed'], r['push'], r['polish']]) for r in R}
    print(f'\n{path}: {len(R)} requests, {sum(v is not None for v in ref.values())} answered by something')
    print('| policy | answered | within 1e-3 of best | within 1e-2 | worst gap | IPOPT calls |')
    print('|---|---|---|---|---|---|')
    for name, f in policies.items():
        gaps, calls, n = [], 0, 0
        for r in R:
            v, c = f(r)
            calls += c if (c and (name != 'push + IPOPT polish (IPOPT from relaxed if no push)' or True)) else 0
            if v is not None:
                n += 1
                s = 1 if r['sense'] == 'MAX' else -1
                gaps.append(s*(ref[r['fingerprint']]-v))
        print(f"| {name} | {n} | {sum(g <= 1e-3 for g in gaps)} | {sum(g <= 1e-2 for g in gaps)} | {max(gaps):.4f} | {calls} |")
    pol = [(1 if r['sense'] == 'MAX' else -1)*(r['polish']-r['push']) for r in R if r['push'] is not None and r['polish'] is not None]
    print('polish minus push where both: improved >1e-3 %d, within 1e-3 %d, worse >1e-3 %d; polish failed %d of %d' % (
        sum(x > 1e-3 for x in pol), sum(abs(x) <= 1e-3 for x in pol), sum(x < -1e-3 for x in pol),
        sum(r['push'] is not None and r['polish'] is None for r in R), sum(r['push'] is not None for r in R)))
