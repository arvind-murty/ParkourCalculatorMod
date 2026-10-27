import copy
import math

from standalone.ipopt.ladder import f32
from standalone.ipopt.model import integer, number
from .ranking import objective_spec


HALF_WIDTH = f32(0.6) / 2


def prepare_route(request):
    route = copy.deepcopy(request)
    if not isinstance(route, dict) or type(route.get('version')) is not int or route['version'] != 1:
        raise ValueError('Expected a version 1 block route')
    route['objective'] = objective_spec(route.get('objective'))
    blocks = route.get('blocks')
    if not isinstance(blocks, list) or len(blocks) < 2:
        raise ValueError('Supply a start block and at least one landing block')
    for i, block in enumerate(blocks):
        if not isinstance(block, dict):
            raise ValueError('Invalid block')
        block.setdefault('id', str(i))
        if not isinstance(block['id'], str) or not block['id']:
            raise ValueError('Block IDs must be nonempty strings')
        if ('bounds' in block) == ('centerBounds' in block):
            raise ValueError('Supply either collision bounds or an explicit center region')
        bounds = block.get('bounds', block.get('centerBounds'))
        if not isinstance(bounds, list) or len(bounds) != 4:
            raise ValueError('Block bounds must be [minX, maxX, minZ, maxZ]')
        for value in bounds:
            number(value, 'block bound')
        if bounds[0] >= bounds[1] or bounds[2] >= bounds[3]:
            raise ValueError('Block bounds are empty')
        if 'standingBounds' in block:
            standing = block['standingBounds']
            if not isinstance(standing, list) or len(standing) != 4:
                raise ValueError('Standing support needs four bounds')
            for value in standing:
                number(value, 'standing bound')
            if standing[0] >= standing[1] or standing[2] >= standing[3]:
                raise ValueError('Standing support is empty')
        number(block.get('top'), 'block top')
    airtimes = route.get('airtimes')
    if not isinstance(airtimes, list) or len(airtimes) != len(blocks) - 1:
        raise ValueError('Supply one valid airtime per transition')
    kinds = route.setdefault('kinds', ['jump'] * len(airtimes))
    if not isinstance(kinds, list) or len(kinds) != len(airtimes) or any(k not in ('jump', 'walk_off', 'step') for k in kinds):
        raise ValueError('Transition kinds must be jump, walk_off or step')
    for value, kind in zip(airtimes, kinds):
        integer(value, 'airtime', 1 if kind == 'step' else 2, 200)
        if kind == 'step' and value != 1:
            raise ValueError('A prescribed step takes one grounded movement input')
    phases = route.setdefault('landingPhases', ['post' if k == 'step' else 'pre' for k in kinds])
    if not isinstance(phases, list) or len(phases) != len(kinds) or any(p not in ('pre', 'post') for p in phases):
        raise ValueError('Landing phases must be pre or post')
    entries = route.setdefault('contactEntries', [None] * len(kinds))
    if not isinstance(entries, list) or len(entries) != len(kinds):
        raise ValueError('Supply one optional contact entry per transition')
    for entry, duration in zip(entries, airtimes):
        if entry is None:
            continue
        if not isinstance(entry, dict) or entry.get('axis') not in ('X', 'Z') or type(entry.get('direction')) is not int or entry['direction'] not in (-1, 1):
            raise ValueError('Invalid contact entry face')
        integer(entry.get('outsideFrom'), 'outsideFrom', 0, duration - 1)
        integer(entry.get('outsideThrough'), 'outsideThrough', entry['outsideFrom'], duration - 1)
        integer(entry.get('enter'), 'entry state', entry['outsideThrough'] + 1, duration)
    exits = route.setdefault('walkOffExits', [None] * len(airtimes))
    if not isinstance(exits, list) or len(exits) != len(airtimes):
        raise ValueError('Supply one optional run-off exit per transition')
    for i, exit_face in enumerate(exits):
        if exit_face is None:
            continue
        if kinds[i] != 'walk_off' or not isinstance(exit_face, dict) or exit_face.get('axis') not in ('X', 'Z'):
            raise ValueError('Invalid run-off exit')
        if type(exit_face.get('direction')) is not int or exit_face['direction'] not in (-1, 1):
            raise ValueError('Run-off direction must be -1 or 1')
    initial = route.get('initial')
    if not isinstance(initial, dict):
        raise ValueError('Supply an initial state')
    for key in ('position', 'velocity'):
        values = initial.get(key)
        if not isinstance(values, list) or len(values) != 3:
            raise ValueError('Initial ' + key + ' must be [X,Y,Z]')
        for value in values:
            number(value, 'initial ' + key)
    number(initial.setdefault('yaw', 0.0), 'initial yaw')
    if abs(initial['position'][1] - blocks[0]['top']) > 1e-7:
        raise ValueError('The initial feet height must equal the start block top')
    if not supports(blocks[0], initial['position'][0], initial['position'][2]):
        raise ValueError('The initial player must overlap the start block')
    inputs = route.setdefault('inputs', {})
    if not isinstance(inputs, dict):
        raise ValueError('Invalid input policy')
    for key, default in [('forward', f32(0.98)), ('strafe', 0.0)]:
        value = number(inputs.setdefault(key, default), key)
        if abs(value) > 1:
            raise ValueError('Movement inputs must be at the scaled moveFlying range')
    for key, default in [('sprint', True), ('incomingSprint', False), ('force45', False)]:
        if not isinstance(inputs.setdefault(key, default), bool):
            raise ValueError(key + ' must be boolean')
    if abs(inputs['forward'] - f32(0.98)) > 1e-7 or inputs['strafe'] != 0:
        raise ValueError('The first route family uses forward movement without strafe')
    integer(route.setdefault('maxTotalRunTicks', 40), 'maxTotalRunTicks', 0, 1000)
    run_bounds = route.setdefault('runBounds', [[0, route['maxTotalRunTicks']] for _ in airtimes])
    if not isinstance(run_bounds, list) or len(run_bounds) != len(airtimes):
        raise ValueError('Supply one run bound pair per transition')
    for bounds in run_bounds:
        if not isinstance(bounds, list) or len(bounds) != 2:
            raise ValueError('Run bounds must be [min,max]')
        integer(bounds[0], 'minimum run ticks', 0, 1000)
        integer(bounds[1], 'maximum run ticks', bounds[0], 1000)
    clearance = number(route.setdefault('supportClearance', 1e-6), 'supportClearance')
    if clearance < 0:
        raise ValueError('Support clearance must be nonnegative')
    if route.get('obstacles'):
        raise ValueError('Obstacle scenes require the integration replay adapter')
    for field in ('ladderTicks', 'ladderPattern', 'contacts', 'vertical'):
        if field in route:
            raise ValueError(field + ' needs a retiming adapter; it cannot be omitted from the compiled request')
    constraints = route.setdefault('constraints', [])
    if not isinstance(constraints, list):
        raise ValueError('Constraints must be a list')
    for row in constraints:
        if not isinstance(row, dict):
            raise ValueError('Invalid route constraint')
        integer(row.get('leg'), 'constraint leg', 0, len(airtimes) - 1)
        integer(row.setdefault('offset', 0), 'constraint offset', 0, 1000)
        if row.get('phase', 'departure') not in ('departure', 'run') or row.get('mode') not in ('X', 'Z'):
            raise ValueError('Only departure/run X/Z constraints are supported')
        if row.get('cmp') not in ('GE', 'LE', 'EQ'):
            raise ValueError('Invalid route comparison')
        number(row.get('rhs'), 'constraint rhs')
    return route


