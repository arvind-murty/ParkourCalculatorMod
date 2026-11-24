"""IPOPT seeds built from the post-alarm pipeline's relaxed information, one IPOPT start each (pkc-notes post-alarm
SEED_PLAN.md). Per alarmed request: the production push from the disk point (recording its iterates), then every seed
below, its build seconds and scalar-validator start violation, and one kept-alive IPOPT start without fallbackYaw at
0.2 s, rerun at 2 s only when 0.2 s did not answer (IPOPT is deterministic, so an answer at 0.2 s is the 2 s answer).
Labels are not read here; seed_bench_score.py scores the rows.

Seeds (facings get disk.ipopt_seed's tilt unless noted):
  prodSeed      the relaxed directions (the pipeline's first IPOPT call)
  fallbackYaw   the request's fallbackYaw, else objective_seed of the relaxed directions (today's second phase)
  polish        last push iterate + its states + mu_init 1e-6 (the pipeline's second IPOPT call)
  relaxedStates relaxed directions + the relaxed point's states
  central       the no-objective central point's directions
  pushLast      last push iterate's directions
  pushBest      push iterate with the least total shortfall
  warm          the request's warmYaw prefix (exact), relaxed directions after it
  track0/track1 full-strength tracking of the relaxed trajectory with error feedback (velocity weight 0 / 1)
  sigmaDelta    each short tick takes the full-strength completion (+-acos(|u|/m) about its direction) that keeps
                the accumulated acceleration error smallest
  flipK         the K-th most short tick reversed at full strength, every other tick at the relaxed direction
  screen        the seed with the least start violation among all yaw-only seeds above and RANDOM random-completion
                samples (each short tick picks a completion at random)
Usage (from standalone/): python -B research/post-alarm/seed_bench.py <out.jsonl> (--misses f.json | --alarmed f.jsonl |
  --population f.json ...) [--only fp ...]"""
import importlib.util
import json
import math
import pathlib
import random
import sys
import time

from parkour_solver.numerics import disk, post_alarm, solver
from parkour_solver.search.ranking import objective_seed
from parkour_solver.validation.numerical import validate, validate_controls

CAPS = (.2, 2.)
SHORT = 1e-3   # a tick at least this short has two distinct full-strength completions
FLIPS, RANDOM = 3, 32


def unit(x, y):
    r = math.hypot(x, y)
    return (x/r, y/r) if r > 1e-12 else None


def completions(c, m):
    """The two full-strength vectors whose projection on c is |c| (one when c is full strength or zero)."""
    r, a = math.hypot(*c), math.atan2(c[1], c[0])
    if m <= 0 or r >= m*(1-SHORT) or r < 1e-12:
        return [[m*math.cos(a), m*math.sin(a)]]
    d = math.acos(r/m)
    return [[m*math.cos(a+s*d), m*math.sin(a+s*d)] for s in (1, -1)]


def track(problem, controls, weight):
    """Greedy full-strength tracking of the relaxed positions (and velocities, weighted) under the exact dynamics."""
    target = post_alarm.states(problem, controls)
    contacts = {(c['state'], c['axis']): c for c in problem['contacts']}
    x, v, out = [0., 0.], list(problem['velocity']), []
    for t, (m, f) in enumerate(zip(problem['magnitude'], problem['friction'])):
        p_star, v_star = target['positions'][t+1], target['velocities'][t+1]
        c = [(p_star[i]-x[i]-v[i]+weight*f*(v_star[i]-f*v[i]))/(1+weight*f*f) for i in range(2)]
        direction = unit(*c) or unit(*controls[t]) or (1., 0.)
        a = [m*direction[0], m*direction[1]]
        out.append(a)
        move = [v[i]+a[i] for i in range(2)]
        x, v = [x[i]+move[i] for i in range(2)], [f*move[i] for i in range(2)]
        for axis in range(2):
            contact = contacts.get((t+1, axis))
            if contact:
                x[axis], v[axis] = contact['plane']-problem['position'][axis], 0.
    return out


def sigma_delta(problem, controls):
    error, out = [0., 0.], []
    for c, m in zip(controls, problem['magnitude']):
        options = completions(c, m)
        a = min(options, key=lambda a: math.hypot(error[0]+a[0]-c[0], error[1]+a[1]-c[1]))
        error = [error[i]+a[i]-c[i] for i in range(2)]
        out.append(a)
    return out


