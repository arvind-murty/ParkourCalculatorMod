"""Score push_vs_ipopt.py rows: who answers (full-strength push or IPOPT), the objective difference where both do
(positive = push better, in the objective's sense), and time.
Usage: push_vs_ipopt_score.py <bench.jsonl>"""
import collections, json, statistics, sys
R = [json.loads(l) for l in open(sys.argv[1])]
sign = lambda r: 1 if r['sense'] == 'MAX' else -1
print(len(R), 'requests; push methods', dict(collections.Counter(r['push']['method'] for r in R)))
c = collections.Counter((r['push']['objective'] is not None, r['ipopt']['status'] == 'feasible') for r in R)
print('both answer', c[(True, True)], '| push only', c[(True, False)], '| IPOPT only', c[(False, True)], '| neither', c[(False, False)])
gaps = sorted(sign(r)*(r['push']['objective']-r['ipopt']['objective']) for r in R if r['push']['objective'] is not None and r['ipopt']['objective'] is not None)
if gaps:
    print('push minus IPOPT where both answer: below by >1e-2 %d, by 1e-3..1e-2 %d, within 1e-3 %d, above by >1e-3 %d; min %.4f median %.2e max %.4f' % (
        sum(g < -1e-2 for g in gaps), sum(-1e-2 <= g < -1e-3 for g in gaps), sum(abs(g) <= 1e-3 for g in gaps),
        sum(g > 1e-3 for g in gaps), gaps[0], statistics.median(gaps), gaps[-1]))
for name, key in (('push', 'push'), ('IPOPT', 'ipopt')):
    s = sorted(r[key]['seconds']*1000 for r in R)
    print(f'{name} ms mean {statistics.mean(s):.1f} median {statistics.median(s):.1f} p90 {s[int(.9*len(s))]:.1f} max {s[-1]:.1f}')
by = collections.defaultdict(list)
for r in R:
    by[r['push']['method']].append(r)
for m, rs in sorted(by.items()):
    print(f"  push {m}: {len(rs)} requests, IPOPT feasible on {sum(r['ipopt']['status'] == 'feasible' for r in rs)}, push ms mean "
          f"{statistics.mean(r['push']['seconds']*1000 for r in rs):.1f}, Clarabel solves mean {statistics.mean(r['push']['clarabel'] for r in rs):.1f}")
for r in R:
    if r['push']['objective'] is None and r['ipopt']['status'] == 'feasible' or (
            r['push']['objective'] is not None and r['ipopt']['objective'] is not None and sign(r)*(r['push']['objective']-r['ipopt']['objective']) < -1e-2):
        print('   ', r['fingerprint'][:12], 'peak shortfall %.3f' % r['peakShortfall'], r['push']['method'], r['push']['objective'], '| IPOPT', r['ipopt']['objective'])
