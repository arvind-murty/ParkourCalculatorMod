import hashlib
import json
import math
from pathlib import Path
import random
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
            program = append_choice(program, choice)
            self.assert_round_trips(self.native.round_trip_program, program)
            cut = program['profile']['airtime']
        self.assertTrue(any(u.get('effects') for u in program['movementUnits']))

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
                 (('profile', 'airtime'), 1., 'program.profile.airtime: expected int'),
                 (('initial', 'position'), '64', 'program.initial.position: expected number'),
                 (('profile', 'velocity'), [2**60], r'program.profile.velocity\[0\]: int outside')]
        for (record, key), value, message in cases:
            with self.subTest(key=key):
                broken = json.loads(json.dumps(program))
                broken[record][key] = value
                with self.assertRaisesRegex(ValueError, message):
                    self.native.round_trip_program(broken)


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
                for node in nodes(scene, roots, gate):
                    if not node['profile']['airtime']:
                        continue
                    expected = gate.prepare(node)
                    self.assertEqual(canonical(self.native.gate_prepare(scene, node, False)), canonical(expected))
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
        empty = dict(prefix, profile=dict(prefix['profile'], airtime=0))
        with self.assertRaisesRegex(ValueError, 'An empty prefix is not a complete movement route'):
            self.native.gate_prepare(scene, empty, False)


if __name__ == '__main__':
    unittest.main()
