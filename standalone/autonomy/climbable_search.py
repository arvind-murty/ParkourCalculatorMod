import copy
import heapq
import math
import time

from standalone.ipopt import prepare, solve, validate
from standalone.ipopt.ladder import f32
from standalone.ipopt.model import integer
from .block_contacts import solve_contact_branches, floor_checks, validate_vertical_schedule
from .ladder_geometry import climb_walls, entry_channels, exit_faces, ladder_columns, schedule_constraints
from .ladder_ranking import rank_ladder_schedules
from .ladder_schedules import generate_ladder_schedules
from .obstacle_geometry import check_geometry
from .route import center_bounds, schedule


def ladder_settings(scene):
    settings = copy.deepcopy(scene.get('ladderSearch', {}))
    defaults = {'maxEntryInputs': 14, 'maxInputs': 48, 'entryDelay': 5, 'maxSkeletons': 2048,
                'maxSchedules': 2000, 'maxMaskWork': 30000, 'maxRanked': 256, 'representatives': 3,
                'maxNumericalSchedules': 32, 'maxOrdinaryPaths': 8, 'maxSuffixPaths': 3,
                'maxRouteContexts': 24, 'maxRunAllocations': 4, 'minClimbInputs': 0,
                'maxInteractions': 1, 'requireLadder': False}
    if not isinstance(settings, dict) or set(settings) - set(defaults):
        raise ValueError('Unsupported ladder search settings')
    for key, default in defaults.items():
        value = settings.setdefault(key, default)
        if key == 'requireLadder':
            if type(value) is not bool:
                raise ValueError('requireLadder must be boolean')
        else:
            integer(value, key, 0 if key in ('entryDelay', 'minClimbInputs') else 1,
                    1 if key == 'maxInteractions' else 5 if key == 'entryDelay' else 200000)
    if settings['maxInputs'] > 200 or settings['maxEntryInputs'] > 199:
        raise ValueError('Ladder input horizons exceed 200')
    return settings


def ordinary_paths(graph, start, hops, limit, goal=None):
    pending, result, serial = [(0, 0, (start,), ())], [], 0
    while pending and len(result) < limit:
        cost, _, path, edges = heapq.heappop(pending)
        if goal is None or path[-1] == goal:
            result.append((path, edges))
        if len(edges) >= hops or goal is not None and path[-1] == goal:
            continue
        for target in graph['adjacency'].get(path[-1], []):
            for edge in graph['edgeOptions'][path[-1]][target]:
                serial += 1
                heapq.heappush(pending, (cost + graph['transitions'][edge]['profile']['airtime'], serial,
                                        path + (target,), edges + (edge,)))
    return result


def run_allocations(legs, per_leg, total, limit):
    def at_cost(left, count):
        if count == 0:
            if left == 0:
                yield ()
            return
        for value in range(min(per_leg, left) + 1):
            for tail in at_cost(left - value, count - 1):
                yield (value,) + tail
    emitted = 0
    for cost in range(min(total, legs * per_leg) + 1):
        for allocation in at_cost(cost, legs):
            yield list(allocation)
            emitted += 1
            if emitted >= limit:
                return


def waypoint_seed(problem, waypoints):
    yaw = problem['yaw'][:]
    for tick in range(min(problem['ticks'], 160)):
        targets = [(at, point) for at, point in waypoints if at > tick]
        if not targets:
            break
        at, point = min(targets, key=lambda item: item[0])
        state = validate(problem, yaw)
        coupling, drag = 0., 1.
        for t in range(tick, at):
            coupling += drag
            drag *= problem['friction'][t]
        delta = [point[a] - state['positions'][tick][a] - coupling * state['velocity'][tick][a] for a in (0, 1)]
        yaw[tick] = math.degrees(math.atan2(delta[1], delta[0]) - problem['base'][tick])
    return yaw


def _raw_candidate(problem, candidate):
    candidate['rawMovement'] = []
    for t, yaw in enumerate(candidate['yaw']):
        angle = problem['base'][t] + math.radians(yaw)
        candidate['rawMovement'].append([(v if abs(v) >= problem['threshold'] else 0) + problem['magnitude'][t] * q
            for v, q in zip(candidate['velocity'][t], (math.cos(angle), math.sin(angle)))])
    candidate['friction'] = problem['friction']
    candidate['movement'] = problem['movement']
    return candidate


