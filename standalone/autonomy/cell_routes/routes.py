import heapq
import itertools
import math
import time
from dataclasses import asdict, dataclass

from .geometry import merge_boxes, segment_intervals
from ..obstacle_geometry import expanded_box


def midpoint(box):
    return tuple((box[a] + box[a + 3]) / 2 for a in range(3))


def goal_point(scene):
    block = next(b for b in scene['blocks'] if b['id'] == scene['goalBlock'])
    return ((block['bounds'][0] + block['bounds'][1]) / 2, block['top'],
            (block['bounds'][2] + block['bounds'][3]) / 2)


def landmarks(scene):
    start, goal = scene['initial']['position'], goal_point(scene)
    axis = 0 if abs(goal[0] - start[0]) >= abs(goal[2] - start[2]) else 2
    lateral = 2 if axis == 0 else 0
    result = []
    for box in merge_boxes(scene['collisionScene']['solids']):
        plane = (box[axis] + box[axis + 3]) / 2
        if min(start[axis], goal[axis]) < plane < max(start[axis], goal[axis]):
            expanded = expanded_box(box)
            result.append(dict(id=len(result), axis=axis, lateral=lateral, plane=plane,
                lo=expanded[lateral], hi=expanded[lateral + 3], box=box))
    return result


def crossings(points, gates):
    result = []
    for index, (a, b) in enumerate(zip(points, points[1:])):
        events = []
        for gate in gates:
            axis, plane = gate['axis'], gate['plane']
            if a[axis] <= plane < b[axis] or b[axis] <= plane < a[axis]:
                f = (plane - a[axis]) / (b[axis] - a[axis])
                value = a[gate['lateral']] + f * (b[gate['lateral']] - a[gate['lateral']])
                side = -1 if value <= gate['lo'] else 1 if value >= gate['hi'] else 0
                token = (gate['id'], 1 if b[axis] > a[axis] else -1, side)
                events.append((f, gate['id'], token))
        result.extend(dict(token=token, segment=index) for _, _, token in sorted(events))
    return result


def signature(points, gates):
    return tuple(e['token'] for e in crossings(points, gates))


def projected_boxes(decomposition, cell_ids):
    return merge_boxes([(decomposition.cells[i].bounds[0], 0, decomposition.cells[i].bounds[2],
                         decomposition.cells[i].bounds[3], 0, decomposition.cells[i].bounds[5]) for i in cell_ids])


@dataclass
class Corridor:
    id: int
    cells: list
    portals: list
    points: list
    signature: tuple
    projection: list
    geometric_length: float
    variants: int = 1
    source: str = 'automatic'

    def to_dict(self):
        return asdict(self)


def corridor_for_points(decomposition, points, gates, *, route_id=0, source='prescribed'):
    if len(points) < 2 or any(len(p) != 3 or any(not math.isfinite(v) for v in p) for p in points):
        raise ValueError('A prescribed spatial route requires finite 3D points')
    cell_ids = set()
    for a, b in zip(points, points[1:]):
        intervals = segment_intervals(decomposition, a, b)
        reach = 0.
        if not intervals:
            raise ValueError('Prescribed spatial route leaves free space')
        for lo, hi, cell in intervals:
            if lo > reach:
                break
            reach = max(reach, hi)
            if hi > lo or a == b:
                cell_ids.add(cell)
        if reach < 1:
            raise ValueError('Prescribed spatial route crosses an obstacle or leaves coverage')
    cells = sorted(cell_ids)
    return Corridor(route_id, cells, [], [list(p) for p in points], signature(points, gates),
        projected_boxes(decomposition, cells), sum(math.dist(a, b) for a, b in zip(points, points[1:])), source=source)


