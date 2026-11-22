"""Shared test builders extracted from tests.unit.spatial.test_checkpoint_guidance."""
from parkour_solver.scene.normalize import prepare_scene

def flat_scene():
    return prepare_scene(dict(version=1, sceneModel='collision_boxes', geometryBounds=[-10,-5,-10,20,12,10],
        initial=dict(position=[.5,1,.5], velocity=[.25,-.0784000015258789,0], yaw=-90),
        startBlock='start', goalBlock='goal', maxWallHits=0, maxContactCombinations=0,
        blocks=[dict(id='start', boxes=[[-2,0,-2,3,1,2]]), dict(id='goal', boxes=[[5,0,-1,6,1,1]])],
        movementSearch=dict(types=['normal'], maxInputs=30, maxPrefixes=96, maxQueuedPrefixes=128, maxChoicesPerType=16)))

def programs(scene, tick, limit=64):
    """Up to `limit` distinct ungated programs of the scene's movement types that end exactly at `tick`: generated
    choices, cut at `tick` when longer, plus a free flight or run filling the remaining inputs, shortest first."""
    from parkour_solver.movement.generation import generate_choices
    from parkour_solver.movement.normal import choice_approaches, event_choice
    from parkour_solver.movement.program import (append_choice, boundary_state, empty_program, program_search_key,
                                                 truncate_program)
    from parkour_solver.movement.settings import movement_settings
    cfg = movement_settings(scene)
    root = empty_program(scene['initial'], threshold=0.)
    root['terminalSupport'] = scene['startBlock']
    found, seen, pending = [], set(), [root]
    while pending and len(found) < limit:
        program = pending.pop(0)
        remaining = tick-program['profile']['duration']
        context = dict(mode='open', scene=scene, program=program, state=boundary_state(program),
                       maxInputs=cfg['maxInputs']-program['profile']['duration'], maxEntryInputs=cfg['maxEntryInputs'],
                       threshold=0., interactionGate=None)
        choices = [c for kind in cfg['types'] for c in generate_choices(kind, context)]
        choices += [c for initial, jumping, _ in choice_approaches(context)
                    for c in [event_choice(context, 'normal', initial, remaining, jumping=jumping)] if c]
        for choice in choices:
            child = append_choice(program, choice, max_inputs=cfg['maxInputs'])
            if child is None:
                continue
            if child['profile']['duration'] > tick:
                child = truncate_program(child, tick)
            key = program_search_key(child)
            if key in seen:
                continue
            seen.add(key)
            if child['profile']['duration'] == tick:
                found.append(child)
            else:
                pending.append(child)
        pending.sort(key=lambda p: p['profile']['duration'])
    return found[:limit]
