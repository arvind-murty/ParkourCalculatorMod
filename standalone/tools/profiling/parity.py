"""Shared Python/native parity checks for C++ migration units.

A unit names its production module and functions. The oracle is an isolated
copy of the recorded Python source, so a replacement installed into the live
modules never compares against itself. Comparisons are exact by default; the
tolerant mode accepts floating-point differences only where a caller passes
explicit tolerances, and still requires identical keys, shapes, types,
discrete values and order.
"""
import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import json
import math
import re
import sys

from parkour_solver.io.checkpoint_store import read_report
from parkour_solver.schema import TOLERANCE as OBJECTIVE_TOLERANCE

# Migration tolerances for the tolerant mode. Kernels absorb operation-reordering
# noise; whole solves compare objectives at the schema's numerical tolerance.
KERNEL = dict(rel_tol=1e-12, abs_tol=1e-12)
SOLVE = dict(abs_tol=OBJECTIVE_TOLERANCE)


def load_oracle(module, name=None):
    spec = importlib.util.spec_from_file_location(name or '_parity_oracle_'+module.__name__.replace('.', '_'),
                                                  module.__file__)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    return oracle


@contextmanager
def replacements(originals, functions):
    """Install each replacement wherever a parkour_solver module holds the original, including imported aliases."""
    for name, replacement in functions.items():
        for module_name, module in list(sys.modules.items()):
            if module_name.startswith('parkour_solver.') and getattr(module, name, None) is originals[name]:
                setattr(module, name, replacement)
    try:
        yield
    finally:
        for name, replacement in functions.items():
            for module_name, module in list(sys.modules.items()):
                if module_name.startswith('parkour_solver.') and getattr(module, name, None) is replacement:
                    setattr(module, name, originals[name])


def selected(ordinal, *, first, every):
    """Deterministic capture by call ordinal, decided before the call's outcome is known."""
    return ordinal < first or ordinal % every == 0


def compare(a, b, path='result', *, rel_tol=None, abs_tol=None, numeric_types=True):
    """Raise AssertionError at the first difference; return the largest accepted float deviation.

    Without tolerances floats must be equal, including the sign of zero. With
    tolerances, finite floats may differ within math.isclose(rel_tol, abs_tol);
    infinities and NaN must still match exactly. numeric_types=False accepts an
    int equal to a float, which only value-level contracts may do: canonical
    digests encode 1 and 1.0 differently.
    """
    tolerant = rel_tol is not None or abs_tol is not None
    worst = 0.
    if isinstance(a, dict):
        if not isinstance(b, dict) or a.keys() != b.keys():
            raise AssertionError(f'{path}: dictionary keys differ')
        for key in a:
            worst = max(worst, compare(a[key], b[key], f'{path}.{key}', rel_tol=rel_tol, abs_tol=abs_tol,
                                       numeric_types=numeric_types))
    elif isinstance(a, (list, tuple)):
        if type(a) is not type(b) or len(a) != len(b):
            raise AssertionError(f'{path}: sequence shape differs')
        for i, (left, right) in enumerate(zip(a, b)):
            worst = max(worst, compare(left, right, f'{path}[{i}]', rel_tol=rel_tol, abs_tol=abs_tol,
                                       numeric_types=numeric_types))
    elif isinstance(a, float) and type(b) is float and tolerant and math.isfinite(a) and math.isfinite(b):
        if not math.isclose(a, b, rel_tol=rel_tol or 0., abs_tol=abs_tol or 0.):
            raise AssertionError(f'{path}: {a!r} != {b!r} beyond tolerance')
        worst = abs(a-b)
    elif isinstance(a, (int, float)) and not isinstance(a, bool):
        if numeric_types and type(a) is not type(b) or isinstance(b, bool) or not isinstance(b, (int, float)) or \
                a != b and not (a != a and b != b) or (a == 0 and math.copysign(1, a) != math.copysign(1, b)):
            raise AssertionError(f'{path}: {a!r} != {b!r}')
    elif type(a) is not type(b) or a != b:
        raise AssertionError(f'{path}: {a!r} != {b!r}')
    return worst


DIGEST = re.compile(r'[0-9a-f]{64}')


def without_digests(value):
    """Drop SHA-256 strings, which change with any float bit even when every decision matches."""
    if isinstance(value, dict):
        return {k: without_digests(v) for k, v in value.items()
                if not (isinstance(v, str) and DIGEST.fullmatch(v))}
    if isinstance(value, list):
        return [without_digests(v) for v in value if not (isinstance(v, str) and DIGEST.fullmatch(v))]
    return value


def transitions(result):
    """The DFS transition sequence without wall-clock timestamps."""
    state = result['searchState']
    dfs = state.get('dfs', state)
    return [{k: v for k, v in event.items() if k != 'elapsedSeconds'} for event in dfs['transitions']]


