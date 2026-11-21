import copy
from functools import lru_cache

from parkour_solver.physics.vertical import REST_CARRY, f32, vertical_carry, vertical_step
from parkour_solver.schema import integer, number
from parkour_solver.scene.route import prepare_route


@lru_cache(maxsize=1)
def normal_jump_height():
    return max(ordinary_jump_profile(0, 0)['position'])


def ordinary_jump_profile(source_top, target_top, *, max_inputs=200, threshold=.005):
    return _flight_profile(source_top, target_top, True, max_inputs, threshold)


def runoff_profile(source_top, target_top, *, max_inputs=200, threshold=.005):
    source_top = number(source_top, 'source top')
    target_top = number(target_top, 'target top')
    integer(max_inputs, 'maximum flight inputs', 1, 200)
    if target_top >= source_top:
        return None
    return _flight_profile(source_top, target_top, False, max_inputs, threshold)


def _flight_profile(source_top, target_top, jumping, max_inputs, threshold=.005):
    source_top = number(source_top, 'source top')
    target_top = number(target_top, 'target top')
    integer(max_inputs, 'maximum flight inputs', 2 if jumping else 1, 200)
    threshold=number(threshold,'physics threshold')
    if threshold<0:raise ValueError('Physics threshold must be nonnegative')
    config = {'threshold': threshold, 'jumpVelocity': [f32(.42) if jumping else None] + [None] * (max_inputs - 1),
              'sneak': [False] * max_inputs, 'contacts': []}
    y, vy, ground = source_top, REST_CARRY, True
    positions, velocities, grounded = [y], [vy], [ground]
    for tick in range(max_inputs):
        next_y, movement, next_ground, failures = vertical_step(config, tick, y, vy, False, ground, 0)
        if movement < 0 and y >= target_top and next_y <= target_top:
            config['contacts'] = [{'state': tick + 1, 'plane': target_top, 'direction': -1}]
            next_y, movement, next_ground, failures = vertical_step(config, tick, y, vy, False, ground, 0)
        if failures:
            raise ValueError('The existing vertical model rejected the derived profile: ' + str(failures))
        vy = vertical_carry(movement, False, False)
        y, ground = next_y, next_ground
        positions.append(y)
        velocities.append(vy)
        grounded.append(ground)
        if ground:
            return {'airtime': tick + 1, 'landingPhase': 'pre', 'position': positions,
                    'velocity': velocities, 'grounded': grounded, 'contacts': config['contacts'],
                    'jump': [jumping] + [False] * tick}
    return None


def stair_jump_profile(source_top, target_top):
    source_top = number(source_top, 'source top')
    target_top = number(target_top, 'target top')
    if not f32(.42) < target_top - source_top <= f32(.6):
        return None
    config = {'threshold': .005, 'jumpVelocity': [f32(.42), None], 'sneak': [False, False],
              'contacts': [{'state': 1, 'plane': target_top, 'direction': -1, 'step': True},
                           {'state': 2, 'plane': target_top, 'direction': -1}]}
    y, vy, ground = source_top, REST_CARRY, True
    positions, velocities, grounded = [y], [vy], [ground]
    for tick in range(2):
        y, movement, ground, failures = vertical_step(config, tick, y, vy, False, ground, 0)
        if failures:
            return None
        vy = vertical_carry(movement, False, False)
        positions.append(y)
        velocities.append(vy)
        grounded.append(ground)
    return {'airtime': 2, 'landingPhase': 'pre', 'position': positions, 'velocity': velocities,
            'grounded': grounded, 'contacts': config['contacts'], 'jump': [True, False]}


def compile_open_air_route(request, *, max_inputs=200):
    if not isinstance(request, dict):
        raise ValueError('Expected a block-route request')
    allowed = {'version', 'sceneModel', 'initial', 'blocks', 'inputs', 'runBounds', 'objective',
               'maxTotalRunTicks', 'supportClearance', 'constraints'}
    extra = set(request) - allowed
    if extra:
        raise ValueError('Unsupported open-air block-route fields: ' + ', '.join(sorted(extra)))
    if request.get('sceneModel') != 'open_air_supports':
        raise ValueError('Declare sceneModel=open_air_supports; solid side faces and surroundings are not modeled')
    integer(max_inputs, 'maximum flight inputs', 2, 200)
    blocks = request.get('blocks')
    if not isinstance(blocks, list) or len(blocks) < 2:
        raise ValueError('Supply a start support and at least one landing support')
    for block in blocks:
        if not isinstance(block, dict) or 'bounds' not in block or set(block) - {'id', 'bounds', 'top', 'standingBounds'}:
            raise ValueError('Supply one rectangular support per route entry, with bounds and top')
    route = copy.deepcopy(request)
    del route['sceneModel']
    route['airtimes'] = [2] * (len(blocks) - 1)
    route = prepare_route(route)
    ids = [block['id'] for block in route['blocks']]
    if len(set(ids)) != len(ids):
        raise ValueError('Route support IDs must be unique')
    profiles = []
    for index, (source, target) in enumerate(zip(route['blocks'], route['blocks'][1:])):
        profile = ordinary_jump_profile(source['top'], target['top'], max_inputs=max_inputs)
        if profile is None:
            return {'status': 'unknown', 'reason': 'no_ordinary_profile_within_limit',
                    'leg': index, 'sourceBlock': source['id'], 'targetBlock': target['id'],
                    'maxInputs': max_inputs, 'profiles': profiles}
        profile.update(sourceBlock=source['id'], targetBlock=target['id'])
        profiles.append(profile)
    route['airtimes'] = [profile['airtime'] for profile in profiles]
    route['profileDerivation'] = {'kind': 'ordinary_open_air', 'sceneModel': request['sceneModel'],
                                 'verticalModel': 'parkour_solver.physics.vertical',
                                 'suppliedAirtimes': False, 'solidSideFaces': False,
                                 'otherSupportContacts': False}
    return {'status': 'derived', 'route': prepare_route(route), 'profiles': profiles}


