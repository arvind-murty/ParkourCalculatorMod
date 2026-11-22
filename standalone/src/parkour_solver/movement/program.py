import copy
import hashlib
import json

from parkour_solver.physics.vertical import vertical_carry
from parkour_solver.schema import integer, number
from parkour_solver.movement.contact_combinations import merge_contacts
from parkour_solver.movement.units import compile_units, profile_program, shifted_unit
from parkour_solver.movement.vertical_profiles import trace_ladder_schedule


def scalar_initial(initial):
    if isinstance(initial.get('position'), (list, tuple)):
        return dict(position=initial['position'][1], velocity=initial['velocity'][1],
                    onGround=initial.get('onGround', True),
                    previousHorizontalCollision=initial.get('previousHorizontalCollision', False))
    return dict(position=initial['position'], velocity=initial['velocity'],
                onGround=initial.get('onGround', False),
                previousHorizontalCollision=initial.get('previousHorizontalCollision', False))


def empty_program(initial, *, threshold=.005):
    if number(threshold, 'program threshold') < 0:
        raise ValueError('Program threshold must be nonnegative')
    initial = scalar_initial(initial)
    profile = dict(duration=0, position=[initial['position']], velocity=[initial['velocity']],
        grounded=[initial['onGround']], jump=[], sneak=[], ladderTicks=[False], contacts=[],
        contactEvents=[], caseEvents=[], positionConstraints=[], generatedUnits=[],
        composition='movement_program', landingPhase='post')
    program = dict(initial=initial, profile=profile, ladderEvents=[], movementUnits=[],
                   terminal='open', terminalSupport=None, interactions=0, trace=None)
    if threshold != .005:
        program['threshold'] = threshold
    return program


def boundary_state(program, on_ladder=False):
    profile = program['profile']
    vy = profile['velocity'][-1]
    if profile['duration'] and on_ladder != profile['ladderTicks'][-1]:
        vy = vertical_carry(program['trace']['movement'][-1], on_ladder, program['trace']['hit'][-1])
    return dict(position=profile['position'][-1], velocity=vy, onGround=profile['grounded'][-1],
                previousHorizontalCollision=(program['trace']['hit'][-1] if profile['duration'] else
                                             program['initial']['previousHorizontalCollision']))


def profile_from_trace(trace, *, contacts=(), cases=(), constraints=(), units=None, landing_phase='post',
                       region_choices=()):
    return _profile(trace, copy.deepcopy(list(contacts)), copy.deepcopy(list(cases)), copy.deepcopy(list(constraints)),
                    copy.deepcopy(units) if units is not None else None, landing_phase,
                    copy.deepcopy(list(region_choices)))


def _profile(trace, contacts, cases, constraints, units, landing_phase, region_choices):
    """Assemble a profile around lists the caller owns; their elements are not copied."""
    n = trace['ticks']
    profile = dict(duration=n, position=trace['position'], velocity=trace['velocity'], grounded=trace['onGround'],
        jump=[v is not None for v in trace['vertical']['jumpVelocity']], sneak=trace['sneak'],
        ladderTicks=trace['ladderTicks'], contacts=trace['vertical']['contacts'],
        contactEvents=contacts, caseEvents=cases, positionConstraints=constraints, landingPhase=landing_phase,
        composition='movement_program')
    profile['generatedUnits'] = units if units is not None else profile_program(profile)
    if region_choices:
        profile['regionChoices'] = region_choices
    if trace['vertical']['threshold'] != .005:
        profile['threshold'] = trace['vertical']['threshold']
    return profile


def movement_choice(kind, initial, trace, *, contacts=(), cases=(), constraints=(), units=None,
                    ladder_events=(), support=None, landing_phase='post', region_choices=()):
    if trace['status'] == 'invalid':
        return None
    profile = profile_from_trace(trace, contacts=contacts, cases=cases, constraints=constraints,
                                 units=units, landing_phase=landing_phase, region_choices=region_choices)
    events = list(contacts)+list(cases)
    owner = max((e.get('offset', e.get('input', -1))+1 for e in events), default=0)
    profile['constraintOwners'] = [owner]*len(profile['positionConstraints'])
    unit = next((u for u in profile['generatedUnits'] if u['type'] == kind), None)
    if unit is None:
        raise ValueError('A movement choice must contain its owning type')
    return dict(unit=unit, units=profile['generatedUnits'], initial=scalar_initial(initial), profile=profile,
                ladderEvents=copy.deepcopy(list(ladder_events)), terminalSupport=support,
                interaction=kind != 'normal')


