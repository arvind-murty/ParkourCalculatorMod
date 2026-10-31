import argparse
import json
import math
from pathlib import Path
import time

from standalone.autonomy.route import HALF_WIDTH, center_bounds
from standalone.autonomy.obstacle_geometry import BODY_HEIGHT
from standalone.ipopt.ladder import REST_CARRY
from standalone.ipopt.model import integer
from .algebra import equal, inertia
from .joint_passages import add_passages, extract_passages
from .joint_bounds import movement_bounds


def departure_domains(case, horizon):
    route = case['route']
    integer(horizon, 'horizon', 1, 200)
    total = horizon - sum(route['airtimes'])
    if total < sum(b[0] for b in route['runBounds']) or total > min(sum(b[1] for b in route['runBounds']), route['maxTotalRunTicks']):
        return []
    result, flight = [], 0
    for leg, duration in enumerate(route['airtimes']):
        before, after = route['runBounds'][:leg + 1], route['runBounds'][leg + 1:]
        lo = max(sum(b[0] for b in before), total - sum(b[1] for b in after))
        hi = min(sum(b[1] for b in before), total - sum(b[0] for b in after))
        result.append(list(range(flight + lo, flight + hi + 1)))
        flight += duration
    return result


def solve_horizon(case, horizon, *, seconds=10, optimize=False, allocation=None, fixed_yaw=None):
    import pyscipopt
    from pyscipopt import Model, quicksum
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError('The solve budget must be finite and positive')
    started = time.monotonic()
    domains = departure_domains(case, horizon)
    if not domains or any(row['t1'] > horizon for row in case['positionRows']):
        return dict(status='domain_infeasible', horizon=horizon, accepted=False, completeAllocationsGenerated=0)
    if allocation is not None:
        from standalone.autonomy.route import schedule
        plan = schedule(case['route'], allocation)
        if plan['ticks'] != horizon:
            raise ValueError('Fixed allocation does not have the requested horizon')
    if fixed_yaw is not None and (len(fixed_yaw) != horizon or not all(math.isfinite(v) for v in fixed_yaw)):
        raise ValueError('fixed_yaw must contain one finite facing per input')
    model = Model('joint_run_ticks_blockages')
    model.hideOutput()
    model.setRealParam('numerics/feastol', 1e-9)
    model.setIntParam('parallel/maxnthreads', 1)
    model.setIntParam('randomization/randomseedshift', 1)
    if not optimize:
        model.setIntParam('limits/solutions', 1)
    route, profiles = case['route'], case['profiles']
    origin = route['initial']['position']
    nlegs = len(profiles)
    depart = [{t: model.addVar(f'depart_{leg}_{t}', vtype='B') for t in ticks} for leg, ticks in enumerate(domains)]
    departures, runs = [], []
    for leg, choices in enumerate(depart):
        model.addCons(quicksum(choices.values()) == 1)
        when = quicksum(t * v for t, v in choices.items())
        prior = departures[-1] + profiles[leg - 1]['airtime'] if leg else 0
        run = when - prior
        lo, hi = route['runBounds'][leg]
        model.addCons(run >= lo)
        model.addCons(run <= hi)
        departures.append(when)
        runs.append(run)
        if allocation is not None:
            model.addCons(run == allocation[leg])
    model.addCons(departures[-1] + profiles[-1]['airtime'] == horizon)
    model.addCons(quicksum(runs) <= route['maxTotalRunTicks'])
    phases, running, mode = [], [], []
    ys, vys, grounded = [], [], []
    for tick in range(horizon):
        phase = [(leg, k, choices[tick - k]) for leg, choices in enumerate(depart)
                 for k in range(profiles[leg]['airtime']) if tick - k in choices]
        run_at = []
        for leg, choices in enumerate(depart):
            arrived = 1 if leg == 0 else quicksum(v for t, v in depart[leg - 1].items()
                                                  if t + profiles[leg - 1]['airtime'] <= tick)
            left = quicksum(v for t, v in choices.items() if t <= tick)
            active = model.addVar(f'run_{leg}_{tick}', vtype='B')
            model.addCons(active == arrived - left)
            run_at.append(active)
        model.addCons(quicksum(run_at) + quicksum(v for leg, k, v in phase) == 1)
        modes = {name: model.addVar(f'mode_{name}_{tick}', vtype='B') for name in ('run', 'jump', 'air')}
        model.addCons(modes['run'] == quicksum(run_at))
        model.addCons(modes['jump'] == quicksum(v for leg, k, v in phase if k == 0))
        model.addCons(modes['air'] == quicksum(v for leg, k, v in phase if k > 0))
        ys.append(quicksum(v * route['blocks'][leg]['top'] for leg, v in enumerate(run_at))
                  + quicksum(v * profiles[leg]['position'][k] for leg, k, v in phase))
        vys.append(quicksum(run_at) * REST_CARRY + quicksum(v * profiles[leg]['velocity'][k] for leg, k, v in phase))
        grounded.append(modes['run'] + modes['jump'])
        phases.append(phase)
        running.append(run_at)
        mode.append(modes)
    ys.append(route['blocks'][-1]['top'])
    vys.append(REST_CARRY)
    grounded.append(1)
    m_max = max(v['magnitude'] for v in case['modes'].values())
    limits, phase_limits = movement_bounds(case, domains, horizon)
    speed = [[values[1], values[1]] for values in limits]
    reach = [[values[0], values[0]] for values in limits]
    geometry = case['scene']['collisionScene']['bounds']
    p, velocity, coordinates, bounds = [], [], [], []
    for tick in range(horizon + 1):
        lo_x, hi_x = max(-reach[tick][0], geometry[0] + HALF_WIDTH - origin[0]), min(reach[tick][0], geometry[3] - HALF_WIDTH - origin[0])
        lo_z, hi_z = max(-reach[tick][1], geometry[2] + HALF_WIDTH - origin[2]), min(reach[tick][1], geometry[5] - HALF_WIDTH - origin[2])
        p.append([model.addVar(f'p_{tick}_{a}', lb=lo, ub=hi) for a, (lo, hi) in enumerate(((lo_x, hi_x), (lo_z, hi_z)))])
        velocity.append([model.addVar(f'v_{tick}_{a}', lb=-speed[tick][a], ub=speed[tick][a]) for a in (0, 1)])
        y_values = [route['blocks'][leg]['top'] for leg in range(nlegs)] + [v for profile in profiles for v in profile['position']]
        y_lo, y_hi = min(y_values), max(y_values)
        y = model.addVar(f'y_{tick}', lb=max(y_lo, geometry[1]), ub=min(y_hi, geometry[4] - BODY_HEIGHT))
        equal(model, y, ys[tick])
        coordinates.append([p[tick][0] + origin[0], y, p[tick][1] + origin[2]])
        bounds.append([(lo_x + origin[0], hi_x + origin[0]), (y_lo, y_hi), (lo_z + origin[2], hi_z + origin[2])])
    for a in (0, 1):
        equal(model, velocity[0][a], route['initial']['velocity'][2 * a])
    for tick in range(horizon + 1):
        model.addCons(p[tick][0] * p[tick][0] + p[tick][1] * p[tick][1] <= limits[tick][0] ** 2)
        model.addCons(velocity[tick][0] * velocity[tick][0] + velocity[tick][1] * velocity[tick][1] <= limits[tick][1] ** 2)
    for (leg, departure), trace in phase_limits.items():
        switch = depart[leg][departure]
        for k, (radius, speed_norm) in enumerate(trace):
            tick = departure + k
            for axis in (0, 1):
                model.addConsIndicator(p[tick][axis] <= radius, binvar=switch)
                model.addConsIndicator(-p[tick][axis] <= radius, binvar=switch)
                model.addConsIndicator(velocity[tick][axis] <= speed_norm, binvar=switch)
                model.addConsIndicator(-velocity[tick][axis] <= speed_norm, binvar=switch)
    controls, inertia_vars = [], []
    for tick in range(horizon):
        u = [model.addVar(f'u_{tick}_{a}', lb=-m_max, ub=m_max) for a in (0, 1)]
        unit = [model.addVar(f'q_{tick}_{a}', lb=-1, ub=1) for a in (0, 1)]
        controls.append(u)
        model.addCons(unit[0] * unit[0] + unit[1] * unit[1] == 1)
        for name, switch in mode[tick].items():
            for axis in (0, 1):
                equal(model, u[axis], case['modes'][name]['magnitude'] * unit[axis], switch)
        if fixed_yaw is not None:
            for name, switch in mode[tick].items():
                entry = case['modes'][name]
                angle = math.radians(fixed_yaw[tick]) + entry['base']
                for axis, direction in enumerate((math.cos(angle), math.sin(angle))):
                    equal(model, u[axis], entry['magnitude'] * direction, switch)
        for axis in (0, 1):
            carry, branches = inertia(model, velocity[tick][axis], speed[tick][axis], .005,
                f'{tick}_{axis}', initial=route['initial']['velocity'][2 * axis] if tick == 0 else None)
            inertia_vars.extend(branches)
            movement = carry + u[axis]
            equal(model, p[tick + 1][axis], p[tick][axis] + movement)
            for name, switch in mode[tick].items():
                equal(model, velocity[tick + 1][axis], case['modes'][name]['friction'] * movement, switch)
        for leg, switch in enumerate(running[tick]):
            support_rows(model, coordinates[tick], route['blocks'][leg], route['supportClearance'], switch, 'run')
        for leg, k, switch in phases[tick]:
            if k == profiles[leg]['airtime'] - 1:
                support_rows(model, coordinates[tick], route['blocks'][leg + 1], route['supportClearance'], switch, 'landing')
    for row in case['positionRows']:
        value = coordinates[row['t1']][0 if row['mode'] == 'X' else 2]
        if row['cmp'] in ('GE', 'EQ'):
            model.addCons(value >= row['rhs'])
        if row['cmp'] in ('LE', 'EQ'):
            model.addCons(value <= row['rhs'])
    passages = add_passages(model, coordinates, bounds, case, horizon)
    objective = route['objective']
    axis = 0 if objective['axis'] == 'X' else 2
    model.setObjective(coordinates[-1][axis], 'maximize' if objective['sense'] == 'MAX' else 'minimize')
    built = time.monotonic()
    model.setRealParam('limits/time', max(.001, seconds - (built - started)))
    model.optimize()
    result = dict(status=str(model.getStatus()), horizon=horizon, buildSeconds=built - started,
        elapsedSeconds=time.monotonic() - started, solveSeconds=model.getSolvingTime(), nodes=model.getNNodes(),
        variables=len(model.getVars(transformed=False)), solutions=model.getNSols(),
        departureChoices=sum(map(len, depart)), inertiaBinaries=len(inertia_vars),
        completeAllocationsGenerated=0, fixedAllocation=allocation is not None, optimizeGoal=optimize,
        backendVersion=pyscipopt.__version__, minimumTicksProven=False, physicalInfeasibilityClaimed=False)
    if model.getNSols():
        solution = model.getBestSol()
        times = [max(choices, key=lambda t: float(solution[choices[t]])) for choices in depart]
        chosen = [t - (times[leg - 1] + profiles[leg - 1]['airtime'] if leg else 0) for leg, t in enumerate(times)]
        names = [max(choices, key=lambda name: float(solution[choices[name]])) for choices in mode]
        acceleration = [[float(solution[v]) for v in u] for u in controls]
        selected, rows = extract_passages(solution, passages, horizon, initial=origin)
        value_of = lambda expression: float(expression) if isinstance(expression, (int, float)) else float(solution[expression])
        result.update(allocation=chosen, departures=times, modes=names, acceleration=acceleration,
            yaw=[math.degrees(math.atan2(u[1], u[0]) - case['modes'][name]['base']) for name, u in zip(names, acceleration)],
            positions=[[float(solution[v]) for v in row] for row in coordinates],
            velocity=[[float(solution[v]) for v in row] for row in velocity],
            y=[float(solution[row[1]]) for row in coordinates],
            vy=[value_of(v) for v in vys], onGround=[value_of(v) > .5 for v in grounded],
            selectedPassages=selected, selectedRows=rows, objective=float(model.getSolObjVal(solution)))
    return result


def support_rows(model, coordinates, block, clearance, switch, kind):
    bounds = center_bounds(block, kind)
    for axis, lo, hi in ((0, bounds[0], bounds[1]), (2, bounds[2], bounds[3])):
        model.addConsIndicator(-coordinates[axis] <= -math.nextafter(lo, math.inf) - clearance, binvar=switch)
        model.addConsIndicator(coordinates[axis] <= math.nextafter(hi, -math.inf) - clearance, binvar=switch)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--seconds', type=float, default=10)
    args = parser.parse_args()
    request = json.loads(Path(args.input).read_text())
    result = solve_horizon(request['case'], request['horizon'], seconds=args.seconds,
                           optimize=request.get('optimize', False), allocation=request.get('allocation'))
    Path(args.output).write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')


if __name__ == '__main__':
    main()
