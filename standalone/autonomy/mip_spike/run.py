import argparse
import copy
import json
import math
import os
from pathlib import Path
import subprocess
import time

from standalone.autonomy.block_contacts import floor_checks, solve_contact_branches
from standalone.autonomy.compiler import JavaCompiler
from standalone.autonomy.obstacle_geometry import check_geometry
from standalone.ipopt import prepare, solve, validate
from .cases import CASES, build_case


def acceptance(case, candidate, compiler, seconds):
    if not candidate or not candidate.get('feasible'):
        return dict(feasible=False, reason='scalar_model')
    candidate = copy.deepcopy(candidate)
    problem = case['problem']
    candidate['rawMovement'] = [[(v if abs(v) >= problem['threshold'] else 0) + problem['magnitude'][t] * value
        for v, value in zip(candidate['velocity'][t], (math.cos(problem['base'][t] + math.radians(yaw)),
                                                       math.sin(problem['base'][t] + math.radians(yaw))))]
        for t, yaw in enumerate(candidate['yaw'])]
    candidate['friction'] = problem['friction']
    candidate['movement'] = problem.get('movement', {})
    checked = check_geometry(case['scene']['collisionScene'], case['plan'], candidate, case['vertical'], compiler, seconds)
    return dict(feasible=checked['feasible'], geometryCheck=checked,
                candidate=candidate if checked['feasible'] else None)


def existing(case, compiler, *, seconds, python):
    scene = case['scene']
    started = time.monotonic()
    result = solve_contact_branches(case['route'], case['profiles'],
        floor_checks(scene, [scene['startBlock'], scene['goalBlock']], case['profiles']), compiler,
        seconds=seconds, candidate_seconds=min(2, seconds), retries=1,
        max_branches=scene['maxContactBranches'], geometry=scene['collisionScene'],
        motion_bound=dict(storedSpeedNorm=2, movementAfterSupportCheck=2),
        max_blockages=scene['maxBlockagesPerObstacle'],
        max_horizontal=scene['maxHorizontalBlockagesPerObstacle'], max_vertical=scene['maxVerticalBlockagesPerObstacle'],
        max_passage_candidates=scene['maxPassageCandidates'],
        python_executable=python)
    best = result.get('best')
    checked = acceptance(case, validate(case['problem'], best['candidate']['yaw']), compiler,
                         max(.01, seconds - (time.monotonic() - started))) if best else None
    return dict(method='existing', elapsedSeconds=time.monotonic() - started,
        status=result['status'], termination=result['termination'],
        attempts=len(result.get('attempts', [])), geometryAttempts=len(result.get('contactAttempts', [])),
        accepted=bool(best and best['geometryCheck']['feasible'] and checked['feasible']),
        ticks=best['plan']['ticks'] if best else None, objective=best['candidate']['objective'] if best else None,
        evidence=best, commonAcceptance=checked,
        candidateFamily='existing bounded contact/passage branches; supplied route/Y/contact schedule')


