import json
import math

from standalone.ipopt.model import integer, number
from standalone.autonomy.solve_spec import motion_record, processing_key


TOLERANCE = 1e-7


def generation_exhausted(report):
    return (report.get('termination')=='exhausted' and not report.get('queueDrops',0)
        and not report.get('remainingPrefixes',0)
        and all(generation_exhausted(parent) for parent in report.get('parents',[]))
        and not any(row.get('termination')=='choice_limit' for row in report.get('generation',[])))


def _measurements(record):
    times = {key:value['ticks'] for key,value in record.get('spatial_results',{}).items()
             if value.get('status')=='feasible' and value.get('ticks') is not None}
    progress = {key:value['objective'] for key,value in record.get('clock_results',{}).items()
                if value.get('status')=='feasible' and value.get('objective') is not None}
    return times,progress


def protections(candidate, peer, progress_margin):
    reasons = []
    for key in candidate.get('required_clocks', candidate.get('clock_results', {})):
        c = candidate.get('clock_results', {}).get(key, {})
        b = peer.get('clock_results', {}).get(key, {})
        cs, bs = c.get('state'), b.get('state')
        if not cs or not bs or not cs.get('motion') or not bs.get('motion') or c.get('objective') is None or b.get('objective') is None:
            reasons.append('missing_state_metadata')
            continue
        if cs['motion']['schedule'] != bs['motion']['schedule']:
            reasons.append('incomparable_schedules')
            if cs['motion']['runs'] < bs['motion']['runs']:
                reasons.append('fewer_run_inputs')
            if cs['motion']['jumps'] > bs['motion']['jumps']:
                reasons.append('more_effective_jumps')
            continue
        if cs['motion']['phase'] != bs['motion']['phase'] or cs.get('scope') != bs.get('scope'):
            reasons.append('incompatible_phase_or_scope')
            continue
        if c['objective'] >= b['objective']-progress_margin-TOLERANCE:
            reasons.append('position_close_or_ahead')
        if cs['forwardVelocity'] > bs['forwardVelocity']+TOLERANCE:
            reasons.append('higher_forward_velocity')
        if cs['speed'] > bs['speed']+TOLERANCE:
            reasons.append('higher_horizontal_speed')
    return sorted(set(reasons))


def clearly_worse(candidate, peer, *, time_margin, progress_margin):
    ct,cp = _measurements(candidate)
    pt,pp = _measurements(peer)
    if not ct or not cp or not ct.keys() <= pt.keys() or not cp.keys() <= pp.keys():
        return False
    expected_t = set(candidate.get('required_spatial',ct))
    expected_p = set(candidate.get('required_clocks',cp))
    if not expected_t <= ct.keys() or not expected_p <= cp.keys():
        return False
    if protections(candidate, peer, progress_margin):
        return False
    if any(pt[key]>value for key,value in ct.items()) or any(pp[key]<value-TOLERANCE for key,value in cp.items()):
        return False
    return any(value-pt[key]>time_margin for key,value in ct.items()) or any(
        pp[key]-value>progress_margin+TOLERANCE for key,value in cp.items())


def retain_two_goals(candidates, *, cap, unknown_slots=1, time_margin=1, progress_margin=.25, prune=True):
    integer(cap,'candidate retention cap',1,100000)
    integer(unknown_slots,'unknown reserve',0,cap)
    if number(time_margin,'time margin')<0 or number(progress_margin,'progress margin')<0:
        raise ValueError('Comparison margins must be nonnegative')
    records = [record for record,_ in candidates]
    ids = [r['id'] for r in records]
    if len(ids)!=len(set(ids)):
        raise ValueError('Candidate identities must be unique')
    decisions = {}
    eligible,unknown = [],[]
    for i,(record,program) in enumerate(candidates):
        times,progress = _measurements(record)
        peer = next((r['id'] for r in records if r['id']!=record['id'] and clearly_worse(
            record,r,time_margin=time_margin,progress_margin=progress_margin)),None) if prune else None
        if peer is not None:
            decisions[record['id']] = dict(reason='markedly_worse',peer=peer)
        elif not times and not progress:
            unknown.append(i)
        else:
            eligible.append(i)
    def priority(i):
        return processing_key(candidates[i][1], records[i]['id'])
    available = sorted(eligible+unknown, key=priority) if prune else sorted(range(len(candidates)), key=priority)
    selected = available[:cap] if prune else available
    deferred = available[len(selected):]
    selected_set=set(selected)
    for i,record in enumerate(records):
        if i in selected_set:
            decisions[record['id']]=dict(reason='retained')
        elif record['id'] not in decisions:
            decisions[record['id']]=dict(reason='deferred')
        if decisions[record['id']]['reason'] != 'markedly_worse':
            guards = {r['id']: protections(record, r, progress_margin) for r in records if r['id'] != record['id']}
            decisions[record['id']]['protections'] = {k: v for k, v in guards.items() if v}
    return [candidates[i] for i in selected],dict(policy='two_goal_margin' if prune else 'unpruned_reference',
        timeMargin=time_margin,progressMargin=progress_margin,hardCap=cap,unknownReserve=unknown_slots,
        decisions=decisions,markedlyWorse=sum(d['reason']=='markedly_worse' for d in decisions.values()),
        resourceDrops=0, deferred=[records[i]['id'] for i in deferred], deferredCount=len(deferred),
        retained=len(selected),objectiveOptimalityProven=False)


def clock_result(checkpoint, result):
    context = dict(tick=checkpoint.tick,direction=list(checkpoint.direction),anchor=list(checkpoint.start),
                   signature=checkpoint.required_signature,region=list(checkpoint.region),segment=checkpoint.segment)
    key=json.dumps(context,sort_keys=True)
    best=result.get('best')
    value=None
    if best is not None and checkpoint.tick<len(best['candidate']['positions']):
        x,z=best['candidate']['positions'][checkpoint.tick]
        value=checkpoint.direction[0]*(x-checkpoint.start[0])+checkpoint.direction[1]*(z-checkpoint.start[2])
    state = None
    if value is not None and best.get('profiles') and best['candidate'].get('velocity'):
        vx, vz = best['candidate']['velocity'][checkpoint.tick]
        state = dict(position=list(best['candidate']['positions'][checkpoint.tick]), velocity=[vx, vz],
            forwardVelocity=checkpoint.direction[0]*vx+checkpoint.direction[1]*vz,
            lateralPosition=-checkpoint.direction[1]*(x-checkpoint.start[0])+checkpoint.direction[0]*(z-checkpoint.start[2]),
            lateralVelocity=-checkpoint.direction[1]*vx+checkpoint.direction[0]*vz,
            witnessProblem=best.get('problemHash'), solveIdentity=result.get('solveIdentity'),
            speed=math.hypot(vx, vz), motion=motion_record(dict(profile=best['profiles'][0])), scope=result.get('solveScope'))
    return key,dict(context=context,status='feasible' if value is not None else 'unknown',objective=value,state=state,
                    unavailableReason='terminal_before_clock' if best is not None and value is None else None,
                    interpretation='fixed_absolute_tick_progress_proxy_not_arrival_time')
