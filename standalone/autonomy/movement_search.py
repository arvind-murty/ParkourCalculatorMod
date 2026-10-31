import copy
import heapq
import json
import math
import time

from standalone.ipopt.model import integer, number
from .case_profiles import entry_possible
from .movement_composition import append_choice, boundary_state, compile_program, empty_program
from .movement_generation import GENERATORS, generate_choices


def movement_settings(scene):
    settings = dict(scene.get('movementSearch', {}))
    defaults = dict(maxInputs=200, maxPrefixes=256, maxQueuedPrefixes=512, maxChoicesPerType=64,
                    maxInteractions=200, maxEntryInputs=14,
                    types=list(GENERATORS))
    if set(settings) - set(defaults):
        raise ValueError('Unsupported movement search settings')
    for key, value in defaults.items():
        settings.setdefault(key, value)
        if key != 'types':
            integer(settings[key], key, 0 if key == 'maxEntryInputs' else 1, 200 if key in ('maxInputs', 'maxInteractions') else 200000)
    if not isinstance(settings['types'], list) or not settings['types'] or len(set(settings['types'])) != len(settings['types']) \
            or any(kind not in GENERATORS for kind in settings['types']):
        raise ValueError('Select registered movement types')
    return settings


def iter_movement_programs(scene, *, settings=None, deadline=None, cancel_event=None, report=None, screen=None):
    cfg = movement_settings(scene) if settings is None else settings
    report = report if report is not None else {}
    initial = empty_program(scene['initial'])
    initial['terminalSupport'] = scene['startBlock']
    pending, serial, expanded, generated, screened, drops, seen = [(0, 0, initial)], 0, 0, 0, 0, 0, set()
    records, termination = [], 'exhausted'
    initial_bounds = [scene['initial']['position'][0]] * 2 + [scene['initial']['position'][2]] * 2
    def budget():
        if deadline is not None and time.monotonic() >= deadline:
            raise TimeoutError('Movement composition exceeded its budget')
    try:
        while pending:
            budget()
            if cancel_event is not None and cancel_event.is_set():
                termination = 'cancelled'
                break
            if expanded >= cfg['maxPrefixes']:
                termination = 'prefix_limit'
                break
            ticks, _, program = heapq.heappop(pending)
            if ticks > report.get('maxAcceptedTicks', cfg['maxInputs']):
                continue
            expanded += 1
            report.update(expandedPrefixes=expanded, generated=generated, remainingPrefixes=len(pending),
                          screenedPrefixes=screened, queueDrops=drops, generation=records, minimumTicksProven=False)
            if ticks and program['terminalSupport'] == scene['goalBlock'] and program['profile']['grounded'][-1] \
                    and (not scene.get('ladderSearch', {}).get('requireLadder') or program['ladderEvents']):
                program['terminal'] = 'landed'
                yield program
                continue
            remaining = cfg['maxInputs'] - ticks
            if not remaining or program['interactions'] >= cfg['maxInteractions']:
                continue
            context = dict(mode='open', scene=scene, program=program, state=boundary_state(program),
                maxInputs=remaining, maxEntryInputs=cfg['maxEntryInputs'], checkBudget=budget,
                deadline=deadline, cancelEvent=cancel_event, normalGap=False)
            for kind in cfg['types']:
                choices = 0
                for choice in generate_choices(kind, context):
                    budget()
                    if choices >= cfg['maxChoicesPerType']:
                        records.append(dict(type=kind, prefixTicks=ticks, termination='choice_limit', emitted=choices))
                        break
                    choices += 1
                    generated += 1
                    child = append_choice(program, choice, max_inputs=cfg['maxInputs'])
                    if child is None or not entry_possible(child['profile'], initial_bounds):
                        continue
                    key = json.dumps([child['profile'], [(e['column']['id'], e['wall'], e['entryFace'], e['exitFace'],
                        e['schedule']['entryState'], e['schedule']['exitState']) for e in child['ladderEvents']]], sort_keys=True)
                    if key in seen:
                        continue
                    seen.add(key)
                    if screen is not None and screen(child) is not None:
                        screened += 1
                        continue
                    serial += 1
                    heapq.heappush(pending, (child['profile']['airtime'], serial, child))
                    if len(pending) > cfg['maxQueuedPrefixes']:
                        drops += len(pending) - cfg['maxQueuedPrefixes']
                        pending = heapq.nsmallest(cfg['maxQueuedPrefixes'], pending)
                        heapq.heapify(pending)
                else:
                    records.append(dict(type=kind, prefixTicks=ticks, termination='exhausted', emitted=choices))
            records.extend(context.get('generationRecords', []))
    except TimeoutError:
        termination = 'deadline'
    report.update(termination=termination, expandedPrefixes=expanded, generated=generated,
        remainingPrefixes=len(pending), screenedPrefixes=screened, queueDrops=drops, generation=records,
        minimumTicksProven=False, types=cfg['types'])