def _contacts(events):
    merged = merge_contacts([dict(e, state=e['offset'] + 1) for e in events])
    return [{k: v for k, v in event.items() if k != 'state'} for event in merged]


def _shift_event(event, offset):
    event = copy.deepcopy(event)
    event['schedule']['constraintStart'] = offset
    event['schedule']['entryState'] += offset
    event['schedule']['exitState'] += offset
    event['schedule']['hit'] = [False] * offset + event['schedule']['hit']
    for key in ('climbWindow', 'stopInputs'):
        if event['schedule'].get(key) is not None:
            event['schedule'][key] = [t + offset for t in event['schedule'][key]]
    for key in ('contactEvents', 'ladderCeilingEvents'):
        if key in event['schedule']:
            event['schedule'][key] = [dict(c, offset=c['offset'] + offset) for c in event['schedule'][key]]
    event['schedule'].pop('generatedUnits', None)
    event['schedule'].pop('movementUnits', None)
    return event


def _events(events, trace):
    n = trace['ticks']
    result = copy.deepcopy(events)
    for index, event in enumerate(result):
        s = event['schedule']
        next_context = next((other['schedule']['constraintStart'] for other in result[index + 1:]
                             if other['column']['id'] == event['column']['id']), n + 1)
        s['constraintEnd'] = next_context
        entry, exit = s['entryState'], s['exitState']
        s.update(ticks=n, ladderTicks=[entry <= t < exit for t in range(n + 1)],
                 hit=(s['hit'] + [False] * n)[:n], sneak=trace['sneak'][:], trace=copy.deepcopy(trace),
                 postExitTicks=n - exit)
        from parkour_solver.scene.ladders import region_contains
        from parkour_solver.movement.units import exit_matches, ladder_units
        if any(inside and not region_contains(event['column'], [event['column']['x'], y, event['column']['z']])
               for inside, y in zip(s['ladderTicks'], trace['position'])):
            return None
        if not exit_matches(s['exitKind'], trace['position'][exit], event['column']):
            return None
        try:
            ladder_units(s, event['column']['id'])
        except ValueError:
            return None
    return result


def append_choice(program, choice, *, max_inputs=200):
    old, tail = program['profile'], choice['profile']
    cut, size = old['duration'], tail['duration']
    integer(size, 'movement choice duration', 1, 200)
    if cut + size > min(max_inputs, 200):
        return None
    expected = boundary_state(program, tail['ladderTicks'][0])
    if choice.get('initial') != expected:
        raise ValueError('Movement choice consumes a different scalar boundary state')
    # Ladder appends still rebuild the whole program (deferred); so does the first append, which has no trace.
    ladder = program['ladderEvents'] or choice.get('ladderEvents') or any(old['ladderTicks']) or any(tail['ladderTicks'])
    built = _rebuild(program, choice) if ladder or program['trace'] is None else _extend(program, choice)
    if built is None:
        return None
    profile, events, units, trace = built
    profile['constraintOwners'] = old.get('constraintOwners', [0]*len(old['positionConstraints'])) + [
        cut+owner if owner else 0 for owner in tail.get('constraintOwners', [0]*len(tail['positionConstraints']))]
    # parentTicks: the reachability travel check pairs only states after the parent's with nearby ones.
    result = dict(initial=copy.deepcopy(program['initial']), profile=profile, ladderEvents=events,
                  movementUnits=units, terminal='open', terminalSupport=choice.get('terminalSupport'),
                  interactions=program['interactions'] + int(choice.get('interaction', False)), trace=trace,
                  parentTicks=cut)
    for key in ('threshold', 'guideSeedYaw', 'repairState', 'solveScope', 'committedThrough', 'settledThrough', 'walkOffs'):
        if key in program:
            if key == 'repairState':
                scope=program[key].get('context', {}).get('scope') or {}
                if scope.get('solveIdentity') or (scope.get('scope') or {}).get('solveIdentity'):
                    continue
            result[key] = copy.deepcopy(program[key])
    # The parent's states before its last are the child's too, so its key resumes there.
    result['prefixChain'] = prefix_chain(program)
    return result


