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
from standalone.autonomy.movement_units import profile_program
from standalone.ipopt import validate
from .joint_contract import compile_allocation, prepare_joint
from .joint_run import accept_joint
from .run import acceptance
from .scaling_cases import CASE_NAMES, allocation_count, scaling_request


class SceneObserver:
    def __init__(self, compiler, case, started):
        self.compiler, self.case, self.started = compiler, case, started
        self.curve, self.firstScene = [], None

    def __getattr__(self, name):
        return getattr(self.compiler, name)

    def check_scene(self, scene, positions, movement, ground, seconds, **contract):
        result = self.compiler.check_scene(scene, positions, movement, ground, seconds, **contract)
        if result['feasible'] and len(movement) >= self.case['minimumHorizon']:
            if 'velocities' not in contract:
                raise ValueError('Comparison requires the full velocity scene contract')
            objective = self.case['route']['objective']
            self.curve.append(dict(availableSeconds=time.monotonic() - self.started, ticks=len(movement),
                                   objective=positions[-1][0 if objective['axis'] == 'X' else 2]))
            if self.firstScene is None:
                self.firstScene = dict(scene=scene, positions=positions, movement=movement, ground=ground, contract=contract)
        return result


def existing_method(case, compiler, *, seconds, python):
    started = time.monotonic()
    observer = SceneObserver(compiler, case, started)
    route = copy.deepcopy(case['route'])
    profiles = copy.deepcopy(case['profiles'])
    for profile in profiles:
        profile['composition'] = 'movement_program'
        profile['generatedUnits'] = profile_program(profile)
    route['verticalProfiles'] = profiles
    scene, modes = case['scene'], case['modes']
    speed = max(math.hypot(*route['initial']['velocity'][::2]),
                max(v['magnitude'] * v['friction'] / (1 - v['friction']) for v in modes.values()))
    motion = dict(storedSpeedNorm=speed, movementAfterSupportCheck=speed + max(v['magnitude'] for v in modes.values()),
                  groundMagnitude=modes['run']['magnitude'], groundFriction=modes['run']['friction'], flightBounds={})
    for profile in profiles:
        n = profile['airtime']
        motion['flightBounds']['jump:' + str(n)] = {key: [modes['jump'][key]] + [modes['air'][key]] * (n - 1)
                                                   for key in ('magnitude', 'friction')}
    source_ids = [b['sourceBlock'] for b in scene['blocks']]
    result = solve_contact_branches(route, profiles,
        floor_checks(scene, [b['id'] for b in route['blocks']], profiles), observer,
        seconds=max(.01, seconds - 1), candidate_seconds=2, retries=1, rank_equal_cost=True, optimize_goal=True,
        max_branches=scene['maxContactBranches'], geometry=scene['collisionScene'], motion_bound=motion,
        max_blockages=scene['maxBlockagesPerObstacle'], max_horizontal=scene['maxHorizontalBlockagesPerObstacle'],
        max_vertical=scene['maxVerticalBlockagesPerObstacle'], max_passage_candidates=scene['maxPassageCandidates'],
        solid_blocks=source_ids, max_wall_hits=0, python_executable=python)
    best, common = result.get('best'), None
    if best:
        compiled = compile_allocation(case, best['allocation'], compiler,
            selected_rows=best['plan']['walls'], yaw=best['candidate']['yaw'])
        common = acceptance(compiled, validate(compiled['problem'], best['candidate']['yaw']), compiler,
                            max(.1, seconds - (time.monotonic() - started)))
        if common['feasible']:
            common.update(problem=compiled['problem'], plan=compiled['plan'])
    good = bool(common and common['feasible'])
    return dict(method='existing', accepted=good, status=result['status'], termination=result.get('termination'),
        elapsedSeconds=time.monotonic() - started,
        firstValidCandidateAvailableSeconds=observer.curve[0]['availableSeconds'] if observer.curve else None,
        firstValidatedSeconds=observer.curve[0]['availableSeconds'] if observer.curve else None,
        availabilityTiming='actual full scene check', curve=observer.curve, firstScene=observer.firstScene,
        ticks=best['plan']['ticks'] if good else None, objective=common['candidate']['objective'] if good else None,
        allocation=best['allocation'] if good else None, evidence=common if good else None,
        allocationProbes=len(result.get('attempts', [])),
        completeAllocationProbes=sum(a.get('stage') == 'jump' and len(a.get('allocation', [])) == len(profiles)
                                     for a in result.get('attempts', [])),
        contactBranches=len(result.get('contactAttempts', [])), result=result)


