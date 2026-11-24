"""Re-run the pipeline arm's 300 s extended case, saving the problems of alarmed confirmations the pipeline left
unanswered (the A/B misses), with what the pipeline returned."""
import json, sys, pathlib
from parkour_solver.numerics import disk
from parkour_solver.numerics.model import fingerprint
from parkour_solver.search.application import run
OUT = pathlib.Path(sys.argv[2]); saved = {}
original = disk.RelaxedDispatch.confirm
def confirm(self, problem, relaxed_candidate, relaxed, **options):
    result = original(self, problem, relaxed_candidate, relaxed, **options)
    if result['status'] != 'feasible' and result.get('termination') == 'post_alarm_none':
        fp = fingerprint(problem)
        saved[fp] = dict(problem=problem, relaxedCandidate=relaxed_candidate, relaxed=relaxed, seconds=options['seconds'])
        (OUT/'misses.json').write_text(json.dumps(saved))
        print('saved', fp[:12], flush=True)
    return result
disk.RelaxedDispatch.confirm = confirm
fixture = json.load(open(sys.argv[1]))
fixture['settings']['seconds'] = 300
import os
result, _ = run(fixture, python_executable=os.environ['PKC_IPOPT_PYTHON'])
print('done', result['termination'], len(saved))