def _extend(program, choice):
    """Trace only the new ticks from the parent's end state and share the parent's content: a built program's
    content never changes, and appending never rewrites a parent state (tracing is Markov in Y, VY and ground)."""
    old, tail, parent = program['profile'], choice['profile'], program['trace']
    cut, size = old['duration'], tail['duration']
    # New contact events land after the parent's last state, so only they need merging.
    contacts = _contacts([dict(c, offset=cut + c['offset']) for c in tail['contactEvents']])
    hit = [any(c['axis'] != 'Y' and c['offset'] == cut + t for c in contacts) for t in range(size)]
    from parkour_solver.physics.vertical import f32
    piece = trace_ladder_schedule(boundary_state(program), tail['ladderTicks'], tail['sneak'], hit,
        jump=[f32(.42) if value else None for value in tail['jump']], contacts=tail['contacts'], stop_on_landing=False,
        threshold=program.get('threshold', .005))
    if piece['status'] == 'invalid' or piece['ticks'] != size:
        return None
    if piece['position'] != tail['position'] or piece['velocity'] != tail['velocity'] or piece['onGround'] != tail['grounded']:
        return None
    vertical = parent['vertical']
    # A truncated parent keeps its trace status, so recompute whether the prefix landed.
    landed = piece['status'] == 'landed' or any(
        c['direction'] < 0 and not c.get('case') and parent['onGround'][c['state']] for c in vertical['contacts'])
    trace = dict(status='landed' if landed else 'simulated', ticks=cut + size,
        position=parent['position'] + piece['position'][1:], velocity=parent['velocity'][:cut] + piece['velocity'],
        onGround=parent['onGround'] + piece['onGround'][1:], movement=parent['movement'] + piece['movement'],
        climbRefreshed=parent['climbRefreshed'] + piece['climbRefreshed'], failures=[],
        vertical=dict(vertical, jumpVelocity=vertical['jumpVelocity'] + piece['vertical']['jumpVelocity'],
                      sneak=vertical['sneak'] + piece['vertical']['sneak'],
                      contacts=vertical['contacts'] + [dict(c, state=cut + c['state']) for c in piece['vertical']['contacts']]),
        ladderTicks=parent['ladderTicks'] + piece['ladderTicks'][1:], sneak=parent['sneak'] + piece['sneak'],
        hit=parent['hit'] + piece['hit'], resume=piece['resume'])
    cases = []
    for event in tail['caseEvents']:
        event = copy.deepcopy(event)
        event['input'] += cut
        event['anchors'] = {k: v + cut for k, v in event.get('anchors', {}).items()}
        cases.append(event)
    constraints = [dict(row, t1=cut + row['t1']) for row in tail['positionConstraints']]
    region_choices = [dict(entry, offset=cut + entry['offset'], options=[[dict(row, offset=cut + row['offset']) for row in rows]
                                                                  for rows in entry['options']])
                      for entry in tail.get('regionChoices', [])]
    from parkour_solver.movement.ladder.ceiling import own_ceiling_contacts
    try:
        contacts, units = own_ceiling_contacts(contacts, [shifted_unit(unit, cut) for unit in tail['generatedUnits']])
    except ValueError:
        return None
    compile_units(size, tail['generatedUnits'])
    units = old['generatedUnits'] + units
    profile = _profile(trace, old['contactEvents'] + contacts, old['caseEvents'] + cases,
                       old['positionConstraints'] + constraints, list(units), tail['landingPhase'],
                       old.get('regionChoices', []) + region_choices)
    return profile, [], units, trace


