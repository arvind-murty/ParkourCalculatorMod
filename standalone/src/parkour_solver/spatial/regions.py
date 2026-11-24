"""Finite free-volume and physical support states for spatial routing.

The graph describes collision geometry, not dynamically feasible jumps. Open
links never reset flight. Contact links require an actual physical interaction;
occupancy of an incident volume alone cannot satisfy them.
"""

import hashlib
import json
from dataclasses import asdict, dataclass, field

from parkour_solver.scene.geometry import thin
from parkour_solver.spatial.cells import Cell, cell_portals, contains, dimension, intersection


SCHEMA = 1


def geometry_key(value):
    # Exact JSON doubles, deliberately no coordinate rounding or raw cell IDs.
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                    allow_nan=False).encode()).hexdigest()


def canonical_cover(boxes):
    """Canonical closed planar X/Z union of the rectangles that are not thin.

    Sweep X strips, union their Z intervals, merge equal consecutive coverage.
    This removes encoding seams without filling holes or concavities. Lines,
    points and slivers narrower than the minimum free width are artifacts.
    """
    boxes = sorted(set(tuple(b) for b in boxes))
    if not boxes:
        return ()
    height = boxes[0][1]
    if any(b[1] != height or b[4] != height for b in boxes):
        raise ValueError('A support cover must lie on one horizontal plane')
    areas = [b for b in boxes if b[0] < b[3] and b[2] < b[5]]
    edges = sorted({b[a] for b in areas for a in (0, 3)})
    strips = []
    for x, xx in zip(edges, edges[1:]):
        intervals = sorted((b[2], b[5]) for b in areas if b[0] <= x and xx <= b[3])
        merged = []
        for lo, hi in intervals:
            if merged and lo <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(hi, merged[-1][1]))
            else:
                merged.append((lo, hi))
        coverage = tuple(merged)
        if strips and strips[-1][1] == x and strips[-1][2] == coverage:
            strips[-1] = (strips[-1][0], xx, coverage)
        elif coverage:
            strips.append((x, xx, coverage))
    return tuple(sorted({(x, height, z, xx, height, zz) for x, xx, intervals in strips for z, zz in intervals
                         if not thin(xx-x) and not thin(zz-z)}))


@dataclass(frozen=True)
class SupportComponent:
    key: str
    owner: str
    height: float
    cover: tuple
    cells: tuple
    solids: tuple
    patches: tuple


def _native():
    """The native core under PKC_NATIVE_CORE, which builds the geometry graph for every caller; None otherwise."""
    from parkour_solver import native_core
    return native_core.from_environment()


def _rows(decomposition):
    """A Decomposition in the native core's row shape."""
    d = decomposition
    return dict(bounds=list(d.bounds), cells=[[list(c.bounds), c.dimension] for c in d.cells],
                portals=[[list(p.cells), list(p.bounds), p.dimension] for p in d.portals],
                contacts=[[p.cell, p.solid, p.owner, p.axis, p.side, p.kind, list(p.bounds)] for p in d.contacts])


def support_components(decomposition):
    native = _native()
    if native is not None:
        return [SupportComponent(key, owner, height, tuple(tuple(b) for b in cover), tuple(cells), tuple(solids),
                                 tuple(patches))
                for key, owner, height, cover, cells, solids, patches in native.support_components(_rows(decomposition))]
    grouped = {}
    for index, patch in enumerate(decomposition.contacts):
        if patch.kind == 'support':
            grouped.setdefault((patch.owner, patch.bounds[1]), []).append((index, patch))
    components = []
    for (owner, height), patches in sorted(grouped.items()):
        cover = canonical_cover(p.bounds for _, p in patches)
        pending = set(range(len(cover)))
        while pending:
            connected = {min(pending)}
            queue = list(connected)
            pending -= connected
            while queue:
                current = queue.pop()
                adjacent = {i for i in pending if intersection(cover[current], cover[i]) is not None}
                connected |= adjacent
                pending -= adjacent
                queue.extend(sorted(adjacent))
            pieces = tuple(cover[i] for i in sorted(connected))
            incident = [(i, p) for i, p in patches if any(intersection(p.bounds, b) is not None for b in pieces)]
            key = 'support:' + geometry_key((owner, height, pieces))
            components.append(SupportComponent(key, owner, height, pieces,
                tuple(sorted({p.cell for _, p in incident})), tuple(sorted({p.solid for _, p in incident})),
                tuple(i for i, _ in incident)))
    return sorted(components, key=lambda c: c.key)


@dataclass(frozen=True)
class RouteRegion:
    key: str
    kind: str
    cover: tuple
    cells: tuple
    owners: tuple = ()
    solids: tuple = ()
    predicate: object = None
    transit_key: str = ''


@dataclass(frozen=True)
class RouteTransition:
    key: str
    source: str
    target: str
    kind: str
    cover: tuple
    portals: tuple = ()
    patches: tuple = ()
    movement_families: tuple = ()
    capability: str = 'not_ruled_out'
    evidence: str = 'geometry_only'


@dataclass
class RouteGraph:
    schema: int
    context: str
    regions: list
    transitions: list
    supports: list
    starts: tuple
    goals: tuple
    transfers: dict = field(default_factory=dict)
    flight_proposals: dict = field(default_factory=dict)

    def to_dict(self):
        return asdict(self)

