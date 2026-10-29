import copy

from standalone.ipopt.model import integer


MOVEMENT_TYPES = {'normal': {'ladder': False}, 'ladder_climb': {'ladder': True},
                  'ladder_fall': {'ladder': True}}


def movement_unit(kind, start, end, **parameters):
    if kind not in MOVEMENT_TYPES:
        raise ValueError('Unsupported movement type: ' + str(kind))
    integer(start, 'unit start', 0, 199)
    integer(end, 'unit end', start + 1, 200)
    return dict(type=kind, start=start, end=end, **copy.deepcopy(parameters))


def ladder_units(schedule, region_id=None):
    e, x = schedule['entryState'], schedule['exitState']
    start = max(0, e - 1)
    hits = [t for t in range(start, x - 1) if schedule['hit'][t]]
    if any(schedule['hit'][t] for t in range(len(schedule['hit'])) if t < start or t >= x - 1):
        raise ValueError('Climb hits must end inside the ladder')
    window = schedule.get('climbWindow')
    if hits:
        if hits != list(range(hits[0], hits[-1] + 1)):
            raise ValueError('A ladder interaction cannot resume climbing after falling')
        first, end = window if window is not None else (hits[0], hits[-1] + 1)
        if not start <= first <= hits[0] <= hits[-1] < end <= x - 1:
            raise ValueError('Invalid climbing window')
        stops = [t for t in range(first, end) if not schedule['hit'][t]]
        if any(t not in (first, end - 1) for t in stops) or len(stops) > 2:
            raise ValueError('Climbing only allows one-input endpoint stops')
        climb_end = x if end == x - 1 else end
        units = ([movement_unit('ladder_fall', start, first, region=region_id)] if start < first else [])
        units.append(movement_unit('ladder_climb', first, climb_end, region=region_id,
            hitWindow=[first, end], stopInputs=stops, hitInputs=hits))
        if climb_end < x:
            units.append(movement_unit('ladder_fall', climb_end, x, region=region_id))
    else:
        units = [movement_unit('ladder_fall', start, x, region=region_id)]
    if schedule['exitKind'] == 'top' and units[-1]['type'] != 'ladder_climb':
        raise ValueError('A top exit must end with ladder_climb')
    for unit in units:
        unit['occupiedStates'] = [max(e, unit['start']), min(x, unit['end'])]
        unit['sneakInputs'] = [t for t in range(unit['start'], unit['end']) if schedule.get('sneak', [False] * x)[t]]
    return units


def movement_program(ticks, events):
    integer(ticks, 'movement program ticks', 1, 200)
    units, cursor = [], 0
    for event in sorted(events, key=lambda e: e['schedule']['entryState']):
        segment = ladder_units(event['schedule'], event['column']['id'])
        if segment[0]['start'] < cursor:
            raise ValueError('Movement units overlap')
        if cursor < segment[0]['start']:
            units.append(movement_unit('normal', cursor, segment[0]['start']))
        units.extend(segment)
        cursor = segment[-1]['end']
    if cursor > ticks:
        raise ValueError('Movement unit exceeds program horizon')
    if cursor < ticks:
        units.append(movement_unit('normal', cursor, ticks))
    return units


def compile_units(ticks, units):
    ladder, hit, sneak, cursor = [False] * (ticks + 1), [False] * ticks, [False] * ticks, 0
    for unit in units:
        if unit['type'] not in MOVEMENT_TYPES or unit['start'] != cursor or not cursor < unit['end'] <= ticks:
            raise ValueError('Movement units must partition the complete input timeline')
        if MOVEMENT_TYPES[unit['type']]['ladder']:
            e, x = unit['occupiedStates']
            for state in range(e, x):
                ladder[state] = True
        for tick in unit.get('hitInputs', []):
            if unit['type'] != 'ladder_climb' or not unit['start'] <= tick < unit['end']:
                raise ValueError('Invalid unit climb input')
            hit[tick] = True
        for tick in unit.get('sneakInputs', []):
            if not unit['start'] <= tick < unit['end']:
                raise ValueError('Invalid unit Sneak input')
            sneak[tick] = True
        cursor = unit['end']
    if cursor != ticks:
        raise ValueError('Movement program is incomplete')
    return {'ladderTicks': ladder, 'hit': hit, 'sneak': sneak}


def exit_matches(kind, y, region):
    if kind == 'side':
        return region['minY'] <= y < region['maxY']
    if kind == 'top':
        return y >= region['maxY']
    if kind == 'bottom':
        return y < region['minY']
    raise ValueError('Exit kind must be side, top or bottom')
