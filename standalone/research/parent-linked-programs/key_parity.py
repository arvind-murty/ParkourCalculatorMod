"""At every program_physics_key call: the incremental key equals the key folded from scratch, and new keys are
equal exactly when the previous commit's keys are equal.

Usage: python -B key_parity.py REF_PROGRAM.py OUT.json <tools.profiling.run args...>
"""
import atexit, collections, importlib.util, json, runpy, sys

REF, OUT = sys.argv.pop(1), sys.argv.pop(1)
spec = importlib.util.spec_from_file_location('ref_program', REF)
ref = importlib.util.module_from_spec(spec); spec.loader.exec_module(ref)
from parkour_solver.movement import program as module
original = module.program_physics_key
stats = collections.Counter(); old_to_new, new_to_old = collections.defaultdict(set), collections.defaultdict(set)
examples = []


def program_physics_key(program):
    key = original(program)
    scratch = original({k: v for k, v in program.items() if k != 'prefixChain'})
    stats['calls'] += 1; stats['withChain'] += 'prefixChain' in program and program['prefixChain'] is not None
    if key != scratch:
        stats['chainMismatch'] += 1
        if len(examples) < 3:
            examples.append(dict(airtime=program['profile']['airtime'], chain=program.get('prefixChain')))
    old = ref.program_physics_key(program)
    old_to_new[old].add(key); new_to_old[key].add(old)
    return key


for name, other in list(sys.modules.items()):
    if name.startswith('parkour_solver') and getattr(other, 'program_physics_key', None) is original:
        other.program_physics_key = program_physics_key


def dump():
    stats['distinctOld'] = len(old_to_new); stats['distinctNew'] = len(new_to_old)
    stats['oldSplitByNew'] = sum(len(v) > 1 for v in old_to_new.values())
    stats['newMergingOld'] = sum(len(v) > 1 for v in new_to_old.values())
    json.dump(dict(stats=stats, examples=examples), open(OUT, 'w'), indent=1)


atexit.register(dump)
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
