from standalone.autonomy.obstacle_geometry import BODY_HEIGHT
from standalone.autonomy.route import HALF_WIDTH


def face(axis, direction, plane, states, *, contacts=(), initial=None, clearance=2e-7):
    index = 0 if axis == 'X' else 2
    boundary = {c['state'] for c in contacts if c['axis'] == index and c['plane'] == plane}
    if initial is not None and initial[index // 2] == plane:
        boundary.add(0)
    return dict(axis=axis, direction=direction, rows=[dict(mode=axis, t1=t,
        cmp='GE' if direction > 0 else 'LE', rhs=plane + (0 if t in boundary else direction * clearance),
        name='mip:clearance') for t in states])


def disjunctions(geometry, heights, *, contacts=(), initial=None, clearance=2e-7):
    result = []
    def outside(axis, direction, plane, states):
        return face(axis, direction, plane, states, contacts=contacts, initial=initial, clearance=clearance)
    for tick, (before, after) in enumerate(zip(heights, heights[1:])):
        for index, box in enumerate(geometry['solids']):
            lo_x, hi_x = box[0] - HALF_WIDTH, box[3] + HALF_WIDTH
            lo_z, hi_z = box[2] - HALF_WIDTH, box[5] + HALF_WIDTH
            if min(before, after) < box[4] and max(before, after) + BODY_HEIGHT > box[1]:
                result.append(dict(tick=tick, solidIndex=index, stage='Y', options=[
                    outside('X', -1, lo_x, [tick]), outside('X', 1, hi_x, [tick]),
                    outside('Z', -1, lo_z, [tick]), outside('Z', 1, hi_z, [tick])]))
            if after < box[4] and after + BODY_HEIGHT > box[1]:
                result.append(dict(tick=tick, solidIndex=index, stage='X', options=[
                    outside('X', -1, lo_x, [tick, tick + 1]), outside('X', 1, hi_x, [tick, tick + 1]),
                    outside('Z', -1, lo_z, [tick]), outside('Z', 1, hi_z, [tick])]))
                result.append(dict(tick=tick, solidIndex=index, stage='Z', options=[
                    outside('X', -1, lo_x, [tick + 1]), outside('X', 1, hi_x, [tick + 1]),
                    outside('Z', -1, lo_z, [tick, tick + 1]), outside('Z', 1, hi_z, [tick, tick + 1])]))
    return result


def satisfied(row, positions, tolerance=0):
    value = positions[row['t1']][0 if row['mode'] == 'X' else 1]
    residual = value - row['rhs']
    return residual <= tolerance if row['cmp'] == 'LE' else residual >= -tolerance


def selected_rows(groups, selected):
    if len(groups) != len(selected):
        raise ValueError('A face is required for every collision stage')
    rows = {}
    for group, index in zip(groups, selected):
        if type(index) is not int or not 0 <= index < len(group['options']):
            raise ValueError('Invalid selected clearance face')
        for row in group['options'][index]['rows']:
            rows[(row['mode'], row['t1'], row['cmp'], row['rhs'])] = row.copy()
    return list(rows.values())