def enumerate_routes(scene, decomposition, *, max_routes=50, max_expansions=5000, max_steps=64,
                     max_visits=2, deadline=None, cancel_event=None):
    for key, value in dict(max_routes=max_routes, max_expansions=max_expansions, max_steps=max_steps, max_visits=max_visits).items():
        if type(value) is not int or value < 1:
            raise ValueError(key + ' must be a positive integer')
    start, goal = tuple(scene['initial']['position']), goal_point(scene)
    starts, goals = decomposition.locate(start), set(decomposition.locate(goal))
    gates, adjacent = landmarks(scene), decomposition.neighbors()
    serial = itertools.count()
    queue = [(math.dist(start, goal), next(serial), 0., c, (c,), (), (start,)) for c in starts]
    heapq.heapify(queue)
    representatives, grouped = {}, {}
    expanded, dominated, visit_drops, depth_drops, duplicates = 0, 0, 0, 0, 0
    termination = 'exhausted'
    while queue:
        if cancel_event is not None and cancel_event.is_set():
            termination = 'cancelled'
            break
        if deadline is not None and time.monotonic() >= deadline:
            termination = 'deadline'
            break
        if expanded >= max_expansions:
            termination = 'expansion_limit'
            break
        _, _, length, cell, cells, portals, points = heapq.heappop(queue)
        sig = signature(points, gates)
        repeats = tuple(sorted(c for c in set(cells) if cells.count(c) > 1))
        key = (cell, sig, repeats)
        if representatives.get(key, math.inf) <= length:
            dominated += 1
            continue
        representatives[key] = length
        expanded += 1
        if cell in goals:
            finished = points + (tuple(goal),)
            full_sig = signature(finished, gates)
            if full_sig in grouped:
                corridor = grouped[full_sig]
                corridor.cells = sorted(set(corridor.cells) | set(cells))
                corridor.projection = projected_boxes(decomposition, corridor.cells)
                corridor.variants += 1
                duplicates += 1
            else:
                grouped[full_sig] = Corridor(len(grouped), list(cells), list(portals), [list(p) for p in finished],
                    full_sig, projected_boxes(decomposition, cells), length + math.dist(points[-1], goal))
                if len(grouped) >= max_routes:
                    termination = 'route_limit'
                    break
            continue
        if len(cells) >= max_steps:
            depth_drops += 1
            continue
        for neighbor, portal_id in adjacent[cell]:
            if cells.count(neighbor) >= max_visits:
                visit_drops += 1
                continue
            p = midpoint(decomposition.portals[portal_id].bounds)
            distance = length + math.dist(points[-1], p)
            next_points = points + (p,)
            next_signature = signature(next_points, gates)
            reverse_count = sum(token[1] != (1 if goal[gates[token[0]]['axis']] > start[gates[token[0]]['axis']] else -1) for token in next_signature)
            revisit_count = len(cells) + 1 - len(set(cells + (neighbor,)))
            ordering_penalty = (reverse_count + revisit_count) * math.dist(decomposition.bounds[:3], decomposition.bounds[3:])
            heapq.heappush(queue, (distance + math.dist(p, goal) + ordering_penalty, next(serial), distance,
                neighbor, cells + (neighbor,), portals + (portal_id,), points + (p,)))
    return list(grouped.values()), dict(termination=termination, expanded=expanded, remaining=len(queue),
        dominanceDrops=dominated, visitDrops=visit_drops, depthDrops=depth_drops, groupedDuplicates=duplicates,
        routes=len(grouped), maxRoutes=max_routes, maxExpansions=max_expansions, maxSteps=max_steps,
        maxVisits=max_visits, coverageProven=False, missingStart=not starts, missingGoal=not goals)


def horizontal_trace(candidate):
    positions = candidate['positions']
    ys = candidate.get('y', [0.] * len(positions))
    if len(ys) != len(positions):
        raise ValueError('Trajectory Y/XZ lengths disagree')
    points, references = [], []
    for t, (a, b) in enumerate(zip(positions, positions[1:])):
        phases = [(a[0], ys[t], a[1]), (a[0], ys[t+1], a[1]),
                  (b[0], ys[t+1], a[1]), (b[0], ys[t+1], b[1])]
        if not points:
            points.append(phases[0])
        points.extend(phases[1:])
        references.extend([(t, t), (t+1, t), (t+1, t+1)])
    return points, references


def projected_segment_covered(projection, a, b, tolerance=1e-7):
    intervals = []
    for box in projection:
        lo, hi = 0., 1.
        for axis in (0, 2):
            delta = b[axis] - a[axis]
            if delta == 0:
                if not box[axis]-tolerance <= a[axis] <= box[axis+3]+tolerance:
                    hi = -1
                    break
            else:
                low, high = sorted(((box[axis]-tolerance-a[axis])/delta, (box[axis+3]+tolerance-a[axis])/delta))
                lo, hi = max(lo, low), min(hi, high)
        if lo <= hi:
            intervals.append((lo, hi))
    reach = 0.
    if not intervals:
        return False
    for lo, hi in sorted(intervals):
        if lo > reach:
            return False
        reach = max(reach, hi)
    return reach >= 1


def audit_candidate(corridor, candidate, gates):
    points, references = horizontal_trace(candidate)
    for i, (a, b) in enumerate(zip(points, points[1:])):
        if not projected_segment_covered(corridor.projection, a, b):
            return dict(accepted=False, reason='corridor_projection', segment=i, references=references[i])
    events = crossings(points, gates)
    actual = tuple(e['token'] for e in events)
    if actual != corridor.signature:
        mismatch = next((i for i, pair in enumerate(zip(actual, corridor.signature)) if pair[0] != pair[1]), min(len(actual), len(corridor.signature)))
        return dict(accepted=False, reason='corridor_signature', mismatch=mismatch, actual=actual,
                    events=events, references=references)
    return dict(accepted=True, signature=actual, validation='horizontal_corridor_and_ordered_gate_crossings')
