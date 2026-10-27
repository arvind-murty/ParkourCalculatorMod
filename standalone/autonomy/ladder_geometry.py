from .obstacle_geometry import BODY_HEIGHT
from .route import HALF_WIDTH


def ladder_columns(cells):
    if not isinstance(cells, list):
        raise ValueError('Climbable cells require a list of integer X/Y/Z cells')
    grouped = {}
    for cell in cells:
        if not isinstance(cell, list) or len(cell) != 3 or any(type(v) is not int for v in cell):
            raise ValueError('Climbable cells require integer X/Y/Z')
        grouped.setdefault((cell[0], cell[2]), set()).add(cell[1])
    columns = []
    for (x, z), heights in sorted(grouped.items()):
        start = previous = None
        for y in sorted(heights):
            if previous is not None and y != previous + 1:
                columns.append({'id': str((x, start, z)), 'x': x, 'z': z, 'minY': start, 'maxY': previous + 1})
                start = None
            if start is None:
                start = y
            previous = y
        if start is not None:
            columns.append({'id': str((x, start, z)), 'x': x, 'z': z, 'minY': start, 'maxY': previous + 1})
    return columns


def climb_walls(column, solids):
    groups = {}
    for index, box in enumerate(solids):
        if box[1] >= column['maxY'] + BODY_HEIGHT or box[4] <= column['minY']:
            continue
        for axis, a, tangent, c in [('X', 0, 2, column['x']), ('Z', 2, 0, column['z'])]:
            cell_tangent = column['z'] if axis == 'X' else column['x']
            if box[tangent] >= cell_tangent + 1 + HALF_WIDTH or box[tangent + 3] <= cell_tangent - HALF_WIDTH:
                continue
            for direction in (-1, 1):
                plane = box[a] - HALF_WIDTH if direction > 0 else box[a + 3] + HALF_WIDTH
                if c <= plane < c + 1:
                    key = axis, direction, plane, box[tangent], box[tangent + 3]
                    groups.setdefault(key, []).append((index, box))
    return [{'axis': axis, 'direction': direction, 'plane': plane,
             'parts': [{'solidIndex': i, 'box': b} for i, b in parts]}
            for (axis, direction, plane, _, _), parts in groups.items()]


def entry_channels(column, wall):
    axis = wall['axis']
    side = 'Z' if axis == 'X' else 'X'
    result = []
    for family, face_axis, direction in [('front', axis, -wall['direction']), ('side', side, -1), ('side', side, 1)]:
        base = column['x'] if face_axis == 'X' else column['z']
        result.append({'family': family, 'channel': family + ':' + face_axis + ':' + str(direction),
                       'axis': face_axis, 'direction': direction, 'plane': base + (direction > 0)})
    return result


def outside_row(face, state, margin, name):
    return {'mode': face['axis'], 't1': state, 'cmp': 'GE' if face['direction'] > 0 else 'LE',
            'rhs': face['plane'] + face['direction'] * margin, 'name': name}


def schedule_constraints(schedule, column, wall, entry_face, exit_face, *, start=0, margin=1e-6):
    rows, contacts, scene_contacts = [], [], []
    ys = schedule['trace']['position']
    ladder = schedule['ladderTicks']
    for state, (y, inside) in enumerate(zip(ys, ladder)):
        at = start + state
        if inside:
            for axis, value in [('X', column['x']), ('Z', column['z'])]:
                rows.extend([{'mode': axis, 't1': at, 'cmp': 'GE', 'rhs': value + margin, 'name': 'ladder:inside'},
                             {'mode': axis, 't1': at, 'cmp': 'LE', 'rhs': value + 1 - margin, 'name': 'ladder:inside'}])
        elif state < schedule['entryState']:
            rows.append(outside_row(entry_face, at, margin, 'ladder:before_entry'))
        elif column['minY'] <= y < column['maxY'] and exit_face is not None:
            rows.append(outside_row(exit_face, at, margin, 'ladder:after_exit'))
        if any(y < part['box'][4] and y + BODY_HEIGHT > part['box'][1] for part in wall['parts']):
            rows.append({'mode': wall['axis'], 't1': at, 'cmp': 'LE' if wall['direction'] > 0 else 'GE',
                         'rhs': wall['plane'], 'name': 'ladder:wall_avoid'})
    for tick, hit in enumerate(schedule['hit']):
        if not hit:
            continue
        y = ys[tick + 1]
        part = next((p for p in wall['parts'] if y < p['box'][4] and y + BODY_HEIGHT > p['box'][1]), None)
        if part is None:
            raise ValueError('Selected climb has no finite wall at its Y stage')
        box = part['box']
        axis = 0 if wall['axis'] == 'X' else 1
        contacts.append({'state': start + tick + 1, 'axis': axis, 'plane': wall['plane'], 'direction': wall['direction']})
        scene_contacts.append(dict(contacts[-1], axis=0 if axis == 0 else 2, box=box))
        tangent, a, stage = ('Z', 2, tick) if axis == 0 else ('X', 0, tick + 1)
        rows.extend([{'mode': tangent, 't1': start + stage, 'cmp': 'GE', 'rhs': box[a] - HALF_WIDTH + margin,
                      'name': 'ladder:finite_face'},
                     {'mode': tangent, 't1': start + stage, 'cmp': 'LE', 'rhs': box[a + 3] + HALF_WIDTH - margin,
                      'name': 'ladder:finite_face'}])
    return rows, contacts, scene_contacts


def exit_faces(column, target):
    faces = [{'axis': axis, 'direction': sign, 'plane': column[key] + (sign > 0)}
             for axis, key in [('X', 'x'), ('Z', 'z')] for sign in (-1, 1)]
    return sorted(faces, key=lambda f: abs((target[0] if f['axis'] == 'X' else target[1]) - f['plane'])
                  - f['direction'] * ((target[0] if f['axis'] == 'X' else target[1]) - f['plane']))
