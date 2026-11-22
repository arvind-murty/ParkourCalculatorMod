"""Outermost copy.deepcopy time by calling site, plus node-memo reads (probe source), then run tools.profiling.run.

Usage: python -B copy_sites.py OUT.json <tools.profiling.run args...>
"""
import atexit, builtins, collections, copy, json, runpy, sys, time

OUT = sys.argv.pop(1)
original = copy.deepcopy
stats = collections.defaultdict(lambda: [0, 0.])
depth = [0]


def deepcopy(x, memo=None, _nil=[]):
    if depth[0]:
        return original(x, memo, _nil)
    f = sys._getframe(1)
    site = f"{f.f_code.co_filename.split('parkour_solver/')[-1]}:{f.f_lineno}:{f.f_code.co_name}"
    depth[0] += 1
    t = time.perf_counter()
    try:
        return original(x, memo, _nil)
    finally:
        depth[0] -= 1
        row = stats[site]; row[0] += 1; row[1] += time.perf_counter()-t


copy.deepcopy = deepcopy


def dump():
    rows = {k: dict(calls=v[0], seconds=round(v[1], 3)) for k, v in sorted(stats.items(), key=lambda kv: -kv[1][1])}
    memo = builtins.__dict__.get('_node_memo', [])
    json.dump(dict(nodeMemo=collections.Counter(map(str, memo)), deepcopy=rows), open(OUT, 'w'), indent=1)


atexit.register(dump)
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
