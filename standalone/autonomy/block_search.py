import copy
import heapq
import math
import time
from collections import deque

from standalone.ipopt.model import integer, number
from .block_contacts import floor_checks, solve_contact_branches
from .block_profiles import compile_open_air_route, ordinary_jump_profile
from .route import prepare_route


def prepare_scene(request):
    scene = copy.deepcopy(request)
    if not isinstance(scene, dict):
        raise ValueError('Expected a block scene')
    allowed = {'version', 'sceneModel', 'initial', 'blocks', 'startBlock', 'goalBlock', 'inputs',
               'maxJumps', 'maxPrefixes', 'maxQueuedPrefixes', 'maxRunTicksPerJump', 'maxTotalRunTicks',
               'maxFlightInputs', 'maxContactBranches', 'supportClearance'}
    extra = set(scene) - allowed
    if extra:
        raise ValueError('Unsupported block-scene fields: ' + ', '.join(sorted(extra)))
    if scene.get('sceneModel') not in ('coplanar_support_planes', 'support_planes'):
        raise ValueError('Declare a support_planes or coplanar_support_planes scene; solid side faces are not modeled')
    blocks = scene.get('blocks')
    if not isinstance(blocks, list) or not 2 <= len(blocks) <= 64:
        raise ValueError('Supply 2 through 64 candidate supports')
    ids = []
    for block in blocks:
        if not isinstance(block, dict) or set(block) - {'id', 'bounds', 'top'}:
            raise ValueError('Each candidate is one rectangular support with id, bounds and top')
        if not isinstance(block.get('id'), str) or not block['id']:
            raise ValueError('Each candidate support needs a nonempty ID')
        ids.append(block['id'])
    if len(set(ids)) != len(ids):
        raise ValueError('Candidate support IDs must be unique')
    if scene.get('startBlock') not in ids or scene.get('goalBlock') not in ids:
        raise ValueError('Start and goal must name candidate support IDs')
    if scene['startBlock'] == scene['goalBlock']:
        raise ValueError('This route search requires different start and goal supports')
    if not isinstance(scene.get('inputs', {}), dict):
        raise ValueError('Input policy must be an object')
    scene['inputs'] = dict(scene.get('inputs', {}))
    if set(scene['inputs']) - {'forward', 'strafe', 'sprint', 'incomingSprint', 'force45'}:
        raise ValueError('Unsupported input-policy fields')
    scene['inputs'].setdefault('sprint', True)
    scene['inputs'].setdefault('force45', True)
    if scene['inputs']['sprint'] is not True or scene['inputs']['force45'] is not True:
        raise ValueError('Block search uses sprint jump and sprint 45')
    for key, default, lo, hi in [('maxJumps', 4, 1, 12), ('maxPrefixes', 200, 1, 100000),
                                 ('maxQueuedPrefixes', 4000, 1, 100000),
                                 ('maxRunTicksPerJump', 6, 0, 1000), ('maxTotalRunTicks', 12, 0, 1000),
                                 ('maxFlightInputs', 200, 2, 200), ('maxContactBranches', 16, 1, 256)]:
        integer(scene.setdefault(key, default), key, lo, hi)
    ordered = [blocks[ids.index(scene['startBlock'])]] + [b for b in blocks if b['id'] != scene['startBlock']]
    checked = prepare_route({'version': scene.get('version'), 'initial': scene.get('initial'),
                             'blocks': ordered, 'airtimes': [12] * (len(blocks) - 1),
                             'inputs': scene['inputs'], 'maxTotalRunTicks': scene['maxTotalRunTicks'],
                             'supportClearance': scene.get('supportClearance', 1e-6)})
    if scene['sceneModel'] == 'coplanar_support_planes' and any(b['top'] != ordered[0]['top'] for b in blocks):
        raise ValueError('Use sceneModel=support_planes for different support heights')
    scene['inputs'] = checked['inputs']
    scene['initial'] = checked['initial']
    scene['supportClearance'] = checked['supportClearance']
    return scene


def minimum_distance(a, b):
    dx = max(0.0, a[0] - b[1], b[0] - a[1])
    dz = max(0.0, a[2] - b[3], b[2] - a[3])
    return math.hypot(dx, dz)


