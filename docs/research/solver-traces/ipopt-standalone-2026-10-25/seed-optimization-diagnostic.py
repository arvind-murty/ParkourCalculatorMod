import argparse
import gzip
import json
import os
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(root))

from standalone.ipopt import solver
from standalone.ipopt.model import prepare, validate


def read(path):
    with gzip.open(path, 'rt') as stream:
        return json.load(stream)


parser = argparse.ArgumentParser()
parser.add_argument('--python', required=True)
args = parser.parse_args()
directory = pathlib.Path(__file__).resolve().parent
request = read(root / 'standalone/ipopt/fixtures/real11-400.json.gz')
no_inertia = read(directory / 'real11-400-no-inertia-result.json.gz')['candidate']
good = read(directory / 'real11-400-result.json.gz')['candidate']
warm = read(directory / 'real11-400-warm-inertia-result.json.gz')['candidate']
threshold = request['threshold']


def pattern(candidate):
    return [[0 if abs(v) < threshold else 1 if v > 0 else -1 for v in row]
            for row in candidate['velocity'][:-1]]


cases = [
    ('cold', request['yaw'], None),
    ('no-inertia-seed', no_inertia['yaw'], None),
    ('no-inertia-seed-good-pattern', no_inertia['yaw'], pattern(good)),
    ('warm-incumbent-own-pattern', warm['yaw'], pattern(warm)),
    ('good-incumbent-own-pattern', good['yaw'], pattern(good)),
]
solver._command = lambda _: [args.python, '-u', str(directory / 'seed-optimization-diagnostic-worker.py')]
summaries = []
for name, yaw, fixed in cases:
    problem = dict(request, yaw=yaw)
    if fixed is not None:
        problem['inertiaPattern'] = fixed
    trace = directory / ('seed-diagnostic-' + name + '.jsonl')
    trace.write_text('')
    os.environ['PKC_SEED_DIAGNOSTIC_TRACE'] = str(trace)
    result = solver.solve(problem, seconds=10)
    if result['candidate'] is not None:
        replay = validate(prepare(problem), result['candidate']['yaw'])
        assert replay['feasible']
        assert replay['positions'] == result['candidate']['positions']
        assert replay['velocity'] == result['candidate']['velocity']
    with gzip.open(directory / ('seed-diagnostic-' + name + '-result.json.gz'), 'wt') as stream:
        json.dump(result, stream, allow_nan=False)
    brief = {k: v for k, v in result.items() if k not in ('candidate', 'problem')}
    brief['case'] = name
    brief['objective'] = result['candidate']['objective'] if result['candidate'] else None
    summaries.append(brief)
    print(json.dumps(brief), flush=True)
with (directory / 'seed-diagnostic-summary.json').open('w') as stream:
    json.dump(summaries, stream, indent=2)
