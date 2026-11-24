"""Frame-end hybrid: the full-strength answer of push_vs_ipopt.py's push (the relaxed point itself when already full
strength, else numerics.post_alarm.run with full-strength acceptance), then one IPOPT start (5 s, no fallbackYaw) from
that answer's exact facings. Joined with push_vs_ipopt.py's rows (IPOPT from the relaxed facings) per request.
Usage (from standalone/): push_then_ipopt.py <out.jsonl> <push_vs_ipopt rows .jsonl> (--alarmed f.jsonl | --population f.json ...)"""
import importlib.util, json, pathlib, sys, time
from parkour_solver.numerics import disk, post_alarm, solver
from parkour_solver.validation.numerical import validate
here = pathlib.Path(__file__)
spec = importlib.util.spec_from_file_location('pv', here.with_name('pipeline_variants.py')); pv = importlib.util.module_from_spec(spec); spec.loader.exec_module(pv)
ACCEPT = post_alarm.ACCEPT_SHORT
prior = {json.loads(l)['fingerprint']: json.loads(l) for l in open(sys.argv[2])}
out = pathlib.Path(sys.argv[1])
for fp, group, problem, candidate, relaxed in pv.requests(sys.argv[3:]):
    if relaxed is None or fp not in prior:
        continue
    yaw = None
    if disk.deficits(problem, candidate['controls'])[0] < post_alarm.FULL:
        checked = validate(problem, post_alarm.facings(problem, candidate['controls']))
        if checked['feasible']:
            yaw, push = checked['yaw'], checked['objective']
    if yaw is None:
        post_alarm.ACCEPT_SHORT = -1.
        try:
            r = post_alarm.run(problem, candidate, relaxed, seconds=20., cancel_event=None)
        finally:
            post_alarm.ACCEPT_SHORT = ACCEPT
        if r['status'] == 'feasible':
            yaw, push = r['candidate']['yaw'], r['candidate']['objective']
    row = dict(fingerprint=fp, set=group, sense=problem['objective']['sense'], push=None, polish=None,
               ipoptFromRelaxed=prior[fp]['ipopt']['objective'] if 'ipopt' in prior[fp] else prior[fp].get('ipoptObjective'))
    if yaw is not None:
        started = time.perf_counter()
        request = {k: v for k, v in dict(problem, yaw=yaw).items() if k != 'fallbackYaw'}
        p = solver.solve(request, seconds=5., stop_on_feasible=False, python_executable=sys.executable)
        row.update(push=push, polish=p['candidate']['objective'] if p['status'] == 'feasible' else None,
                   polishSeconds=time.perf_counter()-started, polishPhases=[x['status'] for x in p['phases']])
    with open(out, 'a') as s:
        s.write(json.dumps(row)+'\n')
