"""Leaner versions of the proposed no-IPOPT pipeline (pipeline_variants 'race-probe-decayfallback', here 'proposed').
All lean variants drop the probes after the decayed push (0 gains in 51 requests, flow.py). Variants:
  proposed          as measured before
  lean              the race builds every candidate's cut node, screens them in order of node bound (5 push steps),
                    stops once an answer is at least the next candidate's bound (no other candidate can beat it), and
                    finishes nothing except, when no screen answered, the best-bound candidate continued to 30 steps;
                    probes after a race answer
  lean-decayprobe   lean with every probe push decaying (a level node's row enforces the objective)
  lean-decayfirst   lean-decayprobe with the decayed push before the race; the race runs only if it finds nothing
  lean-continue     lean-decayfirst with the decayed push continuing the plain push's state instead of restarting
                    from the central point
  lean-decayfirst-guard  lean-decayfirst, and the race also runs when the decayed push finds nothing or an answer
                    more than 0.1 below the plain bound (the better answer is kept)
Writes {fingerprint, set, variants: {name: {objective, method, seconds, clarabel, ...}}} like pipeline_variants.py.
Usage (from standalone/): python -B research/post-alarm/lean_variants.py <out.jsonl> (--alarmed f.jsonl |
  --population f.json ...)"""
import importlib.util
import json
import math
import pathlib
import sys
import time

from parkour_solver.numerics import disk, post_alarm

spec = importlib.util.spec_from_file_location('pipeline_variants', pathlib.Path(__file__).with_name('pipeline_variants.py'))
pv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pv)
PROPOSED = pv.variant(*pv.VARIANTS['race-probe-decayfallback'])


def lean_class(decay_probes, decay_first, continue_plain, guard=None):
    class Lean(PROPOSED):
        def push(self, node, start, cap_, state=None, decaying=None):
            """Production's push, resumable: `state` continues an earlier push (y, w, last_full, best, history);
            returns (best, last iterate, steps) and keeps the final state in self.push_state."""
            if decaying is None:
                decaying = getattr(self, 'force_decay', False)
            y, w, last_full, best, history = state or (start, post_alarm.W0, None, None, [])
            history, u, steps = list(history), None, 0
            for _ in range(cap_):
                point = node.step(self, y, w)
                if point is None:
                    break
                u, steps = point, steps+1
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
            self.push_state = (y, w, last_full, best, history)
            return best, u, steps

        def probes(self, plain, best):
            if not decay_probes:
                return super().probes(plain, best)
            self.force_decay = True
            try:
                return super().probes(plain, best)
            finally:
                self.force_decay = False

        def race_only(self, plain):
            ticks = self.problem['ticks']
            alarms = self.relaxed['alarmTicks']
            jumps = (self.problem.get('vertical') or {}).get('jumpVelocity') or [None]*ticks
            window = {t for a in alarms for t in range(max(0, a-post_alarm.RACE_WINDOW),
                                                        min(ticks, a+post_alarm.RACE_WINDOW+1)) if jumps[t] is not None}
            shortfall = disk.deficits(self.problem, plain.disk)[1]
            top = max(range(ticks), key=shortfall.__getitem__)
            if shortfall[top] >= pv.SHORT:
                window.add(top)
            nodes = []
            for t in sorted(window):
                direction = math.atan2(plain.disk[t][1], plain.disk[t][0])+math.pi
                cut = (t, math.cos(direction), math.sin(direction), post_alarm.RACE_CUT*self.problem['magnitude'][t])
                node = post_alarm._Node(self, self.exact, [cut])
                if node.feasible:
                    nodes.append((t, node))
            nodes.sort(key=lambda tn: -self.sense*tn[1].bound)
            best, states = None, []
            for i, (t, node) in enumerate(nodes):
                found, _, _ = self.push(node, node.central, post_alarm.RACE_STEPS, decaying=False)
                self.info['raced'] += 1
                states.append(self.push_state)
                if found is not None and (best is None or self.better(found[1]['objective'], best[1]['objective'])):
                    best = self.best = found
                    self.info.update(method='race', acceptedAs=found[0], reversalTick=t)
                following = nodes[i+1][1].bound if i+1 < len(nodes) else None
                if best is not None and (following is None or self.sense*(best[1]['objective']-following) >= 0):
                    break
            if best is None and nodes:
                t, node = nodes[0]
                found, _, _ = self.push(node, None, post_alarm.RACE_FINISH_CAP-post_alarm.RACE_STEPS, state=states[0],
                                        decaying=False)
                if found is not None:
                    best = self.best = found
                    self.info.update(method='race', acceptedAs=found[0], reversalTick=t)
            return best

        def answer(self):
            alarmed_point = (self.relaxed['objective'], self.relaxed_candidate['controls'])
            plain = post_alarm._Node(self, self.exact, disk_point=alarmed_point)
            if not plain.feasible:
                return None, 'post_alarm_none'
            self.plain = plain
            start = plain.central
            warm = self.problem.get('warmYaw')
            if warm:
                start = [[m*math.cos(b+math.radians(y)), m*math.sin(b+math.radians(y))]
                         for m, b, y in zip(self.problem['magnitude'], self.problem['base'], warm)]+start[len(warm):]
            best, _, self.info['pushSteps'] = self.push(plain, start, post_alarm.PUSH_CAP, decaying=False)
            plain_state = self.push_state
            if best is not None:
                self.best = best
                self.info.update(method='push', acceptedAs=best[0])
                return self.probes(plain, best), post_alarm.TERMINATIONS[self.info['method']]
            self.info['quadrant'] = self.quadrant()
            if self.info['quadrant'] == 'infeasible':
                self.info['method'] = 'quadrant'
                return None, 'quadrant_infeasible'

            def decayed():
                if continue_plain:
                    found, _, _ = self.push(plain, None, 30, state=plain_state, decaying=True)
                else:
                    found, _, _ = self.push(plain, plain.central, 30, decaying=True)
                if found is not None:
                    self.best = found
                    self.info.update(method='decayFallback', acceptedAs=found[0])
                return found

            def raced():
                found = self.race_only(plain)
                return None if found is None else self.probes(plain, found)
            if guard is not None:
                # The decayed push first; the race also runs when it finds nothing or an answer more than `guard`
                # below the plain bound, and the better answer is kept.
                found = decayed()
                if found is not None and self.sense*(plain.bound-found[1]['objective']) <= guard:
                    return found, post_alarm.TERMINATIONS['push']
                kept = (found, dict(self.info))
                other = raced()
                if other is not None and (found is None or self.better(other[1]['objective'], found[1]['objective'])):
                    return other, post_alarm.TERMINATIONS.get(self.info['method'], post_alarm.TERMINATIONS['push'])
                if found is None:
                    return None, 'post_alarm_none'
                self.best = found
                self.info.update({k: v for k, v in kept[1].items() if k not in ('raced', 'probes')})
                return found, post_alarm.TERMINATIONS['push']
            for stage in ((decayed, raced) if decay_first else (raced, decayed)):
                found = stage()
                if found is not None:
                    return found, post_alarm.TERMINATIONS.get(self.info['method'], post_alarm.TERMINATIONS['push'])
            return None, 'post_alarm_none'
    return Lean


