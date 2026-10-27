import math
from standalone.ipopt.ladder import clamp


def contradiction(problem):
    contacts = {(c['state'], c['axis']): c for c in problem.get('contacts', [])}
    ladder = problem.get('ladderTicks', [False] * (problem['ticks'] + 1))
    bounds = {}
    for wall in problem['walls']:
        if wall['mode'] not in ('X', 'Z') or wall.get('t2') is not None:
            continue
        key = (wall['t1'], 0 if wall['mode'] == 'X' else 1)
        lo, hi = bounds.get(key, (-math.inf, math.inf))
        if wall['cmp'] in ('GE', 'EQ'):
            lo = max(lo, wall['rhs'] - 1e-7)
        if wall['cmp'] in ('LE', 'EQ'):
            hi = min(hi, wall['rhs'] + 1e-7)
        bounds[key] = lo, hi
    p_lo, p_hi = problem['position'][:], problem['position'][:]
    v_lo, v_hi = problem['velocity'][:], problem['velocity'][:]
    if 'initialIntervals' in problem:
        p_lo, p_hi, v_lo, v_hi = [list(v) for v in problem['initialIntervals']]
    for tick in range(problem['ticks'] + 1):
        for axis in range(2):
            lo, hi = bounds.get((tick, axis), (-math.inf, math.inf))
            p_lo[axis], p_hi[axis] = max(p_lo[axis], lo), min(p_hi[axis], hi)
            if p_lo[axis] > p_hi[axis] + 1e-9 * (1 + abs(p_lo[axis]) + abs(p_hi[axis])):
                return {'state': tick, 'axis': axis, 'reason': 'interval_reach_contradiction'}
        if tick == problem['ticks']:
            break
        magnitude, friction = problem['magnitude'][tick], problem['friction'][tick]
        for axis in range(2):
            movement_lo = min(v_lo[axis], 0.0) - magnitude
            movement_hi = max(v_hi[axis], 0.0) + magnitude
            if ladder[tick]:
                movement_lo, movement_hi = clamp(movement_lo), clamp(movement_hi)
            contact = contacts.get((tick + 1, axis))
            if contact is not None:
                plane, direction = contact['plane'], contact['direction']
                if (direction > 0 and (p_lo[axis] > plane + 1e-7 or p_hi[axis] + movement_hi < plane - 1e-7)
                        or direction < 0 and (p_hi[axis] < plane - 1e-7 or p_lo[axis] + movement_lo > plane + 1e-7)):
                    return {'state': tick + 1, 'axis': axis, 'reason': 'contact_reach_contradiction'}
                p_lo[axis] = p_hi[axis] = plane
                v_lo[axis] = v_hi[axis] = 0.
                continue
            scale = 1 + abs(p_lo[axis]) + abs(p_hi[axis]) + abs(movement_lo) + abs(movement_hi)
            padding = 1e-12 * scale
            p_lo[axis] -= padding
            p_hi[axis] += padding
            p_lo[axis] += movement_lo
            p_hi[axis] += movement_hi
            v_lo[axis] = movement_lo * friction - padding
            v_hi[axis] = movement_hi * friction + padding
    return None
