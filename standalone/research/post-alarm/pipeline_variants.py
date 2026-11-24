"""The post-alarm pipeline with research variants, end to end on alarmed requests (no production change). Variants:
  today        numerics.post_alarm as committed
  decay10/30   the push halves its objective weight after every step that is not full strength (production keeps W0
               until the first full-strength iterate); plain-push cap 10 / 30; IPOPT calls as today (production seed,
               then polish)
  decay30-flip1          decay30, one IPOPT call from flip1 (relaxed directions, most short tick reversed)
  decay30-flip1-prod     decay30, IPOPT flip1, then the production seed
  decay30-race-flip1-prod  decay30-flip1-prod with the race also reversing the most short tick (any tick kind)
  decay30-race-probe-flip1-prod  and objective-cut probes after a race answer
  plaindecay30-race-probe-flip1-prod  the same with only the plain push decaying (probe and race pushes as production)
  race-probe-today / race-probe-flip1-prod  no decay: the extended race with probes, IPOPT as today / flip1 then
               the production seed
  race-probe-decayfallback  the extended race with probes, then (no IPOPT) the decayed push from the central point
               and probes
  keep-going   no IPOPT; each later stage also runs while the best accepted answer is more than G below the bound
Every variant runs on each request in turn (requests outer), sharing one kept-alive IPOPT worker; the first request
runs once untimed. Writes one JSON row per request: {fingerprint, set, variants: {name: {objective, method,
termination, seconds, ipopt, clarabel}}}; labels are read only by pipeline_variants_score.py.
Usage (from standalone/): python -B research/post-alarm/pipeline_variants.py <out.jsonl> (--alarmed f.jsonl |
  --population f.json ...) [--only fp ...]"""
import importlib.util
import json
import math
import pathlib
import sys
import time

from parkour_solver.numerics import disk, post_alarm

SHORT = 1e-3


def flip1(problem, candidate):
    """Relaxed directions (tilted as the production seed) with the most short tick reversed, or None if none is short."""
    shortfall = disk.deficits(problem, candidate['controls'])[1]
    t = max(range(len(shortfall)), key=shortfall.__getitem__)
    if shortfall[t] < SHORT:
        return None
    yaw = disk.ipopt_seed(candidate['yaw'])
    yaw[t] += 180.
    return yaw


