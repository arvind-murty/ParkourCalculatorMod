class SupportOrder:
    def __init__(self,scene,supports,terminal_tick):
        ids={b['id'] for b in scene['blocks']}
        if not supports or len(set(supports))!=len(supports) or any(s not in ids for s in supports):
            raise ValueError('Support order requires distinct scene block IDs')
        self.supports=list(supports)
        self.start=scene['startBlock']
        self.terminal_tick=terminal_tick
        self.boxes={tuple(scene['collisionScene']['solids'][b['solidIndex']]):b['id'] for b in scene['blocks']}

    def audit(self,program):
        p=program['profile']
        events=[]
        for e in p.get('contactEvents',[]):
            if e.get('axis')=='Y' and e.get('direction')==-1 and tuple(e['box']) in self.boxes:
                events.append((e['offset']+1,self.boxes[tuple(e['box'])]))
        for e in p.get('caseEvents',[]):
            if e.get('type')=='stepping' and 'box' in e and tuple(e['box']) in self.boxes:
                events.append((e['input']+1,self.boxes[tuple(e['box'])]))
        index,current=0,self.start
        observed=[]
        ranks={name:i for i,name in enumerate(self.supports)}
        for tick,name in sorted(set(events),key=lambda e:(e[0],ranks.get(e[1],-1))):
            if name==current:continue
            observed.append(dict(tick=tick,block=name))
            if index==len(self.supports):
                current=name
                continue
            if name!=self.supports[index]:
                return dict(accepted=False,progress=index,observed=observed,reason='support_order')
            current=name
            index+=1
        accepted=p['airtime']!=self.terminal_tick or index==len(self.supports)
        return dict(accepted=accepted,progress=index,observed=observed,
                    reason=None if accepted else 'unfinished_support_order_at_clock')

    def __call__(self,program):return self.audit(program)['accepted']
