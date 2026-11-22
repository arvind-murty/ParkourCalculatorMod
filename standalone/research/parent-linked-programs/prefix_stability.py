"""For every successful append_choice, does the child's copy of each parent field equal the parent's (as a prefix)?

Usage: python -B prefix_stability.py OUT.json <tools.profiling.run args...>
"""
import atexit, collections, json, runpy, sys

OUT = sys.argv.pop(1)
from parkour_solver.movement import program as module
original = module.append_choice
same, changed, examples = collections.Counter(), collections.Counter(), {}


def check(name, parent, child):
    if not isinstance(parent, list) or not isinstance(child, list):
        ok = parent == child
    else:
        ok = child[:len(parent)] == parent
    (same if ok else changed)[name] += 1
    if not ok and name not in examples:
        n = len(parent) if isinstance(parent, list) else None
        diff = [i for i, (a, b) in enumerate(zip(parent, child)) if a != b] if n else None
        examples[name] = dict(parentLen=n, childLen=len(child) if isinstance(child, list) else None, firstDiffs=(diff or [])[:5])


def append_choice(program, choice, **kw):
    result = original(program, choice, **kw)
    if result is not None:
        cut = program['profile']['airtime']
        old, new = program['profile'], result['profile']
        for k in ('position', 'grounded', 'jump', 'sneak', 'contacts', 'contactEvents', 'caseEvents',
                  'positionConstraints', 'constraintOwners', 'regionChoices', 'generatedUnits'):
            if k in old:
                check('profile.'+k, old[k], new[k])
        check('profile.velocity[:cut]', old['velocity'][:cut], new['velocity'])
        check('profile.ladderTicks[:cut]', old['ladderTicks'][:cut], new['ladderTicks'])
        check('movementUnits', program['movementUnits'], result['movementUnits'])
        if program.get('trace'):
            for k in ('position', 'onGround', 'movement', 'sneak', 'hit', 'climbRefreshed'):
                check('trace.'+k, program['trace'][k], result['trace'][k])
            check('trace.velocity[:cut]', program['trace']['velocity'][:cut], result['trace']['velocity'])
            check('trace.vertical', {k: v for k, v in program['trace']['vertical'].items() if k != 'contacts'} and None, None)
    return result


for name, other in list(sys.modules.items()):
    if name.startswith('parkour_solver') and getattr(other, 'append_choice', None) is original:
        other.append_choice = append_choice
atexit.register(lambda: json.dump(dict(same=same, changed=changed, examples=examples), open(OUT, 'w'), indent=1))
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
