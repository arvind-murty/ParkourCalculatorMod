import math
import struct


def f32(value):
    return struct.unpack('f', struct.pack('f', value))[0]


CAP = f32(0.15)
GRAVITY = 0.08
Y_DRAG = f32(0.98)
CLIMB_CARRY = (0.2 - GRAVITY) * Y_DRAG
REST_CARRY = -GRAVITY * Y_DRAG


def clamp(value):
    return max(-CAP, min(CAP, value))


def branch(value):
    return -1 if value < -CAP else 1 if value > CAP else 0


def vertical_move(config, tick, velocity, on_ladder):
    value = 0.0 if abs(velocity) < config['threshold'] else velocity
    jump = config['jumpVelocity'][tick]
    if jump is not None:
        value = jump
    if on_ladder:
        value = max(value, -0.15)
        if config['sneak'][tick] and value < 0:
            value = 0.0
    return value


def vertical_carry(movement, on_ladder, previous_horizontal_collision):
    if on_ladder and previous_horizontal_collision:
        return CLIMB_CARRY
    return (movement - GRAVITY) * Y_DRAG


def vertical_step(config, tick, position, velocity, on_ladder, grounded, tolerance):
    movement = vertical_move(config, tick, velocity, on_ladder)
    proposed = position + movement
    carry = movement
    ground = False
    failures = []
    if config['jumpVelocity'][tick] is not None and not grounded:
        failures.append({'state': tick, 'reason': 'jump_requires_ground'})
    for contact in config['contacts']:
        if contact['state'] != tick + 1:
            continue
        plane, direction = contact['plane'], contact['direction']
        if contact.get('step', False):
            valid = ((grounded or movement < 0) and movement < plane - position
                     and 0 < plane - position <= f32(0.6) + tolerance)
            if valid:
                proposed, carry, ground = plane, 0.0, movement < 0
            else:
                failures.append({'state': tick + 1, 'reason': 'invalid_step'})
        else:
            approach = direction * (position - plane)
            reach = direction * (proposed - plane)
            if approach > tolerance or reach < 0:
                failures.append({'state': tick + 1, 'reason': 'missed_vertical_contact',
                                 'shortBy': max(approach, -reach, 0.0)})
            else:
                proposed, carry = plane, 0.0
                ground = direction < 0 and movement < 0
    if not math.isfinite(proposed) or not math.isfinite(carry):
        raise ValueError('Non-finite vertical replay')
    return proposed, carry, ground, failures
