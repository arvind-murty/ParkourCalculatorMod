import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import struct
import unittest

from parkour_solver.canonical import digest

ROOT = Path(__file__).resolve().parents[2]


def native():
    from tools.native_cpp import load
    try:
        return load()
    except ImportError as error:
        raise unittest.SkipTest(str(error))


def canonical(value):
    return json.dumps(value, sort_keys=True, allow_nan=False).encode()


class CanonicalIdentityTests(unittest.TestCase):
    """The native encoder must reproduce parkour_solver.canonical byte for byte: search order ties break on it."""

    @classmethod
    def setUpClass(cls):
        cls.native = native()

    def test_edge_values_match_python_bytes(self):
        values = [None, True, False, 0, -1, 2**63-1, -2**63, 0.0, -0.0, 1.0, 5e-324, 1e16, 1e15, 0.0001, 1e-05,
                  123456.0, 0.1, 1/3, 2.0**63, -1093.2381026602418, '', 'a"b\\c', '\n\r\t\b\f\x00\x1f\x7f', 'é',
                  '😀', [], {}, (1, 2.0), {'b': [1, 1.0], 'a': {'é': -0.0, 'A': None}, '': {}}]
        for value in values:
            self.assertEqual(self.native.canonical_json(value), canonical(value), repr(value))
            self.assertEqual(self.native.digest(value), digest(value), repr(value))

    def test_seeded_float_and_string_fuzz(self):
        rng = random.Random(20261120)
        for _ in range(20000):
            x = struct.unpack('d', struct.pack('Q', rng.getrandbits(64)))[0]
            if math.isfinite(x):
                self.assertEqual(self.native.float_repr(x), repr(x))
        for _ in range(2000):
            text = ''.join(chr(rng.choice([rng.randrange(0, 0x80), rng.randrange(0x80, 0xd800),
                                           rng.randrange(0xe000, 0x110000)])) for _ in range(rng.randrange(12)))
            self.assertEqual(self.native.canonical_json(text), canonical(text), repr(text))
        for _ in range(2000):
            data = rng.randbytes(rng.randrange(300))
            self.assertEqual(self.native.sha256_hex(data), hashlib.sha256(data).hexdigest())

    def test_every_value_in_the_real11_requests_matches(self):
        def walk(value):
            yield value
            children = value.values() if isinstance(value, dict) else value if isinstance(value, list) else ()
            for child in children:
                yield from walk(child)
        paths = sorted((ROOT / 'cases/real11').glob('*.json'))
        self.assertTrue(paths)
        for path in paths:
            for value in walk(json.loads(path.read_text())):
                self.assertEqual(self.native.digest(value), digest(value), path.name)

    def test_rejects_what_json_rejects_or_identity_never_uses(self):
        for value in (float('nan'), [float('inf')], {'a': -float('inf')}):
            with self.assertRaises(ValueError):
                digest(value)
            with self.assertRaises(ValueError):
                self.native.digest(value)
        for value in ({1: 2}, 2**64, object(), {'a': {1, 2}}):
            with self.assertRaises(TypeError):
                self.native.digest(value)