def _vertical(config, ladder, hits):
    n, x, contacts = len(ladder) - 1, 0., []
    for t in range(n):
        x += .05 if t + 1 in hits else .1
        if t + 1 in hits:
            contacts.append({'state': t + 1, 'axis': 0, 'plane': x, 'direction': 1})
    return validate(prepare({'version': 1, 'ticks': n, 'position': [0, 0], 'velocity': [0, 0],
        'magnitude': [.1] * n, 'friction': [0] * n, 'base': [0] * n, 'yaw': [0] * n,
        'contacts': contacts, 'vertical': config, 'ladderTicks': ladder,
        'objective': {'axis': 'X', 'sense': 'MAX', 'tick': n}}), [0] * n)


def _prefix(scene, graph, path, edges, target, allocation, compiler, seconds):
    from .block_search import compile_block_path
    source = next(b for b in scene['blocks'] if b['id'] == path[-1])
    dummy = {'airtime': 2, 'contacts': [], 'position': [source['top']] * 3,
             'velocity': [-(.08) * f32(.98)] * 3, 'grounded': [True, False, True], 'jump': [True, False]}
    selected = [graph['transitions'][edge] for edge in edges] + [
        {'source': path[-1], 'target': target, 'profileKind': 'ordinary', 'profile': dummy, 'entryFace': None, 'exitFace': None}]
    compiled = compile_block_path(scene, list(path) + [target], transitions=selected)
    route = compiled['route']
    route['sharedMovement'] = True
    footprints = compiler.derive_footprints(route, seconds)
    for block, bounds in zip(route['blocks'], footprints):
        block['_derivedFootprint'] = bounds
    plan = schedule(route, allocation, 'run')
    if plan['ticks']:
        vertical = validate_vertical_schedule(route, compiled['profiles'], plan)['vertical']
    else:
        vertical = {'profile': 'minecraft-1.8.9', 'position': scene['initial']['position'][1],
                    'velocity': scene['initial']['velocity'][1], 'onGround': True,
                    'previousHorizontalCollision': False, 'threshold': .005,
                    'jumpVelocity': [], 'sneak': [], 'contacts': []}
    return route, plan, vertical