def transition_signature(result):
    return hashlib.sha256(json.dumps(transitions(result), sort_keys=True, allow_nan=False).encode()).hexdigest()


def compare_routes(reference_report, candidate_report, *, rel_tol=None, abs_tol=None):
    """Compare two saved solve reports: outcome, physical validation and the decision sequence.

    Exact mode requires identical transitions. Tolerant mode compares
    transitions without digest strings and the objective within tolerance. In
    both modes each published route must pass its own independent validation.
    """
    tolerant = rel_tol is not None or abs_tol is not None
    verdict = dict(mode='tolerant' if tolerant else 'exact', rel_tol=rel_tol, abs_tol=abs_tol, differences=[])
    validations = [[bool(v.get('passed')) for v in r['validation']] for r in (reference_report, candidate_report)]
    if validations[0] != validations[1] or not all(validations[0]):
        verdict['differences'].append(dict(field='validation', reference=validations[0], candidate=validations[1]))
    reference, candidate = reference_report['result'], candidate_report['result']
    for key in ('status', 'termination'):
        if reference[key] != candidate[key]:
            verdict['differences'].append(dict(field=key, reference=reference[key], candidate=candidate[key]))
    best = reference.get('best'), candidate.get('best')
    if (best[0] is None) != (best[1] is None):
        verdict['differences'].append(dict(field='best', reference=best[0] is not None, candidate=best[1] is not None))
    elif best[0] is not None:
        if best[0]['plan']['ticks'] != best[1]['plan']['ticks']:
            verdict['differences'].append(dict(field='ticks', reference=best[0]['plan']['ticks'],
                                               candidate=best[1]['plan']['ticks']))
        try:
            compare(best[0]['candidate']['objective'], best[1]['candidate']['objective'], 'objective',
                    rel_tol=rel_tol, abs_tol=abs_tol)
        except AssertionError as error:
            verdict['differences'].append(dict(field='objective', detail=str(error)))
    left, right = transitions(reference), transitions(candidate)
    if tolerant:
        left, right = without_digests(left), without_digests(right)
    first = next((i for i, (a, b) in enumerate(zip(left, right)) if _differs(a, b, rel_tol, abs_tol)),
                 None if len(left) == len(right) else min(len(left), len(right)))
    verdict.update(transitions=[len(left), len(right)], firstTransitionDifference=first)
    if first is not None:
        verdict['differences'].append(dict(field='transitions', index=first,
            reference=left[first] if first < len(left) else None,
            candidate=right[first] if first < len(right) else None))
    verdict['matches'] = not verdict['differences']
    return verdict


def _differs(a, b, rel_tol, abs_tol):
    try:
        compare(a, b, rel_tol=rel_tol, abs_tol=abs_tol)
    except AssertionError:
        return True
    return False


def self_test(*, rel_tol, abs_tol):
    """Prove both comparators reject deliberate defects and the tolerant one accepts small float noise."""
    if rel_tol is None and abs_tol is None:
        raise ValueError('The self-test needs the tolerance the unit will be compared with')
    rejected = dict(
        verdict=(dict(ruledOut=True), dict(ruledOut=False)),
        order=([1, 2], [2, 1]),
        missing_key=(dict(a=1, b=2), dict(a=1)),
        int_vs_float=([1], [1.]),
        signed_zero=([0.], [-0.]),
        large_float=([1.], [1.+1e3*max(rel_tol or 0., abs_tol or 0.)+1.]),
    )
    for label, (a, b) in rejected.items():
        for tolerances in ((None, None), (rel_tol, abs_tol)):
            if label == 'signed_zero' and tolerances != (None, None):
                continue
            if not _differs(a, b, *tolerances):
                raise AssertionError(f'Comparator accepted a {label} defect with tolerances {tolerances}')
    noise = 1.+0.5*max(rel_tol or 0., abs_tol or 0.)
    if _differs([1.], [noise], rel_tol, abs_tol) or not _differs([1.], [noise], None, None):
        raise AssertionError('Tolerant comparator must accept, and exact reject, sub-tolerance noise')
    return dict(rejected=sorted(rejected), acceptedNoise=noise)


def main():
    parser = argparse.ArgumentParser(description='Compare two saved search reports for migration parity.')
    parser.add_argument('reference')
    parser.add_argument('candidate')
    parser.add_argument('--tolerant', action='store_true', help=f'accept floating-point differences ({SOLVE})')
    args = parser.parse_args()
    verdict = compare_routes(*(read_report(path) for path in (args.reference, args.candidate)),
                             **(SOLVE if args.tolerant else {}))
    print(json.dumps(verdict, indent=2, allow_nan=False))
    return int(not verdict['matches'])


if __name__ == '__main__':
    raise SystemExit(main())
