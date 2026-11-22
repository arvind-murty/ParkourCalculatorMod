from parkour_solver.physics.vertical import REST_CARRY, f32, vertical_carry, vertical_step
from parkour_solver.schema import integer, number


def ordinary_jump_profile(source_top, target_top, *, max_inputs=200, threshold=.005):
    return _flight_profile(source_top, target_top, True, max_inputs, threshold)


def _flight_profile(source_top, target_top, jumping, max_inputs, threshold=.005):
    source_top = number(source_top, 'source top')
    target_top = number(target_top, 'target top')
    integer(max_inputs, 'maximum flight inputs', 2 if jumping else 1, 200)
    threshold=number(threshold,'physics threshold')
    if threshold<0:raise ValueError('Physics threshold must be nonnegative')
    config = {'threshold': threshold, 'jumpVelocity': [f32(.42) if jumping else None] + [None] * (max_inputs - 1),
              'sneak': [False] * max_inputs, 'contacts': []}
    y, vy, ground = source_top, REST_CARRY, True
    positions, velocities, grounded = [y], [vy], [ground]
    for tick in range(max_inputs):
        next_y, movement, next_ground, failures = vertical_step(config, tick, y, vy, False, ground, 0)
        if movement < 0 and y >= target_top and next_y <= target_top:
            config['contacts'] = [{'state': tick + 1, 'plane': target_top, 'direction': -1}]
            next_y, movement, next_ground, failures = vertical_step(config, tick, y, vy, False, ground, 0)
        if failures:
            raise ValueError('The existing vertical model rejected the derived profile: ' + str(failures))
        vy = vertical_carry(movement, False, False)
        y, ground = next_y, next_ground
        positions.append(y)
        velocities.append(vy)
        grounded.append(ground)
        if ground:
            return {'duration': tick + 1, 'landingPhase': 'pre', 'position': positions,
                    'velocity': velocities, 'grounded': grounded, 'contacts': config['contacts'],
                    'jump': [jumping] + [False] * tick}
    return None

