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
    actual = schedule.get('trace', {}).get('climbRefreshed', schedule['hit'])
    hits = [t for t in range(start, x - 1) if actual[t]]
    if any(schedule['hit'][t] for t in range(len(schedule['hit'])) if t < start or t >= x - 1):
        raise ValueError('Climb hits must end inside the ladder')
    window = schedule.get('climbWindow')
    if hits:
        if hits != list(range(hits[0], hits[-1] + 1)):
            raise ValueError('A ladder interaction cannot resume climbing after falling')
        first, end = window if window is not None else (hits[0], hits[-1] + 1)
        first, end = min(first, hits[0]), max(end, hits[-1] + 1)
        if not start <= first <= hits[0] <= hits[-1] < end <= x - 1:
            raise ValueError('Invalid climbing window')
        stops = [t for t in range(first, end) if not actual[t]]
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
    from .movement_generation import generate_choices
    integer(ticks, 'movement program ticks', 1, 200)
    units, cursor = [], 0
    for event in sorted(events, key=lambda e: e['schedule']['entryState']):
        column = event['column']
        region_id = column['id'] if 'id' in column else str(tuple(column[k] for k in ('x', 'minY', 'z', 'maxY')))
        s = event['schedule']
        templates = s.get('generatedUnits', s.get('movementUnits'))
        segment = [copy.deepcopy(u) for u in templates if MOVEMENT_TYPES[u['type']]['ladder']] if templates is not None else ladder_units(s, region_id)
        if templates is not None:
            kinds = [u['type'] for u in segment]
            if not kinds or any(k not in ('ladder_climb', 'ladder_fall') for k in kinds) \
                    or kinds.count('ladder_climb') > 1 or any(a == b for a, b in zip(kinds, kinds[1:])):
                raise ValueError('Generated ladder units violate the phase grammar')
            if s['exitKind'] == 'top' and kinds[-1] != 'ladder_climb':
                raise ValueError('A top exit must end with ladder_climb')
            expected = ladder_units(s, region_id)
            keys = ('type', 'start', 'end', 'occupiedStates', 'hitInputs', 'hitWindow', 'stopInputs')
            if [[u.get(k) for k in keys] for u in segment] != [[u.get(k) for k in keys] for u in expected]:
                raise ValueError('Generated units disagree with the selected ladder inputs')
            for unit in segment:
                unit['region'] = region_id
        if segment[0]['start'] < cursor:
            seam = segment[0]['start']
            if seam != cursor - 1:
                raise ValueError('Movement units overlap')
            previous = units[-1]
            previous['end'] = seam
            previous['sneakInputs'] = [t for t in previous.get('sneakInputs', []) if t < seam]
            if any(t >= seam for t in previous.get('hitInputs', [])):
                raise ValueError('Competing climb contacts on a transfer seam')
            if previous['start'] == seam:
                segment[0]['additionalOccupiedStates'] = list(range(*previous['occupiedStates']))
                units.pop()
            cursor = seam
        if cursor < segment[0]['start']:
            units.extend(c['unit'] for c in generate_choices('normal', {}, start=cursor, ends=[segment[0]['start']]))
        units.extend(segment)
        cursor = segment[-1]['end']
    if cursor > ticks:
        raise ValueError('Movement unit exceeds program horizon')
    if cursor < ticks:
        units.extend(c['unit'] for c in generate_choices('normal', {}, start=cursor, ends=[ticks]))
    if events:
        sneak = events[0]['schedule']['sneak']
        for unit in units:
            unit['sneakInputs'] = [t for t in range(unit['start'], unit['end']) if sneak[t]]
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
        for state in unit.get('additionalOccupiedStates', []):
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


def shifted_unit(unit, offset):
    result = copy.deepcopy(unit)
    for key in ('start', 'end'):
        result[key] += offset
    for key in ('hitWindow', 'occupiedStates', 'stopInputs', 'hitInputs', 'sneakInputs', 'additionalOccupiedStates'):
        if key in result:
            result[key] = [t + offset for t in result[key]]
    return result


def exit_matches(kind, y, region):
    if kind == 'side':
        return region['minY'] <= y < region['maxY']
    if kind == 'top':
        return y >= region['maxY']
    if kind == 'bottom':
        return y < region['minY']
    raise ValueError('Exit kind must be side, top or bottom')
