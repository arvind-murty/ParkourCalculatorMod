"""Build, validate and time a research C++ port of the live pruning boundary."""
import argparse
from contextlib import contextmanager
import copy
from datetime import datetime
import gzip
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import pickle
import random
import statistics
import struct
import subprocess
import sys
import sysconfig
import time
import unittest
from zoneinfo import ZoneInfo

from tools.paths import STANDALONE_ROOT
from parkour_solver.reachability import intervals
from parkour_solver.io.provenance import source_hashes, source_revision

ROOT = STANDALONE_ROOT
PILOT = ROOT / 'research/cpp-pruning-pilot'
BUILD = ROOT / 'build/cpp-pruning-pilot'
NAMES = ('reachable_envelopes', 'interaction_reachability', 'interaction_reachability_many')
ORIGINAL = {name: getattr(intervals, name) for name in NAMES}
spec = importlib.util.spec_from_file_location('_pruning_python_oracle', intervals.__file__)
ORACLE = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ORACLE)


def stamp():
    return datetime.now(ZoneInfo('America/Los_Angeles')).isoformat()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def native():
    sys.path.insert(0, str(BUILD))
    import _pruning_native
    return _pruning_native




def exact(a, b, path='result'):
    if isinstance(a, dict):
        if not isinstance(b, dict) or a.keys() != b.keys():
            raise AssertionError(f'{path}: dictionary keys differ')
        for key in a:
            exact(a[key], b[key], f'{path}.{key}')
    elif isinstance(a, (list, tuple)):
        if type(a) is not type(b) or len(a) != len(b):
            raise AssertionError(f'{path}: sequence shape differs')
        for i, (left, right) in enumerate(zip(a, b)):
            exact(left, right, f'{path}[{i}]')
    elif isinstance(a, (int, float)) and not isinstance(a, bool):
        if a != b or (a == 0 and math.copysign(1, a) != math.copysign(1, b)):
            raise AssertionError(f'{path}: {a!r} != {b!r}')
    elif type(a) is not type(b) or a != b:
        raise AssertionError(f'{path}: {a!r} != {b!r}')


@contextmanager
def replacements(functions):
    for name, replacement in functions.items():
        original = ORIGINAL[name]
        for module_name, module in list(sys.modules.items()):
            if module_name.startswith('parkour_solver.') and getattr(module, name, None) is original:
                setattr(module, name, replacement)
    try:
        yield
    finally:
        for name, replacement in functions.items():
            for module_name, module in list(sys.modules.items()):
                if module_name.startswith('parkour_solver.') and getattr(module, name, None) is replacement:
                    setattr(module, name, ORIGINAL[name])


def build(output):
    import pybind11
    BUILD.mkdir(parents=True, exist_ok=True)
    source = PILOT / 'pruning.cpp'
    target = BUILD / ('_pruning_native' + sysconfig.get_config_var('EXT_SUFFIX'))
    flags = ['-O3', '-std=c++17', '-ffp-contract=off', '-fno-fast-math', '-DNDEBUG', '-shared', '-fPIC']
    if sys.platform == 'darwin':
        flags += ['-undefined', 'dynamic_lookup']
    command = ['clang++', *flags, '-I'+pybind11.get_include(), '-I'+sysconfig.get_path('include'),
               str(source), '-o', str(target)]
    with (output / 'build.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    save(output / 'build.json', dict(command=command, exitCode=result.returncode,
        sourceSHA256=hashlib.sha256(source.read_bytes()).hexdigest(),
        compiler=subprocess.check_output(['clang++', '--version'], text=True),
        extension=str(target), python=sys.version, pybind11=pybind11.__version__,
        binarySHA256=hashlib.sha256(target.read_bytes()).hexdigest() if result.returncode == 0 else None))
    if result.returncode:
        raise RuntimeError(f'Build failed; see {output / "build.log"}')
    rng = random.Random(8127)
    values = [(0.,0.),(1e-310,1e-309),(1e300,1e300)]
    values += [(rng.uniform(-1e6,1e6),rng.uniform(-1e6,1e6)) for _ in range(10000)]
    module = native()
    for a,b in values:
        if struct.pack('d',math.hypot(a,b)) != struct.pack('d',module.hypot2(a,b)):
            raise AssertionError('Native norm is not bit-exact with this CPython')
    save(output / 'hypot-parity.json', dict(cases=len(values), bitExact=True))