def _rebuild(program, choice):
    old, tail = program['profile'], choice['profile']
    cut, size = old['duration'], tail['duration']
    jump = old['jump'] + tail['jump']
    sneak = old['sneak'] + tail['sneak']
    ladder = old['ladderTicks'][:-1] + [old['ladderTicks'][-1] or tail['ladderTicks'][0]] + tail['ladderTicks'][1:]
    vertical = [{k: v for k, v in c.items() if k != 'axis'} for c in merge_contacts(
        [dict(c, axis=1) for c in old['contacts']] + [dict(c, state=cut + c['state'], axis=1) for c in tail['contacts']])]
    contacts = _contacts(old['contactEvents'] + [dict(c, offset=cut + c['offset']) for c in tail['contactEvents']])
    hit = [any(c['axis'] != 'Y' and c['offset'] == t for c in contacts) for t in range(cut + size)]
    from parkour_solver.physics.vertical import f32
    trace = trace_ladder_schedule(program['initial'], ladder, sneak, hit,
        jump=[f32(.42) if value else None for value in jump], contacts=vertical, stop_on_landing=False,
        threshold=program.get('threshold', .005))
    if trace['status'] == 'invalid' or trace['ticks'] != cut + size:
        return None
    if trace['position'][:cut + 1] != old['position'] or trace['onGround'][:cut + 1] != old['grounded']:
        raise ValueError('Appending a choice rewrote a prior position or ground state')
    if trace['velocity'][:cut] != old['velocity'][:cut]:
        raise ValueError('Appending a choice rewrote a prior velocity')
    if trace['position'][cut:] != tail['position'] or trace['velocity'][cut:] != tail['velocity'] or trace['onGround'][cut:] != tail['grounded']:
        return None
    cases = copy.deepcopy(old['caseEvents'])
    for event in tail['caseEvents']:
        event = copy.deepcopy(event)
        event['input'] += cut
        event['anchors'] = {k: v + cut for k, v in event.get('anchors', {}).items()}
        cases.append(event)
    constraints = old['positionConstraints'] + [dict(row, t1=cut + row['t1']) for row in tail['positionConstraints']]
    region_choices = old.get('regionChoices', []) + [
        dict(entry, offset=cut + entry['offset'], options=[[dict(row, offset=cut + row['offset']) for row in rows]
                                                         for rows in entry['options']])
        for entry in tail.get('regionChoices', [])]
    units = copy.deepcopy(old['generatedUnits']) + [shifted_unit(unit, cut) for unit in tail['generatedUnits']]
    events = _events(program['ladderEvents'] + [_shift_event(e, cut) for e in choice.get('ladderEvents', [])], trace)
    if events is None:
        return None
    if events:
        from parkour_solver.movement.units import movement_program, overlay_program
        templates = units
        units = overlay_program(movement_program(trace['ticks'], events),
                                [u for u in templates if not u['type'].startswith('ladder_')])
        for unit in units:
            if unit['type'].startswith('ladder_'):
                effects = [copy.deepcopy(u) for u in templates if u['type'] not in ('normal', 'ladder_climb', 'ladder_fall')
                           and u['start'] < unit['end'] and u['end'] > unit['start']]
                if effects:
                    unit['effects'] = effects
            unit['jumpInputs'] = [t for t in range(unit['start'], unit['end']) if jump[t]]
            unit['groundedInputs'] = [t for t in range(unit['start'], unit['end']) if trace['onGround'][t]]
            unit['sneakInputs'] = [t for t in range(unit['start'], unit['end']) if sneak[t]]
    from parkour_solver.movement.ladder.ceiling import own_ceiling_contacts
    try:
        contacts, units = own_ceiling_contacts(contacts, units)
    except ValueError:
        return None
    for event in events:
        s = event['schedule']
        s['ladderCeilingEvents'] = [copy.deepcopy(c) for c in contacts if c.get('effect') == 'ladder_ceiling'
            and c.get('ownerRegion') == event['column']['id'] and max(0, s['entryState'] - 1) <= c['offset'] < s['exitState']]
    profile = profile_from_trace(trace, contacts=contacts, cases=cases, constraints=constraints,
                                 units=units, landing_phase=tail['landingPhase'], region_choices=region_choices)
    masks = compile_units(profile['duration'], units)
    if events and masks['ladderTicks'] != ladder:
        raise ValueError('Movement units disagree with composed ladder membership')
    return profile, events, units, trace


