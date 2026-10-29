import copy
import time

from .ladder_episodes import compose_ladder_episodes
from .ladder_geometry import entry_channels, exit_faces
from .ladder_schedules import generate_ladder_schedules


def transfer_programs(first, second_column, second_wall, target, *, max_inputs=64, max_entry=14,
                      max_programs=16, deadline=None, cancel_event=None):
    s = first['schedule']
    first = copy.deepcopy(first)
    first['exitFace'] = exit_faces(first['column'], [(second_column['x'] + second_column.get('maxX', second_column['x'] + 1)) / 2,
                                                   (second_column['z'] + second_column.get('maxZ', second_column['z'] + 1)) / 2])[0]
    cut = s['exitState']
    if cut + 2 >= max_inputs or first['column']['id'] == second_column['id']:
        return {'programs': [], 'generated': 0}
    entries, channels = [], {}
    preferred = first['exitFace']
    faces = sorted(entry_channels(second_column, second_wall), key=lambda f:
                   (f['axis'] != preferred['axis'], f['direction'] == preferred['direction']))
    for face in faces:
        for state in range(min(max_entry + 1, 21, max_inputs - cut - 1)):
            channel = face['channel'] + '@' + str(state)
            channels[channel] = face
            entries.append({'family': face['family'], 'channel': channel, 'firstCandidateState': state})
    pool = generate_ladder_schedules({'version': 1,
        'initial': {'position': s['trace']['position'][cut], 'velocity': s['trace']['velocity'][cut], 'onGround': False},
        'region': {k: second_column[k] for k in ('minY', 'maxY')}, 'entries': entries,
        'targetTop': target['top'], 'jumpAtStart': False, 'maxInputs': max_inputs - cut,
        'entryDelay': 0, 'maxSkeletons': 16000, 'maxSchedules': max_programs * 16,
        'maxMaskWork': 200000}, cancel_event=cancel_event, deadline=deadline)
    programmes = []
    target_point = [(target['bounds'][a] + target['bounds'][a + 1]) / 2 for a in (0, 2)]
    groups = {}
    for second in sorted(pool['schedules'], key=lambda x: (x['entryChannel'].split('@')[0] != faces[0]['channel'], x['ticks'], -sum(x['hit']))):
        groups.setdefault(second['entryState'], []).append(second)
    ordered = [group[0] for _, group in sorted(groups.items())]
    ordered.extend(s for _, group in sorted(groups.items()) for s in group[1:])
    for second in ordered:
        if len(programmes) >= max_programs or deadline is not None and time.monotonic() >= deadline:
            break
        if not any(second['hit']):
            continue
        episodes = [{'column': first['column'], 'wall': first['wall'], 'entryState': s['entryState'],
            'exitState': cut, 'hitInputs': [t for t, hit in enumerate(s['hit'][:cut]) if hit],
            'entryFace': first['entryFace'], 'exitFace': first['exitFace'], 'exitKind': s['exitKind']},
            {'column': second_column, 'wall': second_wall, 'entryState': cut + second['entryState'],
             'exitState': cut + second['exitState'],
             'hitInputs': [cut + t for t, hit in enumerate(second['hit']) if hit],
             'entryFace': channels[second['entryChannel']], 'exitFace': exit_faces(second_column, target_point)[0],
             'exitKind': second['exitKind']}]
        contacts = [e for e in s.get('contactEvents', []) if e['offset'] < cut]
        contacts += [dict(e, offset=cut + e['offset']) for e in second.get('contactEvents', [])]
        program = compose_ladder_episodes(s['trace']['position'][0], target['top'], episodes,
            max_inputs=max_inputs, sneak=s['sneak'][:cut] + second['sneak'], contacts=contacts,
            initial_velocity=s['trace']['velocity'][0], initial_ground=s['trace']['onGround'][0],
            jump_at_start=s['trace']['vertical']['jumpVelocity'][0] is not None)
        if program is not None:
            programmes.append(program)
    return {'programs': programmes, 'generated': len(pool['schedules']),
            'generation': {k: v for k, v in pool.items() if k not in ('schedules', 'timingGroups')},
            'unprobedEntries': True, 'entryHorizon': min(max_entry, 20, max_inputs - cut - 2),
            'entryHorizonClipped': max_entry > min(20, max_inputs - cut - 2), 'maxPrograms': max_programs}
