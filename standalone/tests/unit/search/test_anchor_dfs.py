import copy
import json
import os
import threading
import time
import unittest
from unittest.mock import patch

from parkour_solver.search.engine import find_checkpoint_route, ScopedWork
from parkour_solver.search.feasibility import solve_due
from tests.support.anchor_policy import scene, history


def extend(s,parent,target,n,extra=False):
    p=history(s,[]);start=parent['profile']['duration']
    p['profile'].update(duration=n,position=[1.]*(n+1),velocity=[0.]*(n+1),grounded=[True]*(n+1),
        jump=[False]*n,sneak=[False]*n,ladderTicks=[False]*(n+1))
    p['profile']['jump'][:start]=parent['profile']['jump']
    p['profile']['jump'][start]=True
    if extra:p['profile']['jump'][start+1]=True
    p['profile']['contactEvents']=copy.deepcopy(parent['profile']['contactEvents'])
    b=next(b for b in s['blocks'] if b['id']==target)
    p['profile']['contactEvents'].append(dict(offset=n-1,axis='Y',direction=-1,
        boxes=[s['collisionScene']['solids'][b['solidIndex']]]))
    p['terminalSupport']=target
    p['decisionDepth']=parent.get('decisionDepth',0)+1
    return p


class AnchorDFSTests(unittest.TestCase):
    def run_tree(self, *, logging=True, resume=None, calls=20, interval=1, setup=False, seconds=3,
                 on_progress=None,cancel_event=None,requests_per_evaluation=1,advance=None,node_probe=None,node_best=None,
                 finishes=1,relaxed=False,final=None,leg_best=None,goal=None,goal_choices=None,mid_choices=None,generated=None,compiler=None,
                 extra=(),**settings):
        s=scene(goal,extra) if goal else scene(extra=extra);s['maxTotalRunTicks']=60;s['maxRunTicksPerJump']=60;dispatched=[]
        def generate(target_scene,*,report,initial_programs,continuation=None,node_evaluator,**kwargs):
            saved=copy.deepcopy(continuation) if continuation else dict(expanded=1,cursor=0,exhausted=False)
            report['continuation']=saved
            if generated is not None:generated.append((initial_programs,kwargs))
            parent=initial_programs[0];target=target_scene['goalBlock']
            if target=='mid' and mid_choices is not None:
                choices=mid_choices(s,parent)
            elif target=='mid':
                choices=[extend(s,parent,'mid',10),extend(s,parent,'mid',11)]
                if setup:
                    prepared=extend(s,parent,'mid',11)
                    prepared['profile']['jump'][1]=True
                    start=next(b for b in s['blocks'] if b['id']=='start')
                    prepared['profile']['contactEvents'].insert(0,dict(offset=0,axis='Y',direction=-1,
                        boxes=[s['collisionScene']['solids'][start['solidIndex']]]))
                    choices.append(prepared)
            elif goal_choices is not None:
                choices=goal_choices(s,parent)
            else:
                # Certifying subtree learns the global hard latch. A later
                # incoming history must not reopen a two-jump local structure.
                choices=[extend(s,parent,'goal',20,extra=parent['profile']['duration']==11)]
                for _ in range(finishes-1):
                    # An equally timed, lower-scoring finish (objective -1 per sneak input).
                    choices.append(copy.deepcopy(choices[-1]))
                    choices[-1]['profile']['sneak'][len(choices)-2]=True
            while saved['cursor']<len(choices):
                p=choices[saved['cursor']];saved['cursor']+=1
                if node_probe is not None and target=='mid':node_probe(node_evaluator,p)
                yield p
            saved['exhausted']=True
        def evaluate(target_scene,p,compiler,adapter,work,motion,**kwargs):
            n=p['profile']['duration']
            candidate=dict(yaw=[0.]*n,positions=[[.5,.5]]*(n+1),velocity=[[0.,0.]]*(n+1),objective=-float(sum(p['profile']['sneak'])))
            best=dict(candidate=candidate,plan=dict(ticks=n,walls=[]),allocation=[0],cost=0,route=dict(source='relaxed',objective=dict(axis='X',sense='MAX')))
            frame_end=kwargs.get('final_route') is not None
            if not frame_end:
                scoped=ScopedWork(work,None,min(work.deadline,time.monotonic()+kwargs['seconds']))
                for _ in range(requests_per_evaluation):
                    scoped.before_solve({})
                    if advance is not None:advance()
                dispatched.append((p['terminalSupport'],p['profile']['duration'],kwargs.get('solve_policy')))
                if relaxed and p['terminalSupport']=='goal':
                    candidate.update(model='relaxed_disk',controls=[[0.,0.]]*n)
            extra={}
            node=(kwargs.get('solve_policy') or {}).get('purpose')=='node'
            hook=node_best if node else leg_best
            if hook is not None:
                extra=hook(n)
                best['plan'].update(walls=extra['walls'],events=extra['events'])
                best.update({k:v for k,v in extra.items() if k.startswith('alternative')})
                candidate.update({k:v for k,v in extra.items() if k=='positions'})
            if frame_end:
                # A chosen witness's frame-end re-solve; by default it answers with the witness's own point.
                work.counts['workerLaunches']+=1
                physical=final(p,kwargs,best) if final is not None else best
                return dict(status='feasible' if physical else 'unknown',termination='done',best=physical,
                            work=dict(workerLaunches=1),fullSceneValidated=physical is not None)
            return dict(status='feasible',termination='done',best=best,work=dict(solverCalls=requests_per_evaluation),
                        fullSceneValidated=True,nodeFeasibility=dict(status='feasible',model=str(n)),
                        **({'repairState':extra['repairState']} if 'repairState' in extra else {}))
        with patch('parkour_solver.search.engine.motion_bound',return_value={}), \
             patch('parkour_solver.search.dfs.iter_movement_programs',side_effect=generate), \
             patch('parkour_solver.search.engine.evaluate_program',side_effect=evaluate), \
             patch('parkour_solver.validation.witness.replay_witness',return_value=dict(passed=True)):
            options=dict(seconds=seconds,max_solver_calls=calls)
            options.update(settings)
            result=find_checkpoint_route(s,compiler or object(),anchor_search=dict(anchors=['mid'],observability=logging,
                solveEveryDecisionLevels=interval),minimum_run_pruning=False,
                resume_state=resume,on_progress=on_progress,cancel_event=cancel_event,**options)
        return result,dispatched

    def test_touching_goal_makes_one_group_without_an_anchor_solve(self):
        # The goal touches mid's side (higher, so a face of its own), so start -> mid -> goal is one encounter group.
        def goal_choices(s,parent):
            return [extend(s,parent,'goal',15),extend(s,extend(s,parent,'mid',10),'goal',20)]
        result,calls=self.run_tree(goal=(5,0,0,6,2.5,1),goal_choices=goal_choices)
        groups=result['adapter']['guidePlan']['groups']
        self.assertEqual([(g['first'],g['last']) for g in groups],[(0,1)])
        purposes=[(b,t,(p or {}).get('purpose')) for b,t,p in calls]
        self.assertNotIn('anchor',[p for _,_,p in purposes])
        self.assertIn(('goal',20,'terminal'),purposes)
        self.assertNotIn(15,[t for _,t,_ in purposes])
        self.assertEqual(result['best']['movementProgram']['profile']['duration'],20)

    @patch.dict('os.environ', {'PKC_NATIVE_CORE': ''})  # reads the Python frontier's arguments; the native loop runs its own
    def test_anchor_frontier_gets_the_minimum_run_policy_for_ordinary_decisions(self):
        from parkour_solver.bridge.python_compiler import PythonCompiler
        generated=[]
        with PythonCompiler() as compiler:
            self.run_tree(generated=generated,compiler=compiler)
        (roots,options),*_=generated
        policy=options['run_policy'];s=scene();root=roots[0]
        self.assertIs(root['decisionTurnaround'],False)
        self.assertTrue(policy['ordinary'](root))
        self.assertFalse(policy['ordinary'](dict(root,decisionTurnaround=True)))
        self.assertEqual(policy['targets'](root,extend(s,root,'mid',10)),{('anchor','mid')})
        self.assertEqual(policy['targets'](root,extend(s,root,'goal',10)),set())
        self.assertEqual((policy['next'](root),policy['face'](root)),('mid','start'))
        self.assertIn('["anchor", "mid"]',[e['key'] for e in policy['candidates'](root)])
        # A jump back onto the 1x1 start from its centre, along the +X leg to mid, overshoots its top face.
        from parkour_solver.movement.generation import generate_choices
        from parkour_solver.movement.program import append_choice, boundary_state
        from parkour_solver.movement.contact_combinations import scene_contact_options
        self.assertEqual(root['decisionVelocity'],s['initial']['velocity'][::2])
        context=dict(mode='open',scene=s,program=root,state=boundary_state(root),interactionGate=None,threshold=0.,maxInputs=20,maxEntryInputs=20)
        hop=next(c for c in (append_choice(root,choice,max_inputs=20) for choice in generate_choices('normal',context))
                 if c is not None and c['profile']['grounded'][-1] and c.get('terminalSupport')=='start')
        fit=policy['fits'](root,hop)
        (top,)=[o for o in scene_contact_options(s,1,1) if o.plane==1]
        self.assertTrue(policy['back'](root,hop))
        # The face comes from the visited owner, not terminalSupport, which a blip-up does not set.
        self.assertTrue(policy['back'](dict(root,terminalSupport=None),dict(hop,terminalSupport=None)))
        self.assertEqual((fit['room'],fit['face']),(max(r[2] for r in top.regions)-root['decisionPoint'][0],'start'))
        self.assertFalse(fit['fits'])

    @patch.dict('os.environ', {'PKC_NATIVE_CORE': ''})  # reads the Python frontier's arguments; the native loop runs its own
    def test_a_jump_onto_a_block_merged_into_the_launch_face_lands_back_on_that_face(self):
        from parkour_solver.bridge.python_compiler import PythonCompiler
        from parkour_solver.movement.contact_combinations import face_name, top_faces
        from parkour_solver.movement.generation import generate_choices
        from parkour_solver.movement.program import append_choice, boundary_state
        # pad and rest are flush at start's height: one merged face, two blocks long on X, that is no anchor. Faces merge
        # in player space, so pad keeps more than a player's width from start.
        extra=(('pad',(2,0,0,3,1,1)),('rest',(3,0,0,4,1,1)))
        generated=[]
        with PythonCompiler() as compiler:
            self.run_tree(extra=extra,generated=generated,compiler=compiler)
        (roots,options),*_=generated
        policy=options['run_policy'];s=scene(extra=extra)
        def landings(p, support):
            context=dict(mode='open',scene=s,program=p,state=boundary_state(p),interactionGate=None,threshold=0.,maxInputs=20,maxEntryInputs=20)
            return next(c for c in (append_choice(p,choice,max_inputs=20) for choice in generate_choices('normal',context))
                        if c is not None and c['profile']['grounded'][-1] and c.get('terminalSupport')==support)
        # One landing per face: the jump onto pad lands on pad+rest, and so does the hop back onto it.
        decision=dict(landings(roots[0],'pad+rest'),decisionPoint=[2.5,.5],decisionVelocity=[0.,0.])
        hop=landings(decision,'pad+rest')
        self.assertTrue(policy['back'](decision,hop))
        fit=policy['fits'](decision,hop)
        (top,)=[o for o in top_faces(s) if face_name(s,o)=='pad+rest']
        self.assertEqual((fit['face'],fit['room']),('pad+rest',max(r[2] for r in top.regions)-2.5))

    def test_anchor_search_uses_run_budget_across_routes_and_constraint_requests(self):
        # A frame scores its whole queue (both mid arrivals) before it descends.
        clock=[100.]
        def advance():clock[0]+=.05
        with patch('time.monotonic',side_effect=lambda:clock[0]):
            result,calls=self.run_tree(calls=100,seconds=100,requests_per_evaluation=30,advance=advance)
        self.assertEqual([(b,t) for b,t,_ in calls],[('mid',10),('mid',11),('goal',20)])
        self.assertEqual(result['work']['solverCalls'],90)
        allocations=result['searchState']['allocations']
        self.assertEqual(len(allocations),1)
        self.assertEqual(allocations[0]['calls'],90)
        self.assertEqual(result['best']['plan']['ticks'],20)

    def test_global_deadline_still_stops_anchor_search_and_keeps_continuation(self):
        clock=[100.]
        def advance():clock[0]+=2.
        with patch('time.monotonic',side_effect=lambda:clock[0]):
            first,calls1=self.run_tree(seconds=1,advance=advance)
        self.assertEqual(first['termination'],'deadline')
        self.assertEqual([(b,t) for b,t,_ in calls1],[('mid',10)])
        second,calls2=self.run_tree(resume=json.loads(json.dumps(first['searchState'])))
        self.assertEqual([(b,t) for b,t,_ in calls1+calls2],[('mid',10),('mid',11),('goal',20)])
        self.assertEqual(second['best']['plan']['ticks'],20)

    def test_global_latch_survives_abandoned_certifier_and_changed_incoming_prefix(self):
        result,calls=self.run_tree()
        self.assertEqual([(b,t) for b,t,_ in calls],[('mid',10),('mid',11),('goal',20)])
        dfs=result['searchState']['dfs']
        self.assertIn('mid→goal',dfs['hardLatches'])
        self.assertIn('start→mid',dfs['hardLatches'])
        self.assertGreaterEqual(sum(t['kind']=='backtrack' for t in dfs['transitions']),2)
        self.assertTrue(any(t['kind']=='candidate_exclusion' for t in dfs['transitions']))
        self.assertEqual(dfs['best']['plan']['ticks'],20)

    def test_logging_does_not_change_solver_allocation_and_anchors_force_queries(self):
        on,on_calls=self.run_tree(logging=True,interval=4)
        off,off_calls=self.run_tree(logging=False,interval=4)
        self.assertEqual(on_calls,off_calls)
        self.assertEqual(on['work']['solverCalls'],off['work']['solverCalls'])
        self.assertEqual(on['searchState']['dfs']['hardLatches'],off['searchState']['dfs']['hardLatches'])
        self.assertTrue(on['searchState']['dfs']['observations']['decisions'])
        self.assertEqual(off['searchState']['dfs']['observations']['decisions'],{})
        self.assertTrue(all(p['purpose']=='anchor' and not p['stopOnFeasible'] and p['rankEqualCost'] for b,t,p in on_calls if b=='mid'))
        self.assertTrue(all(p['purpose']=='terminal' and not p['stopOnFeasible'] for b,t,p in on_calls if b=='goal'))

    def test_latch_resume_keeps_exact_parent_and_does_not_repeat_scored_nodes(self):
        first,calls1=self.run_tree(calls=3)
        saved=json.loads(json.dumps(first['searchState']))
        second,calls2=self.run_tree(resume=saved)
        self.assertEqual([(b,t) for b,t,_ in calls1+calls2],[('mid',10),('mid',11),('goal',20)])
        self.assertEqual(first['searchState']['dfs']['hardLatches'],second['searchState']['dfs']['hardLatches'])

    def test_resume_keeps_nested_incumbent_even_without_time_for_a_service_slice(self):
        first,_=self.run_tree(calls=3)
        saved=json.loads(json.dumps(first['searchState']))
        saved['best']=None  # A callback may publish DFS before the allocator catches up.
        result,calls=self.run_tree(resume=saved,seconds=1e-6)
        self.assertEqual(result['best']['plan']['ticks'],20)
        self.assertEqual(calls,[])
        self.assertIsNone(saved['best'])

    def queued_tree(self, refinement=False):
        # Three calls score both mid arrivals and the 20-tick finish: an incumbent, then the budget ends.
        first,_=self.run_tree(calls=3)
        saved=json.loads(json.dumps(first['searchState']))
        root=saved['dfs']['stack'][0]
        template=root['records'][0]
        for n in (11,12,13):
            p=extend(scene(),history(scene(),[]),'mid',n)
            record=copy.deepcopy(template)
            record.update(id='queued-'+str(n),program=p,child=copy.deepcopy(p),arrival=n,ticks=n,tried=False,
                witness=dict(plan=dict(ticks=n,walls=[]),allocation=[0],cost=0,route=dict(source='relaxed',objective=dict(axis='X',sense='MAX')),
                    candidate=dict(yaw=[0.]*n,positions=[[.5,.5]]*(n+1),velocity=[[0.,0.]]*(n+1),objective=0.)))
            if refinement:
                record.pop('witness')
                record['program']['repairState']=dict(pending=[dict(branch='unfinished')])
            root['records'].append(record)
        root.update(phase='select',ready=[],sourceExhausted=True)
        return saved

    def check_lazy_queue_bound(self, *, refinement=False, remaining=15):
        saved=self.queued_tree(refinement)
        checked=[];cancel=threading.Event()
        def bound(guides,span,program,motion,**kwargs):
            checked.append(program['profile']['duration'])
            return dict(remainingTicks=remaining)
        def progress(state):
            last=state['dfs']['transitions'][-1]
            if last['kind']=='candidate_exclusion' or last['kind']=='open' and last['depth']==1:
                cancel.set()
        with patch('parkour_solver.search.bounds.remaining.remaining_bound',side_effect=bound):
            result,calls=self.run_tree(resume=saved,on_progress=progress,cancel_event=cancel)
        self.assertEqual(checked,[11])
        self.assertEqual(calls,[])
        root=result['searchState']['dfs']['stack'][0]
        unselected=[r for r in root['records'] if r['id'] in ('queued-12','queued-13')]
        self.assertTrue(all(not r['tried'] and not r.get('timeBoundExclusion') and not r.get('refinementTimeExclusion') for r in unselected))
        return result

    def test_time_bound_checks_only_selected_arrival_not_other_queued_records(self):
        result=self.check_lazy_queue_bound()
        self.assertEqual(result['searchState']['dfs']['transitions'][-1]['kind'],'candidate_exclusion')

    def test_time_bound_checks_only_selected_refinement_not_other_pending_records(self):
        self.check_lazy_queue_bound(refinement=True)

    def test_equal_optimistic_finish_is_kept_without_checking_other_queued_records(self):
        result=self.check_lazy_queue_bound(remaining=9)
        self.assertEqual(result['searchState']['dfs']['stack'][-1]['parents'][0]['profile']['duration'],11)

    def test_global_latch_accepts_setup_after_certifier_and_backtracking(self):
        result,calls=self.run_tree(setup=True)
        frames=result['searchState']['dfs']['frames']
        prepared=[r for f in frames if f['index']==0 for r in f['records']
                  if r.get('transferStructure',{}).get('preparationJumps')]
        self.assertEqual(len(prepared),1)
        self.assertEqual(prepared[0]['status'],'feasible')
        self.assertEqual(prepared[0]['transferStructure']['transferJumps'],[1])
        from parkour_solver.search.anchors.policy import HARD_JUMP_POLICY_VERSION
        self.assertEqual(result['searchState']['dfs']['hardLatches']['start→mid']['version'],HARD_JUMP_POLICY_VERSION)
        self.assertEqual(prepared[0]['transferStructure']['assumption'],'hard_transfer_without_stepping')

    def test_incompatible_old_latch_policy_is_rejected_on_resume(self):
        first,_=self.run_tree(calls=2)
        with patch('parkour_solver.search.anchors.policy.HARD_JUMP_POLICY_VERSION',1):
            with self.assertRaisesRegex(ValueError,'Incompatible automatic event'):
                self.run_tree(resume=first['searchState'])

    def test_completed_decision_cadence_and_forced_anchor_or_terminal(self):
        for interval,expected in ((1,[1,2,3,4]),(2,[2,4]),(4,[4])):
            solved=[];last=0
            for depth in (1,2,3,4):
                p=dict(decisionDepth=depth,lastSolvedDepth=last)
                if solve_due(p,interval):solved.append(depth);last=depth
            self.assertEqual(solved,expected)
        self.assertTrue(solve_due(dict(decisionDepth=1,lastSolvedDepth=0),4,forced=True))
        self.assertFalse(solve_due(dict(decisionDepth=1,lastSolvedDepth=1),1))

    @patch.dict('os.environ', {'PKC_NATIVE_CORE': ''})  # stubs evaluate_program, which a native decision check never calls
    def test_decision_check_keeps_settled_rows_and_queues_sibling_choice(self):
        settled=dict(leg=0,offset=3,mode='X',cmp='GE',rhs=.25,name='avoidance')
        unsettled=dict(leg=0,offset=6,mode='Z',cmp='GE',rhs=.1,name='avoidance')
        kept=dict(mode='Z',t1=2,cmp='LE',rhs=.75,name='route:0')
        choice=dict(kind='cluster',clusterId=0,begin=0,end=5,sides=[[['X',1,.25]]])
        checked=[]
        def probe(node_evaluator,p):
            p=copy.deepcopy(p);p['decisionNode']='node:'+str(p['profile']['duration'])
            # A ground touch at 5 closes the stretch before it; the prefix ends airborne, so 5 onward is open.
            p['profile']['grounded']=[True]+[False]*4+[True]+[False]*(p['profile']['duration']-5)
            p['profile']['regionChoices']=[dict(surface='s',owner='mid',offset=t,options=[[]]) for t in (3,7)]
            checked.append(node_evaluator(p,{},None))
        def node_best(n):
            return dict(walls=[dict(mode='X',t1=3,cmp='GE',rhs=.25,name='avoidance'),
                               dict(mode='Z',t1=6,cmp='GE',rhs=.1,name='avoidance'),kept],
                        events=[dict(jump=0,landed=0,landing=n,runTicks=0)],alternativeRows=[settled,unsettled],
                        alternativeBranchChoices=[choice],alternativeBranchKey='choice:a')
        self.run_tree(node_probe=probe,node_best=node_best)
        result=checked[0]
        walls=[{k:r[k] for k in ('mode','t1','cmp','rhs')} for r in result['program']['profile']['positionConstraints']]
        self.assertIn(dict(mode='Z',t1=2,cmp='LE',rhs=.75),walls)
        self.assertIn(dict(mode='X',t1=3,cmp='GE',rhs=.25),walls)
        self.assertNotIn(dict(mode='Z',t1=6,cmp='GE',rhs=.1),walls)
        self.assertEqual(result['program']['settledThrough'],5)
        self.assertEqual([c['offset'] for c in result['program']['profile']['regionChoices']],[7])
        self.assertEqual(result['alternatives'],[])

    @patch.dict('os.environ', {'PKC_NATIVE_CORE': ''})  # stubs evaluate_program, which a native decision check never calls
    def test_decision_check_holds_other_branch_choices_as_alternatives(self):
        from parkour_solver.search.repair_state import branch_key
        checked=[]
        def probe(node_evaluator,p):
            p=copy.deepcopy(p);p['decisionNode']='node:'+str(p['profile']['duration'])
            checked.append(node_evaluator(p,{},None))
        def choice(n,rhs,key,value):
            row=dict(leg=0,offset=3,mode='X',cmp='GE',rhs=rhs,name='avoidance')
            return dict(candidate=dict(yaw=[0.]*n,positions=[[.5,.5]]*(n+1),velocity=[[0.,0.]]*(n+1),objective=value),
                        plan=dict(ticks=n,walls=[dict(row,t1=3)],events=[dict(jump=0,landed=0,landing=n,runTicks=0)]),
                        allocation=[0],cost=0,route=dict(objective=dict(axis='X',sense='MAX')),
                        alternativeRows=[row],alternativeBranchKey=key)
        def node_best(n):
            winner,other=choice(n,.25,'a',1.),choice(n,.5,'b',.5)
            return dict(walls=winner['plan']['walls'],events=winner['plan']['events'],alternativeRows=winner['alternativeRows'],
                        alternativeBranchKey='a',repairState=dict(incumbent=dict(rows=[],passages=[],hits=[]),
                            acceptedVariants=[dict(branch=branch_key([],[],[]),best=winner),dict(branch='other',best=other)]))
        self.run_tree(node_probe=probe,node_best=node_best)
        result=checked[0]
        held,=result['alternatives']
        rows=[{k:r[k] for k in ('mode','t1','cmp','rhs')} for r in held['profile']['positionConstraints']]
        self.assertIn(dict(mode='X',t1=3,cmp='GE',rhs=.5),rows)
        self.assertNotIn(dict(mode='X',t1=3,cmp='GE',rhs=.25),rows)
        self.assertEqual(held['alternativeOf'],'node:10')
        self.assertNotIn(held['decisionNode'],('node:10',result['program']['decisionNode']))
        self.assertEqual(held.get('decisionParent'),result['program'].get('decisionParent'))
        self.assertNotIn('needsNodeSolve',held)

    @patch.dict('os.environ', {'PKC_NATIVE_CORE': ''})  # stubs evaluate_program, which a native decision check never calls
    def test_interrupted_node_climb_is_suspended_not_decided(self):
        checked=[]
        def probe(node_evaluator,p):
            p=copy.deepcopy(p);p['decisionNode']='node:'+str(p['profile']['duration'])
            checked.append(node_evaluator(p,{},None))
        def node_best(n):
            # The climb found a best but still has a queued branch: a cancelled or out-of-time check.
            return dict(walls=[],events=[dict(jump=0,landed=0,landing=n,runTicks=0)],
                        repairState=dict(pending=[dict(rows=[],passages=[],hits=[],score=0.)],completed=[]))
        self.run_tree(node_probe=probe,node_best=node_best)
        result=checked[0]
        self.assertEqual(result['status'],'suspended')
        self.assertEqual(result['alternatives'],[])
        self.assertNotIn('settledThrough',result['program'])
        self.assertEqual(result['program']['repairState']['pending'],[dict(rows=[],passages=[],hits=[],score=0.)])

    @patch.dict('os.environ', {'PKC_NATIVE_CORE': ''})  # stubs evaluate_program, which a native decision check never calls
    def test_a_forward_side_variant_reads_its_keep_outside_row_at_its_tick(self):
        checked,nodes={},{}
        def probe(node_evaluator,p):
            for t1,cmp,rhs in ((3,'LE',1.),(4,'LE',1.),(5,'LE',1.),(4,'GE',.9992)):
                q=copy.deepcopy(p);q['decisionNode']=f'node:{t1}{cmp}'
                q['sideVariant']=dict(forward=True,row=dict(mode='Z',t1=t1,cmp=cmp,rhs=rhs,name='case:outside'))
                checked[t1,cmp]=node_evaluator(q,nodes,None)
            # A solved node answers from its saved outcome.
            checked['again']=node_evaluator(q,nodes,None)
            q=copy.deepcopy(p);q['decisionNode']='node:held'
            q['sideVariant']=dict(forward=False,row=dict(mode='Z',t1=4,cmp='LE',rhs=1.,name='case:outside'))
            checked['held']=node_evaluator(q,nodes,None)
        def node_best(n):
            # Z is 0.5 except 0.9997 at state 4: 0.0003 below the 1.0 row, 0.0005 above the 0.9992 one.
            positions=[[.5,.5]]*(n+1);positions[4]=[.5,.9997]
            return dict(walls=[],events=[dict(jump=0,landed=0,landing=n,runTicks=0)],positions=positions)
        self.run_tree(node_probe=probe,node_best=node_best)
        self.assertEqual({k:r['sideOutcome'] for k,r in checked.items() if k!='held'},
                         {(3,'LE'):'slack',(4,'LE'):'tight',(5,'LE'):'slack',(4,'GE'):'tight','again':'tight'})
        self.assertEqual(nodes['node:4GE']['sideOutcome'],'tight')
        self.assertEqual(checked['held']['status'],'feasible')
        self.assertNotIn('sideOutcome',checked['held'])

    def test_an_unsolved_forward_side_variant_is_tight(self):
        checked=[]
        def probe(node_evaluator,p):
            q=copy.deepcopy(p);q['decisionNode']='node:forward'
            q['sideVariant']=dict(forward=True,row=dict(mode='Z',t1=4,cmp='LE',rhs=1.,name='case:outside'))
            checked.append(node_evaluator(q,{},None))
        self.run_tree(node_probe=probe,interval=2)
        self.assertEqual((checked[0]['status'],checked[0]['sideOutcome']),('untested','tight'))

    def test_group_end_arrival_keeps_avoidance_rows_and_settles_through_arrival(self):
        def leg_best(n):
            return dict(walls=[dict(mode='X',t1=n-2,cmp='GE',rhs=.25,name='avoidance')],
                        events=[dict(jump=0,landed=0,landing=n,runTicks=0)])
        result,_=self.run_tree(leg_best=leg_best)
        program=result['best']['movementProgram']
        self.assertIn(dict(mode='X',t1=18,cmp='GE',rhs=.25,name='avoidance'),program['profile']['positionConstraints'])
        self.assertEqual(program['settledThrough'],20)

    def run_recovery_tree(self, reject_first=True, oracle=True, include_setup=False, finish_tick=30,
                          fallbacks=False, reject_all=False):
        from parkour_solver.scene.normalize import prepare_scene
        from tests.support.checkpoint_guidance import flat_scene
        from tests.support.minimum_runs import flight
        s=flat_scene();s['goalBlock']='finish';s['maxTotalRunTicks']=60;s['maxRunTicksPerJump']=60
        # Extend the normalized scene through its raw request representation.
        raw={k:v for k,v in s.items() if k not in ('collisionScene',)}
        raw['blocks']=[dict(id=b['id'],boxes=[s['collisionScene']['solids'][b['solidIndex']]]) for b in s['blocks']]
        raw['blocks'].append(dict(id='finish',boxes=[[8,0,0,9,1,1]]))
        raw['movementSearch']['maxInputs']=max(raw['movementSearch']['maxInputs'],finish_tick+1)
        s=prepare_scene(raw);calls=[]
        def generate(target_scene,*,report,initial_programs,continuation=None,**kwargs):
            saved=copy.deepcopy(continuation) if continuation else dict(expanded=1,cursor=0,exhausted=False)
            report['continuation']=saved
            if target_scene['goalBlock']=='goal':
                base=dict(s,goalBlock='goal')
                choices=[flight(base,r) for r in (0,1,2)]
                if include_setup:
                    from parkour_solver.movement.normal import normal_choices
                    from parkour_solver.movement.program import empty_program, append_choice, boundary_state
                    parent=empty_program(base['initial'],threshold=0.);parent['terminalSupport']='start'
                    options=normal_choices(dict(scene=base,state=boundary_state(parent),maxInputs=30,threshold=0.,interactionGate=None))
                    parent=append_choice(parent,next(c for c in options if c['terminalSupport']=='start' and c['profile']['jump'][0]))
                    options=normal_choices(dict(scene=base,state=boundary_state(parent),maxInputs=30,threshold=0.,interactionGate=None))
                    choices.append(append_choice(parent,next(c for c in options if c['terminalSupport']=='goal' and c['profile']['jump'][0])))
            else:
                # A restored fallback's child finishes one tick later, which the stub accepts.
                restored=any(r.get('name')=='fallback' for r in initial_programs[0]['profile']['positionConstraints'])
                choices=[extend(s,initial_programs[0],'finish',finish_tick+restored)]
            while saved['cursor']<len(choices):
                p=choices[saved['cursor']];saved['cursor']+=1;yield p
            saved['exhausted']=True
        def evaluate(target_scene,p,compiler,adapter,work,motion,**kwargs):
            n=p['profile']['duration']
            if kwargs.get('final_route') is None:
                # A frame-end re-solve (final_route) answers with the chosen witness's own point; only scoring is logged.
                work.before_solve({});calls.append((p['terminalSupport'],n))
            rejected=(reject_first and p['terminalSupport']=='finish' and n==finish_tick and
                      (reject_all or p['profile']['jump'][12]))
            best=None if rejected else dict(plan=dict(ticks=n,walls=p['profile']['positionConstraints']),
                candidate=dict(yaw=[0.]*n,positions=[[.5,.5]]*(n+1),velocity=[[0.,0.]]*(n+1),objective=0.),
                allocation=[0],cost=0,route=dict(source='stub',objective=dict(axis='X',sense='MAX')))
            repair=dict(pending=[],unresolved=[],constraintGeneration=dict(exhausted=True))
            if fallbacks and p['terminalSupport']=='goal':
                alternatives=[dict(branch=f'{n}:{i}',best=dict(copy.deepcopy(best),plan=dict(ticks=n,walls=list(best['plan']['walls'])+
                    [dict(mode='X',t1=1,cmp='GE',rhs=-100.-i,name='fallback')]))) for i in (0,1)]
                repair.update(fallbackVariants=alternatives,acceptedVariants=alternatives)
            return dict(status='unknown' if rejected else 'feasible',termination='done',best=best,
                nodeFeasibility=dict(status=('oracle_rejected' if oracle else 'suspended') if rejected else 'feasible',model=str(n)),
                work=dict(solverCalls=1),fullSceneValidated=not rejected,repairState=repair)
        with patch('parkour_solver.search.engine.motion_bound',return_value={}), \
             patch('parkour_solver.search.dfs.iter_movement_programs',side_effect=generate), \
             patch('parkour_solver.search.engine.evaluate_program',side_effect=evaluate):
            result=find_checkpoint_route(s,object(),anchor_search=dict(anchors=['goal']),seconds=3,max_solver_calls=20)
        return result,calls

    def test_failed_finish_restores_deferred_runs_on_actual_parent_without_redeferring(self):
        result,calls=self.run_recovery_tree()
        dfs=result['searchState']['dfs']
        self.assertEqual(calls[:4],[('goal',12),('finish',30),('goal',13),('finish',30)])
        self.assertEqual(result['anchorSearch']['runDeferrals'],2)
        self.assertEqual(result['anchorSearch']['runRestorations'],1)
        self.assertEqual(calls.count(('goal',13)),1)
        self.assertEqual(calls.count(('goal',14)),0)
        self.assertIsNotNone(result['best'])
        self.assertTrue(all(r['program']['terminalSupport']=='goal' for r in dfs['minimumRunExclusions']))

    def physical(self, p, objective):
        n=p['profile']['duration']
        return dict(candidate=dict(yaw=[1.]*n,positions=[[.5,.6]]*(n+1),velocity=[[0.,0.]]*(n+1),objective=objective),
                    plan=dict(ticks=n,walls=[]),allocation=[0],cost=0)

    def frame_ends(self, result):
        return [(t['kind'],t['depth']) for t in result['searchState']['dfs']['transitions'] if t['kind'].startswith('frame_end')]

    def test_every_chosen_witness_gets_a_full_strength_frame_end_solve(self):
        calls=[]
        def final(p,kwargs,best):
            calls.append((p['terminalSupport'],kwargs['solve_policy'],kwargs['final_route']))
            return self.physical(p,-.5)
        result,_=self.run_tree(final=final)
        # The second mid arrival is chosen too; its finish (21 ticks) is excluded by the incumbent's 20.
        self.assertEqual([(support,policy['purpose']) for support,policy,_ in calls],
                         [('mid','anchor'),('goal','terminal'),('mid','anchor')])
        self.assertEqual(calls[1][1],dict(purpose='terminal',stopOnFeasible=False,rankEqualCost=False,oracleRejection=True,
                                          numerical='full_strength'))
        # The witness's own branch route; the solve seeds itself from the witness's point.
        self.assertEqual(calls[1][2],dict(source='relaxed',objective=dict(axis='X',sense='MAX')))
        self.assertEqual(self.frame_ends(result),[('frame_end_confirmed',0),('frame_end_confirmed',1),('frame_end_confirmed',0)])
        self.assertEqual((result['work']['frameEnds'],result['work']['frameEndRejections'],result['work']['frameEndIpoptCalls']),
                         (3,0,3))

    def test_a_relaxed_finish_becomes_the_incumbent_only_through_its_frame_end_answer(self):
        result,_=self.run_tree(relaxed=True,final=lambda p,kwargs,best:self.physical(p,-.5))
        self.assertEqual((result['status'],result['best']['candidate']['objective']),('feasible',-.5))
        self.assertNotIn('model',result['best']['candidate'])
        self.assertEqual(result['searchState']['dfs']['best']['candidate'],result['best']['candidate'])
        incumbents=[t['objective'] for t in result['searchState']['dfs']['transitions'] if t['kind']=='incumbent']
        self.assertEqual(incumbents,[-.5])

    def test_an_anchor_child_frame_starts_from_the_frame_end_answer(self):
        generated=[]
        def final(p,kwargs,best):
            answer=self.physical(p,0.)
            answer['candidate']['positions'][-1]=[.5,.7]
            return answer
        self.run_tree(final=final,generated=generated)
        goal=next(programs[0] for programs,_ in generated if programs[0]['terminalSupport']=='mid')
        self.assertEqual(goal['decisionPoint'],[.5,.7])
        self.assertEqual(goal['guideSeedYaw'],[1.]*goal['profile']['duration'])

    def sneaking(self, s, parent, block, ticks, sneaks):
        # Equally timed arrivals; the stub's objective is -1 per sneak input.
        p=extend(s,parent,block,ticks)
        for t in sneaks:p['profile']['sneak'][t]=True
        return p

    def test_every_frame_scores_its_whole_queue_then_takes_fewest_ticks_then_best_objective(self):
        generated=[]
        result,calls=self.run_tree(generated=generated,
            mid_choices=lambda s,parent:[self.sneaking(s,parent,'mid',10,[0]),self.sneaking(s,parent,'mid',10,[])])
        self.assertEqual([(b,t) for b,t,_ in calls][:2],[('mid',10),('mid',10)])
        first=next(programs[0] for programs,_ in generated if programs[0]['terminalSupport']=='mid')
        self.assertFalse(any(first['profile']['sneak']))

    def test_a_final_frame_tries_its_next_finish_while_it_can_beat_the_incumbent(self):
        calls=[]
        def final(p,kwargs,best):
            calls.append((p['terminalSupport'],sum(p['profile']['sneak'])))
            # The best relaxed finish confirms below the next one's relaxed value.
            return self.physical(p,-2.) if p['terminalSupport']=='goal' and not any(p['profile']['sneak']) else best
        result,_=self.run_tree(relaxed=True,final=final,
            goal_choices=lambda s,parent:[self.sneaking(s,parent,'goal',20,[]),self.sneaking(s,parent,'goal',20,[0])])
        self.assertEqual([c for c in calls if c[0]=='goal'][:2],[('goal',0),('goal',1)])
        self.assertEqual(result['best']['candidate']['objective'],-1.)

    def test_the_budget_end_confirms_the_best_scored_finish(self):
        # Three calls score both mid arrivals and one of the two finishes; the other is never scored.
        result,_=self.run_tree(relaxed=True,calls=3,final=lambda p,kwargs,best:self.physical(p,-2.) if p['terminalSupport']=='goal' else best,
            goal_choices=lambda s,parent:[self.sneaking(s,parent,'goal',20,[]),self.sneaking(s,parent,'goal',20,[0])])
        self.assertEqual((result['termination'],result['best']['candidate']['objective']),('numerical_call_limit',-2.))
        self.assertEqual(self.frame_ends(result)[-1],('frame_end_confirmed',1))

    def test_a_rejected_finish_falls_back_to_the_frames_next_one(self):
        result,_=self.run_tree(relaxed=True,finishes=2,final=lambda p,kwargs,best:(
            best if p['terminalSupport']!='goal' else self.physical(p,-1.5) if any(p['profile']['sneak']) else None))
        self.assertEqual(self.frame_ends(result),[('frame_end_confirmed',0),('frame_end_rejected',1),('frame_end_confirmed',1),
                                                  ('frame_end_confirmed',0)])
        self.assertEqual(result['best']['candidate']['objective'],-1.5)
        self.assertEqual(result['work']['frameEndRejections'],1)

    def test_when_every_frame_end_solve_fails_no_route_is_published(self):
        result,_=self.run_tree(relaxed=True,finishes=2,
                               final=lambda p,kwargs,best:best if p['terminalSupport']!='goal' else None)
        self.assertIsNone(result['best'])
        self.assertEqual(result['status'],'unknown')
        self.assertIsNone(result['searchState']['dfs']['best'])
        self.assertEqual(result['work']['frameEndRejections'],2)

    def test_resume_repeats_an_interrupted_frame_end_solve_and_reuses_a_confirmed_one(self):
        calls=[]
        cancel=threading.Event()
        def final(p,kwargs,best):
            calls.append(p['terminalSupport'])
            if p['terminalSupport']=='goal' and calls.count('goal')==1:
                cancel.set()
                return None
            return self.physical(p,-.5)
        cancelled,_=self.run_tree(relaxed=True,final=final,cancel_event=cancel)
        self.assertEqual((cancelled['status'],cancelled['best']),('cancelled',None))
        saved=json.loads(json.dumps(cancelled['searchState']))
        resumed,_=self.run_tree(relaxed=True,final=final,resume=copy.deepcopy(saved))
        self.assertEqual(calls,['mid','goal','goal','mid'])
        self.assertEqual(resumed['best']['candidate']['objective'],-.5)
        again,_=self.run_tree(relaxed=True,final=final,resume=json.loads(json.dumps(resumed['searchState'])))
        self.assertEqual(len(calls),4)
        self.assertEqual(again['best']['candidate'],resumed['best']['candidate'])

    def test_more_jump_history_remains_first_class_after_an_earlier_finish_succeeds(self):
        result,calls=self.run_recovery_tree(reject_first=False,include_setup=True,finish_tick=50)
        self.assertIn(('goal',24),calls)
        self.assertEqual(calls.count(('finish',50)),2)
        self.assertNotIn(('goal',13),calls)
        self.assertEqual(result['anchorSearch']['runRestorations'],0)
        decisions=result['searchState']['dfs']['comparisonDecisions'].values()
        self.assertTrue(any(d['action']=='keep_protected' and 'more_jump_inputs' in d['protectionReasons'] for d in decisions))

    def test_successful_finish_never_restores_extra_hard_jump_runs(self):
        result,calls=self.run_recovery_tree(reject_first=False)
        self.assertEqual(calls,[('goal',12),('finish',30)])
        self.assertEqual(result['anchorSearch']['runRestorations'],0)
        self.assertEqual(result['anchorSearch']['runDeferrals'],2)

    def test_unknown_descendant_does_not_trigger_infeasible_recovery(self):
        result,calls=self.run_recovery_tree(oracle=False)
        self.assertEqual(calls,[('goal',12),('finish',30)])
        self.assertEqual(result['anchorSearch']['runRestorations'],0)

    def test_anchor_leg_keeps_fallbacks_without_sibling_records(self):
        result,calls=self.run_recovery_tree(reject_first=False,fallbacks=True)
        self.assertEqual(calls,[('goal',12),('finish',30)])
        dfs=result['searchState']['dfs']
        self.assertFalse(any(r.get('variantOf') for f in dfs['frames']+dfs['stack'] for r in f['records']))

    def test_failed_finish_restores_a_fallback_before_slower_runs(self):
        result,calls=self.run_recovery_tree(fallbacks=True)
        self.assertEqual(calls,[('goal',12),('finish',30),('finish',31)])
        self.assertEqual(result['anchorSearch']['runRestorations'],0)
        self.assertEqual(sum(t['kind']=='restore_alternative' for t in result['searchState']['dfs']['transitions']),1)
        self.assertEqual(result['best']['plan']['ticks'],31)

    def test_every_failed_record_keeps_its_fallbacks(self):
        result,calls=self.run_recovery_tree(fallbacks=True,include_setup=True,reject_all=True,finish_tick=50)
        dfs=result['searchState']['dfs']
        frame=next(f for f in dfs['frames']+dfs['stack'] if f['index']==0)
        chosen=[t['candidate'] for t in dfs['transitions'] if t['kind']=='choose' and t['frame']==frame['id']]
        restored=[t['candidate'] for t in dfs['transitions'] if t['kind']=='restore_alternative']
        self.assertGreaterEqual(len(chosen),2)
        self.assertEqual(frame['failedCandidates'][:2],chosen[:2])
        records={r['id']:r for r in frame['records']}
        self.assertEqual(records[restored[0]]['variantOf'],chosen[0])
        self.assertEqual(result['best']['plan']['ticks'],51)

    def test_external_resume_is_isolated_from_owned_internal_service(self):
        first,_=self.run_tree(calls=2)
        saved=json.loads(json.dumps(first['searchState']))
        before=copy.deepcopy(saved)
        self.run_tree(resume=saved)
        self.assertEqual(saved,before)



