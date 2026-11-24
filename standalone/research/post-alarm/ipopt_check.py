"""Does IPOPT find a full-strength route at every post-alarm pipeline answer? Per alarmed request: the pipeline
(numerics.post_alarm.run), then for an answer one IPOPT start (2 s, no fallbackYaw) from the answer's own facings:
`full` answers already passed the scalar validator at full strength; `relaxed` answers have ticks up to 1% short.
One JSON row per answered request.
Usage (from standalone/): python -B research/post-alarm/ipopt_check.py <out.jsonl> (--alarmed f.jsonl | --population f.json ...)"""
import importlib.util
import json
import pathlib
import sys

from parkour_solver.numerics import disk, post_alarm, solver

spec = importlib.util.spec_from_file_location('pipeline_variants', pathlib.Path(__file__).with_name('pipeline_variants.py'))
pv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pv)

out = pathlib.Path(sys.argv[1])
for fp, group, problem, candidate, relaxed in pv.requests(sys.argv[2:]):
    result = post_alarm.run(problem, candidate, relaxed, seconds=20., cancel_event=None)
    if result['status'] != 'feasible':
        continue
    answer, confirmation = result['candidate'], result['confirmation']
    yaw = answer['yaw'] if confirmation['acceptedAs'] == 'full' else post_alarm.facings(problem, answer['controls'])
    request = {k: v for k, v in dict(problem, yaw=disk.ipopt_seed(yaw)).items() if k != 'fallbackYaw'}
    ipopt = solver.solve(request, seconds=2., stop_on_feasible=False, python_executable=sys.executable)
    row = dict(fingerprint=fp, set=group, method=confirmation['method'], acceptedAs=confirmation['acceptedAs'],
               objective=answer['objective'], peakShortfall=disk.deficits(problem, answer['controls'])[0]
               if answer.get('controls') else 0., ipoptStatus=ipopt['status'], ipoptTermination=ipopt['termination'],
               ipoptPhase=ipopt['phases'][-1]['status'] if ipopt['phases'] else None,
               ipoptObjective=ipopt['candidate']['objective'] if ipopt['status'] == 'feasible' else None)
    with open(out, 'a') as stream:
        stream.write(json.dumps(row)+'\n')
    print(fp[:12], row['method'], row['acceptedAs'], round(row['objective'], 4), row['ipoptStatus'], row['ipoptPhase'],
          row['ipoptObjective'] and round(row['ipoptObjective'], 4), flush=True)
