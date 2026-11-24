"""Summarize persisted paired C++ pruning measurements and search parity."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import random
import statistics

from parkour_solver.io.checkpoint_store import read_report


def interval(ratios):
    rng = random.Random(20261120)
    samples = sorted(statistics.median(rng.choices(ratios,k=len(ratios))) for _ in range(10000))
    return [samples[249],samples[9749]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--kernels',type=Path,nargs='+',required=True)
    parser.add_argument('--build',type=Path)
    parser.add_argument('--batch',action='store_true')
    args = parser.parse_args()
    root = args.evidence
    parity_path = root/('parity-heldout' if args.batch else 'parity-extended')/'parity.json'
    live = json.loads((root/'live-matrix/results.json').read_text())
    build_path = args.build or root/'build-final/build.json'
    build = json.loads(build_path.read_text())
    capture_path = root/('capture-heldout' if args.batch else 'capture-extended')/'provenance.json'
    capture = json.loads(capture_path.read_text())
    paired = defaultdict(dict)
    for row in live:
        if row['exitCode'] or not row['summary']['sourceUnchanged']:
            raise AssertionError('A live run failed or changed production source')
        provenance = json.loads((Path(row['output'])/'provenance.json').read_text())
        if provenance['sourceHashes'] != capture['sourceHashes']:
            raise AssertionError('Live production source differs from the captured baseline')
        if row['backend'] == 'native' and provenance['nativeBinarySHA256'] != build['binarySHA256']:
            raise AssertionError('Live native binary differs from the measured build')
        row['provenance'] = provenance
        paired[row['label'],row['repetition']][row['backend']] = row
    groups = defaultdict(list)
    signatures = []
    for (label,repetition),pair in paired.items():
        if set(pair) != {'python','native'}:
            raise AssertionError('Incomplete live pair')
        p,n = pair['python']['summary'],pair['native']['summary']
        if pair['python']['provenance']['fixture'] != pair['native']['provenance']['fixture']:
            raise AssertionError('Paired fixtures/settings differ')
        item = dict(repetition=repetition,pythonSeconds=p['wallSeconds'],nativeSeconds=n['wallSeconds'],
                    speedup=p['wallSeconds']/n['wallSeconds'],pythonExpanded=p['expandedPrefixes'],
                    nativeExpanded=n['expandedPrefixes'],pythonWork=p['work'],nativeWork=n['work'])
        if label.startswith('short'):
            for key in ('status','ticks','objective','termination','expandedPrefixes','work','validation'):
                if p[key] != n[key]:
                    raise AssertionError(f'{label} repetition {repetition}: {key} differs')
            traces = {}
            for backend,row in pair.items():
                report = read_report(Path(row['output'])/'report.json.gz')
                transitions = [{k:v for k,v in event.items() if k != 'elapsedSeconds'}
                               for event in report['result']['searchState']['dfs']['transitions']]
                traces[backend] = hashlib.sha256(json.dumps(transitions,sort_keys=True,allow_nan=False).encode()).hexdigest()
            if traces['python'] != traces['native']:
                raise AssertionError(f'{label} repetition {repetition}: search transition sequence differs')
            signatures.append(dict(label=label,repetition=repetition,transitionSHA256=traces['python']))
        groups[label].append(item)
    summaries = {}
    for label,rows in groups.items():
        ratios = [r['speedup'] for r in rows]
        summaries[label] = dict(pairs=len(rows),pythonMedianSeconds=statistics.median(r['pythonSeconds'] for r in rows),
            nativeMedianSeconds=statistics.median(r['nativeSeconds'] for r in rows),
            medianPairedSpeedup=statistics.median(ratios),approximate95PercentInterval=interval(ratios),
            pythonExpanded=[r['pythonExpanded'] for r in rows],nativeExpanded=[r['nativeExpanded'] for r in rows],runs=rows)
    kernels = defaultdict(list)
    for path in args.kernels:
        report = json.loads(path.read_text())
        if report['sourceHashes'] != capture['sourceHashes'] or report['nativeBinarySHA256'] != build['binarySHA256']:
            raise AssertionError('Component source/binary differs from the live comparison')
        for row in report['rows']:
            kernels[row['function']].append(dict(pythonSecondsPerCorpus=row['pythonMedianSeconds']/row['loops'],
                nativeSecondsPerCorpus=row['nativeMedianSeconds']/row['loops'],
                speedup=row['medianPairedSpeedup'],cases=row['cases'],source=str(path)))
    kernel_summary = {name:dict(processes=len(rows),cases=rows[0]['cases'],
        pythonMedianSecondsPerCorpus=statistics.median(r['pythonSecondsPerCorpus'] for r in rows),
        nativeMedianSecondsPerCorpus=statistics.median(r['nativeSecondsPerCorpus'] for r in rows),
        medianProcessSpeedup=statistics.median(r['speedup'] for r in rows),
        processSpeedupRange=[min(r['speedup'] for r in rows),max(r['speedup'] for r in rows)],runs=rows)
        for name,rows in kernels.items()}
    result = dict(kernel=kernel_summary,live=summaries,matchingSearchTransitions=signatures,
        parity=json.loads(parity_path.read_text()),
        tests=json.loads((root/'native-tests-final/tests.json').read_text()),
        liveParity=json.loads((root/'live-native-parity/summary.json').read_text()),
        sanitizer=dict(status=json.loads((root/'ubsan-trap/status.json').read_text()),
            tests=json.loads((root/'ubsan-trap/tests/tests.json').read_text()),
            parity=json.loads((root/'ubsan-trap/parity/parity.json').read_text())),
        build=build)
    if args.batch:
        result['movementTests'] = json.loads((root/'native-movement-tests/tests.json').read_text())
        result['originalGateParity'] = json.loads((root/'prebatch-parity.json').read_text())
        sample = json.loads(args.kernels[0].read_text())
        result['batches'] = dict(count=sample['batches'], queries=sample['queries'], sizes=sample['batchSizes'])
        original = json.loads(Path('/private/var/tmp/standalone-cpp-pilot-20261120T130422/comparison.json').read_text())
        previous_traces = {row['transitionSHA256'] for row in original['matchingSearchTransitions']}
        if any(row['transitionSHA256'] not in previous_traces for row in signatures):
            raise AssertionError('Completed search transitions differ from the pre-batch experiment')
        result['matchesPrebatchSearchTransitions'] = True
    (root/'comparison.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    lines = ['# Measured '+('batched ' if args.batch else '')+'Python and C++ pruning comparison','',
        'This experiment ports the real interval-envelope builder and interaction-reachability checks '
        'used during candidate generation and avoidance enumeration. The integrated arm replaces those two '
        'functions in the existing Python search. Movement generators, traversal, other preparation, '
        'numerical solvers and checkpoint storage remain Python or their existing native libraries. '
        'It is a mixed Python/C++ pilot, not a port of the entire solver.','',
        '## Captured identical work','',
        'Inputs were sampled by call ordinal from a live extended Real11 search before measuring speed; '
        'they were not selected by feasibility or eventual outcome. Both timed APIs construct their full '
        'outputs, including Python container conversion for the C++ arm. Corpus load and compiler time '
        'are excluded. Solver-refinement requests are excluded from this component timing; the live runs '
        'retain their real refinement and solver behavior.','',
        '| Function | Captured inputs | Fresh processes | Python seconds per corpus | C++ seconds per corpus | Median speedup |',
        '| --- | ---: | ---: | ---: | ---: | ---: |']
    for name,row in kernel_summary.items():
        lines.append(f'| {name} | {row["cases"]} | {row["processes"]} | '
            f'{row["pythonMedianSecondsPerCorpus"]:.9f} | {row["nativeMedianSecondsPerCorpus"]:.9f} | '
            f'{row["medianProcessSpeedup"]:.6f}x |')
    lines += ['', 'Ratios above 1 favor C++; ratios below 1 favor Python. Each process warmed both '
        'implementations and alternated their measurement order. Process-level summaries are in '
        'comparison.json; the linked component benchmark files contain every inner repetition.','',
        '## Live paired searches','',
        ('Every run starts in a fresh process on the encounter-groups baseline with the same batch-API changes. '
         if args.batch else 'Every run starts in a fresh process on the same frozen encounter-groups snapshot. ')+
        'Arm order alternates between repetitions. These times cover the application search and final '
        'physical replay after backend import; post-run report generation is excluded. The durable '
        'variant includes normal checkpoint callbacks. They are not the earlier CLI process-wall numbers.','',
        '| Workload | Pairs | Python median seconds | C++ pilot median seconds | Paired speedup | Approximate 95% interval |',
        '| --- | ---: | ---: | ---: | ---: | --- |']
    if args.batch:
        lines[2:2] = ['The batch experiment has three distinct boundaries: `build_and_batch` builds '
            'envelopes and compiles complete query trees inside one native call, returning only decisions; '
            '`retained_batch` retains native envelopes while including query conversion and decision '
            'materialization in every timed call; `public_batch` consumes the current mutable Python '
            'snapshot and imports referenced bounds for the duration of that call. The live searches '
            'use the public bridge, not the opaque owner-only boundary. No hidden native cache can '
            'ignore later Python bound mutations.','',
            f'The fixed corpus contains {sample["batches"]} sampled batches and {sample["queries"]} queries. '
            'Sampling is by call ordinal, including the first 32 batch calls and every 64th later call '
            'up to 128 batches; no outcome filtering is used.','']
    for label,row in summaries.items():
        ci=row['approximate95PercentInterval']
        lines.append(f'| {label} | {row["pairs"]} | {row["pythonMedianSeconds"]:.6f} | '
            f'{row["nativeMedianSeconds"]:.6f} | {row["medianPairedSpeedup"]:.6f}x | {ci[0]:.6f}–{ci[1]:.6f}x |')
    lines += ['', 'The interval resamples whole pairs 10,000 times with a fixed seed. Small pair counts '
        'make it descriptive rather than a guarantee for other workloads. Extended runs have fixed '
        '60-second budgets: their wall-time ratio is not a time-to-solution speedup; compare progress below.','']
    for label,row in summaries.items():
        if label == 'extended':
            lines.append(f'Extended expanded prefixes, Python: {row["pythonExpanded"]}; C++ pilot: {row["nativeExpanded"]}.')
    t=result['tests'];v=result['parity']['validated'];l=result['liveParity']
    lines += ['', '## Correctness and scope','',
        f'- {t["tests"]} existing tests passed with {t["failures"]} failures, {t["errors"]} errors and '
        f'{t["skipped"]} skips while native kernel outputs were compared against an isolated copy '
        'of the original Python implementation.',
        f'- A separate undefined-behavior sanitizer build in trap mode passed the corpus and '
        f'{result["sanitizer"]["tests"]["tests"]} tests without a trap. Sanitized execution is excluded from timing.',
        f'- Exact, tolerance-free corpus parity: {v}. Signed zero is checked; floating bounds are not rounded.',
        f'- Live verification compared {l["liveComparisons"]} native results against Python and published '
        'the same replay-validated T64 route.',
        f'- All {len(signatures)} completed short live pairs match final ticks, objective, work counters, '
        'validation and the complete search-transition sequence after removing only elapsed timestamps.',
        ('- Production changes introduce ordered independent-query batches; the search and numerical '
         'policies are preserved. The other agent\'s encounter-groups checkout was not edited.' if args.batch else
         '- The production Python sources and the other agent\'s encounter-groups checkout were not edited.'),
        '- The earlier baseline\'s backend-selection test failure remains separate; it is not suppressed '
        'by these kernel tests. No full Java differential gate is claimed.',
        '- The integrated C++ adapter continues to use Python dictionaries at the boundary and delegates optional '
        'Clarabel refinement to the existing numerical implementation. These measurements do not predict '
        'the performance of a fully typed native search or a complete C++ rewrite.','',
        '## Recommendation','',
        'Keep this port experimental. The envelope kernel is a useful native target, but these two '
        'replacements together deliver only a small completed-search improvement and no consistent '
        'extended-prefix gain in the two measured pairs. These results do not justify a full rewrite '
        'for speed on their own.','',
        'The next measured unit should own approach-envelope construction and a complete ordered batch '
        'of candidate interaction checks in one native call. Keep envelopes and query data in native '
        'storage across the batch, returning the existing ordered decisions once. This is a design '
        'recommendation based on the small-query regression, not a measured claim that conversion '
        'alone caused it. Hold candidate policy, numerical dispatch and checkpoint behavior fixed. '
        'Require exact decisions and search transitions on the same corpus and short tasks, then '
        'repeat the live pairs before expanding to generator traversal. The broader movement, '
        'persistence and numerical ports remain contingent on that evidence.','',
        '## Reproduction and evidence','',
        f'- Build flags and binary hash: [build]({build_path}).',
        f'- Captured input provenance: [capture]({capture_path}).',
        f'- Exact comparison counts: [tests]({root}/native-tests-final/tests.json), [corpus parity]({parity_path}).',
        f'- Sanitized correctness run: [status]({root}/ubsan-trap/status.json), [build]({root}/ubsan-trap/build.json).',
        f'- Full paired results and transition checks: [comparison]({root}/comparison.json).',
        f'- Native live verification: [solve report]({root}/live-native-parity/report.md).','']
    if args.batch:
        start = lines.index('## Recommendation')+2
        end = lines.index('## Reproduction and evidence')
        lines[start:end] = [
            'Keep this implementation experimental. The direct native build-and-batch result measures '
            'a coherent unit that can keep its arrays private. The integrated search still reads shared '
            'Python snapshots; its public bridge has its own measured conversion and query costs. '
            'Do not substitute the opaque component multiplier for whole-search speedup.','',
            'Further migration should own candidate construction together with its native bounds and '
            'queries, so a decision batch can finish before exposing mutable Python containers. Preserve '
            'the Python oracle and existing solver/physics policy; require exact ordered decisions and '
            'resume/accounting parity before selecting the next production replacement.','']
        index = lines.index('## Correctness and scope')+2
        m = result['movementTests']
        lines[index:index] = [
            f'- Additional movement scope: {m["tests"]} tests, {m["failures"]} failures, '
            f'{m["errors"]} errors, {m["skipped"]} skips, with checked native outputs.',
            '- Original scalar-gate oracle: 24 full proofs plus all cache/fallback and exclusion accounting match.',
            '- All completed short search-transition hashes also match the pre-batch experiment.']
    (root/'REPORT.md').write_text('\n'.join(lines))
    print(json.dumps(dict(kernel=kernel_summary,live={k:{a:b for a,b in v.items() if a!='runs'} for k,v in summaries.items()}),indent=2))


if __name__ == '__main__':
    main()
