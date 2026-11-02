import json
import math

from standalone.ipopt.model import integer, number


TOLERANCE = 1e-7


def _measurements(record):
    times = {key:value['ticks'] for key,value in record.get('spatial_results',{}).items()
             if value.get('status')=='feasible' and value.get('ticks') is not None}
    progress = {key:value['objective'] for key,value in record.get('clock_results',{}).items()
                if value.get('status')=='feasible' and value.get('objective') is not None}
    return times,progress


def clearly_worse(candidate, peer, *, time_margin, progress_margin):
    ct,cp = _measurements(candidate)
    pt,pp = _measurements(peer)
    if not ct or not cp or not ct.keys() <= pt.keys() or not cp.keys() <= pp.keys():
        return False
    expected_t = set(candidate.get('required_spatial',ct))
    expected_p = set(candidate.get('required_clocks',cp))
    if not expected_t <= ct.keys() or not expected_p <= cp.keys():
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
    def time_rank(i):
        times,_ = _measurements(records[i])
        return (min(times.values(),default=math.inf),i)
    time_keys = {key for r in records for key in _measurements(r)[0]}
    clock_keys = {key for r in records for key in _measurements(r)[1]}
    queues = []
    for key in sorted(time_keys):
        queues.append(sorted(eligible,key=lambda i:(_measurements(records[i])[0].get(key,math.inf),i)))
    for key in sorted(clock_keys):
        queues.append(sorted(eligible,key=lambda i:(-_measurements(records[i])[1].get(key,-math.inf),i)))
    if not queues:
        queues=[eligible[:]]
    selected=[]
    if prune:
        limit=cap-min(unknown_slots,len(unknown))
        for position in range(len(eligible)):
            for queue in queues:
                if len(selected)>=limit:
                    break
                i=queue[position]
                if i not in selected:
                    selected.append(i)
            if len(selected)>=limit:
                break
        selected += unknown[:min(unknown_slots,cap-len(selected))]
    else:
        selected = list(range(len(candidates)))
    selected.sort(key=time_rank)
    selected_set=set(selected)
    for i,record in enumerate(records):
        if i in selected_set:
            decisions[record['id']]=dict(reason='retained')
        elif record['id'] not in decisions:
            decisions[record['id']]=dict(reason='resource_cap' if i in eligible else 'unknown_resource_cap')
    return [candidates[i] for i in selected],dict(policy='two_goal_margin' if prune else 'unpruned_reference',
        timeMargin=time_margin,progressMargin=progress_margin,hardCap=cap,unknownReserve=unknown_slots,
        decisions=decisions,markedlyWorse=sum(d['reason']=='markedly_worse' for d in decisions.values()),
        resourceDrops=sum(d['reason'] in ('resource_cap','unknown_resource_cap') for d in decisions.values()),
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
    return key,dict(context=context,status='feasible' if value is not None else 'unknown',objective=value,
                    unavailableReason='terminal_before_clock' if best is not None and value is None else None,
                    interpretation='fixed_absolute_tick_progress_proxy_not_arrival_time')
