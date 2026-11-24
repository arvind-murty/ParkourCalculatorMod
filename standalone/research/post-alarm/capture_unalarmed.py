"""Re-run a Real11 case and append every relaxed answer the dispatch returns without an alarm (problem, the validated
disk point, its disk record) to unalarmed.jsonl, once per fingerprint (push-vs-IPOPT bench on unalarmed requests).
Usage (from standalone/): python -B research/post-alarm/capture_unalarmed.py <case.json> <out dir> <seconds>"""
import json, os, sys, pathlib
from parkour_solver.numerics import disk
from parkour_solver.numerics.model import fingerprint
from parkour_solver.search.application import run
OUT, seen = pathlib.Path(sys.argv[2]), set()
original = disk.RelaxedDispatch._dispatch
def dispatch(self, problem, **options):
    result = original(self, problem, **options)
    relaxed = result.get('disk') or {}
    candidate = result.get('candidate')
    if (result['status'] == 'feasible' and candidate is not None and candidate.get('model') == 'relaxed_disk'
            and relaxed.get('alarmTicks') == [] and 'objective' in relaxed):
        fp = fingerprint(problem)
        if fp not in seen:
            seen.add(fp)
            with open(OUT/'unalarmed.jsonl', 'a') as stream:
                stream.write(json.dumps(dict(fingerprint=fp, problem=problem, relaxedCandidate=candidate, relaxed=relaxed)) + '\n')
    return result
disk.RelaxedDispatch._dispatch = dispatch
fixture = json.load(open(sys.argv[1])); fixture['settings']['seconds'] = float(sys.argv[3])
result, _ = run(fixture, python_executable=os.environ['PKC_IPOPT_PYTHON'])
print('done', result['termination'], len(seen), flush=True)