@unittest.skipUnless(os.environ.get('PKC_IPOPT_PYTHON'),'Set PKC_IPOPT_PYTHON')
class SideOutcomeNodeSolveTests(unittest.TestCase):
    @patch.dict('os.environ', {'PKC_NATIVE_CORE': ''})  # patches the Python frontier; the native loop runs its own
    def test_the_forward_slab_boost_row_is_slack_after_three_run_ticks_and_tight_after_five(self):
        # corridor-diagonal's measured state: on the start block's X- edge, moving -X+Z, before the target's Z- side.
        from parkour_solver.bridge.python_compiler import PythonCompiler
        from parkour_solver.movement.generation import generate_choices
        from parkour_solver.movement.program import append_choice, boundary_state
        from parkour_solver.scene.normalize import prepare_scene
        from tests.support.paths import AUTONOMY_DATA
        raw=json.loads((AUTONOMY_DATA/'fixtures/side-variants/corridor-diagonal.json').read_text())
        solved=raw['sideVariant']['solved']
        request=copy.deepcopy(raw['request']);request['initial'].update(position=solved['position'],velocity=solved['velocity'])
        s=prepare_scene(request)
        def context(p,**fields):
            return dict(mode='open',scene=s,program=p,state=boundary_state(p),interactionGate=None,threshold=0.,maxInputs=40,
                        maxEntryInputs=40,**fields)
        def forward(root,k):
            # k same-face run ticks, then the slab boost entering the target's Z- side, as the frontier queues it.
            p=root
            for _ in range(k):
                p,=[q for c in generate_choices('normal',context(p)) if c['profile']['duration']==1 and not any(c['profile']['jump'])
                    for q in [append_choice(p,c,max_inputs=40)] if q is not None and q.get('terminalSupport')=='start']
            choice,=[c for c in generate_choices('stepping',context(p,decision=root))
                     if c['profile']['caseEvents'][-1]['case']=='slab_boost' and (c.get('sideVariant') or {}).get('forward')]
            child=append_choice(p,choice,max_inputs=40)
            child.update({f:copy.deepcopy(root[f]) for f in ('decisionPoint','decisionVelocity','decisionPointTick') if f in root})
            tag=choice['sideVariant']
            child.update(decisionNode=f'forward:{k}',decisionParent=root.get('decisionNode'),decisionDepth=1,lastSolvedDepth=0,
                         needsNodeSolve=True,sideVariant=dict(tag,row=dict(tag['row'],t1=p['profile']['duration'])))
            return child
        found={}
        def generate(target_scene,*,report,initial_programs,node_evaluator,**kwargs):
            report['continuation']=dict(exhausted=True)
            for k in (3,5):
                result=node_evaluator(forward(initial_programs[0],k),{},None)
                found[k]=(result['status'],result['sideOutcome'])
            yield from ()
        with PythonCompiler() as compiler,patch('parkour_solver.search.dfs.iter_movement_programs',side_effect=generate):
            find_checkpoint_route(s,compiler,anchor_search=dict(anchors=[]),seconds=60,
                                  python_executable=os.environ['PKC_IPOPT_PYTHON'])
        # Measured in the production search: the row is 0.3309 clear after 3 run ticks and binds after 5.
        self.assertEqual(found,{3:('feasible','slack'),5:('feasible','tight')})


if __name__=='__main__':unittest.main()
