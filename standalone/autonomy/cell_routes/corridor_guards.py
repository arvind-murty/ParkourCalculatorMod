from .geometry import merge_boxes


def movement_regions(projection, axis):
    lateral = 2 if axis == 0 else 0
    boundaries = sorted({box[i] for box in projection for i in (lateral, lateral+3)})
    bands = [(v, v) for v in boundaries]+list(zip(boundaries, boundaries[1:]))
    regions = []
    for lo, hi in bands:
        intervals = sorted((box[axis], box[axis+3]) for box in projection
                           if box[lateral] <= lo and hi <= box[lateral+3])
        merged = []
        for start, end in intervals:
            if merged and start <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
            else:
                merged.append((start, end))
        for start, end in merged:
            box = [0.]*6
            box[axis], box[axis+3] = start, end
            box[lateral], box[lateral+3] = lo, hi
            regions.append(tuple(box))
    regions = merge_boxes(regions)
    return [box for box in regions if not any(other != box and all(
        other[a] <= box[a] and box[a+3] <= other[a+3] for a in (0, 2)) for other in regions)]
