"""Two-call policies at production's 0.2 s IPOPT cap only (seed A; seed B only if A returns no answer): per set, requests
answered within 1e-2 of the reference, gained/lost vs today's (prodSeed, polish), and mean IPOPT seconds spent per request.
Run from standalone/runs/post-alarm. Sets: misses (12), study population requests that reach IPOPT (31, labels), Real11 alarmed requests that reached IPOPT (78)."""
import json, pathlib, sys
HERE = pathlib.Path(__file__).parent
def answer02(s):
    return s['runs']['0.2']['objective']
def answer(s):
    return next((x['objective'] for x in s['runs'].values() if x['objective'] is not None), None)
POL = {}
# misses: reference = best any seed (any cap)
m = [json.loads(l) for l in open('seed-bench/misses.jsonl')]
POL['misses'] = (m, {r['fingerprint']: max(a for a in map(answer, r['seeds'].values()) if a is not None)
                      for r in m if any(answer(s) is not None for s in r['seeds'].values())}, {r['fingerprint']: 1 for r in m})
# population reach: labels
sys.argv = ['x', 'seed-bench/population.jsonl', '--labels', 'flip-population.json', 'heldout-eval.json',
            'heldout-gurobi-feas.json', 'population-heldout.json']
exec(open(HERE/'seed_bench_score.py').read().split("table(lambda fp: True")[0])
E = json.load(open('eval.json'))
pr = [r for r in rows if E[r['fingerprint']]['runs']['pipeline']['kind'] in ('prodSeed', 'polish', 'none')]
POL['population reach'] = (pr, ref, {r['fingerprint']: 1 for r in pr})
# Real11 reach
exec(open(HERE/'seed_bench_alarmed.py').read().split("print('requests'")[0])
POL['Real11 reach'] = (reach, ref, sign)
def run(r, pair, sgn, refs):
    seconds, got = 0., None
    for n in pair:
        s = r['seeds'].get(n)
        if s is None: continue
        seconds += s['runs']['0.2']['seconds']; got = answer02(s)
        if got is not None: break
    fp = r['fingerprint']
    return got is not None and fp in refs and sgn.get(fp, 1)*(refs[fp]-got) <= 1e-2, seconds
policies = [('prodSeed', 'polish'), ('pushLast', 'flip1'), ('pushBest', 'flip1'), ('polish', 'flip1'), ('flip1', 'polish'),
            ('polish', 'prodSeed'), ('pushLast', 'prodSeed')]
print('| policy | ' + ' | '.join(f'{k} answered (gained/lost vs today) / IPOPT s mean' for k in POL) + ' |')
print('|---|' + '---|'*len(POL))
for p in policies:
    cells = []
    for k, (rs, refs, sgn) in POL.items():
        res = [run(r, p, sgn, refs) for r in rs]; base = [run(r, policies[0], sgn, refs)[0] for r in rs]
        ok_ = [x[0] for x in res]
        cells.append(f"{sum(ok_)}/{sum(r['fingerprint'] in refs for r in rs)} (+{sum(a and not b for a, b in zip(ok_, base))}/-{sum(b and not a for a, b in zip(ok_, base))}) / {sum(x[1] for x in res)/len(res):.3f}")
    print(f"| {' then '.join(p)} | " + ' | '.join(cells) + ' |')
print('\nlosses of polish then flip1 vs today (answer at 0.2 s per seed, reference):')
for k, (rs, refs, sgn) in POL.items():
    for r in rs:
        if run(r, ('prodSeed', 'polish'), sgn, refs)[0] and not run(r, ('polish', 'flip1'), sgn, refs)[0]:
            print(' ', k, r['fingerprint'][:12], {n: answer02(r['seeds'][n]) for n in ('prodSeed', 'polish', 'flip1') if n in r['seeds']}, 'ref', refs.get(r['fingerprint']))