def build_transition_graph(scene, compiler, *, seconds=5):
    scene = prepare_scene(scene)
    if number(seconds, 'graph seconds') <= 0:
        raise ValueError('Graph seconds must be positive')
    deadline = time.monotonic() + seconds

    def remaining():
        value = deadline - time.monotonic()
        if value <= 0:
            raise TimeoutError('Transition graph compilation exceeded its budget')
        return value

    ids = [b['id'] for b in scene['blocks']]
    adjacency = {block: [] for block in ids}
    sample = compiler.compile(scene, {'grounded': [True, True, False], 'jump': [False, True, False]},
                              remaining())
    magnitude, friction = sample['magnitude'], sample['friction']
    if len(magnitude) != 3 or len(friction) != 3:
        raise ValueError('Invalid movement-bound coefficients')
    pairs = list(zip(magnitude, friction))
    if any(not math.isfinite(m) or not math.isfinite(f) or m < 0 or not 0 <= f < 1 for m, f in pairs):
        raise ValueError('Graph bounds require finite nonnegative magnitude and drag below one')
    speed = max(math.hypot(scene['initial']['velocity'][0], scene['initial']['velocity'][2]),
                max(f * m / (1 - f) for m, f in pairs))
    speed += 1e-9 * (1 + speed)
    runoff = speed + max(m for m, _ in pairs)
    footprints = compiler.derive_footprints(scene, remaining())
    if len(footprints) != len(ids):
        raise ValueError('Invalid support footprints')
    bounds = {}
    for block, footprint in zip(ids, footprints):
        if not isinstance(footprint, list) or len(footprint) != 4:
            raise ValueError('Invalid support footprint')
        for value in footprint:
            number(value, 'support footprint')
        if footprint[0] >= footprint[1] or footprint[2] >= footprint[3]:
            raise ValueError('Empty support footprint')
        bounds[block] = footprint
    coordinate_scale = max(abs(value) for footprint in footprints for value in footprint)
    rejected, profile_cache, flight_bounds = [], {}, {}
    profiles = {block: {} for block in ids}
    for source in scene['blocks']:
        for target in scene['blocks']:
            remaining()
            key = (source['top'], target['top'])
            if key not in profile_cache:
                profile_cache[key] = ordinary_jump_profile(*key, max_inputs=scene['maxFlightInputs'])
            profile = profile_cache[key]
            if profile is None:
                rejected.append({'source': source['id'], 'target': target['id'],
                                 'reason': 'ordinary_profile_not_generated'})
                continue
            n = profile['airtime']
            if n not in flight_bounds:
                flight = compiler.compile(scene, {'grounded': [True] + [False] * (n - 1),
                                                   'jump': [True] + [False] * (n - 1)}, remaining())
                if len(flight['magnitude']) != n or len(flight['friction']) != n:
                    raise ValueError('Invalid flight-bound coefficients')
                if any(not math.isfinite(m) or not math.isfinite(f) or m < 0 or not 0 <= f < 1
                       or f * m > (1 - f) * speed
                       for m, f in zip(flight['magnitude'], flight['friction'])):
                    raise ValueError('Flight coefficients exceed the movement bound')
                travel, carry = 0.0, speed
                for m, f in zip(flight['magnitude'][:-1], flight['friction'][:-1]):
                    move = carry + m
                    travel += move
                    carry = move * f
                radius = travel + runoff
                padding = 1e-8 * (1 + radius) + 1e-12 * (1 + coordinate_scale) * (n + 1)
                number(radius + padding, 'horizontal reach bound')
                flight_bounds[n] = {'travel': travel, 'radius': radius + padding}
            radius = flight_bounds[n]['radius']
            gap = minimum_distance(bounds[source['id']], bounds[target['id']])
            if gap > radius:
                rejected.append({'source': source['id'], 'target': target['id'],
                                 'reason': 'optimistic_horizontal_reach', 'gap': gap,
                                 'radius': radius, 'airtime': n})
                continue
            adjacency[source['id']].append(target['id'])
            profiles[source['id']][target['id']] = profile
    reverse = {block: [] for block in ids}
    for source, targets in adjacency.items():
        for target in targets:
            reverse[target].append(source)
    distances = {scene['goalBlock']: 0}
    pending = deque([scene['goalBlock']])
    while pending:
        target = pending.popleft()
        for source in reverse[target]:
            if source not in distances:
                distances[source] = distances[target] + 1
                pending.append(source)
    flight_to_goal, pending = {scene['goalBlock']: 0}, [(0, scene['goalBlock'])]
    while pending:
        cost, target = heapq.heappop(pending)
        if cost != flight_to_goal[target]:
            continue
        for source in reverse[target]:
            next_cost = cost + profiles[source][target]['airtime']
            if next_cost < flight_to_goal.get(source, math.inf):
                flight_to_goal[source] = next_cost
                heapq.heappush(pending, (next_cost, source))
    return {'adjacency': adjacency, 'distanceToGoal': distances, 'profiles': profiles,
            'flightInputsToGoal': flight_to_goal, 'flightBounds': flight_bounds,
            'rejectedEdges': rejected,
            'profileLimitReached': any(r['reason'] == 'ordinary_profile_not_generated' for r in rejected),
            'bound': {'storedSpeedNorm': speed, 'runoffAllowance': runoff,
                      'flightTravelToLandingCheck': max((b['travel'] for b in flight_bounds.values()), default=0),
                      'radius': max((b['radius'] for b in flight_bounds.values()), default=0)}}


