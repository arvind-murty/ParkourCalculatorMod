"""Compare run_real11_ab.sh's runs: per run the final summary line (ticks, objective, termination, search seconds, work
counters, replays); the parity harness (exact, and tolerant abs 1e-6 / rel 1e-9) per A/B pair and for A1/A2, B1/B2;
per bookkeeping run the confirmations in requests.json (count, seconds, outcomes). Run from standalone/.
Usage: compare_real11_ab.py <runs dir> <parity harness .py (tools/profiling/parity.py at 364f5699)>"""
import collections
import importlib.util
import json
import pathlib
import statistics
import sys

from parkour_solver.io.checkpoint_store import read_report

OUT = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location('parity', sys.argv[2])
parity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parity)
KEYS = ('solverCalls', 'conicCandidates', 'workerLaunches', 'confirmations', 'confirmationIpoptCalls',
        'confirmationClarabelSolves', 'materialGaps', 'finalizationIpoptCalls')


def summary(name):
    lines = [l for l in (OUT/f'{name}.log').read_text().splitlines() if l.startswith('{') and '"searchSeconds"' in l]
    return json.loads(lines[-1]) if lines else None


runs = sorted(p.name for p in OUT.iterdir() if p.is_dir() and (p/'report.json.gz').exists())
print('| run | ticks | objective | termination | search s | ' + ' | '.join(KEYS) + ' | replays passed |')
print('|---|---|---|---|---|' + '---|'*len(KEYS) + '---|')
for r in runs:
    s = summary(r)
    if s:
        print(f"| {r} | {s.get('ticks')} | {s.get('objective')} | {s.get('termination')} | {s['searchSeconds']:.2f} | "
              + ' | '.join(str(s['work'].get(k)) for k in KEYS) + f" | {s.get('allReplaysPassed')} |")
print()
pairs = [(f'fixed-A{i}', f'fixed-B{i}') for i in (1, 2, 3)] + [('fixed-A1', 'fixed-A2'), ('fixed-B1', 'fixed-B2'),
                                                               ('extended-A', 'extended-B'), ('long-A', 'long-B')]
for a, b in pairs:
    if a not in runs or b not in runs:
        continue
    ra, rb = read_report(OUT/a/'report.json.gz'), read_report(OUT/b/'report.json.gz')
    exact = parity.compare_routes(ra, rb)
    tolerant = parity.compare_routes(ra, rb, rel_tol=1e-9, abs_tol=1e-6)
    verdict = lambda v: 'match' if v['matches'] else '%d differences, first transition %s' % (
        len(v['differences']), v['firstTransitionDifference'])
    print(f"parity {a} vs {b}: transitions {exact['transitions']}; exact {verdict(exact)}; tolerant {verdict(tolerant)}")
    for d in tolerant['differences'][:3]:
        print('   ', json.dumps(d)[:400])
print()
for r in runs:
    path = OUT/r/'requests.json'
    if not path.exists():
        continue
    rows = json.loads(path.read_text())
    confirmed = [x for x in rows if x.get('confirmation')]
    seconds = [x['seconds'] for x in confirmed if x.get('seconds') is not None]
    fresh = [x for x in confirmed if not x['confirmation']['cached']]
    outcomes = collections.Counter((x['confirmation'].get('method', 'ipopt'), x['status']) for x in fresh)
    print(f"{r}: {len(rows)} requests, {len(confirmed)} confirmations ({len(fresh)} not cached), confirmation seconds "
          + (f"total {sum(seconds):.2f} median {statistics.median(seconds)*1000:.1f} ms mean "
             f"{statistics.mean(seconds)*1000:.1f} ms" if seconds else 'none')
          + f"; outcomes {dict(outcomes)}")
