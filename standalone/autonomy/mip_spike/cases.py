import copy
import hashlib
import json
from pathlib import Path
import time

from standalone.autonomy.block_contacts import validate_vertical_schedule
from standalone.autonomy.block_search import prepare_scene
from standalone.autonomy.movement_composition import append_choice, boundary_state, compile_program, empty_program
from standalone.autonomy.movement_generation import generate_choices
from standalone.autonomy.route import initial_guess, prepare_route, schedule
from standalone.ipopt import prepare
from standalone.ipopt.ladder import REST_CARRY
from .geometry import disjunctions


ROOT = Path(__file__).parents[1]
CASES = ('cross-neo', 'wide-neo', 'interleaved', 'wall-reset', 'wall-ceiling',
         'ladder-continuous', 'ladder-end-stop', 'repeat-support')


def build_case(name, compiler):
    if name not in CASES:
        raise ValueError('Unknown spike case: ' + name)
    started = time.monotonic()
    fixed_events = name.startswith('ladder-') or name in ('wall-reset', 'wall-ceiling')
    if name.startswith('ladder-'):
        filename = 'continuous' if name == 'ladder-continuous' else 'end-stop'
        path = ROOT / 'experiments/ladder-climbing-ceiling' / (filename + '.json')
        evidence = json.loads(path.read_text())
        scene = prepare_scene(json.loads((ROOT / evidence['fixture']).read_text()))
        route, plan, profiles = [copy.deepcopy(evidence[key]) for key in ('route', 'plan', 'profiles')]
    elif name in ('wall-reset', 'wall-ceiling'):
        filename = 'wall-result' if name == 'wall-reset' else 'wall-ceiling-result'
        path = ROOT / 'experiments/one-tick-contacts' / (filename + '.json')
        evidence = json.loads(path.read_text())['best']
        fixture = 'wall-reversal' if name == 'wall-reset' else 'wall-ceiling-reversal'
        scene = prepare_scene(json.loads((ROOT / 'fixtures' / (fixture + '.json')).read_text()))
        route, plan, profiles = [copy.deepcopy(evidence[key]) for key in ('route', 'plan', 'profiles')]
    else:
        if name == 'repeat-support':
            request = dict(version=1, sceneModel='collision_boxes', geometryBounds=[-15, -5, -15, 15, 10, 15],
                initial=dict(position=[0, 1, 0], velocity=[0, REST_CARRY, 0], yaw=0),
                startBlock='start', goalBlock='goal', maxTotalRunTicks=0, maxRunTicksPerJump=0,
                blocks=[dict(id='start', boxes=[[-2, 0, -2, 2, 1, 2]]),
                        dict(id='goal', boxes=[[4, 0, -.5, 5, 1, .5]])])
            path = None
        else:
            filename = 'obstacle-' + name
            path = ROOT / 'fixtures' / (filename + '.json')
            request = json.loads(path.read_text())
        scene = prepare_scene(request)
        program = empty_program(scene['initial'])
        for support in (['start', 'goal'] if name == 'repeat-support' else ['goal']):
            context = dict(mode='open', scene=scene, program=program, state=boundary_state(program), maxInputs=60)
            choice = next(c for c in generate_choices('normal', context)
                          if c.get('terminalSupport') == support and c['profile']['jump'][0])
            program = append_choice(program, choice)
            if program is None:
                raise ValueError('Normal choice failed original-state composition')
        compiled = compile_program(scene, program)
        route = prepare_route(compiled['route'])
        profiles = compiled['profiles']
        plan = schedule(route, [0])
    route['sharedMovement'] = True
    route['maxTotalRunTicks'] = 0
    route['runBounds'] = [[0, 0] for _ in profiles]
    vertical = validate_vertical_schedule(route, profiles, plan)
    if not vertical['feasible']:
        raise ValueError('The selected schedule has an invalid scalar vertical history')
    plan['vertical'] = dict(vertical['vertical'], referencePosition=vertical['y'], referenceVelocity=vertical['vy'])
    plan['movementComposition'] = True
    coefficients = compiler.compile(route, plan, 5)
    problem = prepare(dict(coefficients, version=1, ticks=plan['ticks'],
        position=route['initial']['position'][::2], velocity=route['initial']['velocity'][::2],
        yaw=initial_guess(route, plan), threshold=.005, walls=plan['walls'], contacts=plan.get('contacts', []),
        objective=dict(route['objective'], tick=plan['ticks']), ladderTicks=plan.get('ladderTicks', [False] * (plan['ticks'] + 1))))
    return dict(name=name, scene=scene, route=route, profiles=profiles, plan=plan, problem=problem,
        vertical=vertical, disjunctions=disjunctions(scene['collisionScene'], vertical['y'],
            contacts=plan.get('sceneContacts', []), initial=problem['position']),
        preparationSeconds=time.monotonic() - started, source=str(path.relative_to(ROOT)) if path else 'authored repeat-support scene',
        sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest() if path else None,
        fixedHorizon=True, fixedVerticalSchedule=True, prescribedContactHistory=fixed_events,
        initialYawUsesSavedWitness=False, routeDiscoveryBenchmarked=False)
