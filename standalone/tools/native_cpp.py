"""Build the native C++ core and verify it against the Python oracle on live solves."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from tools.paths import STANDALONE_ROOT
from tools.profiling.parity import replacements

SOURCE = STANDALONE_ROOT / 'native/cpp'
BUILDS = dict(release=STANDALONE_ROOT / 'build/native/cpp', sanitize=STANDALONE_ROOT / 'build/native/cpp-sanitize')


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def load(variant='release'):
    """Import the built extension; raises ImportError naming the build command when it is missing."""
    from parkour_solver import native_core
    try:
        return native_core.load(BUILDS[variant])
    except ImportError as error:
        raise ImportError(f'Native core not built; run python -m tools.native_cpp build ({error})') from None


def build(args):
    directory = BUILDS[args.variant]
    configure = ['cmake', '-S', str(SOURCE), '-B', str(directory), f'-DPython_EXECUTABLE={sys.executable}',
                 f'-DPKC_SANITIZE={"ON" if args.variant == "sanitize" else "OFF"}']
    log = []
    for command in (configure, ['cmake', '--build', str(directory), '-j', str(os.cpu_count() or 4)]):
        result = subprocess.run(command, capture_output=True, text=True)
        log.append(dict(command=command, exitCode=result.returncode, output=result.stdout + result.stderr))
        if result.returncode:
            print(result.stdout + result.stderr, file=sys.stderr)
            raise SystemExit(f'native build failed: {" ".join(command)}')
    binary = next(directory.glob('_pkc_native*.so'))
    sources = sorted(p for p in SOURCE.rglob('*') if p.is_file())
    record = dict(variant=args.variant, python=sys.version, binary=str(binary),
                  binarySHA256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  compiler=subprocess.check_output(['c++', '--version'], text=True),
                  sourceSHA256={str(p.relative_to(SOURCE)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                  commands=log)
    if args.output:
        save(args.output / 'build.json', record)
    print(json.dumps({k: record[k] for k in ('variant', 'binary', 'binarySHA256')}))


def checked_solve(args, originals, checked):
    """Run the fixture's real solve with checked replacements installed; return the shared summary fields."""
    from parkour_solver.bridge.python_compiler import PythonCompiler
    from parkour_solver.io.provenance import source_hashes, source_revision
    from parkour_solver.search.application import run as solve_task
    fixture = json.loads(args.input.read_text())
    fixture['settings']['seconds'] = args.seconds
    before = source_hashes()
    with replacements(originals, checked), PythonCompiler() as compiler:
        result, validation = solve_task(fixture, compiler, python_executable=sys.executable)
    best = result.get('best')
    return dict(sourceRevision=source_revision(), sourceUnchanged=before == source_hashes(), input=str(args.input),
                seconds=args.seconds, variant=args.variant, status=result['status'],
                termination=result['termination'], ticks=best['plan']['ticks'] if best else None,
                objective=best['candidate']['objective'] if best else None,
                validationPassed=[bool(v.get('passed')) for v in validation])


def verify_digest(args):
    """Run a real solve where every canonical.digest call is also computed natively and compared."""
    from parkour_solver import canonical
    args.output.mkdir(parents=True, exist_ok=False)
    native = load(args.variant)
    original = canonical.digest
    calls = dict(count=0, nativeSeconds=0., pythonSeconds=0.)

    def checked(value):
        started = time.perf_counter()
        expected = original(value)
        middle = time.perf_counter()
        actual = native.digest(value)
        calls['pythonSeconds'] += middle - started
        calls['nativeSeconds'] += time.perf_counter() - middle
        calls['count'] += 1
        if actual != expected:
            save(args.output / 'mismatch.json', dict(ordinal=calls['count'], value=value, python=expected, native=actual))
            raise AssertionError(f'native digest differs at call {calls["count"]}')
        return expected

    summary = checked_solve(args, dict(digest=original), dict(digest=checked))
    summary.update(digestCalls=calls['count'], mismatches=0,
                   timing=dict(pythonDigestSeconds=calls['pythonSeconds'], nativeDigestSeconds=calls['nativeSeconds'],
                               note='Per-call wall time inside the checked wrapper; native includes Python-to-C++ conversion.'))
    save(args.output / 'summary.json', summary)
    print(json.dumps(summary))


def verify_programs(args):
    """Run a real solve where every program and movement choice movement/program.py builds round-trips natively."""
    from parkour_solver.movement import program
    args.output.mkdir(parents=True, exist_ok=False)
    native = load(args.variant)
    builders = dict(empty_program='program', append_choice='program', truncate_program='program',
                    movement_choice='choice')
    originals = {name: getattr(program, name) for name in builders}
    counts = dict(program=0, choice=0)

    def checked(name):
        def call(*positional, **keywords):
            value = originals[name](*positional, **keywords)
            if value is not None:
                kind = builders[name]
                expected = canonical_bytes(value)
                actual = canonical_bytes(getattr(native, 'round_trip_' + kind)(value))
                counts[kind] += 1
                if actual != expected:
                    save(args.output / 'mismatch.json', dict(builder=name, value=value))
                    raise AssertionError(f'native {kind} differs after {counts[kind]} round trips')
            return value
        return call

    summary = checked_solve(args, originals, {name: checked(name) for name in builders})
    summary.update(roundTrips=counts, mismatches=0)
    save(args.output / 'summary.json', summary)
    print(json.dumps(summary))


