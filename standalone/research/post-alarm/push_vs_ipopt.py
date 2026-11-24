"""Can full-strength pushes replace IPOPT on every relaxed answer, alarmed or not? Per request: (1) the relaxed point
itself when every tick is full strength and `validate` passes (no solve); else (2) numerics.post_alarm.run with
full-strength acceptance only (ACCEPT_SHORT disabled); and, as the reference, (3) IPOPT as a frame-end re-solve runs it:
the relaxed facings turned by ipopt_seed, the request's fallbackYaw kept, 5 s. One JSON row per request.
Usage (from standalone/): python -B research/post-alarm/push_vs_ipopt.py <out.jsonl> (--alarmed f.jsonl | --population f.json ...)"""
import importlib.util
import json
import pathlib
import sys
import time

from parkour_solver.numerics import disk, post_alarm, solver
from parkour_solver.validation.numerical import validate

spec = importlib.util.spec_from_file_location('pipeline_variants', pathlib.Path(__file__).with_name('pipeline_variants.py'))
pv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pv)
ACCEPT = post_alarm.ACCEPT_SHORT


def full_strength(problem, candidate, relaxed):
    started = time.perf_counter()
    if disk.deficits(problem, candidate['controls'])[0] < post_alarm.FULL:
        checked = validate(problem, post_alarm.facings(problem, candidate['controls']))
        if checked['feasible']:
            return dict(method='direct', objective=checked['objective'], clarabel=0, seconds=time.perf_counter()-started)
    post_alarm.ACCEPT_SHORT = -1.
    try:
        r = post_alarm.run(problem, candidate, relaxed, seconds=20., cancel_event=None)
    finally:
        post_alarm.ACCEPT_SHORT = ACCEPT
    c = r['confirmation']
    return dict(method=c['method'], objective=c['ipoptObjective'], termination=r['termination'],
                clarabel=c['clarabelSolves'], seconds=time.perf_counter()-started)


def ipopt(problem, candidate):
    started = time.perf_counter()
    r = solver.solve(dict(problem, yaw=disk.ipopt_seed(candidate['yaw'])), seconds=5., stop_on_feasible=False,
                     python_executable=sys.executable)
    return dict(status=r['status'], termination=r['termination'], seconds=time.perf_counter()-started,
                objective=r['candidate']['objective'] if r['status'] == 'feasible' else None,
                phases=[p['status'] for p in r['phases']])


def main():
    out = pathlib.Path(sys.argv[1])
    done = {json.loads(l)['fingerprint'] for l in open(out)} if out.exists() else set()
    for fp, group, problem, candidate, relaxed in pv.requests(sys.argv[2:]):
        if fp in done or relaxed is None:
            continue
        row = dict(fingerprint=fp, set=group, sense=problem['objective']['sense'], ticks=problem['ticks'],
                   alarmTicks=relaxed['alarmTicks'], peakShortfall=relaxed['peakRelativeDeficit'],
                   bound=relaxed['objective'], push=full_strength(problem, candidate, relaxed), ipopt=ipopt(problem, candidate))
        with open(out, 'a') as stream:
            stream.write(json.dumps(row)+'\n')
        print(fp[:12], row['push']['method'], row['push']['objective'], row['ipopt']['status'], row['ipopt']['objective'],
              flush=True)


if __name__ == '__main__':
    main()
