"""Why the push leaves Real11 alarmed requests to IPOPT: on each captured request whose pipeline reached IPOPT, replay
the production push (same start, weights, stops) recording why it stopped, then the same push with the stall stop off
and caps 30 and 100 (Clarabel only). Per iterate: peak shortfall, and for a full-strength iterate the validator verdict."""
import json, math, sys, time, collections
from parkour_solver.numerics import disk, post_alarm as pa
from parkour_solver.validation.numerical import validate

def push(pipe, node, start, cap, stall=True):
    y, w, last_full, best, history, u = start, pa.W0, None, None, [], None
    log = []
    for i in range(cap):
        point = node.step(pipe, y, w)
        if point is None:
            return best, log, 'step failed', i
        u = point
        peak = disk.deficits(pipe.problem, u)[0]
        acc = pipe.accept(u)
        entry = dict(peak=peak, accepted=acc and acc[0])
        if peak < pa.FULL and not acc:
            v = validate(pipe.problem, pa.facings(pipe.problem, u)); entry.update(violation=v['violation'], worst=v['worstConstraint'])
        log.append(entry)
        if acc is not None and (best is None or pipe.better(acc[1]['objective'], best[1]['objective'])):
            best = acc
        history.append(None if best is None else best[1]['objective'])
        if best is not None and pipe.sense*(node.bound-best[1]['objective']) <= pa.G:
            return best, log, 'within G of bound', i+1
        if stall and best is not None and len(history) > 3 and history[-4] is not None and pipe.sense*(best[1]['objective']-history[-4]) < pa.G/10:
            return best, log, 'stall', i+1
        full = peak < pa.FULL
        if last_full is not None:
            if full:
                w = min(w*pa.RAMP, pa.W_MAX); y = [[a+pa.MOMENTUM*(a-b) for a, b in zip(c, d)] for c, d in zip(u, last_full)]; last_full = u
            else:
                w, y = w/2, last_full
        else:
            if full: last_full = u
            y = u
    return best, log, 'cap', cap

out = []
for line in open(sys.argv[1]):
    row = json.loads(line)
    if row['result']['confirmation']['method'] not in ('prodSeed', 'polish', 'none'):
        continue
    p, c, rel = row['problem'], row['relaxedCandidate'], row['relaxed']
    pipe = pa._Pipeline(p, c, rel, 600., sys.executable, None)
    plain = pa._Node(pipe, pipe.exact, disk_point=(rel['objective'], c['controls']))
    start = plain.central
    if p.get('warmYaw'):
        start = [[m*math.cos(b+math.radians(yy)), m*math.sin(b+math.radians(yy))] for m, b, yy in zip(p['magnitude'], p['base'], p['warmYaw'])]+start[len(p['warmYaw']):]
    rec = dict(fp=row['fingerprint'], method=row['result']['confirmation']['method'], captured=row['objective'], bound=rel['objective'])
    for name, cap, stall in (('prod', pa.PUSH_CAP, True), ('cap30', 30, False), ('cap100', 100, False)):
        t = time.perf_counter()
        best, log, reason, steps = push(pipe, plain, start, cap, stall)
        fulls = [e for e in log if e['peak'] < pa.FULL]
        rec[name] = dict(answer=best and best[1]['objective'], reason=reason, steps=steps, seconds=time.perf_counter()-t,
                         minPeak=min((e['peak'] for e in log), default=None), lastPeak=log[-1]['peak'] if log else None,
                         fullIterates=len(fulls), fullRejected=collections.Counter((e.get('worst') or '?').split('@')[0] for e in fulls if not e['accepted']))
    out.append(rec)
    print(rec['fp'][:12], rec['method'], {k: (rec[k]['reason'], rec[k]['steps'], rec[k]['answer'] and round(rec[k]['answer'], 4), round(rec[k]['minPeak'] or 0, 4), rec[k]['fullIterates']) for k in ('prod', 'cap30', 'cap100')}, flush=True)
json.dump(out, open(sys.argv[2], 'w'))
