"""The committed pipeline (25f7fc62) with its acceptance rule (full strength, or every tick at most 1% short) against
full strength only (ACCEPT_SHORT disabled), on each request in turn. Rows as pipeline_variants.py
({fingerprint, set, variants: {name: {objective, method, seconds, clarabel, acceptedAs, ...}}}).
Usage (from standalone/): python -B research/post-alarm/full_only.py <out.jsonl> (--alarmed f.jsonl | --population f.json ...)"""
import importlib.util, json, pathlib, sys, time
from parkour_solver.numerics import post_alarm
spec = importlib.util.spec_from_file_location('pv', pathlib.Path(__file__).with_name('pipeline_variants.py'))
pv = importlib.util.module_from_spec(spec); spec.loader.exec_module(pv)
ACCEPT = post_alarm.ACCEPT_SHORT

def run(problem, candidate, relaxed, accept_short):
    post_alarm.ACCEPT_SHORT = accept_short
    started = time.perf_counter()
    try:
        r = post_alarm.run(problem, candidate, relaxed, seconds=20., cancel_event=None)
    finally:
        post_alarm.ACCEPT_SHORT = ACCEPT
    c = r['confirmation']
    return dict(objective=c['ipoptObjective'], method=c['method'], termination=r['termination'],
                seconds=time.perf_counter()-started, ipopt=0, clarabel=c['clarabelSolves'], acceptedAs=c['acceptedAs'],
                quadrant=c['quadrant'], reversal=c['reversalTick'])

out = pathlib.Path(sys.argv[1]); warm = True
for fp, group, problem, candidate, relaxed in pv.requests(sys.argv[2:]):
    if warm:
        run(problem, candidate, relaxed, ACCEPT); warm = False
    row = dict(fingerprint=fp, set=group, variants={'accept1pct': run(problem, candidate, relaxed, ACCEPT),
                                                    'fullOnly': run(problem, candidate, relaxed, -1.)})
    with open(out, 'a') as s:
        s.write(json.dumps(row)+'\n')
