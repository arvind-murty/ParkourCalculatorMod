"""Stage costs and payoffs of the proposed pipeline from flow.py rows."""
import json, sys, collections, statistics as st
for path in sys.argv[1:]:
    R = [json.loads(l) for l in open(path)]
    print(f'\n===== {path}: {len(R)} requests, total Clarabel solves {sum(r["clarabel"] for r in R)}, '
          f'mean {st.mean(r["clarabel"] for r in R):.1f}, total seconds {sum(r["seconds"] for r in R):.2f}')
    print('outcome by method', dict(collections.Counter(r['method'] for r in R)))
    agg = collections.defaultdict(lambda: [0, 0., 0])
    for r in R:
        for k, (n, s) in r['stages'].items():
            a = agg[k]; a[0] += n; a[1] += s; a[2] += 1
    print('| stage | requests | solves | seconds |'); print('|---|---|---|---|')
    for k, (n, s, c) in sorted(agg.items(), key=lambda kv: -kv[1][0]):
        print(f'| {k} | {c} | {n} | {s:.2f} |')
    q = [r for r in R if r['quadrant'] is not None]
    print('quadrant:', dict(collections.Counter(r['quadrant'] for r in q)), 'solves when open', sum(r['stages'].get('quadrant', [0])[0] for r in q if r['quadrant'] == 'open'),
          'when infeasible', sum(r['stages'].get('quadrant', [0])[0] for r in q if r['quadrant'] == 'infeasible'),
          '| race+decay solves on requests quadrant left open but nothing answered', sum(r['stages'].get('race', [0])[0]+r['stages'].get('decay', [0])[0] for r in q if r['quadrant'] == 'open' and r['method'] in ('none', 'quadrant')))
    raced = [r for r in R if r['candidates']]
    won = [r for r in raced if r['method'] in ('race', 'probe') and any(c.get('winner') for c in r['candidates'])]
    print(f'race: {len(raced)} requests raced, {len(won)} won by race; outcome of raced requests', dict(collections.Counter(r['method'] for r in raced)))
    C = [c for r in raced for c in r['candidates']]
    print('  candidates', len(C), 'feasible', sum(c['feasible'] for c in C), '| node solves', sum(c['nodeSolves'] for c in C),
          'screen solves', sum(c.get('screenSolves', 0) for c in C), 'finish solves', sum(c.get('finishSolves', 0) for c in C),
          '| finish steps repeating the screen', sum(min(c.get('screenSteps', 0), c.get('finishSteps', 0)) for c in C if c.get('finished')))
    print('  race solves on requests the race does not win', sum(r['stages'].get('race', [0])[0] for r in raced if r not in won),
          'of which then answered by the decayed push', sum(r['stages'].get('race', [0])[0] for r in raced if r not in won and r['method'] in ('decayFallback',)),
          'requests', sum(r not in won and r['method'] == 'decayFallback' for r in raced))
    # predictors of the winner among feasible candidates
    ranks = collections.Counter()
    for r in won:
        F = [c for c in r['candidates'] if c['feasible']]
        w = next(c for c in F if c.get('winner'))
        s = 1 if r['sense'] == 'MAX' else -1
        byb = sorted(F, key=lambda c: -s*c['bound']); bypk = sorted(F, key=lambda c: c['diskPeak'])
        ranks[('bound rank', byb.index(w)+1, 'of', len(F))] += 1
        ranks[('node disk-point peak rank', bypk.index(w)+1)] += 1
        ranks[('screen found', w.get('screenFound') is not None)] += 1
        ranks[('winner is top-shortfall tick', w['top'])] += 1
    for k, v in sorted(ranks.items(), key=str): print('  winner', k, v)
    # would ranking by bound pick the same winner without screening?
    P = collections.Counter(r['after'] + (' gained' if r['gain'] > 1e-9 else ' no gain') for x in R for r in x['probes'] if r['ran'])
    print('probes (stage, outcome): requests', dict(P), '| probe solves by stage', {k: v[0] for k, v in agg.items() if k.startswith('probes')})
    D = [r for r in R if 'decay' in r['stages']]
    print('decay: requests', len(D), 'answered', sum(r['method'] in ('decayFallback', 'probe') for r in D), 'solves', sum(r['stages']['decay'][0] for r in D),
          'mean', round(st.mean(r['stages']['decay'][0] for r in D), 1) if D else None)
