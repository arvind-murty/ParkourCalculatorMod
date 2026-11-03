import heapq
import itertools
import math
import time
from dataclasses import asdict, dataclass, field

from .geometry import contains, intersection, merge_boxes, segment_intervals
from ..obstacle_geometry import expanded_box
from ..route import center_bounds


def midpoint(box):
    return tuple((box[a] + box[a + 3]) / 2 for a in range(3))


def goal_point(scene):
    block = next(b for b in scene['blocks'] if b['id'] == scene['goalBlock'])
    return ((block['bounds'][0] + block['bounds'][1]) / 2, block['top'],
            (block['bounds'][2] + block['bounds'][3]) / 2)


def goal_region(scene):
    block = next(b for b in scene['blocks'] if b['id'] == scene['goalBlock'])
    b = center_bounds(block)
    margin = scene.get('supportClearance', 1e-6)
    return (b[0]+margin, block['top'], b[2]+margin, b[1]-margin, block['top'], b[3]-margin)


def distance_to_box(point, box):
    return math.sqrt(sum(max(box[a]-point[a], 0, point[a]-box[a+3])**2 for a in range(3)))


def landmarks(scene):
    start, goal = scene['initial']['position'], goal_point(scene)
    axis = 0 if abs(goal[0] - start[0]) >= abs(goal[2] - start[2]) else 2
    lateral = 2 if axis == 0 else 0
    terminal = goal_region(scene)
    result = []
    for box in merge_boxes(scene['collisionScene']['solids']):
        plane = (box[axis] + box[axis + 3]) / 2
        if min(start[axis], goal[axis]) < plane < max(start[axis], goal[axis]) and not terminal[axis] <= plane <= terminal[axis+3]:
            expanded = expanded_box(box)
            result.append(dict(id=len(result), axis=axis, lateral=lateral, plane=plane,
                lo=expanded[lateral], hi=expanded[lateral + 3], box=box))
    return result


def crossings(points, gates):
    events = []
    for gate in gates:
        axis, plane = gate['axis'], gate['plane']
        previous = 0
        for index, (a, b) in enumerate(zip(points, points[1:])):
            first = (a[axis] > plane) - (a[axis] < plane)
            second = (b[axis] > plane) - (b[axis] < plane)
            if first:
                previous = first
            if not second:
                continue
            if previous and second != previous:
                f = (plane-a[axis])/(b[axis]-a[axis])
                value = a[gate['lateral']] + f*(b[gate['lateral']]-a[gate['lateral']])
                side = -1 if value <= gate['lo'] + 1e-7 else 1 if value >= gate['hi'] - 1e-7 else 0
                token = (gate['id'], second, side)
                events.append((index, f, gate['id'], token))
            previous = second
    return [dict(token=token, segment=index) for index, _, _, token in sorted(events)]


def signature(points, gates):
    return tuple(e['token'] for e in crossings(points, gates))


def projected_boxes(decomposition, cell_ids):
    return merge_boxes([(decomposition.cells[i].bounds[0], 0, decomposition.cells[i].bounds[2],
                         decomposition.cells[i].bounds[3], 0, decomposition.cells[i].bounds[5]) for i in cell_ids])


def anchor_box(decomposition, point):
    patches = [p.bounds for p in decomposition.contacts if p.kind == 'support' and contains(p.bounds, point, 1e-7)]
    if not patches:
        return tuple(point) + tuple(point)
    return tuple(min(p[a] for p in patches) for a in range(3)) + tuple(max(p[a+3] for p in patches) for a in range(3))


def walk_projection(decomposition, cells, portals, start, goal):
    boxes = []
    for i, cell in enumerate(cells):
        before = decomposition.portals[portals[i-1]].bounds if i else anchor_box(decomposition, start)
        after = decomposition.portals[portals[i]].bounds if i < len(portals) else anchor_box(decomposition, goal)
        envelope = tuple(min(before[a], after[a]) for a in range(3)) + tuple(max(before[a+3], after[a+3]) for a in range(3))
        clip = intersection(envelope, decomposition.cells[cell].bounds)
        if clip is not None:
            boxes.append((clip[0], 0, clip[2], clip[3], 0, clip[5]))
    return merge_boxes(boxes)


