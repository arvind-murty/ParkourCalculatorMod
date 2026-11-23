"""For the pipeline answers IPOPT did not confirm (ipopt_check.py rows with status unknown): the answer's scalar
validator violation, IPOPT (2 s) from the answer's exact facings without the 1e-4 rad tilt, and whether IPOPT found a
feasible route on the request from any start of the seed bench (Real11) or the study's labels (study).
Usage (from standalone/): ipopt_check_failures.py <ipopt-check dir> <seed-bench alarmed.jsonl> <alarmed.jsonl>
  <population.json> ..."""
import json, sys, importlib.util, pathlib
from parkour_solver.numerics import disk, post_alarm, solver
from parkour_solver.validation.numerical import validate, validate_controls
spec = importlib.util.spec_from_file_location('pv', pathlib.Path(__file__).with_name('pipeline_variants.py'))
pv = importlib.util.module_from_spec(spec); spec.loader.exec_module(pv)
d = pathlib.Path(sys.argv[1])
failed = {json.loads(l)['fingerprint']: json.loads(l) for f in ('real11.jsonl', 'study.jsonl') for l in open(d/f)
          if json.loads(l)['ipoptStatus'] != 'feasible'}
bench = {}
for l in open(sys.argv[2]):
    r = json.loads(l)
    bench[r['fingerprint']] = [x['objective'] for s in r['seeds'].values() for x in s['runs'].values() if x['objective'] is not None]
sources = [('--alarmed', [sys.argv[3]]), ('--population', sys.argv[4:])]
for flag, paths in sources:
    for fp, group, problem, candidate, relaxed in pv.requests([flag]+paths):
        if fp not in failed:
            continue
        r = post_alarm.run(problem, candidate, relaxed, seconds=20., cancel_event=None)
        a, kind = r['candidate'], r['confirmation']['acceptedAs']
        yaw = a['yaw'] if kind == 'full' else post_alarm.facings(problem, a['controls'])
        v = validate(problem, yaw)
        request = {k: x for k, x in dict(problem, yaw=yaw).items() if k != 'fallbackYaw'}
        i = solver.solve(request, seconds=2., stop_on_feasible=False, python_executable=sys.executable)
        other = bench.get(fp)
        print(fp[:12], group, kind, r['confirmation']['method'], '| validate at full strength: feasible', v['feasible'],
              'violation %.2e' % v['violation'], v['worstConstraint'], '| IPOPT untilted', i['status'],
              i['phases'][-1]['status'] if i['phases'] else i['termination'],
              '| IPOPT feasible from another start:', None if other is None else (len(other) > 0), flush=True)
