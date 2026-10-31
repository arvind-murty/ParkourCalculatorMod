from standalone.autonomy.blockages import AXES, DEFAULT_CAPS, blockage_counts, consecutive, faces, intervals


def add_passages(model, coordinates, bounds, case, horizon):
    from pyscipopt import quicksum
    scene = case['scene']
    caps = dict(horizontal=scene['maxHorizontalBlockagesPerObstacle'], vertical=scene['maxVerticalBlockagesPerObstacle'],
                total=scene['maxBlockagesPerObstacle'])
    records, usage = [], {}
    for index, box in enumerate(scene['collisionScene']['solids']):
        block = next(b for b in scene['blocks'] if b['solidIndex'] == index)
        chain_faces = faces(box)
        if any(all(face['direction'] * ((lo if face['direction'] > 0 else hi) - face['plane'])
                   >= (0. if face['axis'] == 'Y' else 2e-7)
                   for tick in bounds for lo, hi in [tick[AXES.index(face['axis'])]]) for face in chain_faces):
            continue
        available = [f for f, face in enumerate(chain_faces) if any(
            face['direction'] * ((hi if face['direction'] > 0 else lo) - face['plane']) >= (0. if face['axis'] == 'Y' else 2e-7)
            for tick in bounds for lo, hi in [tick[AXES.index(face['axis'])]])]
        cap = min(caps['total'], caps['horizontal']) if all(chain_faces[f]['axis'] != 'Y' for f in available) else caps['total']
        slots = [[model.addVar(f'face_{index}_{slot}_{f}', vtype='B', ub=1 if f in available else 0)
                  for f in range(6)] for slot in range(cap + 1)]
        model.addCons(quicksum(slots[0]) == 1)
        gates, enabled, pairs = [], [], []
        for slot in range(cap):
            on = model.addVar(f'gate_on_{index}_{slot}', vtype='B')
            enabled.append(on)
            if slot:
                model.addCons(on <= enabled[slot - 1])
            model.addCons(quicksum(slots[slot + 1]) == on)
            transitions = {(a, b): model.addVar(f'edge_{index}_{slot}_{a}_{b}', vtype='B')
                           for a in available for b in available if chain_faces[a]['axis'] != chain_faces[b]['axis']}
            for (a, b), value in transitions.items():
                model.addCons(value <= slots[slot][a])
                model.addCons(value <= slots[slot + 1][b])
                kind = 'vertical' if 'Y' in (chain_faces[a]['axis'], chain_faces[b]['axis']) else 'horizontal'
                counts = usage.setdefault(block['sourceBlock'], {k: [] for k in DEFAULT_CAPS})
                counts[kind].append(value)
                counts['total'].append(value)
            model.addCons(quicksum(transitions.values()) == on)
            first = [model.addVar(f'gate_first_{index}_{slot}_{t}', vtype='B') for t in range(horizon + 1)]
            second = [model.addVar(f'gate_second_{index}_{slot}_{t}', vtype='B') for t in range(horizon + 1)]
            model.addCons(quicksum(first) == on)
            model.addCons(quicksum(second) == on)
            t_first = quicksum(t * v for t, v in enumerate(first))
            t_second = quicksum(t * v for t, v in enumerate(second))
            shift = quicksum(v for (a, b), v in transitions.items() if consecutive(chain_faces[a], chain_faces[b]))
            model.addCons(t_second == t_first + shift)
            if slot:
                previous = quicksum(t * v for t, v in enumerate(gates[-1][1]))
                model.addConsIndicator(previous - t_first <= 0, binvar=on)
            gates.append((first, second))
            pairs.append(transitions)
        for slot in range(cap + 1):
            for tick in range(horizon + 1):
                entered = 1 if slot == 0 else quicksum(gates[slot - 1][1][:tick + 1])
                left = quicksum(gates[slot][0][:tick]) if slot < cap else 0
                active = model.addVar(f'face_active_{index}_{slot}_{tick}', lb=0, ub=1)
                model.addCons(active == entered - left)
                for f, face in enumerate(chain_faces):
                    axis, sign, plane = AXES.index(face['axis']), face['direction'], face['plane']
                    lo, hi = bounds[tick][axis]
                    margin = 0. if axis == 1 else 2e-7
                    if tick == 0 and case['route']['initial']['position'][axis] == plane:
                        margin = 0.
                    worst = lo - plane if sign > 0 else plane - hi
                    big_m = max(0., margin - worst)
                    model.addCons(sign * (coordinates[tick][axis] - plane) >= margin - big_m * (2 - active - slots[slot][f]))
        records.append(dict(solidIndex=index, blockId=block['sourceBlock'], faces=chain_faces, slots=slots,
                            gates=gates, enabled=enabled, pairs=pairs))
    for counts in usage.values():
        for kind, values in counts.items():
            model.addCons(quicksum(values) <= caps[kind])
    return records


def extract_passages(solution, records, horizon, *, initial=None):
    passages, rows = [], {}
    for record in records:
        length = sum(float(solution[v]) > .5 for v in record['enabled'])
        chain = [record['faces'][max(range(6), key=lambda f: float(solution[slot[f]]))]
                 for slot in record['slots'][:length + 1]]
        gates = [max(range(horizon + 1), key=lambda t: float(solution[first[t]]))
                 for first, second in record['gates'][:length]]
        ranges = intervals(chain, gates, 0, horizon)
        if ranges is None:
            raise ValueError('Invalid selected passage timing')
        passage = dict(solidIndex=record['solidIndex'], blockId=record['blockId'], faces=chain, gates=gates,
                       blockageCounts=blockage_counts(chain))
        passages.append(passage)
        for face, (begin, end) in zip(chain, ranges):
            if face['axis'] == 'Y':
                continue
            for tick in range(begin, end + 1):
                key = face['axis'], tick, face['direction'], face['plane']
                margin = 0. if tick == 0 and initial is not None and initial[AXES.index(face['axis'])] == face['plane'] else 2e-7
                rows[key] = dict(mode=face['axis'], t1=tick, cmp='GE' if face['direction'] > 0 else 'LE',
                                 rhs=face['plane'] + face['direction'] * margin, name='joint:blockage')
    return passages, list(rows.values())