def yaw_of(problem, controls, relaxed_yaw):
    """Facings, the relaxed facing where a tick has no acceleration."""
    return [y if m <= 0 else f for f, y, m in zip(post_alarm.facings(problem, controls), relaxed_yaw, problem['magnitude'])]


def push_iterates(problem, candidate, relaxed):
    """The production push (same start, cap, stops) with every iterate; None when the plain node is infeasible."""
    pipeline = post_alarm._Pipeline(problem, candidate, relaxed, 60., sys.executable, None)
    plain = post_alarm._Node(pipeline, pipeline.exact, disk_point=(relaxed['objective'], candidate['controls']))
    if not plain.feasible:
        return None
    start = plain.central
    warm = problem.get('warmYaw')
    if warm:
        start = [[m*math.cos(b+math.radians(y)), m*math.sin(b+math.radians(y))]
                 for m, b, y in zip(problem['magnitude'], problem['base'], warm)]+start[len(warm):]
    iterates, step = [], plain.step
    def recorded(*args):
        point = step(*args)
        if point is not None:
            iterates.append(point)
        return point
    plain.step = recorded
    accepted, last, steps = pipeline.push(plain, start, post_alarm.PUSH_CAP)
    return dict(central=plain.central, iterates=iterates, last=last, accepted=accepted is not None, steps=steps,
                clarabel=pipeline.clarabel)


def seeds(problem, candidate, push, rng):
    """{name: (yaw, extra request fields, build seconds)} and the screen's candidate pool {name: yaw}."""
    controls, relaxed_yaw, m = candidate['controls'], candidate['yaw'], problem['magnitude']
    out = {}
    def add(name, build, extra=None):
        started = time.perf_counter()
        yaw = build()
        if yaw is not None:
            out[name] = (yaw, extra or {}, time.perf_counter()-started)
    tilt = disk.ipopt_seed
    add('prodSeed', lambda: tilt(relaxed_yaw))
    add('fallbackYaw', lambda: problem.get('fallbackYaw') or objective_seed(dict(problem, yaw=relaxed_yaw)))
    add('relaxedStates', lambda: tilt(relaxed_yaw), dict(stateSeed=post_alarm.states(problem, controls)))
    if push is not None:
        add('central', lambda: tilt(yaw_of(problem, push['central'], relaxed_yaw)))
        if push['last'] is not None:
            last = push['last']
            add('polish', lambda: tilt(yaw_of(problem, last, relaxed_yaw)),
                dict(stateSeed=post_alarm.states(problem, last), ipoptMuInit=post_alarm.POLISH_MU))
            add('pushLast', lambda: tilt(yaw_of(problem, last, relaxed_yaw)))
            best = min(push['iterates'], key=lambda u: sum(disk.deficits(problem, u)[1]))
            add('pushBest', lambda: tilt(yaw_of(problem, best, relaxed_yaw)))
    if problem.get('warmYaw'):
        warm = problem['warmYaw']
        add('warm', lambda: list(warm)+tilt(relaxed_yaw)[len(warm):])
    add('track0', lambda: tilt(yaw_of(problem, track(problem, controls, 0.), relaxed_yaw)))
    add('track1', lambda: tilt(yaw_of(problem, track(problem, controls, 1.), relaxed_yaw)))
    add('sigmaDelta', lambda: tilt(yaw_of(problem, sigma_delta(problem, controls), relaxed_yaw)))
    shortfall = disk.deficits(problem, controls)[1]
    ranked = sorted((t for t, s in enumerate(shortfall) if s >= SHORT), key=lambda t: -shortfall[t])
    for k, t in enumerate(ranked[:FLIPS], 1):
        def flipped(t=t):
            yaw = tilt(relaxed_yaw)
            yaw[t] += 180.
            return yaw
        add(f'flip{k}', flipped)
    pool = {name: yaw for name, (yaw, extra, _) in out.items() if not extra}
    short = [t for t, s in enumerate(shortfall) if s >= SHORT]
    started = time.perf_counter()
    for i in range(RANDOM if short else 0):
        sample = [rng.choice(completions(c, mt)) for c, mt in zip(controls, m)]
        pool[f'random{i}'] = tilt(yaw_of(problem, sample, relaxed_yaw))
    return out, pool, time.perf_counter()-started


