import copy
import json

from parkour_solver.movement.program import truncate_program


def goal_spec(scene, spec=None):
    spec=copy.deepcopy(spec or dict(kind='landing',block=scene['goalBlock']))
    if spec.get('kind')!='landing':
        raise ValueError('A task goal is a landing on a scene block')
    if set(spec)-{'kind','block','blocks','surface'} or spec.get('block') not in {b['id'] for b in scene['blocks']}:
        raise ValueError('Landing comparison requires a scene block')
    if 'blocks' in spec and (not isinstance(spec['blocks'],list) or not spec['blocks'] or
            spec['block'] not in spec['blocks'] or any(b not in {v['id'] for v in scene['blocks']} for b in spec['blocks'])):
        raise ValueError('Landing surface requires its physical support alternatives')
    return spec


def goal_key(scene,spec,corridor=None):
    return json.dumps(dict(goal=spec,route=corridor.to_dict() if corridor is not None else None,
                           initial=scene['initial']),sort_keys=True)


def landing_satisfied(scene,block_id,vertical):
    block=next(b for b in scene['blocks'] if b['id']==block_id)
    return bool(vertical['onGround'][-1] and abs(vertical['y'][-1]-block['top'])<=1e-7)


def _landing_prefixes(scene,program,block_id, *, reuse_full=False):
    block=next(b for b in scene['blocks'] if b['id']==block_id)
    box=scene['collisionScene']['solids'][block['solidIndex']]
    p=program['profile']
    events=p.get('contactEvents',[])+p.get('caseEvents',[])
    times={e.get('offset',e.get('input',-1))+1 for e in events if box in e.get('boxes',())
           and (e.get('axis')=='Y' and e.get('direction')==-1 or e.get('case') in ('normal_step','blip'))}
    if program['terminalSupport']==block_id:times.add(p['duration'])
    for tick in sorted(times):
        if not 0<tick<=p['duration'] or not p['grounded'][tick] or abs(p['position'][tick]-block['top'])>1e-7:
            continue
        candidate=program if reuse_full and tick==p['duration'] and program['terminalSupport']==block_id else truncate_program(program,tick)
        candidate['terminalSupport']=block_id
        at=[e for e in events if box in e.get('boxes',()) and e.get('offset',e.get('input',-1))==tick-1]
        if any(e.get('case') in ('normal_step','blip') for e in at):
            candidate['profile']['landingPhase']='post'
        elif any(e.get('axis')=='Y' and e.get('direction')==-1 for e in at):
            candidate['profile']['landingPhase']='pre'
        yield candidate