def compile_block_path(scene, block_ids):
    by_id = {b['id']: b for b in scene['blocks']}
    if not isinstance(block_ids, (list, tuple)) or len(block_ids) < 2 or block_ids[0] != scene['startBlock']:
        raise ValueError('A candidate path starts at the start support and contains at least one jump')
    blocks = []
    for index, block_id in enumerate(block_ids):
        if block_id not in by_id:
            raise ValueError('Unknown support in candidate path')
        block = copy.deepcopy(by_id[block_id])
        block['id'] = block_id + '@' + str(index)
        blocks.append(block)
    compiled = compile_open_air_route({'version': 1, 'sceneModel': 'open_air_supports',
                                      'initial': scene['initial'], 'blocks': blocks, 'inputs': scene['inputs'],
                                      'maxTotalRunTicks': scene['maxTotalRunTicks'],
                                      'runBounds': [[0, scene['maxRunTicksPerJump']] for _ in block_ids[1:]],
                                      'supportClearance': scene['supportClearance']},
                                     max_inputs=scene['maxFlightInputs'])
    return compiled


def search_block_paths(scene, graph, evaluate, *, seconds=60, improve=False, cancel_event=None, on_attempt=None):
    scene = prepare_scene(scene)
    if number(seconds, 'search seconds') <= 0:
        raise ValueError('Search seconds must be positive')
    started = time.monotonic()
    deadline = started + seconds
    distances = graph['distanceToGoal']
    by_id = {b['id']: b for b in scene['blocks']}
    goal = by_id[scene['goalBlock']]['bounds']
    pending, attempts, unresolved = [], [], []
    best, termination = None, 'search_exhausted'
    serial = 0
    horizon_prunes, disconnected_prunes, pool_drops = 0, 0, 0

    def offer(path, parent_status, parent_cost):
        nonlocal serial, horizon_prunes, disconnected_prunes
        current = path[-1]
        if current not in distances:
            disconnected_prunes += 1
            return
        lower_hops = len(path) - 1 + distances[current]
        if lower_hops > scene['maxJumps']:
            horizon_prunes += 1
            return
        serial += 1
        prefix_inputs = sum(graph.get('profiles', {}).get(a, {}).get(b, {'airtime': 12})['airtime']
                            for a, b in zip(path, path[1:]))
        lower_inputs = prefix_inputs + graph.get('flightInputsToGoal', {}).get(current, 12 * distances[current])
        priority = (lower_inputs, lower_hops, parent_status != 'feasible', parent_cost,
                    minimum_distance(by_id[current]['bounds'], goal), serial)
        heapq.heappush(pending, (priority, tuple(path)))

    offer([scene['startBlock']], 'feasible', 0)
    while pending:
        if cancel_event is not None and cancel_event.is_set():
            termination = 'cancelled'
            break
        if time.monotonic() >= deadline:
            termination = 'deadline'
            break
        if len(attempts) >= scene['maxPrefixes']:
            termination = 'candidate_limit'
            break
        _, path = heapq.heappop(pending)
        status, cost = 'feasible', 0
        if len(path) > 1:
            result = evaluate(list(path), max(.001, deadline - time.monotonic()))
            if not isinstance(result, dict) or result.get('status') not in ('feasible', 'unknown', 'error', 'cancelled'):
                raise ValueError('Invalid path evaluator result')
            status = result['status']
            if status == 'feasible':
                if not isinstance(result.get('best'), dict):
                    raise ValueError('A feasible path needs an accepted candidate')
                cost = integer(result['best'].get('cost'), 'run cost', 0, scene['maxTotalRunTicks'])
            record = {'blockIds': list(path), 'status': status, 'cost': cost if status == 'feasible' else None,
                      'termination': result.get('termination'), 'elapsedSeconds': time.monotonic() - started,
                      'innerProbes': len(result.get('attempts', [])),
                      'contactBranches': len(result.get('contactAttempts', [])), 'error': result.get('error')}
            attempts.append(record)
            if on_attempt is not None:
                on_attempt(copy.deepcopy(record))
            if status == 'unknown':
                unresolved.append(record)
            if status in ('error', 'cancelled'):
                termination = status
                break
            if status == 'feasible' and path[-1] == scene['goalBlock']:
                if best is None or (cost, result['best']['plan']['ticks']) < (best['cost'], best['plan']['ticks']):
                    best = copy.deepcopy(result['best'])
                    best['blockIds'] = list(path)
                if not improve or best['cost'] == 0:
                    termination = 'first_feasible'
                    break
        if path[-1] == scene['goalBlock'] or len(path) - 1 >= scene['maxJumps']:
            continue
        for target in graph['adjacency'][path[-1]]:
            offer(path + (target,), status, cost)
        if len(pending) > scene['maxQueuedPrefixes']:
            pool_drops += len(pending) - scene['maxQueuedPrefixes']
            pending = heapq.nsmallest(scene['maxQueuedPrefixes'], pending)
            heapq.heapify(pending)
    return {'status': 'feasible' if best is not None else
            ('cancelled' if termination == 'cancelled' else 'error' if termination == 'error' else 'unknown'),
            'best': best, 'termination': termination, 'attempts': attempts, 'unresolvedPrefixes': unresolved,
            'elapsedSeconds': time.monotonic() - started, 'minimumRunTicksProven': best is not None and best['cost'] == 0,
            'pruning': {'horizontalEdges': sum(r.get('reason') == 'optimistic_horizontal_reach' for r in graph['rejectedEdges']),
                        'profileEdges': sum(r.get('reason') == 'ordinary_profile_not_generated' for r in graph['rejectedEdges']),
                        'horizonPrefixes': horizon_prunes,
                        'disconnectedPrefixes': disconnected_prunes, 'heuristicPoolDrops': pool_drops},
            'remainingPrefixes': len(pending), 'searchPolicy': 'bounded_best_first_route_prefix_pool'}