def mixed(case, compiler, *, control, seconds, python, output, max_proposals=3):
    started = time.monotonic()
    deadline = started + seconds
    attempts, forbidden, candidate = [], [], None
    for index in range(max_proposals):
        remaining = deadline - time.monotonic()
        if remaining < .1:
            break
        payload = dict(problem=case['problem'], disjunctions=case['disjunctions'], forbidden=forbidden)
        prefix = output / (case['name'] + '-' + control + '-' + str(index))
        request_path, result_path = prefix.with_suffix('.input.json'), prefix.with_suffix('.output.json')
        request_path.write_text(json.dumps(payload))
        master_seconds = min(remaining * .65, seconds * .65)
        before = time.monotonic()
        command = [python, '-m', 'standalone.autonomy.mip_spike.backend', '--input', str(request_path),
                   '--output', str(result_path), '--seconds', str(master_seconds), '--control', control]
        try:
            process = subprocess.run(command, capture_output=True, text=True, timeout=remaining)
        except subprocess.TimeoutExpired:
            attempts.append(dict(masterStatus='process_deadline'))
            break
        if process.returncode or not result_path.exists():
            attempts.append(dict(masterStatus='error', error=process.stderr[-2000:]))
            break
        master = json.loads(result_path.read_text())
        attempt = dict(master=master, masterProcessSeconds=time.monotonic() - before)
        if master.get('normalizedControls'):
            norms = [math.hypot(*unit) for unit in master['normalizedControls']]
            attempt['controlNormRange'] = [min(norms), max(norms)]
        attempts.append(attempt)
        if not master.get('yaw'):
            break
        raw = validate(case['problem'], master['yaw'])
        attempt['rawScalarFeasible'] = raw['feasible']
        attempt['rawViolation'] = raw['violation']
        if control == 'circle':
            raw_check = acceptance(case, raw, compiler, max(.01, deadline - time.monotonic()))
            attempt['rawGeometryAccepted'] = raw_check['feasible']
        refined_problem = copy.deepcopy(case['problem'])
        refined_problem['walls'].extend(master['selectedRows'])
        refined_problem['yaw'] = master['yaw']
        remaining = deadline - time.monotonic()
        if remaining < .1:
            break
        before = time.monotonic()
        refined = solve(refined_problem, seconds=remaining, stop_on_feasible=True, python_executable=python)
        attempt.update(refinementSeconds=time.monotonic() - before, refinementStatus=refined['status'],
                       refinementTermination=refined.get('termination'))
        if refined.get('candidate'):
            checked = acceptance(case, validate(prepare(refined_problem), refined['candidate']['yaw']), compiler,
                                 max(.01, deadline - time.monotonic()))
            attempt['acceptance'] = {k: v for k, v in checked.items() if k != 'candidate'}
            if checked['feasible']:
                candidate = checked['candidate']
                break
        forbidden.append(master['selectedFaces'])
        if not case['disjunctions']:
            break
    return dict(method='scip-' + control + '-ipopt', elapsedSeconds=time.monotonic() - started,
        accepted=candidate is not None, status='feasible' if candidate else 'unknown',
        ticks=case['plan']['ticks'] if candidate else None, objective=candidate['objective'] if candidate else None,
        attempts=attempts, candidate=candidate, heuristicAssignmentDrops=len(forbidden),
        noGoodCutsAreInfeasibilityProofs=False, globalOptimalityClaimed=False,
        candidateFamily='stagewise finite-box clearance, automatic inertia/clamps; supplied route/Y/contact schedule')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    parser.add_argument('--seconds', type=float, default=15)
    parser.add_argument('--methods', nargs='+', choices=['existing', 'circle', 'disk'], default=['existing', 'circle', 'disk'])
    parser.add_argument('--output', required=True)
    parser.add_argument('--compiler', default=os.environ.get('PKC_RUN_TICKS_COMPILER', 'standalone/autonomy/build/compiler.json'))
    parser.add_argument('--python', default=os.environ.get('PKC_IPOPT_PYTHON', str(Path.cwd() / '.venv-ipopt/bin/python')))
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    results = []
    with JavaCompiler(json.loads(Path(args.compiler).read_text())) as compiler:
        for name in args.cases:
            case = build_case(name, compiler)
            (output / (name + '.case.json')).write_text(json.dumps(case, indent=2) + '\n')
            for method in args.methods:
                if method == 'existing':
                    result = existing(case, compiler, seconds=args.seconds, python=args.python)
                else:
                    result = mixed(case, compiler, control=method, seconds=args.seconds, python=args.python, output=output)
                (output / (name + '-' + method + '.result.json')).write_text(json.dumps(result, indent=2) + '\n')
                item = dict(case=name, preparationSeconds=case['preparationSeconds'],
                    **{k: result[k] for k in ('method', 'accepted', 'status', 'elapsedSeconds', 'ticks', 'objective')})
                results.append(item)
                print(json.dumps(item), flush=True)
                (output / 'summary.json').write_text(json.dumps(dict(results=results, fixedHorizon=True,
                    routeAndYDiscoveryBenchmarked=False, globallyOptimalityClaimed=False,
                    candidateFamiliesDiffer=True, perMethodBudgetSeconds=args.seconds), indent=2) + '\n')


if __name__ == '__main__':
    main()
