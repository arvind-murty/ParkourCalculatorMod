import copy
from standalone.ipopt.ladder import f32, vertical_move, vertical_step, vertical_carry
from .contact_combinations import merge_contacts, contact_combinations
from .contact_profiles import contact_profile, contact_opportunities
from .ladder_schedules import trace_ladder_schedule
from .obstacle_geometry import BODY_HEIGHT


def climb_contacts(schedule, wall):
    contacts = []
    for t, hit in enumerate(schedule['hit']):
        if not hit:
            continue
        y = schedule['trace']['position'][t + 1]
        part = next((p for p in wall['parts'] if y < p['box'][4] and y + BODY_HEIGHT > p['box'][1]), None)
        if part is None:
            raise ValueError('Climb hit has no finite face')
        contacts.append({'leg': 0, 'offset': t, 'axis': wall['axis'], 'plane': wall['plane'],
                         'direction': wall['direction'], 'box': part['box'], 'role': 'climb'})
    return contacts


def with_contacts(schedule, extra, column, wall, target_top, *, max_inputs=200):
    s = copy.deepcopy(schedule)
    try:
        base = climb_contacts(s, wall) + s.get('contactEvents', [])
        original = {(e['offset'], e['axis'], e['plane'], e['direction']) for e in base}
        if all((e['offset'], e['axis'], e['plane'], e['direction']) in original for e in extra):
            return None
        selected = merge_contacts([dict(e, state=e['offset'] + 1) for e in base + extra])
    except ValueError:
        return None
    profile = contact_profile(s['trace']['position'][0], target_top, selected, max_inputs=max_inputs,
        ladder=s['ladderTicks'], sneak=s['sneak'], initial_velocity=s['trace']['velocity'][0],
        initial_ground=s['trace']['onGround'][0], jump_at_start=s['trace']['vertical']['jumpVelocity'][0] is not None,
        min_landing_state=s['exitState'])
    if profile is None:
        return None
    n, ys = profile['airtime'], profile['position']
    if any(inside and not column['minY'] <= y < column['maxY'] for y, inside in zip(ys, profile['ladderTicks'])):
        return None
    if s['exitState'] > n or (s['exitKind'] == 'top' and ys[s['exitState']] < column['maxY']) \
            or (s['exitKind'] == 'side' and not column['minY'] <= ys[s['exitState']] < column['maxY']):
        return None
    actual = [any(e['axis'] != 'Y' and e['offset'] == t for e in selected) for t in range(n)]
    trace = trace_ladder_schedule({'position': ys[0], 'velocity': profile['velocity'][0], 'onGround': profile['grounded'][0]},
        profile['ladderTicks'], profile['sneak'], actual,
        jump=[f32(.42) if j else None for j in profile['jump']], contacts=profile['contacts'])
    if trace['status'] != 'landed':
        return None
    s.update(ticks=n, hit=(s['hit'] + [False] * n)[:n], sneak=profile['sneak'], ladderTicks=profile['ladderTicks'],
             trace=trace, postExitTicks=n - s['exitState'],
             contactEvents=[{k: v for k, v in e.items() if k != 'state'} for e in selected if e['role'] != 'climb'])
    return s


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


def mixed_ladder_schedules(schedules, solids, column, wall, target_top, *, max_variants=16,
                           max_events=3, max_inputs=200, cancel_event=None):
    added, attempted, work = [], 0, max_variants * 100
    if not schedules:
        return {'schedules': [], 'attempted': 0, 'limit': max_variants}
    def variants(s):
        for box in solids:
            sequence = ceiling_sequence(s, box)
            if sequence:
                yield sequence
        opportunities = list(contact_opportunities(solids, s['ticks']))
        windows = [{'leg': 0, 'start': 0, 'end': s['ticks'] - 1}]
        yield from contact_combinations(opportunities, max_events=max_events, limit=work,
                                       ladder_windows=windows, cancel_event=cancel_event)
    streams = [(s, iter(variants(s))) for s in schedules[:max_variants]]
    signatures = set()
    while streams and len(added) < max_variants and attempted < work:
        for s, stream in streams[:]:
            if cancel_event is not None and cancel_event.is_set():
                streams = []
                break
            try:
                extra = next(stream)
            except StopIteration:
                streams.remove((s, stream))
                continue
            attempted += 1
            result = with_contacts(s, extra, column, wall, target_top, max_inputs=max_inputs)
            if result is None:
                continue
            key = (tuple(result['hit']), tuple(result['sneak']), tuple(result['ladderTicks']),
                   tuple((e['offset'], e['axis'], e['plane'], e['direction']) for e in result['contactEvents']))
            if key not in signatures:
                signatures.add(key)
                added.append(result)
            if len(added) >= max_variants or attempted >= work:
                break
    return {'schedules': added, 'attempted': attempted, 'limit': max_variants,
            'complete': not streams, 'routeSpecificPruning': False}