def probe_entries(scene, graph, path, edges, target, allocation, column, wall, compiler, *,
                  deadline, candidate_seconds, max_entry, python_executable=None, cancel_event=None):
    route, base, vertical = _prefix(scene, graph, path, edges, target, allocation, compiler,
                                    max(.01, deadline - time.monotonic()))
    records, entries = [], []
    for channel in entry_channels(column, wall):
        unresolved = []
        for entry in range(max_entry + 1):
            if time.monotonic() >= deadline or cancel_event is not None and cancel_event.is_set():
                return entries, records
            offset, n = base['ticks'], base['ticks'] + entry
            if entry == 0 and offset == 0:
                position = scene['initial']['position']
                inside = column['x'] <= position[0] < column['x'] + 1 and column['z'] <= position[2] < column['z'] + 1 \
                    and column['minY'] <= position[1] < column['maxY']
                records.append({'channel': channel['channel'], 'entryState': 0, 'status': 'feasible' if inside else 'blocked',
                                'reason': 'initial_membership'})
                if inside:
                    entries.append(dict(channel, firstValidatedState=0, unresolvedStates=[]))
                    break
                continue
            ladder = [False] * n + [True]
            config = copy.deepcopy(vertical)
            config['jumpVelocity'].extend([f32(.42)] + [None] * (entry - 1) if entry else [])
            config['sneak'].extend([False] * entry)
            found = False
            for hit_on_entry in ([False, True] if entry else [False]):
                checked = _vertical(config, ladder, {n} if hit_on_entry else set())
                if not checked['feasible'] or not column['minY'] <= checked['y'][-1] < column['maxY']:
                    continue
                local = {'entryState': entry, 'exitState': entry + 1,
                         'ladderTicks': ladder[offset:], 'hit': [False] * entry,
                         'trace': {'position': checked['y'][offset:]}}
                if hit_on_entry:
                    local['hit'][-1] = True
                try:
                    rows, contacts, scene_contacts = schedule_constraints(local, column, wall, channel, None, start=offset)
                except ValueError:
                    continue
                plan = dict(base, ticks=n, grounded=base['grounded'] + [True] + [False] * (entry - 1) if entry else base['grounded'],
                    jump=base['jump'] + [True] + [False] * (entry - 1) if entry else base['jump'],
                    ladderTicks=ladder, sneak=config['sneak'], vertical=dict(config, referencePosition=checked['y'], referenceVelocity=checked['vy']),
                    sceneContacts=scene_contacts)
                try:
                    coefficient = compiler.compile(route, plan, max(.01, deadline - time.monotonic()))
                    problem = prepare(dict(coefficient, version=1, ticks=n, position=scene['initial']['position'][::2],
                        velocity=scene['initial']['velocity'][::2], threshold=.005, yaw=[0.] * n,
                        walls=base['walls'] + rows, contacts=contacts, ladderTicks=ladder,
                        objective={'axis': wall['axis'], 'sense': 'MAX' if wall['direction'] > 0 else 'MIN', 'tick': n}))
                    point = [column['x'] + .5, column['z'] + .5]
                    point[0 if wall['axis'] == 'X' else 1] = wall['plane'] if hit_on_entry else point[0 if wall['axis'] == 'X' else 1]
                    problem['yaw'] = waypoint_seed(problem, [(n, point)])
                    solved = solve(problem, seconds=min(candidate_seconds, max(.001, deadline - time.monotonic())),
                        stop_on_feasible=True, python_executable=python_executable, cancel_event=cancel_event)
                    geometry = None
                    if solved['feasible']:
                        candidate = _raw_candidate(problem, validate(problem, solved['candidate']['yaw']))
                        v = {'vertical': problem['vertical'], 'y': candidate['y'], 'vy': candidate['vy'], 'onGround': candidate['onGround']}
                        geometry = check_geometry(scene['collisionScene'], plan, candidate, v, compiler,
                                                  max(.01, deadline - time.monotonic()))
                        if geometry['feasible']:
                            found = True
                except (TimeoutError, ValueError, RuntimeError) as error:
                    geometry = {'feasible': False, 'reason': str(error)}
                records.append({'channel': channel['channel'], 'entryState': entry, 'hitOnEntry': hit_on_entry,
                                'status': 'feasible' if found else 'unknown', 'geometryCheck': geometry})
                if found:
                    entries.append(dict(channel, firstValidatedState=entry, unresolvedStates=unresolved[:]))
                    break
            if found:
                break
            unresolved.append(entry)
    return entries, records


def post_exit_reachable(schedule, coefficients, column, target):
    reach, carry = 0., float(f32(.15))
    for t in range(schedule['exitState'], schedule['ticks']):
        move = carry + coefficients['magnitude'][t]
        reach += move
        carry = coefficients['friction'][t] * move
    bounds = center_bounds(target)
    for a, origin in [(0, column['x']), (2, column['z'])]:
        if max(0., bounds[a] - (origin + 1 + f32(.15)), origin - f32(.15) - bounds[a + 1]) > reach + 1e-6:
            return False
    return True


def first_hit_bounds(scene, graph, source, prefix_edges, allocation, entries, column, wall, compiler, cfg, deadline):
    from .screen import contradiction
    from .ladder_geometry import outside_row
    for entry in entries:
        if 'firstHitLowerBounds' in entry:
            continue
        entry['firstHitLowerBounds'] = {}
        for state in range(entry['firstValidatedState'], min(cfg['maxInputs'], entry['firstValidatedState'] + cfg['entryDelay'] + 1)):
            if time.monotonic() + .05 >= deadline:
                return
            n = cfg['maxInputs']
            plan = {'grounded': [True] + [False] * (n - 1), 'jump': [True] + [False] * (n - 1)}
            coefficient = compiler.compile({'sharedMovement': True, 'inputs': scene['inputs']}, plan,
                                            max(.01, deadline - time.monotonic()))
            rows = [outside_row(entry, t, 1e-6, 'entry_bound') for t in range(state)]
            for axis, value in [('X', column['x']), ('Z', column['z'])]:
                rows.extend([{'mode': axis, 't1': state, 'cmp': 'GE', 'rhs': value},
                             {'mode': axis, 't1': state, 'cmp': 'LE', 'rhs': value + 1}])
            lower = n
            for tick in range(max(0, state - 1), n):
                problem = dict(coefficient, ticks=tick + 1, position=scene['initial']['position'][::2],
                    velocity=scene['initial']['velocity'][::2], walls=rows, ladderTicks=[t >= state for t in range(n + 1)],
                    contacts=[{'state': tick + 1, 'axis': 0 if wall['axis'] == 'X' else 1,
                               'plane': wall['plane'], 'direction': wall['direction']}])
                if prefix_edges or allocation[len(prefix_edges)]:
                    bounds, pad, speed = center_bounds(source), graph['bound']['movementAfterSupportCheck'], graph['bound']['storedSpeedNorm']
                    problem['initialIntervals'] = ([bounds[0] - pad, bounds[2] - pad], [bounds[1] + pad, bounds[3] + pad],
                                                  [-speed] * 2, [speed] * 2)
                if contradiction(problem) is None:
                    lower = tick
                    break
            entry['firstHitLowerBounds'][str(state)] = lower