class ProgramModelTests(unittest.TestCase):
    """Typed native programs must hold everything Python's program dicts hold, and reject what they don't know."""

    @classmethod
    def setUpClass(cls):
        cls.native = native()

    def assert_round_trips(self, convert, value):
        # Opaque search data may hold tuples, so the JSON form is the contract, as for identity.
        self.assertEqual(canonical(convert(value)), canonical(value))

    def test_real11_programs_and_choices_round_trip(self):
        from tests.support.block_table import compiler, generated, nodes, real11_inputs
        from parkour_solver.reachability.service import InteractionGate
        programs = choices = 0
        with compiler() as java:
            for scene, roots in real11_inputs():
                gate = InteractionGate(scene, java, seconds=10)
                for node in nodes(scene, roots, gate):
                    self.assert_round_trips(self.native.round_trip_program, node)
                    programs += 1
                    for choice in (c for kind in generated(scene, node, gate, None) for c in kind):
                        self.assert_round_trips(self.native.round_trip_choice, choice)
                        choices += 1
        self.assertTrue(programs and choices)

    def test_ladder_programs_with_ceiling_effects_round_trip(self):
        from tests.support.archived_events import current_events
        from tests.support.paths import AUTONOMY_DATA
        from parkour_solver.movement.ladder.generation import ladder_event_choice
        from parkour_solver.movement.program import append_choice, empty_program
        evidence = current_events(json.loads(
            (AUTONOMY_DATA/'experiments/contact-combinations/appended-ladder-ceiling.json').read_text()))
        program, cut = empty_program(evidence['route']['initial']), 0
        for event in evidence['route']['ladderEvents']:
            event = dict(event, schedule=dict(event['schedule'], contactEvents=[
                c for c in evidence['route']['contactEvents'] if cut <= c['offset'] < event['schedule']['exitState']]))
            choice = ladder_event_choice(event, start=cut)
            self.assert_round_trips(self.native.round_trip_choice, choice)
            child = self.native.program(program).append_choice(choice)
            program = append_choice(program, choice)
            self.assertEqual(canonical(child.to_python()), canonical(program))
            self.assert_round_trips(self.native.round_trip_program, program)
            cut = program['profile']['duration']
        self.assertTrue(any(u.get('effects') for u in program['movementUnits']))

    def test_real11_appends_build_like_python(self):
        from tests.support.block_table import compiler, generated, nodes, real11_inputs
        from parkour_solver.movement.program import append_choice, program_physics_key
        from parkour_solver.reachability.service import InteractionGate
        built = rejected = 0
        with compiler() as java:
            for scene, roots in real11_inputs():
                gate = InteractionGate(scene, java, seconds=10)
                gate.native = None
                for node in nodes(scene, roots, gate):
                    parent = self.native.program(node)
                    for choice in (c for kind in generated(scene, node, gate, None) for c in kind):
                        expected, child = append_choice(node, choice), parent.append_choice(choice)
                        if expected is None:
                            self.assertIsNone(child)
                            rejected += 1
                            continue
                        self.assertEqual(canonical(child.to_python()), canonical(expected))
                        self.assertEqual(child.physics_key(), program_physics_key(expected))
                        self.assertEqual(child.parent_ticks, node['profile']['duration'])
                        built += 1
        self.assertTrue(built)

    def test_number_kinds_absent_keys_and_annotations_survive(self):
        from parkour_solver.movement.program import empty_program
        program = empty_program(dict(position=[1, 64, 1], velocity=[0, 0., 0]), threshold=0)
        program.update(decisionNode='n1', comparisonCacheToken=[3, 'x'])
        self.assertEqual(type(program['threshold']), int)
        self.assert_round_trips(self.native.round_trip_program, program)

    def test_rejects_unknown_keys_and_wrong_types_by_path(self):
        from parkour_solver.movement.program import empty_program
        program = empty_program(dict(position=[1, 64, 1], velocity=[0, 0., 0]), threshold=0.)
        cases = [(('profile', 'typo'), 1, 'program.profile: unknown key typo'),
                 (('profile', 'duration'), 1., 'program.profile.duration: expected int'),
                 (('initial', 'position'), '64', 'program.initial.position: expected number'),
                 (('profile', 'velocity'), [2**60], r'program.profile.velocity\[0\]: int outside')]
        for (record, key), value, message in cases:
            with self.subTest(key=key):
                broken = json.loads(json.dumps(program))
                broken[record][key] = value
                with self.assertRaisesRegex(ValueError, message):
                    self.native.round_trip_program(broken)

    def test_published_dfs_states_round_trip(self):
        from parkour_solver.bridge.python_compiler import PythonCompiler
        from parkour_solver.search.domain import InitialJumpDomain
        from parkour_solver.search.engine import find_checkpoint_route
        from tests.support.checkpoint_guidance import flat_scene
        states = []
        def progress(state):
            # The checkpoint's JSON form is the contract: Python-mode states hold tuples in opaque data, and a native
            # search publishes snapshots, converted on request.
            dfs = state['dfs']
            states.append(json.loads(json.dumps(dfs.to_python() if hasattr(dfs, 'to_python') else dfs)))
        progress.begin_repair = lambda state, program, extra_targets=(): progress(state)
        progress.repair_progress = progress
        # The real movement frontier: a stubbed one publishes generations persistent_programs never writes.
        scene = flat_scene()
        scene['movementSearch'].update(maxInputs=28, maxPrefixes=64)
        with PythonCompiler() as compiler:
            find_checkpoint_route(scene, compiler, anchor_search={}, seconds=8, max_solver_calls=20,
                                  program_filter=InitialJumpDomain([0], max_inputs=28),
                                  python_executable=os.environ['PKC_IPOPT_PYTHON'], on_progress=progress)
        self.assertTrue(any('finalization' in s for s in states) and
                        any(f['generation'] for s in states for f in s['frames']))
        for state in states:
            self.assertEqual(canonical(self.native.round_trip_dfs_state(state)), canonical(state))


