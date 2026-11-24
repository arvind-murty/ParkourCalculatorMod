"""Instrumented proposed pipeline (pipeline_variants 'race-probe-decayfallback'): per request, Clarabel solves and
seconds per stage, each probe's outcome, and per race candidate its cut node (feasible, bound, disk-point peak
shortfall, central peak shortfall), screen and finish results. Output one JSON row per request.
Usage (from standalone/): flow.py <out.jsonl> (--alarmed f.jsonl | --population f.json ...)"""
import importlib.util, json, math, pathlib, sys, time
from parkour_solver.numerics import disk, post_alarm
spec = importlib.util.spec_from_file_location('pv', 'research/post-alarm/pipeline_variants.py')
pv = importlib.util.module_from_spec(spec); spec.loader.exec_module(pv)
Base = pv.variant(*pv.VARIANTS['race-probe-decayfallback'][:1], post_alarm.PUSH_CAP, *pv.VARIANTS['race-probe-decayfallback'][2:])

class Flow(Base):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.stage, self.log, self.cands, self.probe_log = 'setup', {}, [], []
        self.mark = (0, time.perf_counter())
    def enter(self, stage):
        n, t = self.mark
        e = self.log.setdefault(self.stage, [0, 0.])
        e[0] += self.clarabel-n; e[1] += time.perf_counter()-t
        self.stage, self.mark = stage, (self.clarabel, time.perf_counter())
    def push(self, node, start, cap_):
        return super().push(node, start, cap_)
    def answer(self):
        orig_push, orig_probes, orig_quadrant, orig_race_only = self.push, self.probes, self.quadrant, self.race_only
        first = [True]
        def push(node, start, cap_):
            if first[0] and self.stage == 'setup':
                self.enter('plainPush'); first[0] = False
            if getattr(self, 'force_decay', False) and self.stage != 'decay':
                self.enter('decay')
            return orig_push(node, start, cap_)
        def probes(plain, best):
            prev = self.stage
            label = {'plainPush': 'push', 'after race': 'race', 'decay': 'decay'}.get(prev, prev)
            self.enter('probes after ' + label)
            before = best[1]['objective']
            n0 = self.info['probes']
            out = orig_probes(plain, best)
            self.probe_log.append(dict(after=label, ran=self.info['probes']-n0, gain=self.sense*(out[1]['objective']-before),
                                       gapBefore=self.sense*(plain.bound-before), gapAfter=self.sense*(plain.bound-out[1]['objective'])))
            self.enter('after probes')
            return out
        def quadrant():
            self.enter('quadrant'); r = orig_quadrant(); self.enter('after quadrant'); return r
        self.push, self.probes, self.quadrant = push, probes, quadrant
        try:
            best, term = super().answer()
        finally:
            self.enter('end')
        return best, term
    def race_only(self, plain):
        self.enter('race')
        from parkour_solver.validation.numerical import validate_controls
        ticks = self.problem['ticks']; alarms = self.relaxed['alarmTicks']
        jumps = (self.problem.get('vertical') or {}).get('jumpVelocity') or [None]*ticks
        window = {t for a in alarms for t in range(max(0, a-post_alarm.RACE_WINDOW), min(ticks, a+post_alarm.RACE_WINDOW+1)) if jumps[t] is not None}
        shortfall = disk.deficits(self.problem, plain.disk)[1]
        top = max(range(ticks), key=shortfall.__getitem__)
        cands = sorted(window | ({top} if shortfall[top] >= pv.SHORT else set()))
        scored = []
        for t in cands:
            rec = dict(t=t, inWindow=t in window, top=t == top, shortfall=shortfall[t], jump=jumps[t] is not None)
            n0 = self.clarabel
            direction = math.atan2(plain.disk[t][1], plain.disk[t][0])+math.pi
            cut = (t, math.cos(direction), math.sin(direction), post_alarm.RACE_CUT*self.problem['magnitude'][t])
            node = post_alarm._Node(self, self.exact, [cut])
            rec['nodeSolves'] = self.clarabel-n0; rec['feasible'] = node.feasible
            if node.feasible:
                rec.update(bound=node.bound, plainBound=plain.bound, diskPeak=disk.deficits(self.problem, node.disk)[0],
                           centralPeak=disk.deficits(self.problem, node.central)[0])
                n1 = self.clarabel
                found, last, steps = self.push(node, node.central, post_alarm.RACE_STEPS)
                self.info['raced'] += 1
                rec.update(screenSolves=self.clarabel-n1, screenSteps=steps, screenFound=found and found[1]['objective'],
                           screenLastPeak=last and disk.deficits(self.problem, last)[0])
                if found is not None: score = (1, self.sense*found[1]['objective'])
                elif last is not None: score = (0, self.sense*validate_controls(self.problem, last)['objective'])
                else: score = (0, -math.inf)
                scored.append((score, t, node, rec))
            self.cands.append(rec)
        best = None
        for _, t, node, rec in sorted(scored, key=lambda row: row[0], reverse=True)[:post_alarm.RACE_FINISH]:
            n1 = self.clarabel
            found, _, steps = self.push(node, node.central, post_alarm.RACE_FINISH_CAP)
            rec.update(finished=True, finishSolves=self.clarabel-n1, finishSteps=steps, finishFound=found and found[1]['objective'])
            if found is not None and (best is None or self.better(found[1]['objective'], best[1]['objective'])):
                best = self.best = found
                self.info.update(method='race', acceptedAs=found[0], reversalTick=t)
        if best is not None:
            for rec in self.cands:
                rec['winner'] = rec['t'] == self.info['reversalTick']
        self.enter('after race')
        return best

rows = []
def run(fp, group, problem, candidate, relaxed):
    saved = post_alarm._Pipeline; post_alarm._Pipeline = Flow
    holder = {}
    orig_init = Flow.__init__
    def init(self, *a, **k):
        orig_init(self, *a, **k); holder['p'] = self
    Flow.__init__ = init
    t = time.perf_counter()
    try:
        result = post_alarm.run(problem, candidate, relaxed, seconds=20., python_executable=sys.executable, cancel_event=None)
    finally:
        post_alarm._Pipeline = saved; Flow.__init__ = orig_init
    p = holder['p']; c = result['confirmation']
    return dict(fingerprint=fp, set=group, method=c['method'], objective=c['ipoptObjective'], bound=relaxed['objective'],
                sense=problem['objective']['sense'], seconds=time.perf_counter()-t, clarabel=c['clarabelSolves'],
                stages={k: v for k, v in p.log.items() if v[0] or v[1] > 1e-4}, candidates=p.cands, probes=p.probe_log,
                quadrant=c['quadrant'], pushSteps=c['pushSteps'])

out = pathlib.Path(sys.argv[1])
for i, (fp, group, problem, candidate, relaxed) in enumerate(pv.requests(sys.argv[2:])):
    if i == 0: run(fp, group, problem, candidate, relaxed)
    row = run(fp, group, problem, candidate, relaxed)
    with open(out, 'a') as s: s.write(json.dumps(row)+'\n')
    print(i, fp[:12], row['method'], row['clarabel'], {k: v[0] for k, v in row['stages'].items()}, flush=True)
