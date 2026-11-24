"""Shared intermediate movement ordering and recoverable comparison policy.

This module owns count/peer/bound predicates. Callers own geometry, validated
evidence, queues and recovery. Terminal objective ranking is separate.
"""

POLICY_VERSION = 1


def comparison_label(program, *, start=0, through=None, identity='', mode='ordinary',
                     context=None, evidence=None):
    p = program.get('profile', {})
    n = p.get('duration', 0)
    end = n if through is None else through
    if not 0 <= start <= end <= n:
        raise ValueError('Comparison interval lies outside the program')
    mode = 'turnaround' if mode is True else 'ordinary' if mode is False else mode
    if mode not in ('ordinary', 'turnaround'):
        raise ValueError('Comparison mode must be ordinary or turnaround')
    ground, jump = p.get('grounded', []), p.get('jump', [])
    schedule = None
    if len(ground) >= end and len(jump) >= end:
        schedule = ['J' if ground[t] and jump[t] else 'R' if ground[t] else 'A'
                    for t in range(start, end)]
    result = dict(id=identity, T=end, through=end, start=start, mode=mode,
                  J=schedule.count('J') if schedule is not None else None,
                  R=schedule.count('R') if schedule is not None else None,
                  A=schedule.count('A') if schedule is not None else None,
                  schedule=schedule)
    result['tickCost'] = result['A'] if mode == 'turnaround' else result['R']
    result.update(context or {})
    if evidence:
        result.update(evidence)
    return result


def priority_key(label, identity=None):
    """One deterministic key; unavailable counts may order, never prune."""
    return (label['T'], -(label.get('J') or 0), label.get('tickCost') or 0,
            label.get('id', '') if identity is None else identity)


def _count_protections(candidate, peer):
    reasons = []
    if candidate['J'] > peer['J']:
        reasons.append('more_jump_inputs')
    if candidate['tickCost'] < peer['tickCost']:
        reasons.append('fewer_air_inputs' if candidate['mode'] == 'turnaround' else 'fewer_run_inputs')
    return reasons


def compare_candidates(candidate, peer=None, *, context=None, incumbent=None):
    """Return a pure decision; every peer-based heuristic uses this entry point."""
    context = context or {}
    result = dict(version=POLICY_VERSION, action='keep', candidateId=candidate.get('id'),
                  peerId=peer.get('id') if peer else None, contextId=context.get('comparisonContextId'),
                  priorityKey=list(priority_key(candidate)), protectionReasons=[],
                  peerLookupAllowed=False, elapsedT=candidate['T'])
    remaining = candidate.get('remainingFinishBound') or {}
    optimistic = candidate['T']+remaining.get('ticks', remaining.get('remainingTicks', 0))
    if incumbent is not None:
        tick = incumbent.get('T', incumbent.get('plan', {}).get('ticks'))
        if tick is not None:
            result.update(optimisticFinish=optimistic, incumbentT=tick,
                          boundProofIds=remaining.get('proofIds', []))
            if optimistic > tick:
                result.update(action='exclude_bound', reason='elapsed_inputs_exceed_incumbent_finish'
                              if candidate['T'] > tick else 'optimistic_remaining_time_exceeds_incumbent')
                return result
    enabled = context.get('localDeferralEnabled', False)
    if context.get('backend') == 'automatic_anchor' and not context.get('hardLatched'):
        enabled = False
    if not enabled:
        return result
    available = all(candidate.get(k) is not None for k in ('J','R','A','tickCost'))
    available &= candidate.get('basis') != 'open_prefix'
    result['peerLookupAllowed'] = available
    if peer is None:
        return result
    compatible = available and all(peer.get(k) is not None for k in ('J','R','A','tickCost'))
    compatible &= peer.get('validated', False)
    compatible &= candidate.get('mode') == peer.get('mode')
    compatible &= candidate.get('comparisonContext') == peer.get('comparisonContext')
    for key in ('scope','milestone','landingPhase'):
        compatible &= candidate.get(key) == peer.get(key)
    if not compatible:
        result.update(action='keep_incomparable', compatibilityReason='different_or_unavailable_comparison_context')
        return result
    reasons = _count_protections(candidate, peer)
    result.update(candidateCounts={k:candidate[k] for k in ('T','J','R','A')},
                  peerCounts={k:peer[k] for k in ('T','J','R','A')}, protectionReasons=reasons)
    if reasons:
        result['action'] = 'keep_protected'
    elif candidate['T'] == peer['T']:
        result['action'] = 'keep_equal_time'
    elif peer['T'] < candidate['T']:
        result['action'] = 'defer_preparation'
    return result
