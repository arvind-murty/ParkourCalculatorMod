"""Implemented post_alarm.run vs the benchmarked 'lean-decayprobe' rows: method and objective per request."""
import json, sys, importlib.util, collections, time
from parkour_solver.numerics import post_alarm
spec = importlib.util.spec_from_file_location('pv', 'research/post-alarm/pipeline_variants.py')
pv = importlib.util.module_from_spec(spec); spec.loader.exec_module(pv)
bench = {json.loads(l)['fingerprint']: json.loads(l)['variants']['lean-decayprobe'] for l in open(sys.argv[1])}
NAMES = {'decayFallback': 'decay'}
c = collections.Counter(); diffs = []; seconds = []
for fp, group, problem, candidate, relaxed in pv.requests(sys.argv[2:]):
    t = time.perf_counter()
    r = post_alarm.run(problem, candidate, relaxed, seconds=20., cancel_event=None); seconds.append(time.perf_counter()-t)
    m, o = r['confirmation']['method'], r['confirmation']['ipoptObjective']
    b = bench[fp]; bm = NAMES.get(b['method'], b['method'])
    same = m == bm and (o is None and b['objective'] is None or o is not None and b['objective'] is not None and abs(o-b['objective']) <= 1e-9)
    c['same' if same else 'different'] += 1
    if not same:
        diffs.append((fp[:12], bm, b['objective'], m, o, r['termination']))
print(dict(c), 'mean ms %.1f' % (1000*sum(seconds)/len(seconds)))
for d in diffs[:40]: print('  ', d)