def compile_block_path(scene, block_ids, transitions=None):
    by_id = {b['id']: b for b in scene['blocks']}
    if not isinstance(block_ids, (list, tuple)) or len(block_ids) < 2 or block_ids[0] != scene['startBlock']:
        raise ValueError('A candidate path starts at the start support and contains at least one jump')
    blocks = []
    for index, block_id in enumerate(block_ids):
        if block_id not in by_id:
            raise ValueError('Unknown support in candidate path')
        block = copy.deepcopy(by_id[block_id])
        block = {key: block[key] for key in ('id', 'bounds', 'top')}
        block['id'] = block_id + '@' + str(index)
        blocks.append(block)
    if transitions is not None:
        if len(transitions) != len(block_ids) - 1:
            raise ValueError('One transition is required per selected edge')
        selected, entries, exits, kinds = [], [], [], []
        for leg, transition in enumerate(transitions):
            if transition['source'] != block_ids[leg] or transition['target'] != block_ids[leg + 1]:
                raise ValueError('Selected transition does not match the support path')
            profile = copy.deepcopy(transition['profile'])
            if scene['sceneModel'] == 'collision_boxes' and transition['profileKind'] in ('stair', 'step') and not profile.get('caseEvents'):
                from parkour_solver.movement.units import profile_program
                profile['caseEvents'] = [dict(type='stepping', case='slab_boost' if transition['profileKind'] == 'stair' else 'normal_step',
                    input=0, plane=blocks[leg + 1]['top'], boxes=[scene['collisionScene']['solids'][by_id[block_ids[leg + 1]]['solidIndex']]],
                    anchors={'hit': 1})]
                profile['generatedUnits'] = profile_program(profile)
            profile.update(sourceBlock=blocks[leg]['id'], targetBlock=blocks[leg + 1]['id'],
                           profileKind=transition['profileKind'])
            selected.append(profile)
            entries.append(dict(transition['entryFace'], outsideFrom=0, outsideThrough=0, enter=1)
                           if transition['entryFace'] else None)
            exits.append(transition.get('exitFace'))
            kinds.append('movement' if transition['profileKind'] == 'movement' else 'walk_off' if transition['profileKind'] == 'run_off' else 'step' if transition['profileKind'] == 'step' else 'jump')
        route = prepare_route({'version': 1, 'initial': scene['initial'], 'blocks': blocks,
                               'inputs': scene['inputs'], 'objective': scene['objective'],
                               'airtimes': [p['airtime'] for p in selected],
                               'kinds': kinds, 'landingPhases': ['post' if k == 'step' else p.get('landingPhase', 'pre')
                                                               for p, k in zip(selected, kinds)],
                               'contactEntries': entries, 'walkOffExits': exits,
                               'maxTotalRunTicks': scene['maxTotalRunTicks'],
                               'runBounds': [[0, scene['maxRunTicksPerJump']] for _ in selected],
                               'supportClearance': scene['supportClearance']})
        route['profileDerivation'] = {'kind': 'selected_height_profiles', 'suppliedAirtimes': False,
                                     'verticalModel': 'parkour_solver.physics.vertical', 'solidSideFaces': False}
        if scene['sceneModel'] == 'collision_boxes':
            route['sharedMovement'] = True
            route['ceilingEvents'] = [dict(p['ceilingEvent'], leg=leg) for leg, p in enumerate(selected)
                                      if 'ceilingEvent' in p]
            route['ladderEvents'] = [dict(edge['ladderEvent'], leg=leg) for leg, edge in enumerate(transitions)
                                     if 'ladderEvent' in edge]
            route['ladderEvents'].extend(dict(event, leg=leg) for leg, edge in enumerate(transitions)
                                          for event in edge.get('ladderEvents', []))
            route['verticalProfiles'] = selected
            route['contactEvents'] = [dict(event, leg=leg) for leg, profile in enumerate(selected)
                                      for event in profile.get('contactEvents', [])]
        return {'status': 'derived', 'route': route, 'profiles': selected}
    compiled = compile_open_air_route({'version': 1, 'sceneModel': 'open_air_supports',
                                      'initial': scene['initial'], 'blocks': blocks, 'inputs': scene['inputs'],
                                      'objective': scene['objective'],
                                      'maxTotalRunTicks': scene['maxTotalRunTicks'],
                                      'runBounds': [[0, scene['maxRunTicksPerJump']] for _ in block_ids[1:]],
                                      'supportClearance': scene['supportClearance']},
                                     max_inputs=scene['maxFlightInputs'])
    return compiled