CLASSES = {
    'proposed': PROPOSED,
    'lean': lean_class(False, False, False),
    'lean-decayprobe': lean_class(True, False, False),
    'lean-decayfirst': lean_class(True, True, False),
    'lean-continue': lean_class(True, True, True),
    'lean-decayfirst-guard': lean_class(True, True, False, guard=.1),
}


def run_variant(name, problem, candidate, relaxed):
    saved = post_alarm._Pipeline
    post_alarm._Pipeline = CLASSES[name]
    started = time.perf_counter()
    try:
        result = post_alarm.run(problem, candidate, relaxed, seconds=20., python_executable=sys.executable,
                                cancel_event=None)
    finally:
        post_alarm._Pipeline = saved
    c = result['confirmation']
    return dict(objective=c['ipoptObjective'], method=c['method'], termination=result['termination'],
                seconds=time.perf_counter()-started, ipopt=c['ipoptCalls'], clarabel=c['clarabelSolves'],
                quadrant=c['quadrant'], reversal=c['reversalTick'])


def main():
    out = pathlib.Path(sys.argv[1])
    warm = True
    if '--variants' in sys.argv:
        i = sys.argv.index('--variants')
        keep = sys.argv[i+1].split(',')
        for name in list(CLASSES):
            if name not in keep:
                del CLASSES[name]
        del sys.argv[i:i+2]
    for i, (fp, group, problem, candidate, relaxed) in enumerate(pv.requests(sys.argv[2:])):
        if warm:
            run_variant(next(iter(CLASSES)), problem, candidate, relaxed)
            warm = False
        row = dict(fingerprint=fp, set=group, variants={n: run_variant(n, problem, candidate, relaxed) for n in CLASSES})
        with open(out, 'a') as stream:
            stream.write(json.dumps(row)+'\n')
        print(i, fp[:12], {n: (v['method'], v['objective'] and round(v['objective'], 4), v['clarabel'])
                           for n, v in row['variants'].items()}, flush=True)


if __name__ == '__main__':
    main()
