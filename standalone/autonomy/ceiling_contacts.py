import copy

from standalone.ipopt.ladder import REST_CARRY, f32, vertical_carry, vertical_move, vertical_step
from standalone.ipopt.model import integer, number
from .block_profiles import ordinary_jump_profile


def ceiling_profile(source_top, target_top, box, solid_index, block_id, *, max_inputs=200):
    source_top, target_top = number(source_top, 'source top'), number(target_top, 'target top')
    integer(max_inputs, 'maximum flight inputs', 2, 200)
    ordinary = ordinary_jump_profile(source_top, source_top)
    plane = box[1] - f32(1.8)
    hit = next((t for t in range(ordinary['airtime'])
                if ordinary['position'][t] <= plane < ordinary['position'][t + 1]), None)
    if hit is None:
        return None
    contact = {'state': hit + 1, 'plane': plane, 'direction': 1}
    config = {'threshold': .005, 'jumpVelocity': [f32(.42)] + [None] * (max_inputs - 1),
              'sneak': [False] * max_inputs, 'contacts': [contact]}
    y, vy, ground = source_top, REST_CARRY, True
    positions, velocities, grounded = [y], [vy], [ground]
    for tick in range(max_inputs):
        raw = vertical_move(config, tick, vy, False)
        if tick > hit and raw < 0 and y >= target_top and y + raw <= target_top:
            config['contacts'].append({'state': tick + 1, 'plane': target_top, 'direction': -1})
        y, move, ground, failures = vertical_step(config, tick, y, vy, False, ground, 0)
        if failures:
            return None
        vy = vertical_carry(move, False, False)
        positions.append(y)
        velocities.append(vy)
        grounded.append(ground)
        if ground:
            if tick <= hit:
                return None
            return {'airtime': tick + 1, 'landingPhase': 'pre', 'position': positions, 'velocity': velocities,
                    'grounded': grounded, 'contacts': config['contacts'], 'jump': [True] + [False] * tick,
                    'ceilingEvent': {'offset': hit, 'plane': plane, 'box': copy.deepcopy(box),
                                     'solidIndex': solid_index, 'blockId': block_id}}
    return None
