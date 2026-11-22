"""Shared test builders extracted from tests.unit.spatial.test_cell_geometry."""
def scene_for(solids, bounds=(-5, -5, -5, 6, 6, 6)):
    return dict(collisionScene=dict(bounds=list(bounds), solids=solids),
                blocks=[dict(id=str(i), sourceBlock=str(i), solidIndex=i) for i in range(len(solids))])


def locate(decomposition, point, tolerance=0):
    """Ids of the decomposition's cells containing the point."""
    from parkour_solver.spatial.cells import contains
    return [c.id for c in decomposition.cells if contains(c.bounds, point, tolerance)]


def neighbors(decomposition):
    """Cell id -> [(adjacent cell id, portal id)] from the decomposition's portals."""
    result = {c.id: [] for c in decomposition.cells}
    for p in decomposition.portals:
        a, b = p.cells
        result[a].append((b, p.id))
        result[b].append((a, p.id))
    return result