def supports(block, x, z, kind='landing'):
    if kind == 'run' and 'standingBounds' in block:
        a, b, c, d = block['standingBounds']
        return x + HALF_WIDTH > a and x - HALF_WIDTH < b and z + HALF_WIDTH > c and z - HALF_WIDTH < d
    if 'centerBounds' in block:
        a, b, c, d = block['centerBounds']
        return a < x < b and c < z < d
    a, b, c, d = block['bounds']
    return x + HALF_WIDTH > a and x - HALF_WIDTH < b and z + HALF_WIDTH > c and z - HALF_WIDTH < d


def center_bounds(block, kind='landing'):
    if kind == 'run' and '_derivedStandingFootprint' in block:
        return block['_derivedStandingFootprint'][:]
    if kind == 'run' and 'standingBounds' in block:
        a, b, c, d = block['standingBounds']
        return [a - HALF_WIDTH, b + HALF_WIDTH, c - HALF_WIDTH, d + HALF_WIDTH]
    if '_derivedFootprint' in block:
        return block['_derivedFootprint'][:]
    if 'centerBounds' in block:
        return block['centerBounds'][:]
    a, b, c, d = block['bounds']
    return [a - HALF_WIDTH, b + HALF_WIDTH, c - HALF_WIDTH, d + HALF_WIDTH]


