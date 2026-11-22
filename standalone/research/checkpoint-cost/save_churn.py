"""At every full save, classify the saved search state's outermost subtrees (>= 2 KB, the store's chunk size)
as unchanged since an earlier save (by content), and whether the same object held that content at the
previous save (identity-stable). Aggregated by normalized path.

Usage: python -B save_churn.py OUT.json <tools.profiling.run args...>
"""
import atexit, collections, hashlib, json, re, runpy, sys

OUT = sys.argv.pop(1)
CHUNK = 2048
J = lambda v: json.dumps(v, separators=(',', ':'), allow_nan=True).encode()
known = set()            # content hashes of chunk-sized subtrees in any earlier save
previous_ids = {}        # id -> (hash, object) at the previous save (strong refs keep ids unique)
totals = collections.defaultdict(lambda: dict(bytes=0, unchanged=0, identityStable=0, changed=0))
per_save = []


def norm(path):
    return '.'.join('#' if isinstance(p, int) else '<id>' if re.fullmatch('[0-9a-f]{64}', str(p)) else str(p) for p in path)


def merkle(value, memo):
    """(repr, size, hash-or-None) where children >= CHUNK are replaced by their hash."""
    key = id(value)
    if key in memo:
        return memo[key]
    if isinstance(value, dict):
        parts, size = [], 2
        for k, v in value.items():
            r, s, _ = merkle(v, memo)
            parts.append(J(k)+b':'+r); size += len(J(k))+1+s+1
        data = b'{'+b','.join(parts)+b'}'
    elif isinstance(value, (list, tuple)):
        parts, size = [], 2
        for v in value:
            r, s, _ = merkle(v, memo)
            parts.append(r); size += s+1
        data = b'['+b','.join(parts)+b']'
    else:
        data = J(value); size = len(data)
    h = None
    if size >= CHUNK:
        h = hashlib.sha1(data).hexdigest()
        data = b'"#'+h.encode()+b'"'
    result = (data, size, h)
    if isinstance(value, (dict, list, tuple, str)):
        memo[key] = result
    return result


def classify(value, path, memo, ids, out):
    data, size, h = memo.get(id(value)) or merkle(value, memo)
    if h is None:
        return
    ids[id(value)] = (h, value)
    if h in known:
        cat = norm(path)
        stable = previous_ids.get(id(value), (None,))[0] == h
        row = totals[cat]; row['bytes'] += size; row['unchanged'] += size; row['identityStable'] += size if stable else 0
        out['unchanged'] += size; out['identityStable'] += size if stable else 0
        return
    if isinstance(value, dict):
        children = value.items()
    elif isinstance(value, (list, tuple)):
        children = enumerate(value)
    else:
        children = ()
    child_sizes = 0
    for k, v in children:
        r = memo.get(id(v))
        if r is not None and r[2] is not None:
            child_sizes += r[1]
            classify(v, path+(k,), memo, ids, out)
    own = size-child_sizes
    row = totals[norm(path)]; row['bytes'] += own; row['changed'] += own
    out['changed'] += own


from parkour_solver.io import search_checkpoint
original_call = search_checkpoint.SearchCheckpoint.__call__


def call(self, state, program=None, extra_targets=()):
    global previous_ids
    original_call(self, state, program, extra_targets)
    memo, ids, out = {}, {}, dict(unchanged=0, identityStable=0, changed=0)
    merkle(state, memo)
    classify(state, (), memo, ids, out)
    out['total'] = memo[id(state)][1]
    per_save.append(out)
    known.update(h for _, _, h in memo.values() if h is not None)
    previous_ids = ids


search_checkpoint.SearchCheckpoint.__call__ = call


def dump():
    rows = sorted(totals.items(), key=lambda kv: -kv[1]['bytes'])
    json.dump(dict(perSave=per_save, byPath=dict(rows)), open(OUT, 'w'), indent=1)


atexit.register(dump)
sys.argv[0] = 'tools.profiling.run'
runpy.run_module('tools.profiling.run', run_name='__main__', alter_sys=True)