class GatePreparationTests(unittest.TestCase):
    """Native InteractionGate.prepare must return exactly what the Python gate prepares."""

    @classmethod
    def setUpClass(cls):
        cls.native = native()

    def test_real11_prefixes_prepare_like_the_python_gate(self):
        from tests.support.block_table import compiler, nodes, real11_inputs
        from parkour_solver.reachability.service import InteractionGate
        checked = 0
        with compiler() as java:
            for scene, roots in real11_inputs():
                gate = InteractionGate(scene, java, seconds=10)
                gate.native = None  # the Python oracle, whatever PKC_NATIVE_CORE says
                for node in nodes(scene, roots, gate):
                    if not node['profile']['duration']:
                        continue
                    expected = {k: v for k, v in gate.prepare(node).items() if k != 'key'}
                    self.assertEqual(canonical(self.native.gate_prepare(scene, node, False)), canonical(expected))
                    checked += 1
        self.assertGreater(checked, 0)

    def test_gate_with_the_native_core_screens_like_the_python_gate(self):
        import copy
        from tests.support.block_table import compiler, generated, nodes, real11_inputs
        from parkour_solver.reachability.service import InteractionGate
        same = lambda value: json.dumps(value, sort_keys=True)  # analyses hold infinities
        checked, proofs = 0, set()
        with compiler() as bridge:
            for scene, roots in real11_inputs():
                generator = InteractionGate(scene, bridge, seconds=10)
                generator.native = None
                python, native = InteractionGate(scene, bridge, seconds=10), InteractionGate(scene, bridge, seconds=10)
                python.native, native.native = None, self.native
                for node in nodes(scene, roots, generator):
                    for choice in (c for kind in generated(scene, node, generator, None) for c in kind):
                        self.assertEqual(same(native.sequence(dict(program=node), choice)),
                                         same(python.sequence(dict(program=node), choice)))
                    if not node['profile']['duration']:
                        continue
                    expected = python.required(node)
                    self.assertEqual(same(native.required(node)), same(expected))
                    proofs.add((expected or {}).get('reason'))
                    self.assertEqual(same(native.analyze(node)), same(python.analyze(node)))
                    a, b = copy.deepcopy(node), copy.deepcopy(node)
                    self.assertEqual(same(native.check_program(b)), same(python.check_program(a)))
                    self.assertEqual(same(b), same(a))  # a run-off's face selection lands on the program
                    checked += 1
                self.assertEqual(native.metrics, python.metrics)
        self.assertGreater(checked, 0)
        self.assertIn(None, proofs)

    def test_gate_with_the_native_core_prepares_like_the_python_gate(self):
        from tests.support.block_table import compiler, nodes, real11_inputs
        from parkour_solver.reachability.service import InteractionGate
        checked = 0
        with compiler() as java:
            for scene, roots in real11_inputs():
                python, native = InteractionGate(scene, java, seconds=10), InteractionGate(scene, java, seconds=10)
                python.native, native.native = None, self.native
                for node in nodes(scene, roots, python):
                    if not node['profile']['duration']:
                        continue
                    for complete in (False, True):
                        try:
                            expected = canonical(python.prepare(node, require_complete=complete))
                        except ValueError as error:
                            with self.assertRaisesRegex(ValueError, re.escape(str(error))):
                                native.prepare(node, require_complete=complete)
                            continue
                        self.assertEqual(canonical(native.prepare(node, require_complete=complete)), expected)
                        checked += 1
        self.assertGreater(checked, 0)

    def test_complete_routes_and_python_errors_carry_over(self):
        from tests.support.block_table import real11_inputs
        from parkour_solver.reachability.service import InteractionGate
        scene, roots = real11_inputs()[1]
        prefix = roots[0]
        with self.assertRaisesRegex(ValueError, 'An open prefix is not a completed goal route'):
            InteractionGate(scene, None).prepare(prefix, require_complete=True)
        with self.assertRaisesRegex(ValueError, 'An open prefix is not a completed goal route'):
            self.native.gate_prepare(scene, prefix, True)
        empty = dict(prefix, profile=dict(prefix['profile'], duration=0))
        with self.assertRaisesRegex(ValueError, 'An empty prefix is not a complete movement route'):
            self.native.gate_prepare(scene, empty, False)


if __name__ == '__main__':
    unittest.main()


