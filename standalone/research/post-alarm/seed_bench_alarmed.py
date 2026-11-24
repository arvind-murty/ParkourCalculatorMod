"""Real11 all-alarmed set: per seed and per two-call policy (seed A, then B if A fails), requests answered within 1e-2
of the reference (best of any seed and the pipeline's own captured answer), on all requests and on those whose captured
pipeline reached IPOPT (method prodSeed, polish or none). Answers on quadrant-certified requests must be 0.
Run from standalone/runs/post-alarm."""
import json, itertools, collections
cap = {}
for l in open('alarmed/alarmed.jsonl'):
    r = json.loads(l); cap[r['fingerprint']] = (r['result']['confirmation']['method'], r['objective'], r['problem']['objective']['sense'])
rows = [json.loads(l) for l in open('seed-bench/alarmed.jsonl')]
rows = [r for r in rows if 'seeds' in r]
def answer(s):
    return next((x['objective'] for x in s['runs'].values() if x['objective'] is not None), None)
sign = {fp: 1 if c[2] == 'MAX' else -1 for fp, c in cap.items()}
ref = {}
for r in rows:
    fp = r['fingerprint']; vals = [a for a in map(answer, r['seeds'].values()) if a is not None]
    if cap[fp][1] is not None: vals.append(cap[fp][1])
    if vals: ref[fp] = max(vals, key=lambda v: sign[fp]*v)
names = list(dict.fromkeys(n for r in rows for n in r['seeds'] if not n.startswith('random')))
def ok(r, n):
    s = r['seeds'].get(n); a = s and answer(s); fp = r['fingerprint']
    return a is not None and sign[fp]*(ref[fp]-a) <= 1e-2
reach = [r for r in rows if cap[r['fingerprint']][0] in ('prodSeed', 'polish', 'none')]
quad = [r for r in rows if cap[r['fingerprint']][0] == 'quadrant']
sets = {'reach IPOPT': reach, 'all': rows}
print('requests', len(rows), 'methods', dict(collections.Counter(cap[r['fingerprint']][0] for r in rows)))
print('with a reference:', {k: sum(r['fingerprint'] in ref for r in v) for k, v in sets.items()},
      '| reach, pipeline answered:', sum(cap[r['fingerprint']][1] is not None for r in reach))
print('answers on quadrant-certified:', {n: sum(answer(r['seeds'][n]) is not None for r in quad if n in r['seeds']) for n in names})
def first(r, pair):
    """The first seed in order that answers (the policy stops there), else None."""
    return next((n for n in pair if n in r['seeds'] and answer(r['seeds'][n]) is not None), None)
def score(pair):
    return {k: sum(first(r, pair) is not None and ok(r, first(r, pair)) for r in v) for k, v in sets.items()}
print('| seed | reach answered within 1e-2 | all within 1e-2 | reach IPOPT s mean (both runs) | reach answered at 0.2 s |')
print('|---|---|---|---|---|')
for n in names:
    have = [r for r in reach if n in r['seeds']]
    s = score((n,)); sec = sum(sum(x['seconds'] for x in r['seeds'][n]['runs'].values()) for r in have)/max(1, len(have))
    a02 = sum(r['seeds'][n]['runs']['0.2']['objective'] is not None for r in have)
    print(f"| {n} | {s['reach IPOPT']} | {s['all']} | {sec:.3f} | {a02} |")
print('today (prodSeed, polish):', score(('prodSeed', 'polish')))
seeds = [n for n in names if n != 'screen']
for p in sorted(itertools.permutations(seeds, 2), key=lambda p: tuple(-v for v in score(p).values()))[:10]:
    print('pair', p, score(p))
print('reach requests no seed answers:', sum(not any(ok(r, n) for n in seeds) for r in reach if r['fingerprint'] in ref),
      'with no reference:', sum(r['fingerprint'] not in ref for r in reach))
