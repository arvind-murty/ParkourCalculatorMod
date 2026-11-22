"""A synthetic two-jump program builder shared by search tests."""
from parkour_solver.movement.program import empty_program, append_choice, truncate_program

def program(scene, n, family):
    p = empty_program(scene['initial'], threshold=0.)
    jumps = [8] if family==17 else [0,12]
    p['profile'].update(duration=n,position=[1.]*(n+1),velocity=[0.]*(n+1),
        grounded=[(t<8 if family==17 else False) or t in jumps or t==n for t in range(n+1)],
        jump=[t in jumps for t in range(n)],sneak=[False]*n,ladderTicks=[False]*(n+1))
    p.update(terminalSupport='goal',family=family,trace=dict(hit=[False]*n))
    return p