def run(args):
    from parkour_solver.bridge.python_compiler import PythonCompiler
    from parkour_solver.search.application import run as solve_task
    from parkour_solver.io.checkpoint_store import CheckpointStore
    from parkour_solver.io.search_checkpoint import SearchCheckpoint
    from tools.profiling.trace import write
    fixture = json.loads(args.input.read_text())
    fixture['settings']['seconds'] = args.seconds
    before = source_hashes()
    metadata = dict(startedAt=stamp(), sourceRevision=source_revision(), sourceHashes=before,
        backend=args.backend, fixture=fixture, durable=args.durable, python=sys.executable,
        loadAverageAtStart=os.getloadavg(), threadEnvironment={k:os.environ.get(k) for k in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS')},
        command=sys.argv, driverSHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (args.output/'instrumentation-driver.py').write_bytes(Path(__file__).read_bytes())
    if args.backend != 'python':
        binary = Path(native().__file__)
        metadata['nativeBinarySHA256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        metadata['nativeSourceSHA256'] = hashlib.sha256((PILOT/'pruning.cpp').read_bytes()).hexdigest()
    save(args.output / 'provenance.json', metadata)
    captured = {name: [] for name in NAMES}
    counts = {name: 0 for name in NAMES}
    checks = {name: 0 for name in NAMES}
    functions = {}
    if args.backend != 'python':
        functions = {name: getattr(native(), name) for name in NAMES}
    if args.capture or args.verify:
        for name in NAMES:
            target = functions.get(name, ORIGINAL[name])
            def wrapped(*a, _name=name, _target=target, **kw):
                ordinal = counts[_name]
                counts[_name] += 1
                selected = args.capture and (ordinal < (32 if _name.endswith('_many') else 128) or ordinal % 64 == 0) and len(captured[_name]) < (128 if _name.endswith('_many') else 1024)
                inputs = copy.deepcopy((a,kw)) if selected else None
                result = _target(*a,**kw)
                if args.verify:
                    exact(getattr(ORACLE,_name)(*a,**kw), result)
                    checks[_name] += 1
                if selected:
                    captured[_name].append(dict(ordinal=ordinal,args=inputs[0],kwargs=inputs[1],expected=copy.deepcopy(result)))
                return result
            functions[name] = wrapped
    started = time.perf_counter()
    with replacements(functions), PythonCompiler() as compiler:
        if args.durable:
            with CheckpointStore(args.output / 'report.json.gz') as store:
                checkpoint = SearchCheckpoint(store, dict(provenance=metadata))
                result, validation = solve_task(fixture,compiler,python_executable=sys.executable,on_progress=checkpoint)
                checkpoint.finish(result,validation)
        else:
            result, validation = solve_task(fixture,compiler,python_executable=sys.executable)
    elapsed = time.perf_counter()-started
    if not args.durable:
        with CheckpointStore(args.output / 'report.json.gz') as store:
            store.save(dict(provenance=metadata,result=result,validation=validation))
    write(args.output,dict(provenance=metadata,result=result,validation=validation),[])
    if args.capture:
        with gzip.GzipFile(filename=str(args.output/'corpus.pkl.gz'),mode='wb',mtime=0) as stream:
            pickle.dump(dict(provenance=metadata,cases=captured),stream,protocol=5)
    best = result.get('best')
    summary = dict(backend=args.backend,wallSeconds=elapsed,searchSeconds=result['elapsedSeconds'],
        sourceUnchanged=before==source_hashes(),status=result['status'],termination=result['termination'],
        expandedPrefixes=result.get('expandedPrefixes'),work=result['work'],
        ticks=best['plan']['ticks'] if best else None,objective=best['candidate']['objective'] if best else None,
        validation=validation,liveComparisons=checks,captured={k:len(v) for k,v in captured.items()},calls=counts,
        finishedAt=stamp(), loadAverageAtEnd=os.getloadavg(), timing='Whole application run; final report rendering excluded; capture/verification diagnostic when enabled')
    save(args.output/'summary.json',summary)
    print(json.dumps(summary),flush=True)


def load_cases(paths):
    cases = {name:[] for name in NAMES}
    for path in paths:
        with gzip.open(path,'rb') as stream:
            value = pickle.load(stream)
        for name in NAMES:
            cases[name].extend(value['cases'].get(name, []))
    return cases


def validate(args):
    cases = load_cases(args.corpus)
    validated = {}
    functions = {name:getattr(native(),name) for name in NAMES}
    for name, entries in cases.items():
        for i,entry in enumerate(entries):
            expected = getattr(ORACLE,name)(*entry['args'],**entry['kwargs'])
            exact(entry['expected'],expected,f'{name}[{i}].reference')
            result = functions[name](*entry['args'],**entry['kwargs'])
            exact(expected,result,f'{name}[{i}].native')
            if name == 'reachable_envelopes' and result['model'] is not entry['args'][0]:
                raise AssertionError('Native envelope must retain model identity')
        validated[name] = len(entries)
    # A comparator must reject a wrong pruning verdict, rather than merely run.
    try:
        exact(dict(ruledOut=True),dict(ruledOut=False))
    except AssertionError:
        pass
    else:
        raise AssertionError('Comparator failed to detect a changed rejection')
    save(args.output/'parity.json',dict(validated=validated,exact=True,noTolerance=True,
        corpus={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in args.corpus}))
    print(json.dumps(validated),flush=True)


def tests(args):
    modules = ['tests.unit.reachability.test_screen','tests.unit.reachability.test_interaction_normalization',
               'tests.unit.reachability.test_proposal_analysis','tests.unit.search.test_transfer_time_bounds',
               'tests.unit.constraints.test_encounters','tests.unit.constraints.test_option_keys',
               'tests.unit.movement.test_block_contacts']
    modules = args.test_modules or modules
    counts = {name:0 for name in NAMES}
    functions = {}
    targets = {name:getattr(native(),name) for name in NAMES}
    for name in NAMES:
        def checked(*a,_name=name,**kw):
            # Compare pure bounds without doubling an effectful solver/mock call.
            pure = dict(kw,refine=False) if kw.get('refine') else kw
            expected = getattr(ORACLE,_name)(*a,**pure)
            actual = targets[_name](*a,**pure)
            exact(expected,actual,_name)
            counts[_name] += 1
            return targets[_name](*a,**kw) if kw.get('refine') else actual
        functions[name] = checked
    before = source_hashes()
    with replacements(functions), (args.output/'tests.log').open('w') as log:
        suite = unittest.defaultTestLoader.loadTestsFromNames(modules)
        result = unittest.TextTestRunner(stream=log,verbosity=2).run(suite)
    report = dict(tests=result.testsRun,failures=len(result.failures),errors=len(result.errors),
                  skipped=len(result.skipped),comparisons=counts,sourceUnchanged=before==source_hashes())
    save(args.output/'tests.json',report)
    print(json.dumps(report),flush=True)
    if not result.wasSuccessful():
        raise AssertionError('Native/oracle test failures; see tests.log')


def bench(args):
    cases = load_cases(args.corpus)
    rows = []
    for name, entries in cases.items():
        entries = [e for e in entries if not e['kwargs'].get('refine',False)]
        if not entries:
            continue
        functions = dict(python=getattr(ORACLE,name),native=getattr(native(),name))
        def measure(function,loops):
            start = time.perf_counter()
            for _ in range(loops):
                for entry in entries:
                    result = function(*entry['args'],**entry['kwargs'])
            return time.perf_counter()-start
        # Both public boundaries include input conversion and result materialization.
        for function in functions.values():
            measure(function,1)
        loops = max(1,min(100,int(.2/max(measure(functions['python'],1),1e-9))))
        results = []
        for repeat in range(args.repetitions):
            result = dict(repetition=repeat,order=['python','native'] if repeat%2==0 else ['native','python'])
            for backend in result['order']:
                result[backend] = measure(functions[backend],loops)
            results.append(result)
        row = dict(function=name,cases=len(entries),loops=loops,runs=results,
                   pythonMedianSeconds=statistics.median(r['python'] for r in results),
                   nativeMedianSeconds=statistics.median(r['native'] for r in results),
                   medianPairedSpeedup=statistics.median(r['python']/r['native'] for r in results))
        rows.append(row)
        print(json.dumps({k:v for k,v in row.items() if k!='runs'}),flush=True)
    save(args.output/'benchmark.json',dict(rows=rows,sourceRevision=source_revision(),sourceHashes=source_hashes(),
        driverSHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        nativeBinarySHA256=hashlib.sha256(Path(native().__file__).read_bytes()).hexdigest(),
        corpus={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in args.corpus},
        timing='Same ordered corpus, warmed calls; Python/C++ public boundaries both materialize outputs; no solver refinement'))



def batch_bench(args):
    module = native()
    entries = load_cases(args.corpus)['interaction_reachability_many']
    prepared = []
    for entry in entries:
        envelope, queries = entry['args']
        model = envelope['model']
        states = envelope['states']
        y = [state['Y'][0] for state in states] if all(state['Y'][0] == state['Y'][1] for state in states) else None
        python_bounds = ORACLE.reachable_envelopes(model, y)
        owner = module.Envelope(model, y)
        exact(python_bounds, owner.snapshot(), 'owned_envelope')
        expected = ORACLE.interaction_reachability_many(envelope, queries)
        exact(expected, ORACLE.interaction_reachability_many(python_bounds, queries), 'reconstructed_batch')
        exact(expected, owner.check_many(queries), 'owned_batch')
        exact(expected, module.screen_batch(model, y, queries), 'coupled_batch')
        prepared.append((model, y, envelope, queries, owner, python_bounds))
    def coupled_python(item):
        model,y,_,queries,_,_ = item
        return ORACLE.interaction_reachability_many(ORACLE.reachable_envelopes(model,y),queries)
    def coupled_native(item):
        model,y,_,queries,_,_ = item
        return module.screen_batch(model,y,queries)
    methods = dict(
        build_and_batch=(coupled_python,coupled_native),
        retained_batch=(lambda item: ORACLE.interaction_reachability_many(item[5],item[3]),
                        lambda item: item[4].check_many(item[3])),
        public_batch=(lambda item: ORACLE.interaction_reachability_many(item[2],item[3]),
                      lambda item: module.interaction_reachability_many(item[2],item[3])))
    rows=[]
    for name,(python_call,native_call) in methods.items():
        functions=dict(python=python_call,native=native_call)
        def measure(function,loops):
            started=time.perf_counter()
            for _ in range(loops):
                for item in prepared:
                    result=function(item)
            return time.perf_counter()-started
        for function in functions.values(): measure(function,1)
        loops=max(1,min(100,int(.2/max(measure(python_call,1),1e-9))))
        runs=[]
        for repetition in range(args.repetitions):
            row=dict(repetition=repetition,order=['python','native'] if repetition%2==0 else ['native','python'])
            for backend in row['order']: row[backend]=measure(functions[backend],loops)
            runs.append(row)
        row=dict(function=name,cases=len(prepared),loops=loops,runs=runs,
            pythonMedianSeconds=statistics.median(r['python'] for r in runs),
            nativeMedianSeconds=statistics.median(r['native'] for r in runs),
            medianPairedSpeedup=statistics.median(r['python']/r['native'] for r in runs))
        rows.append(row)
        print(json.dumps({k:v for k,v in row.items() if k!='runs'}),flush=True)
    save(args.output/'benchmark.json',dict(rows=rows,sourceRevision=source_revision(),sourceHashes=source_hashes(),
        driverSHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        nativeBinarySHA256=hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest(),
        corpus={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in args.corpus},
        batches=len(prepared),queries=sum(len(item[3]) for item in prepared),
        batchSizes=[len(item[3]) for item in prepared],exactParity=True,
        timing='Public API calls and full decision outputs included; native owner construction excluded only from retained_batch'))


def matrix(args):
    jobs = []
    short = ROOT/'cases/real11/physical-anchors.json'
    extended = ROOT/'cases/real11/extended-anchors.json'
    for label,fixture,durable,seconds,repetitions in (
            ('short',short,False,300,5),('short-durable',short,True,300,3),
            ('extended',extended,False,60,2)):
        for repeat in range(repetitions):
            order = ('python','native') if repeat%2 == 0 else ('native','python')
            for backend in order:
                directory = args.output/f'{label}-r{repeat+1}-{backend}'
                command = [sys.executable,'-B','-m','tools.profiling.cpp_pruning','run',
                    '--backend',backend,'--input',str(fixture),'--seconds',str(seconds),'--output',str(directory)]
                if durable:
                    command.append('--durable')
                jobs.append(dict(label=label,repetition=repeat+1,backend=backend,output=str(directory),command=command))
    save(args.output/'jobs.json',dict(startedAt=stamp(),jobs=jobs))
    completed = []
    for index,job in enumerate(jobs):
        save(args.output/'status.json',dict(stage='running',index=index,total=len(jobs),job=job,startedAt=stamp()))
        with (args.output/(Path(job['output']).name+'.log')).open('w') as log:
            result = subprocess.run(job['command'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        row = dict(job,exitCode=result.returncode,finishedAt=stamp())
        path = Path(job['output'])/'summary.json'
        if path.exists():
            row['summary'] = json.loads(path.read_text())
        completed.append(row)
        save(args.output/'results.json',completed)
        print(json.dumps(row),flush=True)
        if result.returncode:
            raise RuntimeError('Paired live run failed; see its log')
    save(args.output/'status.json',dict(stage='complete',runs=len(completed),finishedAt=stamp()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('build','run','validate','tests','bench','batch_bench','matrix'))
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--input',type=Path)
    parser.add_argument('--seconds',type=float,default=90.)
    parser.add_argument('--backend',choices=('python','native'),default='python')
    parser.add_argument('--test-modules',nargs='+')
    parser.add_argument('--capture',action='store_true')
    parser.add_argument('--verify',action='store_true')
    parser.add_argument('--durable',action='store_true')
    parser.add_argument('--corpus',type=Path,nargs='+',default=[])
    parser.add_argument('--repetitions',type=int,default=10)
    args = parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    if args.action == 'build':
        build(args.output)
    else:
        globals()[args.action](args)


if __name__ == '__main__':
    main()
