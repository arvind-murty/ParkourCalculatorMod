"""Score seed_bench.py rows per seed. Reference: with --labels, the study's labels (pipeline_grid_score.py's rules: best
full-strength objective known on feasible requests; development known None and held-out Gurobi INFEASIBLE are
infeasible; fresh 3.344 and Gurobi TIME_LIMIT undecided); without, the best answer any seed found (requests no seed
answered are unknown). --reach <eval.json> also scores the subset whose pipeline answer (population_eval.py `kind`) came
from IPOPT or nothing, i.e. the requests that reach the IPOPT calls.
Usage: seed_bench_score.py <bench.jsonl> [--labels flip-population.json heldout-eval.json heldout-gurobi-feas.json
  population-heldout.json] [--reach eval.json]"""
import collections
import json
import sys

import numpy as np

args = sys.argv[1:]
rows = [r for r in map(json.loads, open(args[0])) if 'seeds' in r]
ref, infeasible, sense = {}, set(), {}


def answer(seed):
    return next((r['objective'] for r in seed['runs'].values() if r['objective'] is not None), None)


if '--labels' in args:
    i = args.index('--labels')
    F = {e['fingerprint']: e for e in json.load(open(args[i+1]))}
    H, GF, P = (json.load(open(a)) for a in args[i+2:i+5])
    gurobi = {next(k for k, r in P.items() if f"{r['source']}:{r['name']}" == name): row['status']
              for name, row in GF.items()}
    for fp in (r['fingerprint'] for r in rows):
        if fp in F:
            e = F[fp]
            if e['known'] is None:
                infeasible.add(fp)
            else:
                ref[fp] = max(x for x in [e['known']]+[r.get('objective') for r in e['rows']] if x is not None)
        elif fp in H:
            if fp in gurobi:
                if gurobi[fp] == 'INFEASIBLE':
                    infeasible.add(fp)
                continue
            ref[fp] = max(x for x in (H[fp]['label'], H[fp]['policy']) if x is not None)
    # The study's labels are MAX objectives (every population request maximizes).
else:
    for r in rows:
        found = [a for a in map(answer, r['seeds'].values()) if a is not None]
        if found:
            ref[r['fingerprint']] = max(found)
reach = None
if '--reach' in args:
    E = json.load(open(args[args.index('--reach')+1]))
    reach = {fp for fp, e in E.items() if e['runs']['pipeline']['kind'] in ('prodSeed', 'polish', 'none')}

names = list(dict.fromkeys(n for r in rows for n in r['seeds'] if not n.startswith('random')))


def table(subset, title):
    fps = [r for r in rows if subset(r['fingerprint'])]
    feas = [r for r in fps if r['fingerprint'] in ref]
    bad = [r for r in fps if r['fingerprint'] in infeasible]
    print(f"\n{title}: {len(fps)} requests, {len(feas)} with a reference, {len(bad)} infeasible")
    print('| seed | built on | answered at 0.2 s | answered (0.2 s, else 2 s) | within 1e-3 | within 1e-2 | answers on '
          'infeasible | IPOPT s mean (0.2 s run) | IPOPT s mean (both runs) | median start violation |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    for n in names + ['screen']:
        pick = (lambda r: r['seeds'][r['screen']['pick']]) if n == 'screen' else (lambda r: r['seeds'].get(n))
        have = [r for r in fps if pick(r) is not None]
        a02 = sum(pick(r)['runs']['0.2']['objective'] is not None for r in have if r['fingerprint'] in ref)
        got = {r['fingerprint']: answer(pick(r)) for r in have}
        ans = [r for r in feas if got.get(r['fingerprint']) is not None]
        gaps = [ref[r['fingerprint']]-got[r['fingerprint']] for r in ans]
        wrong = sum(got.get(r['fingerprint']) is not None for r in bad)
        s02 = [pick(r)['runs']['0.2']['seconds'] for r in have]
        sall = [sum(x['seconds'] for x in pick(r)['runs'].values()) for r in have]
        print(f"| {n} | {len(have)} | {a02} | {len(ans)} | {sum(g <= 1e-3 for g in gaps)} | {sum(g <= 1e-2 for g in gaps)} | "
              f"{wrong} | {np.mean(s02):.3f} | {np.mean(sall):.3f} | {np.median([pick(r)['violation'] for r in have]):.3f} |")
    # Coverage: requests with a reference that some seed answers within 1e-2, greedy sets.
    good = {n: {r['fingerprint'] for r in feas if n in r['seeds'] and answer(r['seeds'][n]) is not None
                and ref[r['fingerprint']]-answer(r['seeds'][n]) <= 1e-2} for n in names}
    union = set().union(*good.values())
    chosen, covered = [], set()
    for _ in range(4):
        n = max(names, key=lambda n: len(good[n]-covered))
        if not good[n]-covered:
            break
        chosen.append(n)
        covered |= good[n]
        print(f"greedy set {chosen}: {len(covered)} of {len(union)} answerable within 1e-2 (of {len(feas)} with a reference)")
    # Start violation as a predictor: within each request, does the least-violation deterministic seed answer?
    hit, rate = [], []
    for r in feas:
        det = {n: s for n, s in r['seeds'].items() if not n.startswith('random') and n not in ('polish', 'relaxedStates')}
        best = min(det, key=lambda n: det[n]['violation'])
        hit.append(answer(det[best]) is not None)
        rate.append(np.mean([answer(s) is not None for s in det.values()]))
    if hit:
        print(f"least-violation yaw-only seed answers {sum(hit)}/{len(hit)}; a random yaw-only seed answers "
              f"{np.mean(rate)*len(rate):.1f}/{len(rate)} on average")
    picks = collections.Counter(r['screen']['pick'] if not r['screen']['pick'].startswith('random') else 'random'
                                for r in fps)
    print('screen picks', dict(picks.most_common()))


table(lambda fp: True, 'all')
if reach is not None:
    table(lambda fp: fp in reach, 'requests whose pipeline answer is IPOPT or none (they reach the IPOPT calls)')
