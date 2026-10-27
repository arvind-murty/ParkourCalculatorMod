from .ladder_geometry import schedule_constraints


def validate_ladder_events(route):
    entries = route.setdefault('ladderEvents', [])
    if not isinstance(entries, list):
        raise ValueError('ladderEvents must be a list')
    seen = set()
    for event in entries:
        leg = event.get('leg')
        if type(leg) is not int or not 0 <= leg < len(route['airtimes']) or leg in seen:
            raise ValueError('One ladder interaction is allowed per transition')
        seen.add(leg)
        if event['schedule']['ticks'] != route['airtimes'][leg] or not route.get('sharedMovement'):
            raise ValueError('Ladder interaction requires matching duration and shared movement')


def compile_ladder_events(route, events, ticks):
    ladder, sneak = [False] * (ticks + 1), [False] * ticks
    rows, contacts, scene_contacts = [], [], []
    for selected in route.get('ladderEvents', []):
        if selected['leg'] >= len(events):
            continue
        event = events[selected['leg']]
        s = selected['schedule']
        if event['landing'] is None:
            continue
        offset = event['jump']
        ladder[offset:offset + s['ticks'] + 1] = s['ladderTicks']
        sneak[offset:offset + s['ticks']] = s['sneak']
        walls, hits, scene_hits = schedule_constraints(s, selected['column'], selected['wall'],
            selected['entryFace'], selected['exitFace'], start=offset, margin=max(route['supportClearance'], 2e-7))
        rows.extend(walls)
        contacts.extend(hits)
        scene_contacts.extend(scene_hits)
        if s['entryState'] == 0:
            for state in range(event['landed'], event['jump']):
                ladder[state] = True
                for axis, key in [('X', 'x'), ('Z', 'z')]:
                    value = selected['column'][key]
                    rows.extend([{'mode': axis, 't1': state, 'cmp': 'GE', 'rhs': value + 1e-6, 'name': 'ladder:run_inside'},
                                 {'mode': axis, 't1': state, 'cmp': 'LE', 'rhs': value + 1 - 1e-6, 'name': 'ladder:run_inside'}])
    return rows, contacts, scene_contacts, ladder, sneak
