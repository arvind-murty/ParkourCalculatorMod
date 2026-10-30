import copy

from standalone.ipopt.ladder import vertical_move, vertical_step, vertical_carry
from .obstacle_geometry import BODY_HEIGHT


def ceiling_sequence(schedule, box, *, start=0):
    n = schedule['ticks']
    config = copy.deepcopy(schedule['trace']['vertical'])
    config['contacts'] = [c for c in config['contacts'] if c['direction'] > 0 or c.get('step')]
    selected = []
    y, vy, ground = schedule['trace']['position'][0], schedule['trace']['velocity'][0], schedule['trace']['onGround'][0]
    plane = box[1] - BODY_HEIGHT
    for tick in range(n):
        move = vertical_move(config, tick, vy, schedule['ladderTicks'][tick])
        if tick >= start and move > 0 and y <= plane < y + move:
            existing = next((c for c in config['contacts'] if c['state'] == tick + 1), None)
            if existing is not None and (existing['plane'] != plane or existing['direction'] != 1):
                return []
            if existing is None:
                config['contacts'].append({'state': tick + 1, 'plane': plane, 'direction': 1})
            selected.append({'leg': 0, 'offset': tick, 'axis': 'Y', 'plane': plane,
                             'direction': 1, 'box': box, 'role': 'ceiling'})
        y, move, ground, errors = vertical_step(config, tick, y, vy, schedule['ladderTicks'][tick], ground, 0)
        if errors:
            return []
        vy = vertical_carry(move, schedule['ladderTicks'][tick + 1], schedule['trace']['hit'][tick])
    return selected


