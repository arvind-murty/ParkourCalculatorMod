"""Re-run the pipeline arm's 1800 s case, appending every fresh (not cached) alarmed confirmation to alarmed.jsonl: the
problem, the dispatch's relaxed candidate and record, the budget and what the pipeline returned (seed-bench test set 2).
Usage (from standalone/): python -B research/post-alarm/capture_alarmed.py <long-B.json> <out dir>"""
import json, os, sys, pathlib
from parkour_solver.numerics import disk
from parkour_solver.numerics.model import fingerprint
from parkour_solver.search.application import run
OUT = pathlib.Path(sys.argv[2]); count = 0
original = disk.RelaxedDispatch.confirm
def confirm(self, problem, relaxed_candidate, relaxed, **options):
    global count
    result = original(self, problem, relaxed_candidate, relaxed, **options)
    confirmation = result.get('confirmation')
    if confirmation and not confirmation['cached']:
        row = dict(fingerprint=fingerprint(problem), problem=problem, relaxedCandidate=relaxed_candidate, relaxed=relaxed,
                   seconds=options['seconds'], result={k: result.get(k) for k in ('status', 'termination', 'confirmation')},
                   objective=None if result.get('candidate') is None else result['candidate']['objective'])
        with open(OUT/'alarmed.jsonl', 'a') as stream:
            stream.write(json.dumps(row)+'\n')
        count += 1
        print('saved', count, row['fingerprint'][:12], confirmation['method'], row['objective'], flush=True)
    return result
disk.RelaxedDispatch.confirm = confirm
fixture = json.load(open(sys.argv[1])); fixture['settings']['seconds'] = 1800
result, _ = run(fixture, python_executable=os.environ['PKC_IPOPT_PYTHON'])
print('done', result['termination'], count, flush=True)