class CoefficientCompilerTests(unittest.TestCase):
    """Native compile_coefficients must reply exactly like the Python bridge's coefficient compile."""

    @classmethod
    def setUpClass(cls):
        from parkour_solver.bridge.python_compiler import PythonCompiler
        cls.native = native()
        cls.python = PythonCompiler()

    def assert_same_outcome(self, request):
        try:
            expected = canonical(self.python._exchange(request, 30))
        except ValueError as error:
            with self.assertRaises(ValueError) as raised:
                self.native.compile_coefficients(request)
            self.assertEqual(str(raised.exception), str(error))
            return
        self.assertEqual(canonical(self.native.compile_coefficients(request)), expected)

    def test_random_shared_requests_reply_like_python(self):
        from parkour_solver.bridge.compiler import coefficient_request
        from tests.support.coefficient_requests import shared_request
        rng = random.Random(7)
        for trial in range(400):
            n = rng.choice([1, 2, 3, 11, 12, 40, 601])
            inputs = dict(sprint=rng.random() < .7, force45=rng.random() < .5)
            if rng.random() < .8:
                inputs['incomingSprint'] = rng.random() < .5
            route = dict(sharedMovement=True, inputs=inputs)
            with self.subTest(trial=trial, n=n):
                self.assert_same_outcome(coefficient_request(route, shared_request(rng, n)))

    def test_legacy_requests_reply_like_python_around_the_forward_tolerance(self):
        from parkour_solver.physics.vertical import f32
        rng = random.Random(3)
        bits = struct.unpack('<I', struct.pack('<f', f32(.98)))[0]
        forwards = [f32(.98) - 1e-7, f32(.98) + 1e-7, .98, 1, True, 'x', math.nan]
        forwards += [struct.unpack('<f', struct.pack('<I', b))[0] for b in range(bits - 3, bits + 4)]
        for forward in forwards:
            for strafe in (0., -0., 0, False, .1):
                n = rng.choice([1, 5, 30])
                request = dict(grounded=[rng.random() < .5 for _ in range(n)], jump=[rng.random() < .4 for _ in range(n)],
                               inputs=dict(forward=forward, strafe=strafe, sprint=rng.random() < .7,
                                           incomingSprint=rng.random() < .5, force45=rng.random() < .5))
                with self.subTest(forward=forward, strafe=strafe):
                    self.assert_same_outcome(request)

    def test_invalid_requests_raise_the_python_errors(self):
        base = dict(sharedMovement=True, grounded=[True, False], jump=[True, False], inputs=dict(sprint=True),
                    contacts=[], vertical=dict(profile='minecraft-1.8.9', onGround=True, previousHorizontalCollision=False))
        changes = [dict(grounded=[], jump=[]), dict(jump=[True]), dict(grounded=None), dict(inputs=[]),
                   dict(grounded=[1, 0]), dict(inputs=dict(sprint=1)), dict(inputs=dict(incomingSprint=None)),
                   dict(contacts=[dict(axis=0, state=3)]), dict(contacts=[dict(axis=2, state=True)]),
                   dict(contacts=[dict(axis=1.0, state=9)]), dict(contacts=None), dict(sneak=[True]),
                   dict(sneak=[True, 'no']), dict(ladderTicks=[False]), dict(vertical=dict(base['vertical'], onGround=False)),
                   dict(vertical=dict(base['vertical'], profile='other')), dict(vertical=dict(profile='minecraft-1.8.9')),
                   dict(vertical=dict(base['vertical'], onGround=1)), dict(sharedMovement=0)]
        for change in changes:
            with self.subTest(change=change):
                self.assert_same_outcome(dict(base, **change))


class EnvelopeTests(unittest.TestCase):
    """Native reachable_envelopes must equal the Python function's result, apart from its aliased `model`."""

    @classmethod
    def setUpClass(cls):
        cls.native = native()

    def assert_same(self, problem, y, parent_ticks=None):
        from parkour_solver.reachability.intervals import reachable_envelopes
        expected = {k: v for k, v in reachable_envelopes(problem, y, parent_ticks=parent_ticks).items() if k != 'model'}
        actual = self.native.reachable_envelopes(problem, y, parent_ticks)
        self.assertEqual(json.dumps(actual, sort_keys=True), json.dumps(expected, sort_keys=True))
        return expected

    def test_real11_analyses_build_like_python(self):
        from tests.support.block_table import compiler, nodes, real11_inputs
        from parkour_solver.reachability.service import InteractionGate
        reasons = set()
        with compiler() as bridge:
            for scene, roots in real11_inputs():
                gate = InteractionGate(scene, bridge, seconds=10)
                gate.native = None
                for node in nodes(scene, roots, gate):
                    analysis = gate.analyze(node) if node['profile']['duration'] else None
                    if analysis is None:
                        continue
                    model, y, ticks = analysis['model'], analysis['y'], analysis['model']['ticks']
                    with self.subTest(ticks=ticks):
                        for parent_ticks in (None, ticks // 2, ticks - 1):
                            result = self.assert_same(model, y, parent_ticks)
                            reasons.add((result['contradiction'] or {}).get('reason'))
                        self.assert_same(model, None)
                        p, v = model['position'], model['velocity']
                        widened = dict(model, initialIntervals=[[c - .25 for c in p], [c + .25 for c in p],
                                                               [c - .1 for c in v], [c + .1 for c in v]])
                        self.assert_same(widened, y)
                        blocked = dict(model, walls=model['walls'] + [dict(mode='X', cmp='GE', rhs=p[0] + 50., t1=1),
                                                                      dict(mode='X', cmp='LE', rhs=p[0] - 50., t1=1)])
                        reasons.add(self.assert_same(blocked, y)['contradiction']['reason'])
        self.assertIn(None, reasons)
        self.assertIn('interval_reach_contradiction', reasons)