def truncate_program(program, ticks):
    n = program['profile']['duration']
    integer(ticks, 'prefix ticks', 1, n)
    if ticks == n:
        return copy.deepcopy(program)
    if program['ladderEvents'] or any(program['profile']['ladderTicks']):
        raise ValueError('Checkpoint truncation currently supports non-ladder programs')
    result = copy.deepcopy(program)
    result.pop('parentTicks', None)
    result.pop('prefixChain', None)
    profile = result['profile']
    profile['duration'] = ticks
    for key in ('position', 'velocity', 'grounded', 'ladderTicks'):
        profile[key] = profile[key][:ticks+1]
    for key in ('jump', 'sneak'):
        profile[key] = profile[key][:ticks]
    profile['contacts'] = [c for c in profile['contacts'] if c['state'] <= ticks]
    profile['contactEvents'] = [c for c in profile['contactEvents'] if c['offset'] < ticks]
    profile['caseEvents'] = [c for c in profile['caseEvents'] if c['input'] < ticks]
    region_choices = [c for c in profile.pop('regionChoices', []) if c['offset'] <= ticks]
    if region_choices:
        profile['regionChoices'] = region_choices
    for event in profile['caseEvents']:
        event['anchors'] = {k: v for k, v in event.get('anchors', {}).items() if v <= ticks}
    rows = [(r, owner) for r, owner in zip(profile['positionConstraints'],
        profile.get('constraintOwners', [0]*len(profile['positionConstraints'])))
        if r['t1'] <= ticks and (r.get('t2') is None or r['t2'] <= ticks)
        and (not owner or owner <= ticks or program.get('committedThrough', 0) >= owner)]
    profile['positionConstraints'] = [r for r, owner in rows]
    profile['constraintOwners'] = [owner for r, owner in rows]
    from parkour_solver.movement.units import profile_program
    profile['generatedUnits'] = profile_program(profile)
    profile['landingPhase'] = 'post'
    result.update(movementUnits=copy.deepcopy(profile['generatedUnits']), terminal='open', terminalSupport=None,
                  interactions=sum(u['type'] != 'normal' for u in profile['generatedUnits']))
    trace = result['trace']
    for key in ('position', 'velocity', 'onGround', 'ladderTicks'):
        trace[key] = trace[key][:ticks+1]
    for key in ('movement', 'climbRefreshed', 'sneak', 'hit'):
        trace[key] = trace[key][:ticks]
    trace['ticks'] = ticks
    trace['vertical']['jumpVelocity'] = trace['vertical']['jumpVelocity'][:ticks]
    trace['vertical']['sneak'] = trace['vertical']['sneak'][:ticks]
    trace['vertical']['contacts'] = [c for c in trace['vertical']['contacts'] if c['state'] <= ticks]
    trace['resume'] = dict(position=profile['position'][-1], velocity=profile['velocity'][-1],
                           onGround=profile['grounded'][-1], previousHorizontalCollision=trace['hit'][-1], onLadder=False)
    if 'guideSeedYaw' in result:
        result['guideSeedYaw'] = result['guideSeedYaw'][:ticks]
    if 'settledThrough' in result:
        result['settledThrough'] = min(result['settledThrough'], ticks)
    if 'walkOffs' in result:
        result['walkOffs'] = [w for w in result['walkOffs'] if w['offset'] < ticks]
    return result


# Per-state fields: a program's identity folds them state by state, so a child resumes its parent's chain.
_STATE_FIELDS = ('position', 'velocity', 'grounded', 'jump', 'sneak', 'ladderTicks', 'contacts', 'contactEvents',
                 'caseEvents', 'positionConstraints', 'regionChoices')
_CHAIN_SEED = hashlib.sha256(b'pkc-program-v1').digest()


def _canonical(value):
    return json.dumps(value, sort_keys=True, allow_nan=False).encode()


def _chain(profile, through, anchor=None):
    """sha256 chain over states 0..through, resumed from anchor = [state, hex] when given; returns the hashes at
    through-1 and through. A state owns its position, velocity, ground and ladder flags, the input ending at it,
    and the contacts, events, rows and region choices that act at it; anything past the last state counts at the
    last state."""
    start, h = (anchor[0]+1, bytes.fromhex(anchor[1])) if anchor else (0, _CHAIN_SEED)
    n = profile['duration']
    owned = [dict(contacts=[], contactEvents=[], caseEvents=[], rows=[], regionChoices=[]) for _ in range(start, through+1)]
    for field, items, key, shift in (('contacts', profile['contacts'], 'state', 0),
                                     ('contactEvents', profile['contactEvents'], 'offset', 1),
                                     ('caseEvents', profile['caseEvents'], 'input', 1),
                                     ('regionChoices', profile.get('regionChoices', ()), 'offset', 1)):
        for item in items:
            t = min(item[key]+shift, n)
            if start <= t <= through:
                owned[t-start][field].append(item)
    for row in profile['positionConstraints']:
        t2 = row.get('t2')
        t = min(row['t1'] if t2 is None or t2 < row['t1'] else t2, n)
        if start <= t <= through:
            owned[t-start]['rows'].append({k: v for k, v in row.items() if k != 'name'})
    before = h
    for t, record in enumerate(owned, start):
        record['rows'].sort(key=_canonical)
        record.update(t=t, position=profile['position'][t], velocity=profile['velocity'][t], grounded=profile['grounded'][t],
                      ladder=profile['ladderTicks'][t], jump=profile['jump'][t-1] if t else None,
                      sneak=profile['sneak'][t-1] if t else None)
        before, h = h, hashlib.sha256(h+_canonical(record)).digest()
    return before, h


