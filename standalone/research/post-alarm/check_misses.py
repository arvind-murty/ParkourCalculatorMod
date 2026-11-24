"""For each captured miss: IPOPT from the production seed with and without fallbackYaw, at 0.2 s and at the request's
seconds, and from fallbackYaw alone; which phase produced the answer."""
import json, sys, pathlib
from parkour_solver.numerics import disk, solver
saved = json.loads(pathlib.Path(sys.argv[1]).read_text())
for fp, row in saved.items():
    p, cand = row['problem'], row['relaxedCandidate']
    seed = disk.ipopt_seed(cand['yaw'])
    print(fp[:12], 'ticks', p['ticks'], 'has fallbackYaw', 'fallbackYaw' in p, 'request seconds', row['seconds'])
    arms = [('confirmation (seed+fallback)', dict(p, yaw=seed), row['seconds']),
            ('seed only, 0.2 s', {k: v for k, v in dict(p, yaw=seed).items() if k != 'fallbackYaw'}, .2),
            ('seed only, request seconds', {k: v for k, v in dict(p, yaw=seed).items() if k != 'fallbackYaw'}, row['seconds'])]
    if 'fallbackYaw' in p:
        arms.append(('fallback only, 0.2 s', {k: v for k, v in dict(p, yaw=p['fallbackYaw']).items() if k != 'fallbackYaw'}, .2))
    for name, request, seconds in arms:
        r = solver.solve(request, seconds=seconds, stop_on_feasible=False, python_executable=sys.executable)
        phases = [(ph['seed'], ph['status'], round(ph['seconds'], 3)) for ph in r['phases']]
        print('   ', name, r['status'], r['termination'], round(r['elapsedSeconds'], 3),
              None if r['candidate'] is None else round(r['candidate']['objective'], 5), phases)
