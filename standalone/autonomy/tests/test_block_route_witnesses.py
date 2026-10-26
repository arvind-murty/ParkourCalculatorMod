import hashlib
import json
from pathlib import Path
import unittest

from standalone.autonomy.route import supports
from standalone.ipopt import prepare, validate
from standalone.ipopt.model import fingerprint


class BlockRouteWitnessTests(unittest.TestCase):
    def test_saved_routes_validate_horizontally_vertically_and_on_actual_supports(self):
        root = Path(__file__).parents[1]
        artifacts = root / 'experiments' / 'block-route-search'
        summaries = json.loads((artifacts / 'summary.json').read_text())
        for label, summary in summaries.items():
            with self.subTest(label=label):
                request = prepare(json.loads((artifacts / (label + '-request.json')).read_text()))
                result = json.loads((artifacts / (label + '-result.json')).read_text())
                vertical = json.loads((artifacts / (label + '-vertical.json')).read_text())
                fixture = root / 'fixtures' / summary['fixture']
                self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(), summary['fixtureSha256'])
                best = result['best']
                self.assertEqual(fingerprint(request), best['problemHash'])
                full = prepare(dict(request, vertical=vertical))
                self.assertEqual(fingerprint(full), summary['withVerticalHash'])
                checked = validate(full, best['candidate']['yaw'])
                self.assertTrue(checked['feasible'], checked)
                self.assertEqual(checked['onGround'], best['plan']['grounded'] + [True])
                self.assertEqual(best['blockIds'][0], result['scene']['startBlock'])
                self.assertEqual(best['blockIds'][-1], result['scene']['goalBlock'])
                self.assertFalse(any(w['mode'] == 'F' for w in request['walls']))
                self.assertTrue(best['route']['inputs']['force45'])
                for state, block, kind in best['plan']['supportChecks']:
                    self.assertTrue(supports(best['route']['blocks'][block], *checked['positions'][state], kind=kind))


if __name__ == '__main__':
    unittest.main()
