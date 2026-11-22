"""Every append_choice call in a real run is also computed by the previous commit's implementation and compared.

Usage: python -B append_parity.py REF_PROGRAM.py OUT.json <tools.profiling.run args...>
"""
import atexit, collections, importlib.util, json, runpy, sys

REF, OUT = sys.argv.pop(1), sys.argv.pop(1)
spec = importlib.util.spec_from_file_location('ref_program', REF)
ref = importlib.util.module_from_spec(spec); spec.loader.exec_module(ref)
from parkour_solver.movement import program as module
original = module.append_choice
stats = collections.Counter(); first = {}


def append_choice(program, choice, **kw):
    try:
        expected = ref.append_choice(program, choice, **kw)
    except ValueError as error:
        expected = ('raised', str(error))
    try:
        result = original(program, choice, **kw)
    except ValueError as error:
        result = ('raised', str(error))
    path = 'rebuild' if program['ladderEvents'] or choice.get('ladderEvents') or any(program['profile']['ladderTicks']) \
        or any(choice['profile']['ladderTicks']) or program['trace'] is None else 'extend'
    same = result == expected
    stats[f'{path}:{"same" if same else "different"}:{"none" if result is None else "raised" if isinstance(result, tuple) else "program"}'] += 1
    if not same and path not in first:
        diffs = sorted(k for k in set(result or {}) | set(expected or {}) if (result or {}).get(k) != (expected or {}).get(k)) \
            if isinstance(result, dict) and isinstance(expected, dict) else [repr(result)[:200], repr(expected)[:200]]
        first[path] = diffs
    if isinstance(result, tuple):
        raise ValueError(result[1])
    return result


for name, other in list(sys.modules.items()):
    if name.startswith('parkour_solver') and getattr(other, 'append_choice', None) is original:
        other.append_choice = append_choice
atexit.register(lambda: json.dump(dict(stats=stats, firstDifference=first), open(OUT, 'w'), indent=1))
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
