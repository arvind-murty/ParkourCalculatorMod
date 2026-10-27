import heapq
import itertools
import math
import time

from .obstacle_geometry import expanded_box
from .route import center_bounds


AXES = ('X', 'Y', 'Z')
ORDER = {'Y': 0, 'X': 1, 'Z': 2}


def consecutive(first, second):
    return ORDER[first['axis']] > ORDER[second['axis']]


def faces(box):
    bounds = expanded_box(box)
    return [{'axis': axis, 'direction': sign, 'plane': bounds[i + (3 if sign > 0 else 0)]}
            for i, axis in enumerate(AXES) for sign in (-1, 1)]


def outside(face, point, clearance=0):
    return face['direction'] * (point[AXES.index(face['axis'])] - face['plane']) >= clearance


def passage_kind(chain):
    count = sum(a['axis'] != 'Y' and b['axis'] != 'Y' for a, b in zip(chain, chain[1:]))
    return 'cross_neo' if count == 1 else 'wide_neo' if count == 2 else 'vertical_passage' if count == 0 else 'long_neo'


def intervals(chain, gates, begin, end):
    start = begin
    result = []
    for i, gate in enumerate(gates):
        if gate < start or gate > end:
            return None
        result.append((start, gate))
        start = gate + int(consecutive(chain[i], chain[i + 1]))
    if start > end:
        return None
    result.append((start, end))
    return result


def face_chains(length):
    yield from itertools.permutations(range(6), length + 1)
    if length >= 2:
        for prefix in itertools.permutations(range(6), length):
            yield prefix + (prefix[0],)


def validate_passages(route):
    passages = route.setdefault('passages', [])
    if not isinstance(passages, list):
        raise ValueError('Passages must be a list')
    for passage in passages:
        leg = passage.get('leg') if isinstance(passage, dict) else None
        if type(leg) is not int or not 0 <= leg < len(route['airtimes']):
            raise ValueError('Invalid passage leg')
        chain, gates = passage.get('faces'), passage.get('gates')
        if not isinstance(chain, list) or not 1 <= len(chain) <= 5 or not isinstance(gates, list) or len(gates) != len(chain) - 1:
            raise ValueError('Invalid blockage chain')
        for face in chain:
            if not isinstance(face, dict) or face.get('axis') not in AXES or type(face.get('direction')) is not int or face['direction'] not in (-1, 1):
                raise ValueError('Invalid blockage face')
            if type(face.get('plane')) not in (float, int) or not math.isfinite(face['plane']):
                raise ValueError('Invalid blockage plane')
        if any(a['axis'] == b['axis'] for a, b in zip(chain, chain[1:])):
            raise ValueError('A blockage joins faces on different axes')
        if any(type(k) is not int or not -1000 <= k <= route['airtimes'][leg] for k in gates):
            raise ValueError('Invalid blockage gate offset')


def compile_passages(route, events):
    rows, failures = [], []
    for passage in route['passages']:
        if passage['leg'] >= len(events):
            continue
        event = events[passage['leg']]
        if event['landing'] is None:
            continue
        ranges = intervals(passage['faces'], passage['gates'], -event['runTicks'], event['landing'] - event['jump'])
        if ranges is None:
            failures.append({'leg': passage['leg'], 'reason': 'passage_timing'})
            continue
        for face, (begin, end) in zip(passage['faces'], ranges):
            if face['axis'] == 'Y':
                continue
            for offset in range(begin, end + 1):
                rows.append({'mode': face['axis'], 't1': event['jump'] + offset,
                             'cmp': 'GE' if face['direction'] > 0 else 'LE',
                             'rhs': face['plane'] + face['direction'] * route['supportClearance'],
                             'name': 'blockage:' + str(passage.get('solidIndex'))})
    return rows, failures


def passage_y_valid(passage, event, profile):
    ranges = intervals(passage['faces'], passage['gates'], -event['runTicks'], profile['airtime'])
    if ranges is None:
        return False
    for face, (begin, end) in zip(passage['faces'], ranges):
        if face['axis'] == 'Y' and any(not outside(face, [0, profile['position'][max(0, t)], 0])
                                     for t in range(begin, end + 1)):
            return False
    return True


