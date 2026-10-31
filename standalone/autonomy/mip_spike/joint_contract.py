import copy
import math

from standalone.autonomy.block_contacts import validate_vertical_schedule
from standalone.autonomy.block_profiles import ordinary_jump_profile
from standalone.autonomy.block_search import prepare_scene
from standalone.autonomy.route import prepare_route, schedule
from standalone.ipopt import prepare
from standalone.ipopt.ladder import REST_CARRY
from standalone.ipopt.model import integer, number


def prepare_joint(request, compiler):
    if not isinstance(request, dict) or set(request) - {'scene', 'route', 'runBounds', 'positionRows'}:
        raise ValueError('Supply scene, route, optional runBounds and positionRows')
    scene = prepare_scene(request['scene'])
    if scene['sceneModel'] != 'collision_boxes' or scene.get('climbableCells'):
        raise ValueError('The joint subset requires finite collision boxes without ladders')
    ids = request['route']
    by_id = {b['id']: b for b in scene['blocks']}
    if not isinstance(ids, list) or not 2 <= len(ids) <= 13 or any(not isinstance(i, str) or i not in by_id for i in ids):
        raise ValueError('Supply a route of two through thirteen known supports')
    if ids[0] != scene['startBlock'] or ids[-1] != scene['goalBlock']:
        raise ValueError('Route endpoints must match the scene')
    blocks = [copy.deepcopy(by_id[i]) for i in ids]
    initial = scene['initial']
    if initial['position'][1] != blocks[0]['top'] or abs(initial['velocity'][1] - REST_CARRY) > 1e-12:
        raise ValueError('The joint subset starts grounded with the normal resting Y carry')
    profiles = [ordinary_jump_profile(a['top'], b['top'], max_inputs=scene['maxFlightInputs'])
                for a, b in zip(blocks, blocks[1:])]
    if any(p is None for p in profiles):
        raise ValueError('A route leg has no ordinary jump profile')
    bounds = request.get('runBounds', [[0, scene['maxRunTicksPerJump']] for _ in profiles])
    route = prepare_route(dict(version=1, blocks=blocks, initial=initial, inputs=scene['inputs'],
        airtimes=[p['airtime'] for p in profiles], verticalProfiles=profiles, runBounds=bounds,
        maxTotalRunTicks=scene['maxTotalRunTicks'], supportClearance=scene['supportClearance'],
        sharedMovement=True, objective=scene['objective']))
    footprints = compiler.derive_footprints(route, 5)
    standing = compiler.derive_standing_footprints(route, 5)
    for block, landing, running in zip(route['blocks'], footprints, standing):
        block['_derivedFootprint'], block['_derivedStandingFootprint'] = landing, running
    sample = compiler.compile(route, dict(grounded=[True, True, False, False, True, True, False],
        jump=[False, True, False, False, False, True, False]), 5)
    modes = {}
    for name, tick, repeat in [('run', 0, 4), ('jump', 1, 5), ('air', 2, 6)]:
        modes[name] = {key: sample[key][tick] for key in ('magnitude', 'friction', 'base')}
        if any(sample[key][tick] != sample[key][repeat] for key in modes[name]):
            raise ValueError('This input policy requires an extended input-state model')
    if not all(sample['movement']['sprinting']):
        raise ValueError('The joint subset requires uninterrupted sprint without contacts')
    rows = copy.deepcopy(request.get('positionRows', []))
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise ValueError('positionRows must be a list')
    for row in rows:
        if set(row) - {'mode', 't1', 'cmp', 'rhs', 'name'} or row.get('mode') not in ('X', 'Z') \
                or row.get('cmp') not in ('GE', 'LE', 'EQ'):
            raise ValueError('The joint subset supports single-state X/Z position rows')
        integer(row['t1'], 'position state', 0, 200)
        number(row['rhs'], 'position boundary')
    minimum = sum(route['airtimes']) + sum(b[0] for b in bounds)
    maximum = min(200, sum(route['airtimes']) + min(sum(b[1] for b in bounds), route['maxTotalRunTicks']))
    if minimum > maximum:
        raise ValueError('No supported horizon satisfies the run bounds')
    return dict(scene=scene, route=route, profiles=profiles, modes=modes, positionRows=rows,
                coefficientSource=sample['model'], completeAllocationsGenerated=0,
                minimumHorizon=minimum, maximumHorizon=maximum)


def compile_allocation(case, allocation, compiler, *, selected_rows=(), yaw=None):
    route = case['route']
    plan = schedule(route, allocation)
    plan['walls'].extend(copy.deepcopy(case['positionRows']) + list(selected_rows))
    if any(row['t1'] > plan['ticks'] for row in plan['walls']):
        raise ValueError('A position row lies beyond the selected horizon')
    vertical = validate_vertical_schedule(route, case['profiles'], plan)
    if not vertical['feasible']:
        raise ValueError('The selected schedule failed original vertical simulation')
    plan['vertical'] = dict(vertical['vertical'], referencePosition=vertical['y'], referenceVelocity=vertical['vy'])
    plan['movementComposition'] = True
    coefficients = compiler.compile(route, plan, 5)
    for tick in range(plan['ticks']):
        mode = 'jump' if plan['jump'][tick] else 'run' if plan['grounded'][tick] else 'air'
        if any(not math.isclose(coefficients[key][tick], case['modes'][mode][key], rel_tol=0, abs_tol=1e-14)
               for key in ('magnitude', 'friction', 'base')):
            raise ValueError('Selected schedule disagrees with local coefficient definitions')
    problem = prepare(dict(coefficients, version=1, ticks=plan['ticks'],
        position=route['initial']['position'][::2], velocity=route['initial']['velocity'][::2],
        threshold=.005, yaw=yaw if yaw is not None else [0.] * plan['ticks'], walls=plan['walls'],
        contacts=[], objective=dict(route['objective'], tick=plan['ticks']), vertical=plan['vertical']))
    return dict(name='joint-schedule', scene=case['scene'], route=route, profiles=case['profiles'],
                plan=plan, vertical=vertical, problem=problem)
