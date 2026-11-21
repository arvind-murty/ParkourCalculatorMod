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
    directory = str(BUILDS[variant])
    if directory not in sys.path:
        sys.path.insert(0, directory)
    try:
        import _pkc_native
    except ImportError as error:
        raise ImportError(f'Native core not built; run python -m tools.native_cpp build ({error})') from None
    return _pkc_native


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


def verify_digest(args):
    """Run a real solve where every canonical.digest call is also computed natively and compared."""
    from parkour_solver import canonical
    from parkour_solver.bridge.python_compiler import PythonCompiler
    from parkour_solver.io.provenance import source_hashes, source_revision
    from parkour_solver.search.application import run as solve_task
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

    fixture = json.loads(args.input.read_text())
    fixture['settings']['seconds'] = args.seconds
    before = source_hashes()
    with replacements(dict(digest=original), dict(digest=checked)), PythonCompiler() as compiler:
        result, validation = solve_task(fixture, compiler, python_executable=sys.executable)
    best = result.get('best')
    summary = dict(sourceRevision=source_revision(), sourceUnchanged=before == source_hashes(), input=str(args.input),
                   seconds=args.seconds, variant=args.variant, digestCalls=calls['count'], mismatches=0,
                   status=result['status'], termination=result['termination'],
                   ticks=best['plan']['ticks'] if best else None,
                   objective=best['candidate']['objective'] if best else None,
                   validationPassed=[bool(v.get('passed')) for v in validation],
                   timing=dict(pythonDigestSeconds=calls['pythonSeconds'], nativeDigestSeconds=calls['nativeSeconds'],
                               note='Per-call wall time inside the checked wrapper; native includes Python-to-C++ conversion.'))
    save(args.output / 'summary.json', summary)
    print(json.dumps(summary))


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
    args = parser.parse_args()
    dict(build=build, verify_digest=verify_digest)[args.command.replace('-', '_')](args)


if __name__ == '__main__':
    main()
