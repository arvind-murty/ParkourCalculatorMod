import copy
import heapq
import time

from standalone.ipopt import prepare, validate
from standalone.ipopt.ladder import f32, vertical_move, vertical_step
from .route import center_bounds, supports
from .ranking import candidate_rank
from .search import find_route
from .blockages import blockage_usage, gate_timeline, passage_options, passage_y_valid, within_caps
from .obstacle_geometry import check_geometry


def floor_checks(scene, block_ids, profiles):
    checks = []
    by_id = {b['id']: b for b in scene['blocks']}
    for leg, profile in enumerate(profiles):
        target_top = by_id[block_ids[leg + 1]]['top']
        jumps = profile.get('jump', [True] + [False] * (profile['airtime'] - 1))
        config = {'threshold': .005, 'jumpVelocity': [f32(.42) if j else None for j in jumps],
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
        for block in scene['blocks']:
            selected_step = profile.get('profileKind') == 'stair' and block['id'] == block_ids[leg + 1]
            step_config = dict(config, contacts=[{'state': 1, 'plane': block['top'], 'direction': -1, 'step': True}])
            _, _, _, failures = vertical_step(step_config, 0, profile['position'][0], profile['velocity'][0], False, True, 0)
            if not selected_step and not failures:
                checks.append({'leg': leg, 'offset': 1, 'block': copy.deepcopy(block), 'reason': 'unplanned_stair'})
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


def validate_vertical_schedule(route, profiles, plan):
    n = plan['ticks']
    contacts = []
    for event, profile in zip(plan['events'], profiles):
        for contact in profile['contacts']:
            contacts.append(dict(contact, state=event['jump'] + contact['state']))
        for tick in range(event['landed'], event['jump']):
            contacts.append({'state': tick + 1, 'plane': profile['position'][0], 'direction': -1})
    vertical = {'profile': 'minecraft-1.8.9', 'position': route['initial']['position'][1],
                'velocity': route['initial']['velocity'][1], 'onGround': True,
                'previousHorizontalCollision': False, 'threshold': .005,
                'jumpVelocity': [f32(.42) if jump else None for jump in plan['jump']],
                'sneak': [False] * n, 'contacts': sorted(contacts, key=lambda contact: contact['state'])}
    problem = prepare({'version': 1, 'ticks': n, 'position': [0, 0], 'velocity': [0, 0],
                       'magnitude': [0] * n, 'friction': [.91] * n, 'base': [0] * n, 'yaw': [0] * n,
                       'objective': {'axis': 'X', 'sense': 'MAX', 'tick': n}, 'vertical': vertical})
    checked = validate(problem, problem['yaw'])
    return {'feasible': checked['feasible'] and checked['onGround'] == plan['grounded'] + [True],
            'vertical': vertical, 'y': checked['y'], 'vy': checked['vy'], 'onGround': checked['onGround']}


def solve_contact_branches(route, profiles, checks, compiler, *, seconds, candidate_seconds, retries,
                           max_branches, improve=False, rank_equal_cost=False, optimize_goal=False,
                           python_executable=None, cancel_event=None, geometry=None, motion_bound=None,
                           max_blockages=3, max_horizontal=2, max_vertical=2,
                           max_passage_candidates=128, solid_blocks=None):
    route = copy.deepcopy(route)
    if solid_blocks is not None:
        for passage in route.get('passages', []):
            passage['blockId'] = solid_blocks[passage['solidIndex']]
    deadline = time.monotonic() + seconds
    queue, seen, serial = [(0.0, 0, [], route.get('passages', []))], {()}, 0
    attempts, inner_attempts = [], []
    best_result = None
    termination = 'contact_search_exhausted'
    while queue and len(attempts) < max_branches:
        if cancel_event is not None and cancel_event.is_set():
            if best_result is not None:
                best_result.update(termination='cancelled', attempts=inner_attempts, contactAttempts=attempts)
                return best_result
            return {'status': 'cancelled', 'termination': 'cancelled', 'best': None,
                    'attempts': inner_attempts, 'contactAttempts': attempts}
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            termination = 'deadline'
            break
        score, _, rows, passages = heapq.heappop(queue)
        branch = copy.deepcopy(route)
        branch['constraints'].extend(rows)
        branch['passages'] = passages
        if best_result is not None:
            branch['maxTotalRunTicks'] = min(branch['maxTotalRunTicks'], best_result['best']['cost'])
        result = find_route(branch, compiler, seconds=remaining, candidate_seconds=candidate_seconds,
                            retries=retries, improve=improve, rank_equal_cost=rank_equal_cost,
                            optimize_goal=optimize_goal, python_executable=python_executable,
                            cancel_event=cancel_event)
        inner_attempts.extend(result['attempts'])
        conflicts = contact_conflicts(checks, result['best']['plan'], result['best']['candidate']) if result['best'] else []
        attempts.append({'status': result['status'], 'constraintCount': len(rows),
                         'passageCount': len(passages),
                         'passageKinds': [p['kind'] for p in passages],
                         'conflicts': [{'block': c['block']['id'], 'state': c['state'], 'reason': c['reason']}
                                       for c in conflicts], 'termination': result['termination']})
        if result['status'] in ('error', 'cancelled'):
            if best_result is not None:
                best_result.update(termination=result['status'], attempts=inner_attempts, contactAttempts=attempts)
                return best_result
            result.update(attempts=inner_attempts, contactAttempts=attempts)
            return result
        if result['best'] is None:
            continue
        if not conflicts:
            vertical = validate_vertical_schedule(branch, profiles, result['best']['plan'])
            if not vertical['feasible']:
                attempts[-1]['status'] = 'unknown'
                attempts[-1]['termination'] = 'vertical_schedule_mismatch'
                continue
            if any(not passage_y_valid(p, result['best']['plan']['events'][p['leg']], profiles[p['leg']]) for p in passages):
                attempts[-1].update(status='unknown', termination='passage_y_mismatch')
                continue
            if geometry is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    termination = 'deadline'
                    break
                try:
                    checked = check_geometry(geometry, result['best']['plan'], result['best']['candidate'],
                                             vertical, compiler, remaining)
                except TimeoutError:
                    termination = 'geometry_deadline'
                    break
                except (ValueError, RuntimeError, OSError) as error:
                    if best_result is not None:
                        best_result.update(termination='geometry_error', error=str(error),
                                           attempts=inner_attempts, contactAttempts=attempts)
                        return best_result
                    return {'status': 'error', 'termination': 'geometry_check', 'error': str(error),
                            'best': None, 'attempts': inner_attempts, 'contactAttempts': attempts}
                attempts[-1]['geometryCheck'] = checked
                if not checked['feasible']:
                    attempts[-1]['status'] = 'unknown'
                    if 'solidIndex' not in checked:
                        attempts[-1]['termination'] = checked['reason']
                        continue
                    plan = result['best']['plan']
                    leg = next(i for i, event in enumerate(plan['events'])
                               if event['landed'] <= checked['tick'] < event['landing'])
                    index = checked['solidIndex']
                    alternatives = passage_options(geometry['solids'][index], index, leg, profiles[leg], branch,
                                                   plan, result['best']['candidate'], motion_bound,
                                                   max_blockages=max_blockages, max_horizontal=max_horizontal,
                                                   max_vertical=max_vertical, max_candidates=max_passage_candidates,
                                                   block_id=solid_blocks[index] if solid_blocks is not None else None,
                                                   deadline=deadline)
                    attempts[-1]['passageGeneration'] = {k: v for k, v in alternatives.items() if k != 'options'}
                    for option in alternatives['options']:
                        new_passages = [p for p in passages if (p['leg'], p['solidIndex']) != (leg, index)] + [option['passage']]
                        if not within_caps(new_passages, branch['passageCaps']):
                            continue
                        key = (tuple(sorted((r['leg'], r['offset'], r['mode'], r['cmp'], r['rhs']) for r in rows)),
                               tuple(sorted((p['leg'], p['solidIndex'], tuple((f['axis'], f['direction'], f['plane']) for f in p['faces']),
                                             tuple(p['gates'])) for p in new_passages)))
                        if key in seen:
                            continue
                        seen.add(key)
                        serial += 1
                        heapq.heappush(queue, (score + option['score'], serial, rows, new_passages))
                    continue
                result['best']['geometryCheck'] = checked
            result['best'].update(route=result['best'].get('route', branch), profiles=profiles, contactChecks=checks)
            result['best']['passages'] = passages
            result['best']['blockageTimeline'] = gate_timeline(passages, result['best']['plan']['events'])
            result['best']['blockageUsage'] = [{'blockId': value if kind == 'block' else 'solid:' + str(value), **counts}
                                              for (kind, value), counts in blockage_usage(passages).items()]
            result['best']['vertical'] = vertical['vertical']
            result['best']['candidate'].update({key: vertical[key] for key in ('y', 'vy', 'onGround')})
            result.update(attempts=inner_attempts, contactAttempts=attempts)
            if not rank_equal_cost:
                return result
            if best_result is None or candidate_rank(result['best'], route['objective']) < candidate_rank(best_result['best'], route['objective']):
                best_result = result
            continue
        conflict = conflicts[0]
        for row in avoid_contact_rows(conflict, max(route['supportClearance'], 2e-7)):
            new_rows = rows + [row]
            key = (tuple(sorted((r['leg'], r['offset'], r['mode'], r['cmp'], r['rhs']) for r in new_rows)),
                   tuple(sorted((p['leg'], p['solidIndex'], tuple((f['axis'], f['direction'], f['plane']) for f in p['faces']),
                                 tuple(p['gates'])) for p in passages)))
            if key in seen:
                continue
            seen.add(key)
            serial += 1
            delta = abs(conflict['position'][0 if row['mode'] == 'X' else 1] - row['rhs'])
            heapq.heappush(queue, (score + delta, serial, new_rows, passages))
    if queue and len(attempts) >= max_branches:
        termination = 'contact_branch_limit'
    if best_result is not None:
        best_result.update(termination=termination, attempts=inner_attempts, contactAttempts=attempts)
        return best_result
    return {'status': 'unknown', 'termination': termination, 'best': None,
            'attempts': inner_attempts, 'contactAttempts': attempts}