def schedule(route, allocation, stage='jump'):
    if stage not in ('jump', 'run') or not allocation or len(allocation) > len(route['airtimes']):
        raise ValueError('Invalid allocation or stage')
    grounded, jump, events, supports_at, exit_checks = [], [], [], [], []
    state = 0
    supports_at.append((0, 0, 'initial'))
    for i, extra in enumerate(allocation):
        lo, hi = route['runBounds'][i]
        integer(extra, 'run ticks', lo, hi)
        landing = state
        for _ in range(extra):
            supports_at.append((state, i, 'run'))
            grounded.append(True)
            jump.append(False)
            state += 1
        fire = state
        if stage == 'run' and i == len(allocation) - 1:
            events.append({'leg': i, 'landed': landing, 'jump': fire, 'landing': None, 'runTicks': extra,
                           'kind': route['kinds'][i]})
            break
        airtime = route['airtimes'][i]
        grounded.extend([True] + [False] * (airtime - 1))
        jump.extend([route['kinds'][i] == 'jump'] + [False] * (airtime - 1))
        if route['kinds'][i] == 'walk_off':
            exit_checks.append((fire, i))
        state += airtime
        supports_at.append((state if route['landingPhases'][i] == 'post' else state - 1, i + 1, 'landing'))
        events.append({'leg': i, 'landed': landing, 'jump': fire, 'landing': state, 'runTicks': extra,
                       'kind': route['kinds'][i]})
    if sum(allocation) > route['maxTotalRunTicks']:
        raise ValueError('Allocation exceeds the total run-tick limit')
    walls = []
    clearance = route['supportClearance']
    for tick, index, kind in sorted(set(supports_at)):
        block = route['blocks'][index]
        bounds = center_bounds(block, kind)
        for axis, lo, hi in [('X', bounds[0], bounds[1]), ('Z', bounds[2], bounds[3])]:
            lower = math.nextafter(lo, math.inf) + clearance
            upper = math.nextafter(hi, -math.inf) - clearance
            walls.extend([{'mode': axis, 't1': tick, 'cmp': 'GE', 'rhs': lower,
                           'name': kind + ':' + str(index) + ':' + axis + ':lo'},
                          {'mode': axis, 't1': tick, 'cmp': 'LE', 'rhs': upper,
                           'name': kind + ':' + str(index) + ':' + axis + ':hi'}])
    for tick, index in exit_checks:
        source = center_bounds(route['blocks'][index], 'run')
        target = center_bounds(route['blocks'][index + 1])
        exit_face = route['walkOffExits'][index]
        if exit_face is None:
            dx = target[0] + target[1] - source[0] - source[1]
            dz = target[2] + target[3] - source[2] - source[3]
            axis, delta = ('X', dx) if abs(dx) > abs(dz) else ('Z', dz)
            exit_face = {'axis': axis, 'direction': 1 if delta >= 0 else -1}
        axis, direction = exit_face['axis'], exit_face['direction']
        k = 0 if axis == 'X' else 2
        edge = source[k + 1] if direction > 0 else source[k]
        walls.append({'mode': axis, 't1': tick, 'cmp': 'GE' if direction > 0 else 'LE',
                      'rhs': edge + direction * clearance, 'name': 'walk_off:' + str(index)})
    for event in events:
        if event['landing'] is None:
            continue
        entry = route['contactEntries'][event['leg']]
        if entry is None:
            continue
        target = center_bounds(route['blocks'][event['leg'] + 1])
        axis, direction = entry['axis'], entry['direction']
        k = 0 if axis == 'X' else 2
        edge = target[k + 1] if direction > 0 else target[k]
        for offset in range(entry['outsideFrom'], entry['outsideThrough'] + 1):
            walls.append({'mode': axis, 't1': event['jump'] + offset,
                          'cmp': 'GE' if direction > 0 else 'LE', 'rhs': edge + direction * clearance,
                          'name': 'contact_outside:' + str(event['leg'])})
        tick = event['jump'] + entry['enter']
        supports_at.append((tick, event['leg'] + 1, 'contact'))
        for axis, lo, hi in [('X', target[0], target[1]), ('Z', target[2], target[3])]:
            walls.extend([{'mode': axis, 't1': tick, 'cmp': 'GE', 'rhs': lo + clearance,
                           'name': 'contact_enter:' + str(event['leg'])},
                          {'mode': axis, 't1': tick, 'cmp': 'LE', 'rhs': hi - clearance,
                           'name': 'contact_enter:' + str(event['leg'])}])
    for row in route['constraints']:
        if row['leg'] >= len(events):
            continue
        event = events[row['leg']]
        if row.get('phase', 'departure') == 'run':
            if row['offset'] >= event['runTicks']:
                continue
            tick = event['landed'] + row['offset']
        else:
            if event['landing'] is None:
                continue
            tick = event['jump'] + row['offset']
            if tick > event['landing']:
                raise ValueError('Constraint lies beyond its transition')
        walls.append({'mode': row['mode'], 't1': tick, 'cmp': row['cmp'], 'rhs': row['rhs'],
                      'name': row.get('name', 'route:' + str(row['leg']))})
    return {'ticks': state, 'grounded': grounded, 'jump': jump, 'events': events,
            'supportChecks': supports_at, 'exitChecks': exit_checks, 'walls': walls,
            'allocation': list(allocation), 'stage': stage}


def initial_guess(route, plan, parent=None):
    yaw = route['initial']['yaw']
    result = [yaw] * plan['ticks']
    if parent:
        prior = parent['candidate']['yaw']
        result[:min(len(prior), len(result))] = prior[:len(result)]
    for event in plan['events']:
        target = center_bounds(route['blocks'][event['leg'] + 1])
        source = center_bounds(route['blocks'][event['leg']])
        dx = (target[0] + target[1] - source[0] - source[1]) / 2
        dz = (target[2] + target[3] - source[2] - source[3]) / 2
        facing = math.degrees(math.atan2(-dx, dz)) if dx or dz else yaw
        start = event['landed']
        end = event['landing'] if event['landing'] is not None else event['jump']
        if parent:
            start = max(start, len(parent['candidate']['yaw']))
        result[start:end] = [facing] * (end - start)
        yaw = facing
    if route['inputs']['force45']:
        for t in range(len(result)):
            if (not parent or t >= len(parent['candidate']['yaw'])) and not plan['jump'][t]:
                result[t] += 45
    return result