def variant(decay, cap, seeds, race_short, race_probes=False, plain_only=False, keep_going=False):
    class Pipeline(post_alarm._Pipeline):
        def push(self, node, start, cap_):
            # Production's push with one change: a step that is not full strength before the first full-strength
            # iterate halves w.
            y, w, last_full, best, history, u, steps = start, post_alarm.W0, None, None, [], None, 0
            # plain_only: only the pipeline's first push (the plain node's) decays; probe and race pushes do not.
            self.pushes = getattr(self, 'pushes', 0)+1
            decaying = decay and (not plain_only or self.pushes == 1) or getattr(self, 'force_decay', False)
            for _ in range(cap_):
                point = node.step(self, y, w)
                if point is None:
                    break
                u, steps = point, steps+1
                if self.pushes == 1:
                    self.last = u
                accepted = self.accept(u)
                if accepted is not None and (best is None or self.better(accepted[1]['objective'], best[1]['objective'])):
                    best = accepted
                history.append(None if best is None else best[1]['objective'])
                if best is not None and self.sense*(node.bound-best[1]['objective']) <= post_alarm.G:
                    break
                if (best is not None and len(history) > 3 and history[-4] is not None
                        and self.sense*(best[1]['objective']-history[-4]) < post_alarm.G/10):
                    break
                full = disk.deficits(self.problem, u)[0] < post_alarm.FULL
                if last_full is not None:
                    if full:
                        w = min(w*post_alarm.RAMP, post_alarm.W_MAX)
                        y = [[a+post_alarm.MOMENTUM*(a-b) for a, b in zip(c, d)] for c, d in zip(u, last_full)]
                        last_full = u
                    else:
                        w, y = w/2, last_full
                else:
                    if full:
                        last_full = u
                    elif decaying:
                        w = w/2
                    y = u
            return best, u, steps

        def race(self, plain):
            self.plain = plain
            best = self.race_only(plain)
            if best is not None and race_probes:
                # Objective-cut probes after a race answer, as after a push answer.
                best = self.probes(plain, best)
            return best

        def race_only(self, plain):
            """Production's race (post_alarm._Pipeline.race) with the disk point's most short tick, of any kind,
            added to the window."""
            if not race_short:
                return super().race(plain)
            from parkour_solver.validation.numerical import validate_controls
            ticks = self.problem['ticks']
            alarms = self.relaxed['alarmTicks']
            jumps = (self.problem.get('vertical') or {}).get('jumpVelocity') or [None]*ticks
            window = {t for a in alarms for t in range(max(0, a-post_alarm.RACE_WINDOW),
                                                        min(ticks, a+post_alarm.RACE_WINDOW+1)) if jumps[t] is not None}
            shortfall = disk.deficits(self.problem, plain.disk)[1]
            top = max(range(ticks), key=shortfall.__getitem__)
            if shortfall[top] >= SHORT:
                window.add(top)
            scored = []
            for t in sorted(window):
                direction = math.atan2(plain.disk[t][1], plain.disk[t][0])+math.pi
                cut = (t, math.cos(direction), math.sin(direction), post_alarm.RACE_CUT*self.problem['magnitude'][t])
                node = post_alarm._Node(self, self.exact, [cut])
                if not node.feasible:
                    continue
                found, last, _ = self.push(node, node.central, post_alarm.RACE_STEPS)
                self.info['raced'] += 1
                if found is not None:
                    score = (1, self.sense*found[1]['objective'])
                elif last is not None:
                    score = (0, self.sense*validate_controls(self.problem, last)['objective'])
                else:
                    score = (0, -math.inf)
                scored.append((score, t, node))
            best = None
            for _, t, node in sorted(scored, key=lambda row: row[0], reverse=True)[:post_alarm.RACE_FINISH]:
                found, _, _ = self.push(node, node.central, post_alarm.RACE_FINISH_CAP)
                if found is not None and (best is None or self.better(found[1]['objective'], best[1]['objective'])):
                    best = self.best = found
                    self.info.update(method='race', acceptedAs=found[0], reversalTick=t)
            return best

        def answer(self):
            if keep_going:
                return self.answer_keep_going()
            plain_ipopt = self.ipopt_call
            calls = []
            def recorded(request):
                calls.append(request)
                return None
            # Run production's answer with IPOPT stubbed out to learn whether it reaches the IPOPT stage.
            if seeds == 'today':
                return super().answer()
            self.ipopt_call = recorded
            try:
                best, termination = super().answer()
            finally:
                self.ipopt_call = plain_ipopt
            if not calls:
                return best, termination
            for name in seeds:
                if name == 'decayfallback':
                    # Instead of IPOPT: the plain push again from the central point, its weight halving while not
                    # full strength (cap 30), then the objective-cut probes.
                    self.force_decay = True
                    try:
                        found, _, _ = self.push(self.plain, self.plain.central, 30)
                    finally:
                        self.force_decay = False
                    if found is None:
                        continue
                    self.best = found
                    self.info.update(method='decayFallback', acceptedAs=found[0])
                    return self.probes(self.plain, found), post_alarm.TERMINATIONS['push']
                request = dict(self.problem, yaw=disk.ipopt_seed(self.relaxed_candidate['yaw']))
                if name == 'flip1':
                    request['yaw'] = flip1(self.problem, self.relaxed_candidate)
                    if request['yaw'] is None:
                        continue
                elif name == 'polish':
                    # Production's polish call: the plain push's last iterate, its states, mu_init 1e-6.
                    if getattr(self, "last", None) is None:
                        continue
                    request = dict(self.problem, yaw=disk.ipopt_seed(post_alarm.facings(self.problem, self.last)),
                                   stateSeed=post_alarm.states(self.problem, self.last), ipoptMuInit=post_alarm.POLISH_MU)
                found = self.ipopt_call(request)
                if found is not None:
                    self.info.update(method='prodSeed', acceptedAs='full', seed=name)
                    return found, post_alarm.TERMINATIONS['prodSeed']
            return None, 'post_alarm_none'
        def answer_keep_going(self):
            """No IPOPT. Every stage keeps the best accepted answer and the next stage runs while it is more than G
            below the plain bound: push and probes, the sector check (only without an answer), the extended race and
            probes, the decayed push from the central point and probes."""
            alarmed_point = (self.relaxed['objective'], self.relaxed_candidate['controls'])
            plain = post_alarm._Node(self, self.exact, disk_point=alarmed_point)
            if not plain.feasible:
                return None, 'post_alarm_none'
            start = plain.central
            warm = self.problem.get('warmYaw')
            if warm:
                start = [[m*math.cos(b+math.radians(y)), m*math.sin(b+math.radians(y))]
                         for m, b, y in zip(self.problem['magnitude'], self.problem['base'], warm)]+start[len(warm):]
            best, _, self.info['pushSteps'] = self.push(plain, start, post_alarm.PUSH_CAP)
            if best is not None:
                self.best = best
                self.info.update(method='push', acceptedAs=best[0])
                best = self.probes(plain, best)
            close = lambda b: b is not None and self.sense*(plain.bound-b[1]['objective']) <= post_alarm.G
            def keep(found, method):
                nonlocal best
                if found is not None and (best is None or self.better(found[1]['objective'], best[1]['objective'])):
                    best = found
                    self.info.update(method=method, acceptedAs=found[0])
                self.best = best
            if best is None:
                self.info['quadrant'] = self.quadrant()
                if self.info['quadrant'] == 'infeasible':
                    self.info['method'] = 'quadrant'
                    return None, 'quadrant_infeasible'
            # Each stage gets its own LEVEL_PROBES probes; info['probes'] ends as the total.
            probes = self.info['probes']
            if not close(best):
                kept = (best, dict(self.info))
                self.info['probes'] = 0
                found = self.race(plain)
                tick, probes = self.info['reversalTick'], probes+self.info['probes']
                best, info = kept
                self.info.update({k: v for k, v in info.items() if k not in ('raced', 'probes')})
                keep(found, 'race')
                if self.info['method'] == 'race':
                    self.info['reversalTick'] = tick
            if not close(best):
                info = dict(self.info)
                self.info['probes'] = 0
                self.force_decay = True
                try:
                    found, _, _ = self.push(plain, plain.central, 30)
                finally:
                    self.force_decay = False
                if found is not None:
                    found = self.probes(plain, found) if self.sense*(plain.bound-found[1]['objective']) > post_alarm.G \
                        else found
                probes += self.info['probes']
                self.info.update({k: v for k, v in info.items() if k != 'probes'})
                keep(found, 'decayFallback')
            self.info['probes'] = probes
            if best is None:
                return None, 'post_alarm_none'
            return best, post_alarm.TERMINATIONS.get(self.info['method'], post_alarm.TERMINATIONS['push'])
    return Pipeline


