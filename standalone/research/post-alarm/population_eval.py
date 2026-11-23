"""The post-alarm pipeline on the alarmed-request populations: per request, this source's disk solve of its spec, then
`numerics.post_alarm.run` on that point (20 s budget, kept-alive IPOPT worker). Writes the research grid format
(`{fingerprint: {name, source, disk, runs: {'pipeline': {kind, objective, seconds, ipopt, clarabel, ...}}}}`) that the
study's `pipeline_grid_score.py` scores. The first request runs once untimed (warm-up).
Usage (from standalone/): python -B research/post-alarm/population_eval.py <out.json> <population.json> ... [--only fp ...]"""
import json
import sys
import time

from parkour_solver.numerics import disk, post_alarm
from parkour_solver.validation.numerical import validate_controls


def relaxed_record(problem, content):
    """What RelaxedDispatch._dispatch hands `confirm`: the validated disk point and its disk record."""
    solved = disk.solve(content, seconds=disk.RETRY_CAP)
    if solved['outcome'] != 'feasible':
        return solved, None, None
    candidate = validate_controls(problem, solved['controls'])
    peak, shortfall, tick = disk.deficits(problem, solved['controls'])
    relaxed = {k: solved[k] for k in ('status', 'outcome', 'iterations', 'cached')}
    relaxed.update(peakRelativeDeficit=peak, peakTick=tick, alarmTicks=disk.alarm_ticks(problem, shortfall),
                   objective=solved['objective'])
    candidate['model'] = 'relaxed_disk'
    return solved, candidate, relaxed


def evaluate(row):
    problem = dict(row['problem'], objectiveMode='configured')
    solved, candidate, relaxed = relaxed_record(problem, disk.spec(problem, strict=True))
    if candidate is None or not candidate['feasible'] or not relaxed['alarmTicks']:
        # This source's disk point differs from the study's: production answers an unalarmed one as it is.
        unalarmed = candidate is not None and candidate['feasible']
        return solved.get('objective'), dict(kind='not alarmed' if unalarmed else 'disk',
                                             objective=candidate['objective'] if unalarmed else None, seconds=0.,
                                             ipopt=0, ipoptSeconds=0., clarabel=0)
    started = time.perf_counter()
    result = post_alarm.run(problem, candidate, relaxed, seconds=20., python_executable=sys.executable,
                            cancel_event=None)
    seconds = time.perf_counter()-started
    confirmation = result['confirmation']
    return solved['objective'], dict(
        kind=confirmation['method'], termination=result['termination'], acceptedAs=confirmation['acceptedAs'],
        objective=confirmation['ipoptObjective'], seconds=seconds, ipopt=confirmation['ipoptCalls'],
        # The kept-alive worker's calls are part of `seconds`; the score's production estimate subtracts this.
        ipoptSeconds=0., clarabel=confirmation['clarabelSolves'], pushSteps=confirmation['pushSteps'],
        probes=confirmation['probes'], raced=confirmation['raced'], reversal=confirmation['reversalTick'],
        quadrant=confirmation['quadrant'])


def main():
    args = sys.argv[2:]
    only = args[args.index('--only')+1:] if '--only' in args else []
    paths = args[:args.index('--only')] if '--only' in args else args
    population = {}
    for path in paths:
        population.update(json.load(open(path)))
    if only:
        population = {k: v for k, v in population.items() if any(k.startswith(o) for o in only)}
    evaluate(next(iter(population.values())))
    out = {}
    for i, (fp, row) in enumerate(population.items()):
        bound, run = evaluate(row)
        out[fp] = dict(name=row['name'], source=row['source'], disk=bound, runs={'pipeline': run})
        print(i, fp[:12], row['name'], row['source'][:14], run['kind'], run.get('termination'),
              None if run['objective'] is None else round(run['objective'], 6), '%.3fs' % run['seconds'],
              'ipopt', run['ipopt'], 'clarabel', run['clarabel'], flush=True)
    json.dump(out, open(sys.argv[1], 'w'))


if __name__ == '__main__':
    main()