def find_blocks(request, compiler, *, seconds=60, route_seconds=8, candidate_seconds=2, retries=1,
                improve=False, python_executable=None, cancel_event=None, on_attempt=None):
    scene = prepare_scene(request)
    for value, name in [(seconds, 'search seconds'), (route_seconds, 'route seconds'),
                         (candidate_seconds, 'candidate seconds')]:
        if number(value, name) <= 0:
            raise ValueError(name + ' must be positive')
    integer(retries, 'retries', 0, 10)
    started = time.monotonic()
    if cancel_event is not None and cancel_event.is_set():
        return {'status': 'cancelled', 'termination': 'cancelled', 'best': None, 'attempts': [],
                'elapsedSeconds': time.monotonic() - started}
    try:
        graph = build_transition_graph(scene, compiler, seconds=min(seconds, 3 * candidate_seconds))
    except TimeoutError as error:
        return {'status': 'unknown', 'termination': 'graph_timeout', 'error': str(error), 'best': None,
                'attempts': [], 'elapsedSeconds': time.monotonic() - started}
    except (ValueError, RuntimeError, OSError) as error:
        return {'status': 'error', 'termination': 'graph_compilation', 'error': str(error), 'best': None,
                'attempts': [], 'elapsedSeconds': time.monotonic() - started}

    def evaluate(block_ids, available):
        compiled = compile_block_path(scene, block_ids)
        if compiled['status'] != 'derived':
            return {'status': 'unknown', 'termination': 'profile_limit'}
        checks = floor_checks(scene, block_ids, compiled['profiles'])
        return solve_contact_branches(compiled['route'], compiled['profiles'], checks, compiler,
                                      seconds=min(route_seconds, available), candidate_seconds=candidate_seconds,
                                      retries=retries, max_branches=scene['maxContactBranches'], improve=improve,
                                      python_executable=python_executable, cancel_event=cancel_event)

    remaining = seconds - (time.monotonic() - started)
    result = search_block_paths(scene, graph, evaluate, seconds=max(.001, remaining), improve=improve,
                                cancel_event=cancel_event, on_attempt=on_attempt)
    result.update(elapsedSeconds=time.monotonic() - started, graph=graph, scene=scene,
                  validationLevel='continuous_support_planes_with_floor_checks',
                  assumptions={'solidSideFaces': False, 'ceilings': False, 'stairBoost': False,
                               'inputPolicy': 'sprint_jump_sprint_45', 'facings': 'free',
                               'routeOrderSupplied': False, 'airtimesSupplied': False,
                               'otherFloorContacts': 'checked_at_generated_crossings'})
    return result