# id(program) -> (program, prefix chain). A built program never changes, and the entry keeps it alive, so its id
# cannot be reused while cached. Every child of one parent shares the parent's prefix.
_PREFIXES = {}


def prefix_chain(program):
    """[n-1, hex] over a program's states before its last, which a child never rewrites; None for an empty one."""
    n = program['profile']['duration']
    if not n:
        return None
    cached = _PREFIXES.get(id(program))
    if cached is None:
        cached = _remember(program, _chain(program['profile'], n-1, program.get('prefixChain'))[1])
    return cached[1]


def _remember(program, before):
    _PREFIXES[id(program)] = entry = program, [program['profile']['duration']-1, before.hex()]
    while len(_PREFIXES) > 1024:
        _PREFIXES.pop(next(iter(_PREFIXES)))
    return entry


def program_physics_key(program):
    profile = program['profile']
    n = profile['duration']
    before, h = _chain(profile, n, program.get('prefixChain'))
    if n and id(program) not in _PREFIXES:
        _remember(program, before)
    rest = {k: v for k, v in profile.items() if k not in _STATE_FIELDS+('generatedUnits', 'constraintOwners')}
    return hashlib.sha256(h+_canonical([rest, program['terminalSupport'], program.get('threshold', .005),
                                        program['ladderEvents']])).hexdigest()


def program_search_key(program):
    key = program_physics_key(program)
    key += '|owners=' + json.dumps([program['profile'].get('constraintOwners', []), program.get('committedThrough', 0), program.get('interactions', 0), program.get('solveScope'),
                                    program.get('settledThrough', 0)])
    if program.get('repairState') is not None:
        key += '|repair=' + hashlib.sha256(json.dumps(program['repairState'],sort_keys=True).encode()).hexdigest()
    return key


def compile_program(scene, program, *, require_complete=True):
    """The program as a one-leg numerical route on a collision scene, from the start support to the goal support."""
    from parkour_solver.scene.route import prepare_route
    if not program['profile']['duration']:
        raise ValueError('An empty prefix is not a complete movement route')
    if require_complete and (not program['profile']['grounded'][-1] or program['terminalSupport'] != scene['goalBlock']):
        raise ValueError('An open prefix is not a completed goal route')
    by_id = {b['id']: b for b in scene['blocks']}
    supports = [dict(id=name+'@'+str(index), bounds=by_id[name]['bounds'], top=by_id[name]['top'])
                for index, name in enumerate([scene['startBlock'], scene['goalBlock']])]
    profile = copy.deepcopy(program['profile'])
    profile.update(sourceBlock=supports[0]['id'], targetBlock=supports[1]['id'], profileKind='movement')
    route = prepare_route({'version': 1, 'initial': scene['initial'], 'blocks': supports,
                           'inputs': scene['inputs'], 'objective': scene['objective'],
                           'airtimes': [profile['duration']], 'kinds': ['movement'],
                           'landingPhases': [profile.get('landingPhase', 'pre')],
                           'contactEntries': [None], 'walkOffExits': [None],
                           'maxTotalRunTicks': scene['maxTotalRunTicks'],
                           'runBounds': [[0, scene['maxRunTicksPerJump']]],
                           'supportClearance': scene['supportClearance']})
    route['profileDerivation'] = {'kind': 'selected_height_profiles', 'suppliedAirtimes': False,
                                  'verticalModel': 'parkour_solver.physics.vertical', 'solidSideFaces': False}
    route.update(sharedMovement=True, ceilingEvents=[], verticalProfiles=[profile],
                 ladderEvents=[dict(event, leg=0) for event in program['ladderEvents']],
                 contactEvents=[dict(event, leg=0) for event in profile.get('contactEvents', [])])
    route.update(maxTotalRunTicks=0, runBounds=[[0, 0]])
    if not require_complete:
        route['terminalSupportRequired'] = False
    if 'threshold' in program:
        route['physicsThreshold'] = program['threshold']
    if 'guideSeedYaw' in program:
        route['warmYaw'] = program['guideSeedYaw'][:]
    return {'status': 'derived', 'route': route, 'profiles': [profile]}
