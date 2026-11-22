"""At every full save, compare SearchCheckpoint's archive-collected paths with the old identity_paths walk.

Usage: python -B paths_parity.py OUT.json <tools.profiling.run args...>
"""
import atexit, json, runpy, sys

OUT = sys.argv.pop(1)


def identity_paths(value, target):
    memo = {}

    def paths(node):
        if node is target:
            return [()]
        if not isinstance(node, (dict,list,tuple)):
            return []
        if id(node) not in memo:
            children = node.items() if isinstance(node,dict) else enumerate(node)
            memo[id(node)] = [(key,)+tail for key,child in children for tail in paths(child)]
        return memo[id(node)]

    return paths(value)


from parkour_solver.io import checkpoint_store, search_checkpoint
stats = dict(saves=0, withProgram=0, extraTargets=0, aliasPaths=0, mismatches=[])
original_archive = checkpoint_store.CheckpointStore.archive
last = {}


def archive(self, report, targets=()):
    result = original_archive(self, report, targets)
    if targets:
        last['report'] = report
    return result


original_call = search_checkpoint.SearchCheckpoint.__call__


def call(self, state, program=None, extra_targets=()):
    last.clear()
    original_call(self, state, program, extra_targets)
    stats['saves'] += 1
    if self.program is None and not extra_targets:
        return
    saved = last['report']
    expected = identity_paths(saved, self.program) if self.program is not None else []
    stats['withProgram'] += self.program is not None
    stats['aliasPaths'] += len(expected)
    if expected != self.paths:
        stats['mismatches'].append(dict(save=stats['saves'], expected=len(expected), got=len(self.paths)))
    for paths, target in self.extra_targets:
        stats['extraTargets'] += 1
        if identity_paths(saved, target) != paths:
            stats['mismatches'].append(dict(save=stats['saves'], extra=True))


checkpoint_store.CheckpointStore.archive = archive
search_checkpoint.SearchCheckpoint.__call__ = call
atexit.register(lambda: json.dump(stats, open(OUT, 'w'), indent=1))
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
