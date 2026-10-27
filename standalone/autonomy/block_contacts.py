import copy
import heapq
import time

from standalone.ipopt.ladder import f32, vertical_move
from .route import center_bounds, supports
from .search import find_route


def floor_checks(scene, block_ids, profiles):
    checks = []
    for leg, profile in enumerate(profiles):
        target_top = scene['blocks'][next(i for i, b in enumerate(scene['blocks']) if b['id'] == block_ids[leg + 1])]['top']
        config = {'threshold': .005, 'jumpVelocity': [f32(.42)] + [None] * (profile['airtime'] - 1),
                  'sneak': [False] * profile['airtime']}
        for offset in range(profile['airtime']):
            y = profile['position'][offset]
            next_y = profile['position'][offset + 1]
            movement = vertical_move(config, offset, profile['velocity'][offset], False)
            if movement >= 0:
                continue
            for block in scene['blocks']:
                top = block['top']
                if next_y <= top <= y and not (offset + 1 == profile['airtime'] and top == target_top):
                    checks.append({'leg': leg, 'offset': offset, 'block': copy.deepcopy(block), 'reason': 'earlier_floor'})
    return checks


def contact_conflicts(checks, plan, candidate):
    conflicts = []
    for check in checks:
        tick = plan['events'][check['leg']]['jump'] + check['offset']
        x, z = candidate['positions'][tick]
        if supports(check['block'], x, z):
            conflicts.append(dict(check, state=tick, position=[x, z]))
    return conflicts


def avoid_contact_rows(conflict, clearance):
    bounds = center_bounds(conflict['block'])
    x, z = conflict['position']
    rows = []
    for axis, coordinate, lo, hi in [('X', x, bounds[0], bounds[1]), ('Z', z, bounds[2], bounds[3])]:
        for cmp, edge in [('LE', lo - clearance), ('GE', hi + clearance)]:
            rows.append((abs(coordinate - edge), {'leg': conflict['leg'], 'offset': conflict['offset'],
                                                 'mode': axis, 'cmp': cmp, 'rhs': edge,
                                                 'name': conflict['reason'] + ':' + conflict['block']['id']}))
    return [row for _, row in sorted(rows, key=lambda value: value[0])]


def solve_contact_branches(route, profiles, checks, compiler, *, seconds, candidate_seconds, retries,
                           max_branches, improve=False, python_executable=None, cancel_event=None):
    deadline = time.monotonic() + seconds
    queue, seen, serial = [(0.0, 0, [])], {()}, 0
    attempts, inner_attempts = [], []
    termination = 'contact_search_exhausted'
    while queue and len(attempts) < max_branches:
        if cancel_event is not None and cancel_event.is_set():
            return {'status': 'cancelled', 'termination': 'cancelled', 'best': None,
                    'attempts': inner_attempts, 'contactAttempts': attempts}
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            termination = 'deadline'
            break
        score, _, rows = heapq.heappop(queue)
        branch = copy.deepcopy(route)
        branch['constraints'].extend(rows)
        result = find_route(branch, compiler, seconds=remaining, candidate_seconds=candidate_seconds,
                            retries=retries, improve=improve, python_executable=python_executable,
                            cancel_event=cancel_event)
        inner_attempts.extend(result['attempts'])
        conflicts = contact_conflicts(checks, result['best']['plan'], result['best']['candidate']) if result['best'] else []
        attempts.append({'status': result['status'], 'constraintCount': len(rows),
                         'conflicts': [{'block': c['block']['id'], 'state': c['state'], 'reason': c['reason']}
                                       for c in conflicts], 'termination': result['termination']})
        if result['status'] in ('error', 'cancelled'):
            result.update(attempts=inner_attempts, contactAttempts=attempts)
            return result
        if result['best'] is None:
            continue
        if not conflicts:
            result['best'].update(route=branch, profiles=profiles, contactChecks=checks)
            result.update(attempts=inner_attempts, contactAttempts=attempts)
            return result
        conflict = conflicts[0]
        for row in avoid_contact_rows(conflict, max(route['supportClearance'], 2e-7)):
            new_rows = rows + [row]
            key = tuple(sorted((r['leg'], r['offset'], r['mode'], r['cmp'], r['rhs']) for r in new_rows))
            if key in seen:
                continue
            seen.add(key)
            serial += 1
            delta = abs(conflict['position'][0 if row['mode'] == 'X' else 1] - row['rhs'])
            heapq.heappush(queue, (score + delta, serial, new_rows))
    if queue and len(attempts) >= max_branches:
        termination = 'contact_branch_limit'
    return {'status': 'unknown', 'termination': termination, 'best': None,
            'attempts': inner_attempts, 'contactAttempts': attempts}
