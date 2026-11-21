"""Hash every container inside each program append_choice returns (profile, trace, movementUnits, ladderEvents,
initial) when it is created; at exit report which ones changed afterwards, by field path.

Usage: python -B mutation_audit.py OUT.json <tools.profiling.run args...>
"""
import atexit, collections, json, re, runpy, sys

OUT = sys.argv.pop(1)
from parkour_solver.movement import program as module
original = module.append_choice
registry = {}   # id -> (object, json, path)


def norm(path):
    return '.'.join('#' if isinstance(p, int) else str(p) for p in path)


def register(value, path):
    if isinstance(value, (dict, list)):
        if id(value) not in registry:
            registry[id(value)] = (value, json.dumps(value, sort_keys=True), norm(path))
        for k, v in (value.items() if isinstance(value, dict) else enumerate(value)):
            register(v, path+(k,))


def append_choice(program, choice, **kw):
    result = original(program, choice, **kw)
    if result is not None:
        for key in ('profile', 'trace', 'movementUnits', 'ladderEvents', 'initial'):
            register(result.get(key), (key,))
    return result


def report():
    changed = collections.Counter()
    examples = {}
    for value, before, path in registry.values():
        if json.dumps(value, sort_keys=True) != before:
            changed[path] += 1
            examples.setdefault(path, dict(before=before[:300], after=json.dumps(value, sort_keys=True)[:300]))
    json.dump(dict(checked=len(registry), changed=changed, examples=examples), open(OUT, 'w'), indent=1)


for name, other in list(sys.modules.items()):
    if name.startswith('parkour_solver') and getattr(other, 'append_choice', None) is original:
        other.append_choice = append_choice
atexit.register(report)
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
