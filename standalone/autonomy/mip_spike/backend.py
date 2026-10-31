import argparse
import json
import math
from pathlib import Path
import time

from standalone.ipopt import prepare
from standalone.ipopt.ladder import CAP
from .geometry import selected_rows
from .algebra import inertia


def solve(request, *, seconds=10, control='circle', forbidden=()):
    import pyscipopt
    from pyscipopt import Model, quicksum
    if control not in ('circle', 'disk'):
        raise ValueError('Choose circle or disk controls')
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError('The solve budget must be finite and positive')
    problem = prepare(request['problem'])
    if any(row['mode'] not in ('X', 'Z') for row in problem['walls']):
        raise ValueError('The spike supports X/Z positional rows and free facing')
    started = time.monotonic()
    n, origin = problem['ticks'], problem['position']
    model = Model('parkour_' + control)
    model.hideOutput()
    model.setRealParam('numerics/feastol', 1e-9)
    model.setIntParam('parallel/maxnthreads', 1)
    model.setIntParam('randomization/randomseedshift', 1)
    model.setIntParam('limits/solutions', 1)
    contacts = {(c['state'], c['axis']): c for c in problem['contacts']}
    ladder = problem.get('ladderTicks', [False] * (n + 1))
    threshold = problem['threshold']
    p, v, q, geom, regime = [], [], [], [], []
    bounds_p, bounds_v = [[0., 0.]], [[abs(value) for value in problem['velocity']]]
    for tick in range(n):
        move = [value + problem['magnitude'][tick] for value in bounds_v[-1]]
        if ladder[tick]:
            move = [min(value, CAP) for value in move]
        bounds_p.append([bounds_p[-1][a] + move[a] for a in (0, 1)])
        bounds_v.append([0. if (tick + 1, a) in contacts else move[a] * problem['friction'][tick] for a in (0, 1)])
    for tick in range(n + 1):
        p.append([model.addVar(f'p_{tick}_{a}', lb=-bounds_p[tick][a], ub=bounds_p[tick][a]) for a in (0, 1)])
        v.append([model.addVar(f'v_{tick}_{a}', lb=-bounds_v[tick][a], ub=bounds_v[tick][a]) for a in (0, 1)])
    for axis in (0, 1):
        model.addCons(v[0][axis] == problem['velocity'][axis])

    def conditional(row, switch):
        model.addConsIndicator(row, binvar=switch)

    def equal(lhs, rhs, switch=None):
        if switch is None:
            model.addCons(lhs == rhs)
        else:
            conditional(lhs - rhs <= 0, switch)
            conditional(rhs - lhs <= 0, switch)

    def choice(name, count):
        values = [model.addVar(name + '_' + str(i), vtype='B') for i in range(count)]
        model.addCons(quicksum(values) == 1)
        return values

    for tick in range(n):
        unit = [model.addVar(f'q_{tick}_{a}', lb=-1, ub=1) for a in (0, 1)]
        q.append(unit)
        squared = unit[0] * unit[0] + unit[1] * unit[1]
        model.addCons(squared == 1 if control == 'circle' else squared <= 1)
        for axis in (0, 1):
            carry, modes = inertia(model, v[tick][axis], bounds_v[tick][axis], threshold,
                                   f'{tick}_{axis}', initial=problem['velocity'][axis] if tick == 0 else None)
            regime.extend(modes)
            raw = carry + problem['magnitude'][tick] * unit[axis]
            movement = raw
            if ladder[tick]:
                movement = model.addVar(f'move_{tick}_{axis}', lb=-CAP, ub=CAP)
                modes = choice(f'clamp_{tick}_{axis}', 3)
                regime.extend(modes)
                conditional(raw <= CAP, modes[0])
                conditional(-raw <= CAP, modes[0])
                equal(movement, raw, modes[0])
                conditional(-raw <= -CAP, modes[1])
                equal(movement, CAP, modes[1])
                conditional(raw <= -CAP, modes[2])
                equal(movement, -CAP, modes[2])
            contact = contacts.get((tick + 1, axis))
            if contact is None:
                equal(p[tick + 1][axis], p[tick][axis] + movement)
                equal(v[tick + 1][axis], problem['friction'][tick] * movement)
            else:
                plane, sign = contact['plane'] - origin[axis], contact['direction']
                equal(p[tick + 1][axis], plane)
                equal(v[tick + 1][axis], 0)
                model.addCons(sign * (p[tick][axis] + movement - plane) >= 1e-7)
                model.addCons(sign * (p[tick][axis] - plane) <= 0)

    def expression(row):
        axis = 0 if row['mode'] == 'X' else 1
        value = p[row['t1']][axis] + origin[axis]
        if row.get('t2') is not None:
            value += (1 if row.get('op') == 'PLUS' else -1) * (p[row['t2']][axis] + origin[axis])
        return value

    def add_row(row, switch=None):
        value, rhs = expression(row), row['rhs']
        if row['cmp'] in ('LE', 'EQ'):
            condition = value <= rhs
            model.addCons(condition) if switch is None else conditional(condition, switch)
        if row['cmp'] in ('GE', 'EQ'):
            condition = -value <= -rhs
            model.addCons(condition) if switch is None else conditional(condition, switch)

    for row in problem['walls']:
        add_row(row)
    for index, group in enumerate(request.get('disjunctions', [])):
        switches = choice('face_' + str(index), len(group['options']))
        geom.append(switches)
        for option, switch in zip(group['options'], switches):
            for row in option['rows']:
                add_row(row, switch)
    for selected in forbidden:
        if len(selected) != len(geom):
            raise ValueError('Forbidden assignment has the wrong number of stages')
        model.addCons(quicksum(values[index] for values, index in zip(geom, selected)) <= len(geom) - 1)
    objective = problem['objective']
    axis = 0 if objective['axis'] == 'X' else 1
    model.setObjective(p[objective['tick']][axis], 'maximize' if objective['sense'] == 'MAX' else 'minimize')
    built = time.monotonic()
    model.setRealParam('limits/time', max(.001, seconds - (built - started)))
    model.optimize()
    result = dict(status=str(model.getStatus()), control=control, buildSeconds=built - started,
        solveSeconds=model.getSolvingTime(), elapsedSeconds=time.monotonic() - started,
        nodes=model.getNNodes(), variables=len(model.getVars(transformed=False)), binaries=len(regime) + sum(map(len, geom)),
        geometryDisjunctions=len(geom), solutions=model.getNSols(), backend='pyscipopt', backendVersion=pyscipopt.__version__,
        scipVersion='.'.join(str(getattr(model, method)()) for method in ('getMajorVersion', 'getMinorVersion', 'getTechVersion')),
        minimumTicksProven=False, globalModelProofClaimed=False, inertiaNumericalGuard=1e-8)
    if model.getNSols():
        solution = model.getBestSol()
        controls = [[float(solution[value]) for value in unit] for unit in q]
        selected = [max(range(len(values)), key=lambda i: float(solution[values[i]])) for values in geom]
        result.update(yaw=[math.degrees(math.atan2(values[1], values[0]) - problem['base'][tick]) for tick, values in enumerate(controls)],
            normalizedControls=controls, selectedFaces=selected, selectedRows=selected_rows(request.get('disjunctions', []), selected),
            positions=[[float(solution[value]) + origin[a] for a, value in enumerate(row)] for row in p],
            velocity=[[float(solution[value]) for value in row] for row in v],
            objective=float(model.getSolObjVal(solution)) + origin[axis])
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--seconds', type=float, default=10)
    parser.add_argument('--control', choices=['circle', 'disk'], default='circle')
    args = parser.parse_args()
    request = json.loads(Path(args.input).read_text())
    result = solve(request, seconds=args.seconds, control=args.control, forbidden=request.get('forbidden', []))
    Path(args.output).write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')


if __name__ == '__main__':
    main()
