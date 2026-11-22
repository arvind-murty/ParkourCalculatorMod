import copy
import math


def horizontal_segments(corridor):
    result = []
    for index, (a, b) in enumerate(zip(corridor.points, corridor.points[1:])):
        delta = (b[0]-a[0], b[2]-a[2])
        length = math.hypot(*delta)
        if length <= 1e-9:
            continue
        direction = tuple(v/length for v in delta)
        if result and sum(x*y for x, y in zip(result[-1]['direction'], direction)) >= 1-1e-9:
            result[-1].update(end=list(b), lastPoint=index+1)
        else:
            result.append(dict(start=list(a), end=list(b), firstPoint=index, lastPoint=index+1, direction=direction))
    return result


def direction_objective(direction):
    """The progress objective along a unit horizontal direction (x, z), as a custom-yaw X maximization."""
    return dict(axis='X', sense='MAX', customYaw=math.degrees(math.atan2(-direction[0], direction[1])))


def retain_prefix(program, best):
    result = copy.deepcopy(program)
    result.pop('repairState', None)
    result.pop('prefixChain', None)
    retained = result['profile']['positionConstraints']
    identities = {repr(sorted(r.items())) for r in retained}
    for row in best['plan']['walls']:
        if row['mode'] == 'F':
            raise ValueError('Checkpoint retention must not fix a facing')
        if row.get('name', '').startswith('checkpoint_probe:'):
            continue
        chosen = {k: v for k, v in row.items() if k in ('mode', 't1', 't2', 'op', 'cmp', 'rhs', 'name')}
        identity = repr(sorted(chosen.items()))
        if identity not in identities:
            retained.append(chosen)
            identities.add(identity)
    result['guideSeedYaw'] = best['candidate']['yaw'][:]
    return result