def ipopt(problem, yaw, extra, seconds):
    request = {k: v for k, v in dict(problem, yaw=yaw, **extra).items() if k != 'fallbackYaw'}
    r = solver.solve(request, seconds=seconds, stop_on_feasible=False, python_executable=sys.executable, keep_alive=True)
    return dict(status=r['status'], termination=r['termination'], seconds=r['elapsedSeconds'],
                objective=r['candidate']['objective'] if r['status'] == 'feasible' else None,
                phase=r['phases'][-1]['status'] if r['phases'] else None)


def starts(problem, yaw, extra):
    runs = {}
    for cap in CAPS:
        runs[str(cap)] = ipopt(problem, yaw, extra, cap)
        if runs[str(cap)]['objective'] is not None:
            break
    return runs


def violation(problem, yaw):
    v = validate(problem, yaw)
    discrete = sum(len(v.get(k) or []) for k in ('missedContacts', 'wrongSideContacts', 'patternMismatches',
                                                  'verticalFailures', 'movementMismatches'))
    return dict(violation=v['violation'], worst=v['worstConstraint'], discrete=discrete, feasible=v['feasible'])


def bench(fp, problem, candidate, relaxed):
    row = dict(fingerprint=fp, ticks=problem['ticks'], alarmTicks=relaxed['alarmTicks'])
    started = time.perf_counter()
    push = push_iterates(problem, candidate, relaxed)
    row['push'] = None if push is None else dict(accepted=push['accepted'], steps=push['steps'],
                                                 clarabel=push['clarabel'], seconds=time.perf_counter()-started)
    built, pool, random_seconds = seeds(problem, candidate, push, random.Random(fp))
    checks = {name: violation(problem, yaw) for name, yaw in pool.items()}
    pick = min(pool, key=lambda name: (checks[name]['violation'], checks[name]['discrete']))
    row['screen'] = dict(pick=pick, randomSeconds=random_seconds, pool=len(pool),
                         randomViolations=sorted(checks[n]['violation'] for n in pool if n.startswith('random')))
    row['seeds'] = {}
    for name, (yaw, extra, seconds) in built.items():
        row['seeds'][name] = dict(build=seconds, **(checks.get(name) or violation(problem, yaw)),
                                  runs=starts(problem, yaw, extra))
    if pick.startswith('random'):
        row['seeds'][pick] = dict(build=0., **checks[pick], runs=starts(problem, pool[pick], {}))
    return row


def requests(args):
    """(fingerprint, problem, relaxed candidate, relaxed record or None when the disk point is not alarmed)."""
    if args[0] == '--misses':
        for fp, row in json.load(open(args[1])).items():
            yield fp, row['problem'], row['relaxedCandidate'], row['relaxed']
    elif args[0] == '--alarmed':
        for line in open(args[1]):
            row = json.loads(line)
            yield row['fingerprint'], row['problem'], row['relaxedCandidate'], row['relaxed']
    else:
        path = pathlib.Path(__file__).with_name('population_eval.py')
        spec = importlib.util.spec_from_file_location('population_eval', path)
        population_eval = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(population_eval)
        for path in args[1:]:
            for fp, row in json.load(open(path)).items():
                problem = dict(row['problem'], objectiveMode='configured')
                _, candidate, relaxed = population_eval.relaxed_record(problem, disk.spec(problem, strict=True))
                alarmed = candidate is not None and candidate['feasible'] and relaxed['alarmTicks']
                yield fp, problem, candidate, relaxed if alarmed else None


def main():
    out = pathlib.Path(sys.argv[1])
    args = sys.argv[2:]
    only = args[args.index('--only')+1:] if '--only' in args else []
    args = args[:args.index('--only')] if '--only' in args else args
    done = {json.loads(l)['fingerprint'] for l in open(out)} if out.exists() else set()
    for i, (fp, problem, candidate, relaxed) in enumerate(requests(args)):
        if fp in done or only and not any(fp.startswith(o) for o in only):
            continue
        if relaxed is None:
            row = dict(fingerprint=fp, skipped='not alarmed on this source')
        else:
            row = bench(fp, problem, candidate, relaxed)
        with open(out, 'a') as stream:
            stream.write(json.dumps(row)+'\n')
        done.add(fp)
        if 'seeds' in row:
            answered = [n for n, s in row['seeds'].items() if any(r['objective'] is not None for r in s['runs'].values())]
            print(i, fp[:12], 'push', row['push'] and row['push']['accepted'], 'screen', row['screen']['pick'],
                  'answered', len(answered), '/', len(row['seeds']), answered, flush=True)
        else:
            print(i, fp[:12], row['skipped'], flush=True)
    solver._close_workers()


if __name__ == '__main__':
    main()
