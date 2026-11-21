"""Classify full saves: does each one store the same content (root key) as the full save just before it,
with no repair patch in between? Records the caller line of each full save.

Usage: python -B dup_saves.py OUT.json <tools.profiling.run args...>
"""
import atexit, collections, json, runpy, sys, time

OUT = sys.argv.pop(1)
from parkour_solver.io import search_checkpoint
rows, previous = [], dict(key=None, patched=False)
original_call = search_checkpoint.SearchCheckpoint.__call__
original_repair = search_checkpoint.SearchCheckpoint.repair_progress


def caller():
    # The first frames above the adapter that are not callback plumbing.
    frame, names = sys._getframe(2), []
    while frame and len(names) < 3:
        if 'search_checkpoint' not in frame.f_code.co_filename and 'profiling' not in frame.f_code.co_filename:
            names.append(f"{frame.f_code.co_filename.split('/')[-1]}:{frame.f_lineno}:{frame.f_code.co_name}")
        frame = frame.f_back
    return ' < '.join(names)


def call(self, state, program=None, extra_targets=()):
    where = caller()
    t = time.perf_counter()
    original_call(self, state, program, extra_targets)
    rows.append(dict(where=where, seconds=time.perf_counter()-t, same=self.base.key == previous['key'] and not previous['patched'],
                     program=program is not None, extra=len(extra_targets)))
    previous.update(key=self.base.key, patched=False)


def repair(self, state):
    original_repair(self, state)
    previous['patched'] = True


search_checkpoint.SearchCheckpoint.__call__ = call
search_checkpoint.SearchCheckpoint.repair_progress = repair


def dump():
    summary = collections.defaultdict(lambda: dict(calls=0, seconds=0., same=0, sameSeconds=0.))
    for r in rows:
        s = summary[r['where']]
        s['calls'] += 1; s['seconds'] += r['seconds']
        if r['same']:
            s['same'] += 1; s['sameSeconds'] += r['seconds']
    json.dump(dict(summary=summary, rows=rows), open(OUT, 'w'), indent=1)


atexit.register(dump)
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
