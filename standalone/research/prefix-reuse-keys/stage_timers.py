"""Direct inclusive timers (outermost call per name) on the per-child pipeline, then run tools.profiling.run.

Usage: python -B stage_timers.py OUT.json <tools.profiling.run args...>
"""
import atexit, functools, importlib, json, runpy, sys, time

OUT = sys.argv.pop(1)
TARGETS = [
    ('parkour_solver.movement.program', ['append_choice', 'profile_from_trace', 'compile_program', '_events',
                                         'program_physics_key', 'truncate_program', 'movement_choice']),
    ('parkour_solver.movement.vertical_profiles', ['trace_ladder_schedule']),
    ('parkour_solver.movement.contact_combinations', ['merge_contacts', 'validate_contact_events']),
    ('parkour_solver.movement.ladder.ceiling', ['own_ceiling_contacts']),
    ('parkour_solver.movement.units', ['compile_units', 'profile_program', 'shifted_unit']),
    ('parkour_solver.search.support_profiles', ['compile_block_path']),
    ('parkour_solver.scene.route', ['prepare_route', 'schedule']),
    ('parkour_solver.validation.vertical', ['validate_vertical_schedule']),
    ('parkour_solver.reachability.intervals', ['reachable_envelopes', 'interaction_reachability']),
    ('parkour_solver.reachability.service', ['interaction_context_key', 'query_interaction']),
    ('parkour_solver.search.identity', ['digest']),
    ('parkour_solver.bridge.compiler', ['coefficient_request']),
    ('parkour_solver.numerics.disk', ['screen']),
]
METHODS = [('parkour_solver.reachability.service', 'InteractionGate',
            ['prepare', 'analyze_model', 'required', 'sequence', 'check_program', 'query', 'relaxation', 'condition'])]
stats, depth = {}, {}


def timed(name, original):
    @functools.wraps(original)
    def wrapper(*a, **k):
        outer = depth.get(name, 0) == 0
        depth[name] = depth.get(name, 0)+1
        t = time.perf_counter()
        try:
            return original(*a, **k)
        finally:
            depth[name] -= 1
            if outer:
                row = stats.setdefault(name, [0, 0.])
                row[0] += 1
                row[1] += time.perf_counter()-t
    return wrapper


for module_name, names in TARGETS:
    module = importlib.import_module(module_name)
    for name in names:
        original = getattr(module, name)
        wrapped = timed(module_name.split('.')[-1]+'.'+name, original)
        for other_name, other in list(sys.modules.items()):
            if other_name.startswith('parkour_solver') and getattr(other, name, None) is original:
                setattr(other, name, wrapped)
for module_name, cls_name, names in METHODS:
    cls = getattr(importlib.import_module(module_name), cls_name)
    for name in names:
        setattr(cls, name, timed(cls_name+'.'+name, getattr(cls, name)))
# Compiler coefficient compile (Python bridge) is a bound method on the instance's class.
from parkour_solver.bridge.python_compiler import PythonCompiler
PythonCompiler.compile = timed('PythonCompiler.compile', PythonCompiler.compile)


def dump():
    rows = {k: dict(calls=v[0], seconds=round(v[1], 3)) for k, v in sorted(stats.items(), key=lambda kv: -kv[1][1])}
    json.dump(rows, open(OUT, 'w'), indent=1)


atexit.register(dump)
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