def find_movement_blocks(request, compiler, *, seconds=60, candidate_seconds=2, retries=1,
                         improve=False, python_executable=None, cancel_event=None, on_attempt=None,
                         route_seconds=8):
    from .block_search import prepare_scene
    from .block_contacts import floor_checks, solve_contact_branches
    from .ranking import candidate_rank
    for name, value in [('seconds', seconds), ('candidate_seconds', candidate_seconds), ('route_seconds', route_seconds)]:
        if number(value, name) <= 0:
            raise ValueError(name + ' must be positive')
    integer(retries, 'retries', 0, 10)
    scene = prepare_scene(request)
    if scene['sceneModel'] != 'collision_boxes':
        raise ValueError('Generic movement composition requires collision_boxes geometry')
    cfg = movement_settings(scene)
    started = time.monotonic()
    deadline = started + seconds
    sample = compiler.compile(dict(scene, sharedMovement=True), dict(grounded=[True, True, False], jump=[False, True, False]), seconds)
    speed = max(math.hypot(scene['initial']['velocity'][0], scene['initial']['velocity'][2]),
                max(f * m / (1 - f) for m, f in zip(sample['magnitude'], sample['friction'])))
    motion_bound = dict(storedSpeedNorm=speed + 1e-8, movementAfterSupportCheck=speed + max(sample['magnitude']) + 1e-8,
                        groundMagnitude=sample['magnitude'][0])
    owners = {b['solidIndex']: b['sourceBlock'] for b in scene['blocks']}
    report, attempts, best = {}, [], None
    def screen(program):
        return screen_program(scene, program, compiler, max(.001, deadline - time.monotonic()))
    for program in iter_movement_programs(scene, settings=cfg, deadline=deadline, cancel_event=cancel_event,
                                          report=report, screen=screen):
        if time.monotonic() >= deadline:
            break
        compiled = compile_program(scene, program)
        result = solve_contact_branches(compiled['route'], compiled['profiles'],
            floor_checks(scene, [scene['startBlock'], scene['goalBlock']], compiled['profiles']), compiler,
            seconds=min(route_seconds, max(.001, deadline - time.monotonic())), candidate_seconds=candidate_seconds,
            retries=retries, max_branches=scene['maxContactBranches'], improve=improve,
            rank_equal_cost=True, optimize_goal=True, python_executable=python_executable,
            cancel_event=cancel_event, geometry=scene['collisionScene'],
            motion_bound=motion_bound,
            max_blockages=scene['maxBlockagesPerObstacle'], max_horizontal=scene['maxHorizontalBlockagesPerObstacle'],
            max_vertical=scene['maxVerticalBlockagesPerObstacle'], max_passage_candidates=scene['maxPassageCandidates'],
            solid_blocks=[owners[i] for i in range(len(owners))], max_wall_hits=scene['maxWallHits'],
            max_wall_candidates=scene['maxWallHitCandidates'])
        attempt = dict(ticks=program['profile']['airtime'], status=result['status'], termination=result['termination'],
                       units=[u['type'] for u in program['movementUnits']])
        if result.get('error'):
            attempt['error'] = result['error']
        attempts.append(attempt)
        if on_attempt is not None:
            on_attempt(attempt)
        if result['status'] == 'error':
            report['termination'] = 'numerical_error'
            break
        if result['best'] is not None and (best is None or candidate_rank(result['best'], scene['objective']) < candidate_rank(best, scene['objective'])):
            best = result['best']
            best['movementProgram'] = program
            best['blockIds'] = [scene['startBlock'], scene['goalBlock']]
            report['maxAcceptedTicks'] = best['plan']['ticks']
    return dict(status='feasible' if best is not None else 'cancelled' if report.get('termination') == 'cancelled' else 'error' if report.get('termination') == 'numerical_error' else 'unknown',
        best=best, termination=report.get('termination', 'deadline'), attempts=attempts, movementGeneration=report,
        elapsedSeconds=time.monotonic() - started, minimumTicksProven=False, objectiveOptimalityProven=False,
        validationLevel='continuous_movement_program_with_shared_scene_collision_checks')


def screen_program(scene, program, compiler, seconds):
    from .block_contacts import validate_vertical_schedule
    from .route import prepare_route, schedule
    from .screen import contradiction
    compiled = compile_program(scene, program, require_complete=False)
    route = prepare_route(compiled['route'])
    plan = schedule(route, [0])
    vertical = validate_vertical_schedule(route, compiled['profiles'], plan)
    if not vertical['feasible']:
        return dict(reason='vertical_schedule')
    if scene.get('maxWallHits', 0):
        return None
    plan['vertical'] = dict(vertical['vertical'], referencePosition=vertical['y'], referenceVelocity=vertical['vy'])
    coefficient = compiler.compile(route, plan, seconds)
    rows = [row for row in plan['walls'] if not row.get('name', '').startswith('landing:1:')]
    return contradiction(dict(coefficient, ticks=plan['ticks'], position=scene['initial']['position'][::2],
        velocity=scene['initial']['velocity'][::2], walls=rows, contacts=plan['contacts'],
        ladderTicks=plan.get('ladderTicks', [False] * (plan['ticks'] + 1))))