def prescribed_projection(decomposition, cells, points):
    return projected_boxes(decomposition, [c.id for c in decomposition.cells])


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
    stage_paths: list = field(default_factory=list)

    def to_dict(self):
        return asdict(self)


def corridor_for_points(decomposition, points, gates, *, route_id=0, source='prescribed'):
    if len(points) < 2 or any(len(p) != 3 or any(not math.isfinite(v) for v in p) for p in points):
        raise ValueError('A prescribed spatial route requires finite 3D points')
    cell_ids, ordered_cells = set(), []
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
        covered = 0.
        while covered < 1:
            available = [(hi, decomposition.cells[cell].dimension, -cell, cell) for lo, hi, cell in intervals if lo <= covered and hi > covered]
            if not available:
                raise ValueError('Prescribed itinerary has a coverage gap')
            hi, _, _, cell = max(available)
            if not ordered_cells or ordered_cells[-1] != cell:
                ordered_cells.append(cell)
            covered = hi
    cells = sorted(cell_ids)
    return Corridor(route_id, cells, [], [list(p) for p in points], signature(points, gates),
        prescribed_projection(decomposition, cells, points), sum(math.dist(a, b) for a, b in zip(points, points[1:])), source=source)


def enumerate_routes(scene, decomposition, *, max_routes=50, max_expansions=5000, max_steps=64,
                     max_visits=2, deadline=None, cancel_event=None):
    for key, value in dict(max_routes=max_routes, max_expansions=max_expansions, max_steps=max_steps, max_visits=max_visits).items():
        if type(value) is not int or value < 1:
            raise ValueError(key + ' must be a positive integer')
    start, goal = tuple(scene['initial']['position']), goal_point(scene)
    terminal = goal_region(scene)
    starts = decomposition.locate(start)
    goals = {c.id: overlap for c in decomposition.cells if (overlap := intersection(c.bounds, terminal)) is not None}
    gates, adjacent = landmarks(scene), decomposition.neighbors()
    serial = itertools.count()
    queue = [(distance_to_box(start, terminal), next(serial), 0., c, (c,), (), (start,)) for c in starts]
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
            endpoint = midpoint(goals[cell])
            finished = points + (endpoint,)
            full_sig = signature(finished, gates)
            if full_sig in grouped:
                corridor = grouped[full_sig]
                corridor.cells = sorted(set(corridor.cells) | set(cells))
                corridor.projection = projected_boxes(decomposition, [c.id for c in decomposition.cells])
                corridor.variants += 1
                duplicates += 1
            else:
                grouped[full_sig] = Corridor(len(grouped), list(cells), list(portals), [list(p) for p in finished],
                    full_sig, projected_boxes(decomposition, [c.id for c in decomposition.cells]), length + math.dist(points[-1], endpoint))
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
            heapq.heappush(queue, (distance + distance_to_box(p, terminal) + ordering_penalty, next(serial), distance,
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


def audit_candidate(corridor, candidate, gates, *, terminal_pre=False, partial=False):
    points, references = horizontal_trace(candidate)
    if terminal_pre and len(points) >= 4:
        points, references = points[:-2], references[:-2]
    for i, (a, b) in enumerate(zip(points, points[1:])):
        if not projected_segment_covered(corridor.projection, a, b):
            return dict(accepted=False, reason='corridor_projection', segment=i, references=references[i])
    events = crossings(points, gates)
    actual = tuple(e['token'] for e in events)
    expected = corridor.signature[:len(actual)] if partial else corridor.signature
    if actual != expected:
        mismatch = next((i for i, pair in enumerate(zip(actual, corridor.signature)) if pair[0] != pair[1]), min(len(actual), len(corridor.signature)))
        return dict(accepted=False, reason='corridor_signature', mismatch=mismatch, actual=actual,
                    events=events, references=references)
    return dict(accepted=True, signature=actual, partial=partial, validation='horizontal_corridor_and_ordered_gate_crossings')
