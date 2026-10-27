import math

from standalone.ipopt.model import integer, number


def objective_spec(value=None):
    spec = dict(value) if isinstance(value, dict) else {'axis': 'X', 'sense': 'MAX'} if value is None else None
    if spec is None or set(spec) - {'axis', 'sense'}:
        raise ValueError('Objective must contain only axis and sense')
    if spec.get('axis') not in ('X', 'Z') or spec.get('sense') not in ('MAX', 'MIN'):
        raise ValueError('Objective requires axis X/Z and sense MAX/MIN')
    return spec


def candidate_rank(best, objective):
    ticks = integer(best['plan']['ticks'], 'total ticks', 1, 100000)
    value = number(best['candidate']['objective'], 'candidate objective')
    return ticks, -value if objective['sense'] == 'MAX' else value


def objective_seed(problem):
    target = 0 if problem['objective']['axis'] == 'X' else math.pi / 2
    if problem['objective']['sense'] == 'MIN':
        target += math.pi
    seed = []
    for yaw, base in zip(problem['yaw'], problem['base']):
        difference = target - base - math.radians(yaw)
        turn = math.degrees(math.atan2(math.sin(difference), math.cos(difference)))
        offset = max(-10, min(10, turn))
        seed.append(yaw + (offset if abs(offset) > 1e-3 else 5))
    return seed
