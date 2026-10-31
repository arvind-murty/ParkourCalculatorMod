import copy
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).parents[1]
CASE_NAMES = ('flat-3', 'flat-5', 'required-run-3', 'height-3', 'cross-wide-3', 'wide-5')


def fixture(name):
    path = ROOT / 'fixtures' / (name + '.json')
    return json.loads(path.read_text()), dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def scene(blocks, initial, *, runs=4, total=6):
    return dict(version=1, sceneModel='collision_boxes', geometryBounds=[-10, -10, -10, 40, 15, 20],
        initial=copy.deepcopy(initial), blocks=blocks, startBlock=blocks[0]['id'], goalBlock=blocks[-1]['id'],
        maxRunTicksPerJump=runs, maxTotalRunTicks=total, maxContactBranches=64, maxPassageCandidates=128,
        maxBlockagesPerObstacle=3, maxHorizontalBlockagesPerObstacle=2, maxVerticalBlockagesPerObstacle=2)


def slab(name, bounds, top):
    x0, x1, z0, z1 = bounds
    return dict(id=name, boxes=[[x0, top - 1, z0, x1, top, z1]])


def scaling_request(name):
    if name not in CASE_NAMES:
        raise ValueError('Unknown scaling case')
    if name.startswith('flat-'):
        original, provenance = fixture('blocks-flat-two-jumps')
        n = int(name[-1])
        blocks = [slab(b['id'], b['bounds'], b['top']) for b in original['blocks']]
        for leg in range(3, n + 1):
            x = 7 + 4 * (leg - 2)
            blocks.append(slab('support:' + str(leg), [x, x + 1, 0, 1], 1))
        request = dict(scene=scene(blocks, original['initial']), route=[b['id'] for b in blocks])
        note = 'Original two-jump support rectangles extruded into unit-depth collision boxes, then extended at four-block spacing.'
    elif name == 'required-run-3':
        original, provenance = fixture('blocks-required-run')
        by_id = {b['id']: b for b in original['blocks']}
        blocks = [slab(b['id'], b['bounds'], b['top']) for b in (by_id['start'], by_id['goal'])]
        blocks.extend(slab('support:' + str(i), [4 * i, 4 * i + 1, 0, 1], 1) for i in (2, 3))
        request = dict(scene=scene(blocks, original['initial']), route=[b['id'] for b in blocks])
        note = 'Required-run support rectangles extruded into unit-depth collision boxes, with two additional four-block-spaced supports.'
    elif name == 'height-3':
        original, provenance = fixture('blocks-height-route')
        by_id = {b['id']: b for b in original['blocks']}
        blocks = [slab(b['id'], b['bounds'], b['top']) for b in (by_id['start'], by_id['raised'], by_id['goal'])]
        blocks.append(slab('lower-finish', [8, 9, 0, 1], 2.5))
        request = dict(scene=scene(blocks, original['initial'], runs=3, total=4), route=[b['id'] for b in blocks])
        note = 'Existing rising support route extruded into unit-depth collision boxes, then extended by a half-block-lower landing.'
    elif name == 'cross-wide-3':
        original, provenance = fixture('obstacle-cross-neo')
        blocks = copy.deepcopy(original['blocks'])
        blocks.extend([slab('support:2', [6, 7, 1.8, 2.8], 1),
                       dict(id='wall:2', boxes=[[7.8, 0, 1.9, 8, 4, 2.3]]),
                       slab('finish', [10, 11, 2.0375, 2.1625], 1)])
        request = dict(scene=scene(blocks, original['initial'], runs=3, total=4), route=['start', 'goal', 'support:2', 'finish'])
        note = 'Original finite cross-neo scene extended by two landings and a second finite wall; the final support is two pixels wide in Z.'
    else:
        path = Path(__file__).parent / 'fixtures/two-jump-wide-neo.json'
        original = json.loads(path.read_text())
        provenance = dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        blocks = copy.deepcopy(original['scene']['blocks'])
        blocks.extend([slab('support:3', [11.5, 12.5, -.5, .5], 1),
                       slab('support:4', [15.5, 16.5, .5, 1.5], 1),
                       slab('finish', [19.5, 20.5, .5, 1.5], 1)])
        request = dict(scene=scene(blocks, original['scene']['initial']),
                       route=['start', 'middle', 'goal', 'support:3', 'support:4', 'finish'])
        note = 'Existing two-jump joint wide-neo fixture extended by three landings; all original finite boxes are preserved.'
    return request, dict(source=provenance, adaptation=note, syntheticExtension=True,
                         recordedFacingsUsed=False, suppliedAllocation=False, realWorldSceneClaimed=False)


def allocation_count(bounds, maximum):
    counts = {0: 1}
    for low, high in bounds:
        following = {}
        for total, count in counts.items():
            for extra in range(low, min(high, maximum - total) + 1):
                following[total + extra] = following.get(total + extra, 0) + count
        counts = following
    return sum(counts.values())
