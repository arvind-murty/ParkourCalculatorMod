"""Shared test builders extracted from tests.unit.movement.test_stepping_cases."""
from tests.support.paths import AUTONOMY_DATA

import json

FIXTURES = AUTONOMY_DATA / 'fixtures/stepping'

def fixture(name):
    return json.loads((FIXTURES / (name + '.json')).read_text())


def _context(scene, program, gate, **fields):
    from parkour_solver.movement.program import boundary_state
    return dict(mode='open', scene=scene, program=program, state=boundary_state(program), maxInputs=40,
                maxEntryInputs=40, threshold=0., interactionGate=gate, **fields)


def case_programs(data):
    """(scene, [(case, program)]) for a stepping fixture: each open-generator choice of the fixture's case from the start
    state (`case`), and the complete program landing on the goal, with one normal choice after the case when the case
    itself does not end grounded on the goal. Generation and the complete programs are screened by the production
    interaction gate, so a side the start state cannot reach gives no program."""
    from parkour_solver.bridge.python_compiler import PythonCompiler
    from parkour_solver.movement.generation import generate_choices
    from parkour_solver.movement.program import append_choice, empty_program
    from parkour_solver.reachability.service import InteractionGate
    from parkour_solver.scene.normalize import prepare_scene
    scene = prepare_scene(data['request'])
    with PythonCompiler() as compiler:
        return scene, _case_programs(scene, data['case'], InteractionGate(scene, compiler))


def _case_programs(scene, kind, gate):
    from parkour_solver.movement.generation import generate_choices
    from parkour_solver.movement.program import append_choice, empty_program
    root = empty_program(scene['initial'], threshold=0.)
    root['terminalSupport'] = scene['startBlock']
    # As in search, a complete program also passes the proposal screen (gate.check_program).
    landed = lambda p: (p is not None and p['profile']['grounded'][-1] and p.get('terminalSupport') == scene['goalBlock']
                        and gate.check_program(p) is None)
    result = []
    for choice in generate_choices('ceiling' if kind == 'grind' else 'stepping', _context(scene, root, gate, cases=[kind])):
        case = append_choice(root, choice, max_inputs=60)
        if case is None or not any(e.get('case') == kind for e in case['profile'].get('caseEvents', [])):
            continue
        if landed(case):
            result.append((case, case))
            continue
        result.extend((case, p) for p in (append_choice(case, c, max_inputs=60)
                                          for c in generate_choices('normal', _context(scene, case, gate))) if landed(p))
    return result


def _hit(program, case):
    return next(e['anchors']['hit'] for e in program['profile']['caseEvents'] if e['case'] == case)


def _corner(case, x_cmp, z_cmp):
    """The corner whose outside rows approach along X with `x_cmp` and escape along Z with `z_cmp`."""
    def wanted(_, program):
        rows = {r['mode']: r['cmp'] for r in program['profile']['positionConstraints'] if r['name'] == 'case:outside'}
        return rows == {'X': x_cmp, 'Z': z_cmp}
    return wanted


# Which generated variant each fixture names: the blip that approaches a wall, a blip-up or corner blip-up that walks on
# from the higher top or jumps from it, and the corner each corner fixture recorded (approach from -X; escape -Z for the
# corner blip-up, +Z for the corner jump cancel).
VARIANTS = {
    'blip-wall': lambda case, p: any(e['axis'] != 'Y' for e in p['profile']['contactEvents']),
    'blip': lambda case, p: not any(e['axis'] != 'Y' for e in p['profile']['contactEvents']),
    'blip_up-jump': lambda case, p: p['profile']['jump'][_hit(p, 'blip_up')],
    'blip_up': lambda case, p: not p['profile']['jump'][_hit(p, 'blip_up')],
    'corner_blip_up': lambda case, p: (not p['profile']['jump'][_hit(p, 'corner_blip_up')]
                                       and _corner('corner_blip_up', 'LE', 'LE')(case, p)),
    'corner_jump_cancel': _corner('corner_jump_cancel', 'LE', 'GE'),
}


def fixture_program(name):
    """(scene, case, program) for a stepping fixture's named variant; the fixture's recorded tick count wins a tie."""
    data = fixture(name)
    scene, found = case_programs(data)
    wanted = [(c, p) for c, p in found if VARIANTS.get(name, lambda c, p: True)(c, p)]
    case, program = min(wanted, key=lambda item: item[1]['profile']['duration'] != data['expectedTicks'])
    return scene, case, program
