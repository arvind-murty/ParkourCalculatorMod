"""The push with the objective weight halved after every step that is not full strength (production keeps W0 until the
first full-strength iterate), and with w = 0 (feasibility only); caps 30. Accepted answers vs the best known Real11
answer (seed bench reference: best of any seed and the pipeline)."""
import json, math, sys, time, collections
from parkour_solver.numerics import disk, post_alarm as pa
ref = {}
for l in open('runs/post-alarm/seed-bench/alarmed.jsonl'):
    r = json.loads(l)
    vals = [x['objective'] for s in r['seeds'].values() for x in s['runs'].values() if x['objective'] is not None]
    ref[r['fingerprint']] = vals
def push(pipe, node, start, cap, w0, decay):
    y, w, last_full, best, u = start, w0, None, None, None
    for i in range(cap):
        u = node.step(pipe, y, w)
        if u is None: return best, i, 'step failed'
        acc = pipe.accept(u)
        if acc is not None and (best is None or pipe.better(acc[1]['objective'], best[1]['objective'])): best = acc
        if best is not None and pipe.sense*(node.bound-best[1]['objective']) <= pa.G: return best, i+1, 'G'
        full = disk.deficits(pipe.problem, u)[0] < pa.FULL
        if last_full is not None:
            if full: w = min(w*pa.RAMP, pa.W_MAX); y = [[a+pa.MOMENTUM*(a-b) for a, b in zip(c, d)] for c, d in zip(u, last_full)]; last_full = u
            else: w, y = w/2, last_full
        else:
            if full: last_full = u
            elif decay: w = w/2
            y = u
    return best, cap, 'cap'
rows = [json.loads(l) for l in open('runs/post-alarm/alarmed/alarmed.jsonl')]
rows = [r for r in rows if r['result']['confirmation']['method'] in ('prodSeed', 'polish', 'none')]
stats = collections.defaultdict(list)
for row in rows:
    p, c, rel = row['problem'], row['relaxedCandidate'], row['relaxed']
    sense = 1 if p['objective']['sense'] == 'MAX' else -1
    pipe = pa._Pipeline(p, c, rel, 600., sys.executable, None)
    plain = pa._Node(pipe, pipe.exact, disk_point=(rel['objective'], c['controls']))
    start = plain.central
    if p.get('warmYaw'):
        start = [[m*math.cos(b+math.radians(yy)), m*math.sin(b+math.radians(yy))] for m, b, yy in zip(p['magnitude'], p['base'], p['warmYaw'])]+start[len(p['warmYaw']):]
    known = ref[row['fingerprint']] + ([row['objective']] if row['objective'] is not None else [])
    best_known = max(known, key=lambda v: sense*v) if known else None
    line = [row['fingerprint'][:12], row['result']['confirmation']['method'], None if best_known is None else round(best_known, 4)]
    for name, w0, decay in (('decay', pa.W0, True), ('w0', 0., False)):
        t = time.perf_counter(); best, steps, why = push(pipe, plain, start, 30, w0, decay); s = time.perf_counter()-t
        ans = best and best[1]['objective']
        gap = None if ans is None or best_known is None else sense*(best_known-ans)
        stats[name].append((ans is not None, best_known is not None, gap, s))
        line.append((name, why, steps, ans and round(ans, 4), gap and round(gap, 4)))
    print(*line, flush=True)
for name, v in stats.items():
    known = [x for x in v if x[1]]
    print(name, 'answered', sum(x[0] for x in v), 'of', len(v), '| on', len(known), 'with a known answer: answered', sum(x[0] for x in known),
          'within 1e-2', sum(x[2] is not None and x[2] <= 1e-2 for x in known), '| answered where nothing known', sum(x[0] and not x[1] for x in v),
          '| mean s %.3f' % (sum(x[3] for x in v)/len(v)))
