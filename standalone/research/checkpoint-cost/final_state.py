"""Duplication in one saved search state: repeated identical subtrees and per-tick arrays that repeat an
earlier program's array whole or as its prefix (final-state-duplication.txt).

Usage: python -B final_state.py REPORT.json.gz
"""
import collections, hashlib, json, re, sys

from parkour_solver.io.checkpoint_store import read_report

st = read_report(sys.argv[1])['result']['searchState']
J = lambda v: json.dumps(v, separators=(',', ':'))
first, dupbytes, dupcount, pairs = {}, collections.Counter(), collections.Counter(), collections.Counter()


def norm(path):
    return '.'.join('#' if isinstance(p, int) else ('<id>' if re.fullmatch('[0-9a-f]{64}', str(p)) else str(p)) for p in path)


def walk(v, path):
    if isinstance(v, (dict, list)):
        s = J(v); h = hashlib.sha1(s.encode()).hexdigest()
        if h in first:
            if len(s) >= 2048:
                dupbytes[norm(path)] += len(s); dupcount[norm(path)] += 1
                pairs[(norm(path), norm(first[h]))] += len(s)
            return
        first[h] = path
        for k, c in (v.items() if isinstance(v, dict) else enumerate(v)):
            walk(c, path+(k,))


walk(st, ())
print('duplicate copies >=2KB by location (bytes, count):')
for k, b in dupbytes.most_common(20):
    print(f'{b:>10} {dupcount[k]:>5}  {k}')
print('top (copy location <- first location):')
for (a, b), n in pairs.most_common(15):
    print(f'{n:>10}  {a}  <-  {b}')
programs = []


def find(v):
    if isinstance(v, dict):
        if isinstance(v.get('profile'), dict) and isinstance(v.get('trace'), dict):
            programs.append(v)
        for c in v.values():
            find(c)
    elif isinstance(v, list):
        for c in v:
            find(c)


find(st)
arrays = [(part, k, [J(x) for x in a]) for p in programs for part in ('profile', 'trace')
          for k, a in p[part].items() if isinstance(a, list) and len(a) > 1]
tot = sum(len(e)+1 for *_, el in arrays for e in el)
seen_whole, covered, uniq = set(), 0, 0
for part, k, el in sorted(arrays, key=lambda a: len(a[2])):
    key = (part, k, hash(tuple(el)))
    if key in seen_whole:
        covered += sum(len(e)+1 for e in el); continue
    uniq += sum(len(e)+1 for e in el)
    best = next((m for m in range(len(el)-1, 0, -1) if (part, k, hash(tuple(el[:m]))) in seen_whole), 0)
    covered += sum(len(e)+1 for e in el[:best]); seen_whole.add(key)
print('programs', len(programs), 'per-tick array bytes', tot, 'distinct-array bytes', uniq,
      'bytes repeating an earlier array whole or as its prefix', covered)
