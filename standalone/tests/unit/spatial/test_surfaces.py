"""Scene surfaces: obstacle clusters against their maximal free rectangles, and merged contact options."""
import copy
from dataclasses import FrozenInstanceError
from fractions import Fraction
import itertools
import json
import math
import random
import unittest

from parkour_solver.scene.geometry import BODY_HEIGHT, expanded_box
from parkour_solver.spatial.cells import decompose
from parkour_solver.spatial.regions import canonical_cover, geometry_key
from parkour_solver.spatial.surfaces import TANGENT_AXES, free_rectangles, nested, scene_geometry
from tests.support.cell_geometry import scene_for


B5 = [730, 0, -1096, 731, 6, -1095]
B6 = [730, 0, -1095, 731, 20, -1094]
B7 = [729, 0, -1095, 730, 7, -1093]


def sides(geometry, cluster, y):
    return {s.bounds: tuple(sorted(set().union(*s.owners))) for s in geometry.sides(cluster, geometry.active(cluster, y, y))}


def interior(box, x, z):
    return box[0] < x < box[3] and box[2] < z < box[5]


def within(region, x, z):
    return region[0] <= x <= region[2] and region[1] <= z <= region[3]


class ClusterTests(unittest.TestCase):
    def test_real11_flush_blocks_form_one_cluster_with_shared_sides(self):
        geometry = scene_geometry(dict(solids=[B5, B6, B7]))
        cluster, = geometry.clusters([0, 1, 2])
        self.assertEqual(cluster.members, (0, 1, 2))
        b6, b7 = expanded_box(B6), expanded_box(B7)
        self.assertEqual(sides(geometry, cluster, 6.5), {
            (('Z', -1, b6[2]),): (1, 2),
            (('X', 1, b6[3]),): (1,),
            (('X', -1, b7[0]),): (2,),
            (('Z', 1, b7[5]),): (2,),
            (('X', 1, b7[3]), ('Z', 1, b6[5])): (1, 2)})
        self.assertEqual(sides(geometry, cluster, 7.5), {
            (('Z', -1, b6[2]),): (1,),
            (('X', 1, b6[3]),): (1,),
            (('X', -1, b6[0]),): (1,),
            (('Z', 1, b6[5]),): (1,)})
        self.assertEqual(geometry.active(cluster, 7., 7.2), (1,))
        self.assertEqual(geometry.active(cluster, 5.5, 6.), (0, 1, 2))

    def test_lower_and_upper_sides_on_one_side_are_nested(self):
        geometry = scene_geometry(dict(solids=[B6, B7]))
        cluster, = geometry.clusters([0, 1])
        low = {s.bounds: s for s in geometry.sides(cluster, (0, 1))}
        high = {s.bounds: s for s in geometry.sides(cluster, (0,))}
        west_low = low[(('X', -1, expanded_box(B7)[0]),)]
        west_high = high[(('X', -1, expanded_box(B6)[0]),)]
        self.assertTrue(nested(west_low.region, west_high.region))
        for side in low.values():
            self.assertTrue(any(nested(side.region, other.region) for other in high.values()))

    def test_ring_encloses_one_bounded_pocket(self):
        ring = [(0, 0, 0, 3, 1, 1), (0, 0, 2, 3, 1, 3), (0, 0, 0, 1, 1, 3), (2, 0, 0, 3, 1, 3)]
        self.assertIn((1, 1, 2, 2), free_rectangles(ring))
        self.assertEqual(sum(all(math.isfinite(v) for v in r) for r in free_rectangles(ring)), 1)

    def test_blocks_a_real_gap_apart_are_separate_clusters(self):
        geometry = scene_geometry(dict(solids=[[0, 0, 0, 1, 2, 1], [1.612, 0, 0, 2.612, 2, 1]]))
        self.assertEqual([c.members for c in geometry.clusters([0, 1])], [(0,), (1,)])
        geometry = scene_geometry(dict(solids=[[0, 0, 0, 1, 2, 1], [1.5, 0, 0, 2.5, 2, 1]]))
        self.assertEqual([c.members for c in geometry.clusters([0, 1])], [(0, 1)])

    def test_out_of_scope_solids_are_ignored(self):
        geometry = scene_geometry(dict(solids=[B5, B6, B7]))
        self.assertEqual([c.members for c in geometry.clusters([1])], [(1,)])

    def test_random_unions_are_covered_exactly_by_maximal_sides(self):
        rng = random.Random(7)
        for _ in range(60):
            boxes = []
            for _ in range(rng.randint(1, 5)):
                x, z = rng.randint(0, 6)/2, rng.randint(0, 6)/2
                boxes.append((x, 0, z, x+rng.randint(1, 4)/2, 1, z+rng.randint(1, 4)/2))
            rects = free_rectangles(boxes)
            for _ in range(300):
                x, z = rng.uniform(-1, 6), rng.uniform(-1, 6)
                free = not any(interior(b, x, z) for b in boxes)
                self.assertEqual(any(within(r, x, z) for r in rects), free, (boxes, x, z))
            for r in rects:
                self.assertFalse(any(b[0] < r[2] and r[0] < b[3] and b[2] < r[3] and r[1] < b[5] for b in boxes))
                for i, step in ((0, -1e-6), (1, -1e-6), (2, 1e-6), (3, 1e-6)):
                    if math.isinf(r[i]):
                        continue
                    grown = list(r)
                    grown[i] += step
                    self.assertTrue(any(b[0] < grown[2] and grown[0] < b[3] and b[2] < grown[3] and grown[1] < b[5]
                                        for b in boxes), (boxes, r, i))

    def test_clusters_match_the_pairwise_definition_with_near_gaps(self):
        rng = random.Random(13)
        for _ in range(40):
            solids = []
            for _ in range(rng.randint(2, 25)):
                x, z = 2.6*rng.randint(-4, 4)+rng.choice([0, 0, .005, .007]), 2.6*rng.randint(-4, 4)
                solids.append([x, rng.randint(0, 2), z, x+2, rng.randint(3, 4), z+2])
            geometry = scene_geometry(dict(solids=solids))
            indices = sorted(rng.sample(range(len(solids)), rng.randint(1, len(solids))))
            boxes = geometry.boxes
            parent = {i: i for i in indices}
            def root(i):
                while parent[i] != i:
                    i = parent[i]
                return i
            for a, b in itertools.combinations(indices, 2):
                if all(max(boxes[a][k], boxes[b][k])-min(boxes[a][k+3], boxes[b][k+3]) < .006 for k in range(3)):
                    parent[root(b)] = root(a)
            groups = {}
            for i in indices:
                groups.setdefault(root(i), []).append(i)
            self.assertEqual([c.members for c in geometry.clusters(indices)],
                             [tuple(m) for m in sorted(groups.values())], solids)

    def test_near_matches_a_scan_on_cell_borders_and_long_boxes(self):
        rng = random.Random(11)
        for _ in range(40):
            solids = []
            for _ in range(rng.randint(1, 30)):
                x, z = rng.randint(-20, 20), rng.randint(-20, 20)
                solids.append([x, rng.randint(0, 5), z, x+rng.choice([1, 1, 3, 13]), 6, z+rng.choice([1, 1, 3, 13])])
            geometry = scene_geometry(dict(solids=solids))
            for _ in range(50):
                x0 = rng.choice([rng.uniform(-30, 30), 4.*rng.randint(-8, 8), rng.randint(-25, 25)-.3])
                z0 = rng.choice([rng.uniform(-30, 30), 4.*rng.randint(-8, 8), rng.randint(-25, 25)-.3])
                rect = [x0, z0, x0+rng.choice([0., 4., rng.uniform(0, 20)]), z0+rng.choice([0., 4., rng.uniform(0, 20)])]
                expected = [i for i, b in enumerate(geometry.boxes)
                            if b[0] <= rect[2] and rect[0] <= b[3] and b[2] <= rect[3] and rect[1] <= b[5]]
                self.assertEqual(geometry.near(rect), expected, (solids, rect))



def planar(rects):
    return canonical_cover((r[0], 0, r[1], r[2], 0, r[3]) for r in rects)

