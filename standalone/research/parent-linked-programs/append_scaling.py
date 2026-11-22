"""Per-call (parent ticks, new ticks, seconds) for append_choice and (ticks, seconds) for program_physics_key.

Usage: python -B append_scaling.py OUT.json <tools.profiling.run args...>
"""
import atexit, json, runpy, sys, time

OUT = sys.argv.pop(1)
from parkour_solver.movement import program as module
rows = dict(append=[], key=[])
original_append, original_key = module.append_choice, module.program_physics_key


def append_choice(program, choice, **kw):
    t = time.perf_counter()
    result = original_append(program, choice, **kw)
    rows['append'].append((program['profile']['airtime'], choice['profile']['airtime'], time.perf_counter()-t, result is not None))
    return result


def program_physics_key(program):
    t = time.perf_counter()
    result = original_key(program)
    rows['key'].append((program['profile']['airtime'], time.perf_counter()-t))
    return result


for name, other in list(sys.modules.items()):
    if name.startswith('parkour_solver'):
        if getattr(other, 'append_choice', None) is original_append:
            other.append_choice = append_choice
        if getattr(other, 'program_physics_key', None) is original_key:
            other.program_physics_key = program_physics_key
atexit.register(lambda: json.dump(rows, open(OUT, 'w')))
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