def build_geometry_graph(scene, decomposition, supports=None, task_goal=None, *, deadline=None):
    """Exact raw-cell incidence without mandatory overlapping-box expansion.

    This is a covering substrate, not a selected route. Every raw cell/portal
    survives, including boundary strata. Contact attachments use the same
    positive-overlap convention as the legacy graph.
    """
    native = _native()
    if native is not None:
        supports = support_components(decomposition) if supports is None else supports
        built = native.build_geometry_graph(scene, _rows(decomposition), [
            [s.key, s.owner, s.height, [list(b) for b in s.cover], list(s.cells), list(s.solids), list(s.patches)]
            for s in supports], task_goal)
        return RouteGraph(SCHEMA, built['context'],
            [RouteRegion(key, kind, tuple(tuple(b) for b in cover), tuple(cells), tuple(owners), tuple(solids),
                         transit_key=transit) for key, kind, cover, cells, owners, solids, transit in built['regions']],
            [RouteTransition(key, source, target, kind, tuple(tuple(b) for b in cover), tuple(portals), tuple(patches),
                             tuple(families))
             for key, source, target, kind, cover, portals, patches, families in built['transitions']],
            supports, tuple(built['starts']), tuple(built['goals']))
    return _incidence_graph(scene, decomposition,
        support_components(decomposition) if supports is None else supports,
        task_goal, [c.bounds for c in decomposition.cells], deadline=deadline, raw=True)


def _incidence_graph(scene, decomposition, supports, task_goal, transit, *, deadline=None, raw=False):
    regions=[RouteRegion('air:'+geometry_key(box),'air_volume',(box,),
        tuple(c.id for c in decomposition.cells if intersection(c.bounds,box) is not None),
        transit_key='transit:'+geometry_key(box)) for box in transit]
    air_regions={r.key:r for r in regions}
    regions += [RouteRegion(s.key, 'support_surface', s.cover, s.cells, (s.owner,), s.solids) for s in supports]
    transitions = []

    def link(source, target, kind, cover, portals=(), patches=()):
        key = 'transition:' + geometry_key((source, target, kind, cover))
        families = () if kind == 'open_portal' else ('normal', 'stepping', 'ceiling')
        transitions.append(RouteTransition(key, source, target, kind, cover, portals, patches, families))

    transit_cells=[Cell(i,b,dimension(b)) for i,b in enumerate(transit)]
    for p in (decomposition.portals if raw else cell_portals(transit_cells,deadline=deadline)):
        a,b=(regions[i].key for i in p.cells)
        # Both transit regions are free convex cuboids. Their exact shared
        # patch is connected and free; the raw subdivisions only encode it.
        cover=(p.bounds,)
        portals=tuple(raw.id for raw in decomposition.portals if intersection(raw.bounds,p.bounds) is not None)
        link(a,b,'open_portal',cover,tuple(portals))
        link(b,a,'open_portal',cover,tuple(portals))
    for support in supports:
        from parkour_solver.scene.geometry import expanded_box
        for region,volume in sorted(air_regions.items()):
            incident=[]
            for i in support.patches:
                patch=decomposition.contacts[i]
                overlap=intersection(volume.cover[0],patch.bounds)
                if overlap is None:
                    continue
                box=expanded_box(scene['collisionScene']['solids'][patch.solid])
                if all(overlap[a+3]>box[a] and overlap[a]<box[a+3] for a in (0,2)):
                    incident.append((i,overlap))
            if not incident:
                continue
            patches=tuple(i for i,_ in incident)
            cover = canonical_cover(b for _,b in incident)
            link(region, support.key, 'support_arrival', cover, patches=patches)
            link(support.key, region, 'support_departure', cover, patches=patches)

    start = scene['initial']['position']
    start_owner = next(b.get('sourceBlock', b['id']) for b in scene['blocks'] if b['id'] == scene['startBlock'])
    starts = tuple(s.key for s in supports if scene['initial'].get('onGround', True) and
                   s.owner == start_owner and any(contains(b, start) for b in s.cover))
    # Do not fabricate grounded contact if the actual initial state is airborne.
    if not starts:
        starts = tuple(sorted(r.key for r in air_regions.values() if any(contains(b,start) for b in r.cover)))
    if task_goal is not None and task_goal.get('kind') != 'landing':
        raise ValueError('Route graph requires a physical landing task goal')
    goal_id = scene['goalBlock'] if task_goal is None else task_goal.get('block', scene['goalBlock'])
    goal_block = next(b for b in scene['blocks'] if b['id'] == goal_id)
    goal_owner = goal_block.get('sourceBlock', goal_id)
    goals = tuple(s.key for s in supports if s.owner == goal_owner and s.height == goal_block['top']
                  and goal_block['solidIndex'] in s.solids)
    context = geometry_key(dict(schema=SCHEMA, bounds=decomposition.bounds,
        regions=[asdict(r) for r in regions], transitions=[asdict(t) for t in transitions],
        starts=starts, goals=goals, taskGoal=task_goal))
    return RouteGraph(SCHEMA, context, regions, transitions, supports, starts, goals)

