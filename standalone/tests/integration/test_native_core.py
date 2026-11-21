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


if __name__ == '__main__':
    unittest.main()
