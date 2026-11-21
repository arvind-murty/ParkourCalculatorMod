"""Inclusive timers (outermost call per name) on checkpoint internals, then run tools.profiling.run.

Usage: python -B checkpoint_timers.py OUT.json <tools.profiling.run args...>
"""
import atexit, functools, json, runpy, sys, time

OUT = sys.argv.pop(1)
stats, depth, sizes = {}, {}, []


def timed(name, original):
    @functools.wraps(original)
    def wrapper(*a, **k):
        outer = depth.get(name, 0) == 0
        depth[name] = depth.get(name, 0)+1
        t = time.perf_counter()
        try:
            result = original(*a, **k)
            if name == 'CheckpointStore.archive' and outer:
                sizes.append(result[1])
            return result
        finally:
            depth[name] -= 1
            if outer:
                row = stats.setdefault(name, [0, 0.])
                row[0] += 1
                row[1] += time.perf_counter()-t
    return wrapper


from parkour_solver.io import checkpoint_store, search_checkpoint
for name in ('archive', 'publish', 'patch', 'compact'):
    setattr(checkpoint_store.CheckpointStore, name, timed('CheckpointStore.'+name, getattr(checkpoint_store.CheckpointStore, name)))
for name in ('__call__', 'state', 'repair_progress', 'finish'):
    setattr(search_checkpoint.SearchCheckpoint, name, timed('SearchCheckpoint.'+name, getattr(search_checkpoint.SearchCheckpoint, name)))
if hasattr(search_checkpoint, 'identity_paths'):  # removed once full saves collect alias paths themselves
    search_checkpoint.identity_paths = timed('identity_paths', search_checkpoint.identity_paths)


def dump():
    rows = {k: dict(calls=v[0], seconds=round(v[1], 3)) for k, v in sorted(stats.items(), key=lambda kv: -kv[1][1])}
    rows['archiveSizes'] = sizes
    json.dump(rows, open(OUT, 'w'), indent=1)


atexit.register(dump)
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
