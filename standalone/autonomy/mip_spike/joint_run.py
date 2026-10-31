import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import time

from standalone.autonomy.compiler import JavaCompiler
from standalone.ipopt import solve, validate
from .joint_contract import compile_allocation, prepare_joint
from .run import acceptance


def accept_joint(case, master, compiler, *, seconds=5, python=None):
    if 'yaw' not in master:
        return dict(accepted=False, reason='no_integer_trajectory')
    compiled = compile_allocation(case, master['allocation'], compiler,
                                  selected_rows=master['selectedRows'], yaw=master['yaw'])
    raw = validate(compiled['problem'], master['yaw'])
    differences = dict(position=max(abs(a - b) for p, q in zip(master['positions'],
        [[xz[0], y, xz[1]] for xz, y in zip(raw['positions'], raw['y'])]) for a, b in zip(p, q)),
        velocity=max(abs(a - b) for p, q in zip(master['velocity'], raw['velocity']) for a, b in zip(p, q)))
    if 'vy' in master:
        differences['verticalVelocity'] = max(abs(a - b) for a, b in zip(master['vy'], raw['vy']))
    if master.get('onGround', raw['onGround']) != raw['onGround']:
        return dict(accepted=False, reason='joint_ground_disagreement')
    if max(differences.values()) > 1e-6:
        return dict(accepted=False, reason='joint_scalar_disagreement', errors=differences)
    raw_check = acceptance(compiled, raw, compiler, seconds)
    refined = solve(compiled['problem'], seconds=seconds, stop_on_feasible=True, python_executable=python)
    checked = acceptance(compiled, validate(compiled['problem'], refined['candidate']['yaw']), compiler, seconds) \
        if refined.get('candidate') else dict(feasible=False)
    return dict(accepted=checked['feasible'], rawAccepted=raw_check['feasible'], errors=differences,
        rawGeometry=raw_check.get('geometryCheck'), refinementStatus=refined['status'],
        refinementTermination=refined.get('termination'), geometry=checked.get('geometryCheck'),
        candidate=checked.get('candidate'), plan=compiled['plan'], problem=compiled['problem'])


def search(case, compiler, *, output, seconds=30, horizon_seconds=8, python=None, optimize=True, horizons=None):
    if any(not math.isfinite(v) or v <= 0 for v in (seconds, horizon_seconds)):
        raise ValueError('Budgets must be finite and positive')
    started = time.monotonic()
    python = python or str(Path.cwd() / '.venv-ipopt/bin/python')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'case.json').write_text(json.dumps(case, indent=2) + '\n')
    attempts, best = [], None
    limits = range(case['minimumHorizon'], case['maximumHorizon'] + 1) if horizons is None else horizons
    for horizon in limits:
        remaining = seconds - (time.monotonic() - started)
        if remaining < .2:
            break
        budget = min(horizon_seconds, remaining * .8)
        prefix = output / str(horizon)
        request_path, result_path = prefix.with_suffix('.input.json'), prefix.with_suffix('.master.json')
        request_path.write_text(json.dumps(dict(case=case, horizon=horizon, optimize=optimize)))
        try:
            process = subprocess.run([python, '-m', 'standalone.autonomy.mip_spike.joint_model',
                '--input', str(request_path), '--output', str(result_path), '--seconds', str(budget)],
                capture_output=True, text=True, timeout=remaining)
        except subprocess.TimeoutExpired:
            attempts.append(dict(horizon=horizon, status='process_deadline', accepted=False))
            break
        if process.returncode or not result_path.exists():
            attempts.append(dict(horizon=horizon, status='error', error=process.stderr[-3000:], accepted=False))
            break
        master = json.loads(result_path.read_text())
        checked = accept_joint(case, master, compiler, seconds=max(.1, min(5, seconds - (time.monotonic() - started))), python=python)
        attempt = dict(horizon=horizon, status=master['status'], accepted=checked['accepted'],
            masterSeconds=master.get('elapsedSeconds'), allocation=master.get('allocation'),
            rawAccepted=checked.get('rawAccepted'), acceptance=checked)
        attempts.append(attempt)
        (output / (str(horizon) + '.acceptance.json')).write_text(json.dumps(checked, indent=2) + '\n')
        print(json.dumps({k: v for k, v in attempt.items() if k != 'acceptance'}), flush=True)
        if checked['accepted']:
            best = dict(master=master, **checked)
            break
    result = dict(status='feasible' if best else 'unknown', best=best, attempts=attempts,
        elapsedSeconds=time.monotonic() - started, completeAllocationsGenerated=0,
        minimumTicksProven=False, globalOptimalityClaimed=False,
        shorterUnknownHorizons=[a['horizon'] for a in (attempts[:-1] if best else attempts)
                                if a['status'] not in ('infeasible', 'domain_infeasible')])
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--horizon-seconds', type=float, default=10)
    parser.add_argument('--horizons', nargs='+', type=int)
    parser.add_argument('--optimize', action='store_true', default=True)
    parser.add_argument('--feasibility-only', action='store_false', dest='optimize')
    parser.add_argument('--compiler', default=os.environ.get('PKC_RUN_TICKS_COMPILER', 'standalone/autonomy/build/compiler.json'))
    args = parser.parse_args()
    with JavaCompiler(json.loads(Path(args.compiler).read_text())) as compiler:
        case = prepare_joint(json.loads(Path(args.input).read_text()), compiler)
        result = search(case, compiler, output=args.output, seconds=args.seconds,
                        horizon_seconds=args.horizon_seconds, optimize=args.optimize, horizons=args.horizons)
    print(json.dumps(dict(status=result['status'], elapsedSeconds=result['elapsedSeconds'],
                          allocation=result['best']['master']['allocation'] if result['best'] else None)))


if __name__ == '__main__':
    main()
