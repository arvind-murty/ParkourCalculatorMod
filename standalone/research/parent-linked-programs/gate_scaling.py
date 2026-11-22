"""Per-call (program ticks, seconds) for the gate's per-child stages.

Usage: python -B gate_scaling.py OUT.json <tools.profiling.run args...>
"""
import atexit, collections, functools, importlib, json, runpy, sys, time

OUT = sys.argv.pop(1)
rows, current = collections.defaultdict(list), [None]
TARGETS = [('parkour_solver.movement.program', 'compile_program'), ('parkour_solver.scene.route', 'prepare_route'),
           ('parkour_solver.scene.route', 'schedule'), ('parkour_solver.validation.vertical', 'validate_vertical_schedule'),
           ('parkour_solver.reachability.intervals', 'reachable_envelopes'), ('parkour_solver.bridge.compiler', 'coefficient_request')]


def timed(name, original):
    @functools.wraps(original)
    def wrapper(*a, **k):
        t = time.perf_counter()
        try:
            return original(*a, **k)
        finally:
            if current[0] is not None:
                rows[name].append((current[0], time.perf_counter()-t))
    return wrapper


for module_name, name in TARGETS:
    module = importlib.import_module(module_name)
    original = getattr(module, name)
    wrapped = timed(name, original)
    for other in list(sys.modules.values()):
        if getattr(other, '__name__', '').startswith('parkour_solver') and getattr(other, name, None) is original:
            setattr(other, name, wrapped)
from parkour_solver.reachability.service import InteractionGate
for method, ticks in (('prepare', lambda a, k: a[1]['profile']['airtime']), ('analyze_model', lambda a, k: a[2]['ticks'])):
    original = getattr(InteractionGate, method)
    def make(original, ticks, method):
        def wrapper(*a, **k):
            outer, current[0] = current[0], ticks(a, k)
            t = time.perf_counter()
            try:
                return original(*a, **k)
            finally:
                rows['InteractionGate.'+method].append((current[0], time.perf_counter()-t))
                current[0] = outer
        return wrapper
    setattr(InteractionGate, method, make(original, ticks, method))
atexit.register(lambda: json.dump(rows, open(OUT, 'w')))
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
