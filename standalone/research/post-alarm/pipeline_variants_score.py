"""Score pipeline_variants.py rows per variant. Study set: references and infeasible labels as pipeline_grid_score.py.
Real11 set: reference = best answer of any variant, the seed bench (seed_bench.py rows) or the captured pipeline; the
requests today's sector check certified infeasible stand in for known-infeasible. Answers more than 1e-3 above the
reference are relaxed (1% short) answers, counted apart.
Run from standalone/runs/post-alarm. Usage: pipeline_variants_score.py <study.jsonl> <real11.jsonl> <alarmed.jsonl>
  <seed-bench alarmed.jsonl> [other variants .jsonl ... (Real11 references only)]"""
import json
import pathlib
import sys

import numpy as np

HERE = pathlib.Path(__file__).parent
study_path, real_path, alarmed_path, bench_path = sys.argv[1:5]
extra = sys.argv[5:]

# Study labels (seed_bench_score.py's rules).
sys.argv = ['x', study_path, '--labels', 'flip-population.json', 'heldout-eval.json', 'heldout-gurobi-feas.json',
            'population-heldout.json']
source = open(HERE/'seed_bench_score.py').read()
source = source.replace("rows = [r for r in map(json.loads, open(args[0])) if 'seeds' in r]",
                        "rows = [r for r in map(json.loads, open(args[0]))]")
exec(source.split("reach = None")[0])
study = dict(rows=rows, ref=dict(ref), bad=set(infeasible), sign={})

real = [json.loads(l) for l in open(real_path)]
captured = {}
for line in open(alarmed_path):
    r = json.loads(line)
    captured[r['fingerprint']] = (r['problem']['objective']['sense'], r['objective'], r['result']['confirmation']['method'])
sign = {fp: 1 if c[0] == 'MAX' else -1 for fp, c in captured.items()}
known = {fp: [c[1]] if c[1] is not None else [] for fp, c in captured.items()}
for line in open(bench_path):
    r = json.loads(line)
    known[r['fingerprint']] += [x['objective'] for s in r['seeds'].values() for x in s['runs'].values()
                                if x['objective'] is not None]
for r in real+[json.loads(l) for path in extra for l in open(path)]:
    known[r['fingerprint']] += [v['objective'] for v in r['variants'].values() if v['objective'] is not None]
real_ref = {fp: max(v, key=lambda x: sign[fp]*x) for fp, v in known.items() if v}
real_set = dict(rows=real, ref=real_ref, bad={fp for fp, c in captured.items() if c[2] == 'quadrant'}, sign=sign)


def table(name, data, subset=lambda r: True):
    rows = [r for r in data['rows'] if subset(r)]
    ref, bad, sgn = data['ref'], data['bad'], data['sign']
    variants = list(rows[0]['variants'])
    base = 'today' if 'today' in variants else variants[0]
    feas = [r for r in rows if r['fingerprint'] in ref]
    print(f"\n{name}: {len(rows)} requests, {len(feas)} with a reference, {sum(r['fingerprint'] in bad for r in rows)} "
          "infeasible")
    print('| variant | answered | within 1e-3 | within 1e-2 | above reference by > 1e-3 | answers on infeasible | '
          f'gained / lost within 1e-2 vs {base} | mean / median / p90 ms | IPOPT calls (requests calling) | '
          'Clarabel solves mean |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    def good(r, v):
        x = r['variants'][v]['objective']
        fp = r['fingerprint']
        return x is not None and fp in ref and sgn.get(fp, 1)*(ref[fp]-x) <= 1e-2
    for v in variants:
        got = {r['fingerprint']: r['variants'][v]['objective'] for r in rows}
        gaps = [sgn.get(r['fingerprint'], 1)*(ref[r['fingerprint']]-got[r['fingerprint']]) for r in feas
                if got[r['fingerprint']] is not None]
        s = np.array([r['variants'][v]['seconds'] for r in rows])*1000
        ipopt = [r['variants'][v]['ipopt'] for r in rows]
        print(f"| {v} | {sum(x is not None for x in gaps)} | {sum(g <= 1e-3 for g in gaps)} | "
              f"{sum(g <= 1e-2 for g in gaps)} | {sum(g < -1e-3 for g in gaps)} | "
              f"{sum(got[r['fingerprint']] is not None for r in rows if r['fingerprint'] in bad)} | "
              f"+{sum(good(r, v) and not good(r, base) for r in rows)} / "
              f"-{sum(good(r, base) and not good(r, v) for r in rows)} | "
              f"{s.mean():.0f} / {np.median(s):.0f} / {np.percentile(s, 90):.0f} | {sum(ipopt)} ({sum(i > 0 for i in ipopt)}) | "
              f"{np.mean([r['variants'][v]['clarabel'] for r in rows]):.1f} |")


table('Study (371 alarmed requests)', study)
table('Real11 (482 alarmed requests from an 1800 s pipeline run)', real_set)
if 'today' in real[0]['variants']:
    table('Real11, requests today sends to IPOPT', real_set, lambda r: r['variants']['today']['ipopt'] > 0)