def verify_gate(args):
    """Run a real solve where every InteractionGate.prepare result is also prepared natively and compared."""
    from parkour_solver.reachability.service import InteractionGate
    args.output.mkdir(parents=True, exist_ok=False)
    native = load(args.variant)
    os.environ.pop('PKC_NATIVE_CORE', None)  # the gate under check must build with Python, the oracle
    original = InteractionGate.prepare
    calls = dict(count=0)

    def checked(gate, program, *, scene=None, require_complete=False):
        expected = original(gate, program, scene=scene, require_complete=require_complete)
        actual = native.gate_prepare(gate.scene if scene is None else scene, program, require_complete)
        built = None if expected is None else {k: v for k, v in expected.items() if k != 'key'}  # key: the gate's
        calls['count'] += 1
        if canonical_bytes(actual) != canonical_bytes(built):
            save(args.output / 'mismatch.json', dict(program=program, requireComplete=require_complete))
            raise AssertionError(f'native gate preparation differs at call {calls["count"]}')
        return expected

    InteractionGate.prepare = checked
    try:
        summary = checked_solve(args, {}, {})
    finally:
        InteractionGate.prepare = original
    summary.update(prepareCalls=calls['count'], mismatches=0)
    save(args.output / 'summary.json', summary)
    print(json.dumps(summary))


def verify_coefficients(args):
    """Run a real solve where every PythonCompiler.compile reply is also compiled natively and compared."""
    from parkour_solver.bridge.compiler import coefficient_request
    from parkour_solver.bridge.python_compiler import PythonCompiler
    args.output.mkdir(parents=True, exist_ok=False)
    native = load(args.variant)
    original = PythonCompiler.compile
    calls = dict(count=0)

    def checked(compiler, route, plan, seconds):
        expected = original(compiler, route, plan, seconds)
        request = coefficient_request(route, plan)
        calls['count'] += 1
        if canonical_bytes(native.compile_coefficients(request)) != canonical_bytes(expected):
            save(args.output / 'mismatch.json', dict(request=request))
            raise AssertionError(f'native coefficients differ at call {calls["count"]}')
        return expected

    PythonCompiler.compile = checked
    try:
        summary = checked_solve(args, {}, {})
    finally:
        PythonCompiler.compile = original
    summary.update(compileCalls=calls['count'], mismatches=0)
    save(args.output / 'summary.json', summary)
    print(json.dumps(summary))


def verify_envelopes(args):
    """Run a real solve where every reachable_envelopes result is also built natively and compared."""
    from parkour_solver.reachability import intervals
    args.output.mkdir(parents=True, exist_ok=False)
    native = load(args.variant)
    original = intervals.reachable_envelopes
    calls = dict(count=0)

    def checked(problem, y=None, *, parent_ticks=None):
        expected = original(problem, y, parent_ticks=parent_ticks)
        actual = native.reachable_envelopes(problem, y, parent_ticks)
        calls['count'] += 1
        # Envelopes hold infinities, so compare with JSON's Infinity; `model` aliases the input.
        if json.dumps(actual, sort_keys=True) != json.dumps({k: v for k, v in expected.items() if k != 'model'},
                                                             sort_keys=True):
            save_infinite(args.output / 'mismatch.json', dict(problem=problem, y=y, parentTicks=parent_ticks))
            raise AssertionError(f'native envelopes differ at call {calls["count"]}')
        return expected

    summary = checked_solve(args, dict(reachable_envelopes=original), dict(reachable_envelopes=checked))
    summary.update(envelopeCalls=calls['count'], mismatches=0)
    save(args.output / 'summary.json', summary)
    print(json.dumps(summary))


def save_infinite(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')


def canonical_bytes(value):
    return json.dumps(value, sort_keys=True, allow_nan=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('build')
    p.add_argument('--variant', choices=sorted(BUILDS), default='release')
    p.add_argument('--output', type=Path, help='directory for build.json provenance')
    p = commands.add_parser('verify-digest')
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--seconds', type=float, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--variant', choices=sorted(BUILDS), default='release')
    p = commands.add_parser('verify-programs')
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--seconds', type=float, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--variant', choices=sorted(BUILDS), default='release')
    p = commands.add_parser('verify-gate')
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--seconds', type=float, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--variant', choices=sorted(BUILDS), default='release')
    p = commands.add_parser('verify-envelopes')
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--seconds', type=float, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--variant', choices=sorted(BUILDS), default='release')
    p = commands.add_parser('verify-coefficients')
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--seconds', type=float, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--variant', choices=sorted(BUILDS), default='release')
    args = parser.parse_args()
    dict(build=build, verify_digest=verify_digest, verify_programs=verify_programs, verify_gate=verify_gate,
         verify_coefficients=verify_coefficients, verify_envelopes=verify_envelopes)[args.command.replace('-', '_')](args)


if __name__ == '__main__':
    main()