def passage_options(box, solid_index, leg, profile, route, plan, candidate, motion_bound,
                    *, max_blockages=2, max_candidates=128, deadline=None):
    event = plan['events'][leg]
    begin, end = -event['runTicks'], profile['airtime']
    offsets = list(range(begin, end + 1))
    clearance = max(route['supportClearance'], 2e-7)
    chain_faces = faces(box)
    source, target = center_bounds(route['blocks'][leg]), center_bounds(route['blocks'][leg + 1])
    reach_step = motion_bound['movementAfterSupportCheck']
    speed = motion_bound['storedSpeedNorm']
    origin = [source[0] - reach_step, source[1] + reach_step, source[2] - reach_step, source[3] + reach_step]
    if leg == 0:
        runs = min(route['runBounds'][0][1], route['maxTotalRunTicks'])
        radius = runs * reach_step
        px, _, pz = route['initial']['position']
        origin = [max(origin[0], px - radius), min(origin[1], px + radius),
                  max(origin[2], pz - radius), min(origin[3], pz + radius)]
        incoming = math.hypot(route['initial']['velocity'][0], route['initial']['velocity'][2])
        speed = incoming
        for _ in range(runs):
            incoming = (incoming + motion_bound['groundMagnitude']) * motion_bound['groundFriction']
            speed = max(speed, incoming)
    motion_key = ('jump:' if profile.get('jump', [True])[0] else 'run_off:') + str(end)
    coefficients = motion_bound.get('flightBounds', {}).get(motion_key)
    moves = []
    for tick in range(end):
        movement = speed + coefficients['magnitude'][tick] if coefficients else reach_step
        moves.append(movement)
        if coefficients:
            speed = movement * coefficients['friction'][tick]

    def travel(a, b):
        return sum(moves[t] if t >= 0 else reach_step for t in range(min(a, b), max(a, b)))

    points, envelopes = [], []
    for t in offsets:
        state = event['jump'] + t
        x, z = candidate['positions'][state]
        y = profile['position'][max(0, t)]
        points.append([x, y, z])
        forward, backward = travel(0, max(0, t)), travel(t, end - 1)
        envelopes.append([(max(origin[0] - forward, target[0] - backward), min(origin[1] + forward, target[1] + backward)),
                          (y, y),
                          (max(origin[2] - forward, target[2] - backward), min(origin[3] + forward, target[3] + backward))])
    valid, costs = [], []
    for face in chain_faces:
        axis = AXES.index(face['axis'])
        mask, penalty = [], []
        for point, envelope in zip(points, envelopes):
            lo, hi = envelope[axis]
            edge = face['plane'] + (0 if axis == 1 else face['direction'] * clearance)
            mask.append(lo <= hi and (hi >= edge if face['direction'] > 0 else lo <= edge))
            penalty.append(max(0, face['direction'] * (face['plane'] - point[axis])) ** 2)
        valid.append([0] + list(itertools.accumulate(not v for v in mask)))
        costs.append([0.0] + list(itertools.accumulate(penalty)))

    def segment(face, start, stop):
        lo, hi = start - begin, stop - begin + 1
        if start > stop or valid[face][hi] != valid[face][lo]:
            return None
        return costs[face][hi] - costs[face][lo]

    def reachable(chain, gates):
        ranges = intervals(chain, gates, begin, end)
        limits = [[list(pair) for pair in envelope] for envelope in envelopes]
        for face, (start, stop) in zip(chain, ranges):
            axis = AXES.index(face['axis'])
            if axis == 1:
                continue
            edge = face['plane'] + face['direction'] * clearance
            for offset in range(start, stop + 1):
                pair = limits[offset - begin][axis]
                if face['direction'] > 0:
                    pair[0] = max(pair[0], edge)
                else:
                    pair[1] = min(pair[1], edge)
        for axis in (0, 2):
            lo, hi = limits[0][axis]
            for i, offset in enumerate(offsets):
                if i:
                    reach = moves[offset - 1] if offset > 0 else reach_step
                    lo, hi = lo - reach, hi + reach
                lo, hi = max(lo, limits[i][axis][0]), min(hi, limits[i][axis][1])
                if lo > hi + 1e-8:
                    return False
        return True

    options, serial, generated, discarded, reach_pruned = [], 0, 0, 0, 0
    expansion_cap, expanded_total, incomplete = max_candidates * 128, 0, False
    for length in range(max_blockages + 1):
        for indices in face_chains(length):
            if expanded_total >= expansion_cap or deadline is not None and time.monotonic() >= deadline:
                incomplete = True
                break
            if any(chain_faces[a]['axis'] == chain_faces[b]['axis'] for a, b in zip(indices, indices[1:])):
                continue
            if segment(indices[0], begin, begin) is None or segment(indices[-1], end, end) is None:
                continue
            chain = [chain_faces[i] for i in indices]
            states = [(0.0, 0, begin, ())]
            while states and expanded_total < expansion_cap:
                if deadline is not None and time.monotonic() >= deadline:
                    incomplete = True
                    break
                score, depth, start, gates = heapq.heappop(states)
                expanded_total += 1
                if depth == length:
                    tail = segment(indices[-1], start, end)
                    if tail is None:
                        continue
                    if not reachable(chain, gates):
                        reach_pruned += 1
                        continue
                    rank = score + tail + length * 1e-8
                    passage = {'leg': leg, 'solidIndex': solid_index, 'faces': chain, 'gates': list(gates),
                               'kind': passage_kind(chain), 'blockageCount': length,
                               'blockageTypes': [a['axis'] + '->' + b['axis'] for a, b in zip(chain, chain[1:])]}
                    serial += 1
                    generated += 1
                    item = (-rank, serial, passage)
                    if len(options) < max_candidates:
                        heapq.heappush(options, item)
                    elif item[0] > options[0][0]:
                        heapq.heapreplace(options, item)
                    continue
                next_face = indices[depth + 1]
                shift = int(consecutive(chain[depth], chain[depth + 1]))
                for gate in range(start, end + 1 - shift):
                    value = segment(indices[depth], start, gate)
                    if value is None or segment(next_face, gate + shift, gate + shift) is None:
                        continue
                    heapq.heappush(states, (score + value, depth + 1, gate + shift, gates + (gate,)))
                if len(states) > max_candidates * 16:
                    discarded += len(states) - max_candidates * 16
                    states = heapq.nsmallest(max_candidates * 16, states)
                    heapq.heapify(states)
            discarded += len(states)
            incomplete |= bool(states)
    ranked = sorted([(-rank, serial, passage) for rank, serial, passage in options])
    return {'options': [{'score': rank, 'passage': passage} for rank, _, passage in ranked],
            'generated': generated, 'candidateDrops': max(0, generated - len(options)),
            'timingPoolDrops': discarded,
            'generationIncomplete': incomplete,
            'reachPruned': reach_pruned,
            'verticalFacesPruned': sum(valid[i][-1] == len(offsets) for i, f in enumerate(chain_faces) if f['axis'] == 'Y')}