VARIANTS = {
    'today': (False, post_alarm.PUSH_CAP, 'today', False),
    'decay10': (True, 10, 'today', False),
    'decay30': (True, 30, 'today', False),
    'decay30-flip1': (True, 30, ('flip1',), False),
    'decay30-flip1-prod': (True, 30, ('flip1', 'prod'), False),
    'decay30-race-flip1-prod': (True, 30, ('flip1', 'prod'), True),
    'decay30-race-probe-flip1-prod': (True, 30, ('flip1', 'prod'), True, True),
    'plaindecay30-race-probe-flip1-prod': (True, 30, ('flip1', 'prod'), True, True, True),
    'race-probe-today': (False, post_alarm.PUSH_CAP, ('prod', 'polish'), True, True),
    'race-probe-flip1-prod': (False, post_alarm.PUSH_CAP, ('flip1', 'prod'), True, True),
    'race-probe-decayfallback': (False, post_alarm.PUSH_CAP, ('decayfallback',), True, True),
    'keep-going': (False, post_alarm.PUSH_CAP, (), True, True, False, True),
}


def run_variant(name, problem, candidate, relaxed):
    decay, cap, seeds = VARIANTS[name][:3]
    cls = post_alarm._Pipeline if name == 'today' else variant(decay, cap, *VARIANTS[name][2:])
    saved_pipeline, saved_cap = post_alarm._Pipeline, post_alarm.PUSH_CAP
    post_alarm._Pipeline, post_alarm.PUSH_CAP = cls, cap
    started = time.perf_counter()
    try:
        result = post_alarm.run(problem, candidate, relaxed, seconds=20., python_executable=sys.executable,
                                cancel_event=None)
    finally:
        post_alarm._Pipeline, post_alarm.PUSH_CAP = saved_pipeline, saved_cap
    c = result['confirmation']
    return dict(objective=c['ipoptObjective'], method=c['method'], seed=c.get('seed'), termination=result['termination'],
                seconds=time.perf_counter()-started, ipopt=c['ipoptCalls'], clarabel=c['clarabelSolves'],
                quadrant=c['quadrant'], reversal=c['reversalTick'])


def requests(args):
    if args[0] == '--alarmed':
        for line in open(args[1]):
            row = json.loads(line)
            yield row['fingerprint'], 'real11', row['problem'], row['relaxedCandidate'], row['relaxed']
    else:
        path = pathlib.Path(__file__).with_name('population_eval.py')
        spec = importlib.util.spec_from_file_location('population_eval', path)
        population_eval = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(population_eval)
        for path in args[1:]:
            for fp, row in json.load(open(path)).items():
                problem = dict(row['problem'], objectiveMode='configured')
                _, candidate, relaxed = population_eval.relaxed_record(problem, disk.spec(problem, strict=True))
                if candidate is not None and candidate['feasible'] and relaxed['alarmTicks']:
                    yield fp, 'study', problem, candidate, relaxed


def main():
    out = pathlib.Path(sys.argv[1])
    args = sys.argv[2:]
    only = args[args.index('--only')+1:] if '--only' in args else []
    args = args[:args.index('--only')] if '--only' in args else args
    done = {json.loads(l)['fingerprint'] for l in open(out)} if out.exists() else set()
    warm = True
    for i, (fp, group, problem, candidate, relaxed) in enumerate(requests(args)):
        if fp in done or only and not any(fp.startswith(o) for o in only):
            continue
        if warm:
            run_variant('today', problem, candidate, relaxed)
            warm = False
        row = dict(fingerprint=fp, set=group, variants={name: run_variant(name, problem, candidate, relaxed)
                                                        for name in VARIANTS})
        with open(out, 'a') as stream:
            stream.write(json.dumps(row)+'\n')
        print(i, fp[:12], {n: (v['method'], v['objective'] and round(v['objective'], 4), v['ipopt'])
                           for n, v in row['variants'].items()}, flush=True)
    from parkour_solver.numerics import solver
    solver._close_workers()


if __name__ == '__main__':
    if '--variants' in sys.argv:
        i = sys.argv.index('--variants')
        keep = sys.argv[i+1].split(',')
        VARIANTS = {k: v for k, v in VARIANTS.items() if k in keep}
        del sys.argv[i:i+2]
    main()
