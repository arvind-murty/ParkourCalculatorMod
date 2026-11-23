"""Kept-alive IPOPT worker against a worker per call: the production confirmation seed (disk directions turned 1e-4 rad,
`ipopt_seed`) on the first N requests of an alarmed-request population, through `numerics.solver.solve` with and without
`keep_alive`, alternated per request. The kept-alive worker is launched once by an untimed warm-up call.
Usage (from standalone/): python -B research/post-alarm/keep_alive_bench.py <population.json> [N]"""
import json
import statistics
import sys
import time

from parkour_solver.numerics import solver
from parkour_solver.numerics.disk import ipopt_seed
from parkour_solver.validation.numerical import validate_controls


def main():
    population = json.load(open(sys.argv[1]))
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    rows = list(population.values())[:count]
    plain, kept, same = [], [], 0

    def request(row):
        problem = dict(row['problem'], objectiveMode='configured')
        return dict(problem, yaw=ipopt_seed(validate_controls(problem, row['disk']['controls'])['yaw']))

    solver.solve(request(rows[0]), seconds=20, stop_on_feasible=False, python_executable=sys.executable, keep_alive=True)
    for row in rows:
        started = time.perf_counter()
        a = solver.solve(request(row), seconds=20, stop_on_feasible=False, python_executable=sys.executable)
        plain.append(time.perf_counter()-started)
        started = time.perf_counter()
        b = solver.solve(request(row), seconds=20, stop_on_feasible=False, python_executable=sys.executable,
                         keep_alive=True)
        kept.append(time.perf_counter()-started)
        same += a['candidate'] == b['candidate']
        print(row['name'], a['status'], 'per call %.3f s' % plain[-1], 'kept alive %.3f s' % kept[-1],
              'reused', b['workerReused'], 'same candidate', a['candidate'] == b['candidate'], flush=True)
    print('requests %d, same candidate %d; median per call %.3f s, kept alive %.3f s; mean %.3f / %.3f s' % (
        len(rows), same, statistics.median(plain), statistics.median(kept), statistics.mean(plain), statistics.mean(kept)))


if __name__ == '__main__':
    main()