def joint_method(case, compiler, *, seconds, horizon_seconds, python, output):
    started = time.monotonic()
    attempts, curve, best = [], [], None
    for horizon in range(case['minimumHorizon'], case['maximumHorizon'] + 1):
        remaining = seconds - (time.monotonic() - started)
        if remaining < 1:
            break
        before = time.monotonic()
        budget = min(horizon_seconds, remaining - .5)
        request = output / (str(horizon) + '.input.json')
        destination = output / (str(horizon) + '.master.json')
        request.write_text(json.dumps(dict(case=case, horizon=horizon, optimize=True, captureIncumbents=True)))
        try:
            process = subprocess.run([python, '-m', 'standalone.autonomy.mip_spike.joint_model',
                '--input', str(request), '--output', str(destination), '--seconds', str(budget)],
                capture_output=True, text=True, timeout=remaining)
        except subprocess.TimeoutExpired:
            attempts.append(dict(horizon=horizon, status='process_deadline'))
            break
        if process.returncode or not destination.exists():
            attempts.append(dict(horizon=horizon, status='error', error=process.stderr[-3000:]))
            break
        master = json.loads(destination.read_text())
        process_elapsed = time.monotonic() - before
        overhead = max(0, process_elapsed - master.get('elapsedSeconds', process_elapsed))
        checked = accept_joint(case, master, compiler, seconds=max(.1, min(1, seconds - (time.monotonic() - started))), python=python)
        candidates = master.get('incumbents', []) + ([dict(availableSeconds=master['elapsedSeconds'], candidate=master)] if 'yaw' in master else [])
        first = None
        validations = []
        for incumbent in candidates:
            check_start = time.monotonic()
            validation = accept_joint(case, incumbent['candidate'], compiler, seconds=1, python=python)
            validations.append(dict(availableSeconds=incumbent['availableSeconds'], accepted=validation['accepted'],
                                    rawAccepted=validation.get('rawAccepted'), reason=validation.get('reason')))
            if validation['accepted']:
                available = before - started + overhead + incumbent['availableSeconds'] + (time.monotonic() - check_start)
                if not validation.get('rawAccepted'):
                    available = time.monotonic() - started
                item = dict(availableSeconds=available, validatedSeconds=time.monotonic() - started,
                            ticks=horizon, objective=validation['candidate']['objective'])
                curve.append(item)
                first = dict(master=incumbent['candidate'], **validation)
                break
        if checked['accepted']:
            best = dict(master={k: v for k, v in master.items() if k != 'incumbents'}, **checked)
        elif first:
            best = first
        attempts.append(dict(horizon=horizon, status=master['status'], accepted=best is not None,
            statistics={k: master.get(k) for k in ('buildSeconds', 'solveSeconds', 'nodes', 'variables', 'departureChoices',
                'solutions', 'root', 'lpIterations', 'lpSolves', 'dualBound', 'primalBound', 'gap', 'uncapturedIncumbents')},
            capturedIncumbents=len(master.get('incumbents', [])), validations=validations,
            acceptance={k: v for k, v in checked.items() if k not in ('candidate', 'plan', 'problem')}))
        if best:
            break
    return dict(method='joint', accepted=best is not None, status='feasible' if best else 'unknown',
        elapsedSeconds=time.monotonic() - started,
        firstValidCandidateAvailableSeconds=curve[0]['availableSeconds'] if curve else None,
        firstValidatedSeconds=curve[0]['validatedSeconds'] if curve else None,
        availabilityTiming='SCIP incumbent timestamp plus estimated process overhead and measured validation; validated after horizon optimization',
        curve=curve, ticks=best['plan']['ticks'] if best else None,
        objective=best['candidate']['objective'] if best else None,
        allocation=best['plan']['allocation'] if best else None, evidence=best, attempts=attempts,
        completeAllocationsGenerated=0, minimumTicksProven=False, globalObjectiveOptimalityClaimed=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--cases', nargs='+', choices=CASE_NAMES, default=list(CASE_NAMES))
    parser.add_argument('--seconds', type=float, default=20)
    parser.add_argument('--horizon-seconds', type=float, default=6)
    parser.add_argument('--repeats', type=int, default=2)
    parser.add_argument('--compiler', default=os.environ.get('PKC_RUN_TICKS_COMPILER', 'standalone/autonomy/build/compiler.json'))
    args = parser.parse_args()
    if any(not math.isfinite(v) or v <= 0 for v in (args.seconds, args.horizon_seconds)) or args.repeats < 1:
        parser.error('Budgets and repeat count must be positive')
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    python = str(Path.cwd() / '.venv-ipopt/bin/python')
    records = []
    with JavaCompiler(json.loads(Path(args.compiler).read_text())) as compiler:
        for name in args.cases:
            request, provenance = scaling_request(name)
            prepared = time.monotonic()
            case = prepare_joint(request, compiler)
            preparation_seconds = time.monotonic() - prepared
            base = output / name
            base.mkdir(exist_ok=True)
            (base / 'request.json').write_text(json.dumps(request, indent=2) + '\n')
            (base / 'case.json').write_text(json.dumps(case, indent=2) + '\n')
            for repeat in range(args.repeats):
                methods = ('existing', 'joint') if repeat % 2 == 0 else ('joint', 'existing')
                for method in methods:
                    directory = base / (str(repeat) + '-' + method)
                    directory.mkdir(exist_ok=True)
                    if method == 'existing':
                        result = existing_method(case, compiler, seconds=args.seconds, python=python)
                    else:
                        result = joint_method(case, compiler, seconds=args.seconds, horizon_seconds=args.horizon_seconds,
                                              python=python, output=directory)
                    (directory / 'result.json').write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
                    item = dict(case=name, repeat=repeat, methodOrder=list(methods), preparationSeconds=preparation_seconds,
                        jumps=len(case['profiles']), allocationDomainSize=allocation_count(case['route']['runBounds'], case['route']['maxTotalRunTicks']),
                        **{k: result.get(k) for k in ('method', 'accepted', 'status', 'elapsedSeconds', 'firstValidCandidateAvailableSeconds',
                            'firstValidatedSeconds', 'ticks', 'objective', 'allocation', 'availabilityTiming', 'allocationProbes',
                            'completeAllocationProbes', 'contactBranches')}, provenance=provenance)
                    records.append(item)
                    print(json.dumps(item), flush=True)
                    (output / 'summary.json').write_text(json.dumps(dict(results=records, searchBudgetSeconds=args.seconds,
                        horizonSeconds=args.horizon_seconds, objectivePolicy='ticks then positional objective; bounded search',
                        initialSavedYawOrAllocationUsed=False, candidateRepresentationsDiffer=True,
                        firstJointAvailabilityIsPostvalidatedEstimate=True), indent=2) + '\n')


if __name__ == '__main__':
    main()