def find_climbable_blocks(scene, compiler, normal_solver, *, seconds=60, route_seconds=8, candidate_seconds=2,
                         retries=1, improve=False, python_executable=None, cancel_event=None, on_attempt=None):
    from .block_search import build_transition_graph, compile_block_path
    started, cfg = time.monotonic(), ladder_settings(scene)
    deadline = started + seconds
    base = normal_solver(scene, compiler, seconds=min(seconds * .2, route_seconds), route_seconds=route_seconds,
        candidate_seconds=candidate_seconds, retries=retries, python_executable=python_executable,
        cancel_event=cancel_event) if not cfg['requireLadder'] else None
    try:
        graph = base['graph'] if base is not None and 'graph' in base else build_transition_graph(scene, compiler, seconds=max(.01, deadline - time.monotonic()))
    except TimeoutError as error:
        return {'status': 'unknown', 'best': None, 'termination': 'graph_timeout', 'error': str(error), 'attempts': []}
    best, attempts, cache, contexts = base.get('best') if base else None, [], {}, 0
    blocks = {b['id']: b for b in scene['blocks']}
    prefixes = ordinary_paths(graph, scene['startBlock'], scene['maxJumps'] - 1, cfg['maxOrdinaryPaths'])
    termination = 'search_exhausted'
    for path, prefix_edges in prefixes:
        for column in ladder_columns(scene['climbableCells']):
            for wall in sorted(climb_walls(column, scene['collisionScene']['solids']),
                               key=lambda w: (-max(p['box'][4] for p in w['parts']), w['direction'] * w['plane'])):
                targets = sorted(blocks, key=lambda name: math.hypot((blocks[name]['bounds'][0] + blocks[name]['bounds'][1]) / 2 - column['x'],
                                                                    (blocks[name]['bounds'][2] + blocks[name]['bounds'][3]) / 2 - column['z']))
                for target in targets:
                    from .block_profiles import normal_jump_height
                    source_top = blocks[path[-1]]['top']
                    if max(p['box'][4] for p in wall['parts']) <= source_top \
                            and blocks[target]['top'] > source_top + normal_jump_height():
                        continue
                    suffixes = ordinary_paths(graph, target, scene['maxJumps'] - len(prefix_edges) - 1,
                                               cfg['maxSuffixPaths'], scene['goalBlock'])
                    for suffix_path, suffix_edges in suffixes:
                        for allocation in run_allocations(len(prefix_edges) + 1 + len(suffix_edges), scene['maxRunTicksPerJump'],
                                                          scene['maxTotalRunTicks'], cfg['maxRunAllocations']):
                            if time.monotonic() >= deadline or contexts >= cfg['maxRouteContexts'] \
                                    or cancel_event is not None and cancel_event.is_set():
                                termination = 'deadline' if time.monotonic() >= deadline else 'cancelled' if cancel_event is not None and cancel_event.is_set() else 'context_limit'
                                break
                            contexts += 1
                            probe_key = (prefix_edges, tuple(allocation[:len(prefix_edges) + 1]), column['id'],
                                         wall['axis'], wall['direction'], wall['plane'],
                                         tuple(tuple(p['box']) for p in wall['parts']))
                            if probe_key not in cache:
                                try:
                                    cache[probe_key] = probe_entries(scene, graph, path, prefix_edges, target,
                                        allocation[:len(prefix_edges) + 1], column, wall, compiler, deadline=deadline,
                                        candidate_seconds=min(candidate_seconds, .5), max_entry=min(cfg['maxEntryInputs'], cfg['maxInputs'] - 1),
                                        python_executable=python_executable, cancel_event=cancel_event)
                                except TimeoutError:
                                    termination = 'deadline'
                                    break
                            entries, probes = cache[probe_key]
                            record = {'prefix': list(path), 'target': target, 'suffix': list(suffix_path), 'allocation': allocation,
                                      'column': column['id'], 'entryProbes': probes, 'status': 'unknown', 'numericalAttempts': []}
                            attempts.append(record)
                            if time.monotonic() + .05 >= deadline:
                                termination = 'deadline'
                                break
                            if not entries:
                                continue
                            source = blocks[path[-1]]
                            first_hit_bounds(scene, graph, source, prefix_edges, allocation, entries, column, wall, compiler, cfg, deadline)
                            if time.monotonic() + .05 >= deadline:
                                termination = 'deadline'
                                break
                            pool = generate_ladder_schedules({'version': 1,
                                'initial': {'position': source['top'], 'velocity': -.08 * f32(.98), 'onGround': True},
                                'region': {k: column[k] for k in ('minY', 'maxY')},
                                'entries': [{k: e[k] for k in ('family', 'channel', 'firstValidatedState', 'unresolvedStates', 'firstHitLowerBounds')} for e in entries],
                                'targetTop': blocks[target]['top'], 'jumpAtStart': True,
                                'maxInputs': min(cfg['maxInputs'], 200), 'entryDelay': cfg['entryDelay'],
                                'maxSkeletons': cfg['maxSkeletons'], 'maxSchedules': cfg['maxSchedules'], 'maxMaskWork': cfg['maxMaskWork']},
                                cancel_event=cancel_event)
                            record['generation'] = {k: v for k, v in pool.items() if k not in ('schedules', 'timingGroups')}
                            candidates = [s for s in pool['schedules'] if sum(s['hit']) >= cfg['minClimbInputs']]
                            def screen_candidate(s, coefficients):
                                from .screen import contradiction
                                channel = next(e for e in entries if e['channel'] == s['entryChannel'])
                                try:
                                    rows, contacts, _ = schedule_constraints(s, column, wall, channel, None)
                                except ValueError:
                                    return {'reason': 'finite_climb_wall'}
                                if not post_exit_reachable(s, coefficients, column, blocks[target]):
                                    return {'reason': 'post_exit_reach'}
                                problem = dict(coefficients, ticks=s['ticks'], position=scene['initial']['position'][::2],
                                    velocity=scene['initial']['velocity'][::2], walls=rows, contacts=contacts,
                                    ladderTicks=s['ladderTicks'])
                                if prefix_edges or allocation[len(prefix_edges)]:
                                    bounds, pad = center_bounds(source), graph['bound']['movementAfterSupportCheck']
                                    speed = graph['bound']['storedSpeedNorm']
                                    problem['initialIntervals'] = ([bounds[0] - pad, bounds[2] - pad],
                                        [bounds[1] + pad, bounds[3] + pad], [-speed] * 2, [speed] * 2)
                                return contradiction(problem)
                            ranking = rank_ladder_schedules(candidates, compiler, seconds=max(.01, min(5, deadline - time.monotonic())),
                                max_ranked=cfg['maxRanked'], representatives=cfg['representatives'], inputs=scene['inputs'],
                                cancel_event=cancel_event, screen=screen_candidate)
                            record['ranking'] = {k: v for k, v in ranking.items() if k not in ('ranked', 'backups')}
                            context_deadline = min(deadline, time.monotonic() + route_seconds)
                            for ranked in (ranking['ranked'] + ranking['backups'])[:cfg['maxNumericalSchedules']]:
                                s = ranked['schedule']
                                prefix_cost = sum(graph['transitions'][e]['profile']['airtime'] for e in prefix_edges + suffix_edges) + sum(allocation)
                                if best is not None and s['ticks'] + prefix_cost > best['plan']['ticks']:
                                    continue
                                if time.monotonic() >= context_deadline:
                                    break
                                channel = next(e for e in entries if e['channel'] == s['entryChannel'])
                                coefficient = ranked['coefficients']
                                if not post_exit_reachable(s, coefficient, column, blocks[target]):
                                    record.setdefault('postExitPruned', 0)
                                    record['postExitPruned'] += 1
                                    continue
                                target_point = [(blocks[target]['bounds'][a] + blocks[target]['bounds'][a + 1]) / 2 for a in (0, 2)]
                                faces = exit_faces(column, target_point)
                                if s['exitKind'] == 'top' and all(y >= column['maxY'] for y in s['trace']['position'][s['exitState']:]):
                                    faces = faces[:1]
                                for face in faces:
                                    if time.monotonic() >= context_deadline:
                                        break
                                    profile = {'airtime': s['ticks'], 'landingPhase': 'pre', 'position': s['trace']['position'],
                                        'velocity': s['trace']['velocity'], 'grounded': s['trace']['onGround'],
                                        'contacts': s['trace']['vertical']['contacts'], 'jump': [v is not None for v in s['trace']['vertical']['jumpVelocity']],
                                        'sneak': s['sneak'], 'ladderTicks': s['ladderTicks']}
                                    selected = {'source': path[-1], 'target': target, 'profileKind': 'ladder', 'profile': profile,
                                        'entryFace': None, 'exitFace': None,
                                        'ladderEvent': {'schedule': s, 'column': column, 'wall': wall, 'entryFace': channel, 'exitFace': face}}
                                    edges = [graph['transitions'][e] for e in prefix_edges] + [selected] + [graph['transitions'][e] for e in suffix_edges]
                                    full_path = list(path) + [target] + list(suffix_path[1:])
                                    compiled = compile_block_path(scene, full_path, transitions=edges)
                                    route = compiled['route']
                                    route['runBounds'] = [[v, v] for v in allocation]
                                    route['maxTotalRunTicks'] = sum(allocation)
                                    try:
                                        solved = solve_contact_branches(route, compiled['profiles'], floor_checks(scene, full_path, compiled['profiles']),
                                            compiler, seconds=max(.001, context_deadline - time.monotonic()), candidate_seconds=candidate_seconds,
                                            retries=retries, max_branches=scene['maxContactBranches'], rank_equal_cost=True, optimize_goal=True,
                                            python_executable=python_executable, cancel_event=cancel_event, geometry=scene['collisionScene'],
                                            motion_bound=graph['bound'], solid_blocks=[b['sourceBlock'] for b in sorted(scene['blocks'], key=lambda b: b['solidIndex'])])
                                    except (ValueError, TimeoutError) as error:
                                        record['numericalAttempts'].append({'status': 'unknown', 'reason': str(error)})
                                        continue
                                    record['numericalAttempts'].append({'status': solved['status'], 'ticks': s['ticks'] + prefix_cost,
                                        'entryState': s['entryState'], 'entryChannel': s['entryChannel'], 'exitKind': s['exitKind'],
                                        'exitFace': face, 'score': ranked['score'], 'contactAttempts': solved.get('contactAttempts', []),
                                        'solverAttempts': solved.get('attempts', [])})
                                    if solved.get('best') is not None:
                                        candidate = solved['best']
                                        candidate.update(blockIds=full_path, ladderSchedule=s, ladderColumn=column)
                                        sense = -1 if scene['objective']['sense'] == 'MAX' else 1
                                        if best is None or (candidate['plan']['ticks'], sense * candidate['candidate']['objective']) \
                                                < (best['plan']['ticks'], sense * best['candidate']['objective']):
                                            best = candidate
                                        record['status'] = 'feasible'
                            if on_attempt is not None:
                                on_attempt(copy.deepcopy(record))
                        if termination != 'search_exhausted':
                            break
                    if termination != 'search_exhausted':
                        break
                if termination != 'search_exhausted':
                    break
            if termination != 'search_exhausted':
                break
        if termination != 'search_exhausted':
            break
    if cancel_event is not None and cancel_event.is_set():
        termination = 'cancelled'
    return {'status': 'feasible' if best is not None else 'cancelled' if termination == 'cancelled' else 'unknown',
            'best': best, 'termination': termination, 'attempts': attempts, 'normalResult': base,
            'scene': scene, 'graph': graph, 'elapsedSeconds': time.monotonic() - started,
            'ticksLowerBound': 1, 'minimumTicksProven': False, 'objectiveOptimalityProven': False,
            'searchPolicy': 'total_ticks_then_objective', 'validationLevel': 'continuous_ladder_with_shared_scene',
            'assumptions': {'maxLadderInteractions': 1, 'climbableCellsSupplied': True,
                            'initialStateReoptimized': False, 'wholePrefixFacingsReoptimized': True}}
