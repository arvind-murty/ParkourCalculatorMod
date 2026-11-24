from tests.support.anchor_policy import flush_scene, scene, history
from tests.support.paths import AUTONOMY_DATA
import copy
import json
import math
import unittest

from parkour_solver.scene.normalize import prepare_scene
from parkour_solver.search.anchors.policy import (SAME_FACE_FIT_FACTOR, audit, encounter_groups, local_counts, priority,
    same_face_fit, settings, visit_state, visits, within_caps, transfer_structure)
from parkour_solver.spatial.anchor_guides import build_guides, classify, decision_objective, nearest, onward_objective
from parkour_solver.search.anchors.adapter import AnchorAdapter
from parkour_solver.spatial.event_routes import build_route_domain, build_event_graph
from parkour_solver.search.feasibility import node_identity, outcome, policy_spec
from parkour_solver.observability.turns import record, replay, summary
from parkour_solver.movement.program import empty_program
from tests.support.checkpoint_guidance import flat_scene
from tests.support.archived_events import current_events


class EncounterGroupTests(unittest.TestCase):
    def groups(self, case):
        scene=prepare_scene(case['request'])
        names=[scene['startBlock']]+case['anchorSearch']['anchors']+[scene['goalBlock']]
        return {(g['first'],g['last']):g for g in encounter_groups(scene,names)}

    def case(self, name):
        from tools.paths import STANDALONE_ROOT
        return json.loads((STANDALONE_ROOT/'cases/real11'/name).read_text())

    def test_real11_four_anchor_groups_b4_through_the_goal(self):
        groups=self.groups(self.case('physical-anchors.json'))
        self.assertEqual(sorted(groups),[(0,0),(1,1),(2,2),(3,4)])
        self.assertEqual(groups[(3,4)]['anchors'],['b5','b7'])
        self.assertEqual(groups[(3,4)]['members'],['b5','b6','b7'])
        self.assertEqual(groups[(3,4)]['landings'],['b5','b7'])

    def test_real11_extended_group_runs_b4_to_b22(self):
        # b22 touches hit-only b6 and b8, which now extend the chain too.
        groups=self.groups(self.case('extended-anchors.json'))
        self.assertEqual(sorted(groups),[(0,0),(1,1),(2,2),(3,6)])
        self.assertEqual(groups[(3,6)]['anchors'],['b5','b7','b20','b22'])
        self.assertEqual(groups[(3,6)]['members'],['b10','b11','b12','b13a','b13b','b14','b15','b16','b17','b18','b19',
                                                   'b20','b22','b5','b6','b7','b8','b9a','b9b'])
        self.assertEqual(groups[(3,6)]['landings'],['b10','b11','b12','b13a','b13b','b15','b16','b18','b19','b20','b22',
                                                    'b5','b7','b8','b9a','b9b'])

    def test_hit_only_block_extends_the_chain(self):
        boxes=dict(start=[0,0,0,1,1,1],a=[3,0,0,4,1,1],w=[4,0,0,5,10,1],f=[5,0,0,6,1.5,1],goal=[0,0,3,1,1,4])
        request=dict(version=1,sceneModel='collision_boxes',maxWallHits=0,geometryBounds=[-5,-5,-5,12,12,10],
            initial=dict(position=[.5,1,.5],velocity=[0,-.0784000015258789,0],yaw=0),startBlock='start',goalBlock='goal',
            blocks=[dict(id=k,boxes=[v]) for k,v in boxes.items()],objective=dict(axis='Z',sense='MAX'),
            movementSearch=dict(types=['normal'],maxInputs=60))
        groups=self.groups(dict(request=request,anchorSearch=dict(anchors=['a'])))
        self.assertEqual(groups[(0,0)]['members'],['a','f','w'])
        self.assertEqual(groups[(0,0)]['landings'],['a','f'])


class AnchorPolicyTests(unittest.TestCase):
    def test_hard_transfer_excludes_stepping_but_preserves_closed_and_pending_preparations(self):
        s=scene()
        p=history(s,['mid'])
        p['profile']['caseEvents']=[dict(input=0,type='stepping',case='slab_boost')]
        self.assertEqual(transfer_structure(s,p,0,'start','mid',complete=True)['reason'],
                         'stepping_during_hard_transfer')
        p['profile']['caseEvents'][0].update(type='ceiling',case='grind')
        self.assertTrue(transfer_structure(s,p,0,'start','mid',complete=True)['valid'])
        p=history(s,['start','mid'])
        p['profile']['caseEvents']=[dict(input=0,type='stepping',case='corner_jump_cancel')]
        self.assertTrue(transfer_structure(s,p,0,'start','mid',complete=True)['valid'])
        # Until a pending cancelled flight lands, it may still close on S as
        # preparation. Do not prohibit that setup by guessing its destination.
        p=history(s,['mid']);p['profile']['contactEvents']=[]
        p['terminalSupport']=None;p['profile']['grounded'][-1]=False
        p['profile']['caseEvents']=[dict(input=0,type='stepping',case='corner_jump_cancel')]
        self.assertTrue(transfer_structure(s,p,0,'start','mid')['valid'])

    def test_group_pair_transfer_ignores_history_after_its_arrival(self):
        s=scene()
        p=history(s,['mid','optional'])
        self.assertEqual(transfer_structure(s,p,0,'start','mid',complete=True)['reason'],
                         'intermediate_support_between_anchors')
        self.assertTrue(transfer_structure(s,p,0,'start','mid',complete=True,until_arrival=True)['valid'])

    def test_group_end_arrival_requires_every_anchor_inside_the_group(self):
        s=scene((5,0,0,6,2.5,1));data=build_route_domain(s,dict(kind='landing',block='goal')).to_dict()
        adapter=AnchorAdapter(data,build_event_graph(data),settings(dict(anchors=['mid']),s))
        root=history(s,[]);cursor=adapter.initial_cursor([root])
        self.assertEqual((cursor['anchorIndex'],cursor['groupEnd']),(0,1))
        requests,_,_=adapter.next_goals(cursor,[root])
        self.assertEqual({r['goal']['block'] for r in requests},{'goal'})
        view=adapter.for_goal(requests[0],cursor)
        skipped,visited=history(s,['goal']),history(s,['mid','goal'])
        self.assertFalse(view.program_allowed(skipped))
        self.assertTrue(view.program_allowed(visited))
        self.assertEqual(view.bind_program(skipped).candidate({},dict(positions=[[.5,.5]]*2),{})['reason'],
                         'missing_prior_anchor')
        ctx=dict(program=history(s,['mid']));view.context(ctx)
        self.assertTrue(ctx['feetAllowed']('goal'))

    def test_owner_returns_excluded_but_all_same_owner_jumps_preserved(self):
        s=scene()
        for names in (['start','start','mid'],['start','optional','mid'],['mid','mid','goal']):
            p=history(s,names)
            self.assertTrue(visit_state(s,p)['valid'])
            self.assertEqual(local_counts(p)['J'],len(names))
        self.assertFalse(visit_state(s,history(s,['optional','start']))['valid'])
        self.assertEqual(audit(s,history(s,['optional','start']),['mid'])['reason'],'departed_owner_return')

    def test_anchor_order_optional_contacts_and_span_counts(self):
        s=scene();p=history(s,['start','optional','mid','mid'])
        self.assertTrue(audit(s,p,['mid'],reached=1)['accepted'])
        self.assertEqual(audit(s,p,['mid'])['ticks'],[3])
        self.assertFalse(audit(s,history(s,['optional']),['mid','optional'])['accepted'])
        p['profile']['grounded']=[True,True,False,True,True]
        p['profile']['jump']=[True,False,False,True]
        self.assertEqual(local_counts(p,1),dict(T=4,J=1,R=1,A=1,start=1))
        self.assertEqual(priority(p,1,False,'id'),(4,-1,1,'id'))
        # A requested grounded impulse counts even when vertical is canceled.
        p['profile']['jump'][1]=True
        self.assertEqual(local_counts(p,1)['J'],2)
        self.assertEqual(priority(p,1,False)[:3],(4,-2,0))
        self.assertEqual(priority(p,1,True)[:3],(4,-2,1))

    def test_unique_anchors_and_explicit_policy_validation(self):
        s=scene()
        for value in (dict(anchors=['mid','mid']),dict(anchors=['start']),dict(solveEveryDecisionLevels=0),
                      dict(turnaround=dict(spatialMargin=float('nan'))),dict(turnaround=dict(degrees=180))):
            with self.assertRaises(ValueError):settings(value,s)

    def test_backward_guides_finite_terminal_and_onward_objective(self):
        s=scene();data=build_route_domain(s,dict(kind='landing',block='goal')).to_dict()
        policy=settings(dict(anchors=['mid']),s)
        guides=build_guides(data,policy)
        self.assertEqual(guides['points'][-1],[.5,3,3])
        self.assertEqual(guides['points'][0],s['initial']['position'])
        self.assertEqual(tuple(guides['points'][1]),nearest(guides['covers']['mid'],guides['points'][-1]))
        self.assertTrue(guides['turns'])
        objective=onward_objective(guides,0,s['objective'])
        e,n=guides['points'][1:]
        self.assertAlmostEqual(objective['customYaw'],math.degrees(math.atan2(-(n[0]-e[0]),n[2]-e[2])) )
        json.dumps(guides,allow_nan=False)

    def test_decision_objective_aims_from_the_last_block_toward_the_next_required_anchor(self):
        from parkour_solver.spatial.checkpoints import direction_objective
        s=scene();data=build_route_domain(s,dict(kind='landing',block='goal')).to_dict()
        def view(anchors):
            return AnchorAdapter(data,build_event_graph(data),settings(dict(anchors=anchors),s))
        def aim(point,target):
            return direction_objective((target[0]-point[0],target[2]-point[2]))
        adapter=view(['mid']);points=adapter.guide_plan['points']
        # The start's guide point is the initial position.
        self.assertEqual(adapter.program_objective(history(s,['start'])),aim(points[0],points[1]))
        # Landing on the group's last anchor aims as the group end does.
        landed=history(s,['start','mid'])
        self.assertEqual(adapter.program_objective(landed),onward_objective(adapter.guide_plan,0,s['objective']))
        # Ending airborne aims from the block it took off from.
        airborne=history(s,['start','mid']);airborne['terminalSupport']=None;airborne['profile']['grounded'][-1]=False
        self.assertEqual(adapter.program_objective(airborne),adapter.program_objective(landed))
        # Inside the next anchor's turnaround a program aims past it.
        self.assertTrue(classify(adapter.guide_plan,0,[6.5,.5])['isTurnaround'])
        self.assertEqual(adapter.program_objective(history(s,['start','optional'])),aim([6.5,2,.5],points[2]))
        # The finish keeps the scene objective.
        self.assertEqual(adapter.program_objective(history(s,['start','mid','goal'])),s['objective'])
        # A block that is not an anchor aims from its top-face centre.
        adapter=view(['optional']);points=adapter.guide_plan['points']
        self.assertFalse(classify(adapter.guide_plan,0,[4.5,.5])['isTurnaround'])
        self.assertEqual(adapter.program_objective(history(s,['start','mid'])),aim([4.5,2,.5],points[1]))
        # Turnaround membership reads the end point's X and Z, never its height.
        g=dict(id='g',points=[[0,0,0],[5,0,0],[10,0,0],[0,0,10]],
               turns=[dict(id='t',index=2,spans=[1,2],normal=[0.,1.],offset=0.,margin=.3)])
        self.assertEqual(decision_objective(g,1,[3,5,-1],s['objective']),aim([3,5,-1],[10,0,0]))
        self.assertEqual(decision_objective(g,1,[3,-5,1],s['objective']),aim([3,-5,1],[0,0,10]))

    def test_nearest_preserves_disconnected_cover_and_stable_ties(self):
        boxes=[[0,1,0,1,1,1],[3,1,0,4,1,1]]
        self.assertEqual(nearest(boxes,[2,1,.5]),(1,1,.5))
        self.assertEqual(nearest(list(reversed(boxes)),[2,1,.5]),(1,1,.5))

    def test_axis_and_diagonal_binary_edge_equality_scope_and_reflection(self):
        for normal in ([1.,0.],[2**-.5,2**-.5],[-1.,0.]):
            g=dict(id='g',turns=[dict(id='t',spans=[0,1],normal=normal,offset=0.,margin=.3)])
            for distance,expected in ((-.31,False),(-.3,True),(-.29,True),(1.,True)):
                point=[distance*a for a in normal]
                # Diagonal roundoff should not obscure exact axis equality.
                if distance==-.3 and normal[1]:point[0]+=1e-15
                label=classify(g,0,point)
                self.assertIs(label['isTurnaround'],expected)
            self.assertFalse(classify(g,2,[100.,100.])['isTurnaround'])
            self.assertEqual(classify(g,2,[0.,0.])['distances'],[])

    def test_observability_is_binary_idempotent_and_logs_replay_flip(self):
        g=dict(id='g',turns=[dict(id='t',spans=[0,1],normal=[1.,0.],offset=0.,margin=.3)])
        s=scene();p=history(s,['mid']);p.update(decisionNode='n',decisionPoint=[-.4,0.])
        observations={}
        for _ in range(2):record(observations,g,p,span=0,start=0,model='m',status='feasible')
        self.assertEqual(len(observations['decisions']),1)
        self.assertEqual(len(observations['decisions']['n']['queries']),1)
        before=copy.deepcopy(p)
        replay(observations,g,'n',0,[1.,0.],'later')
        self.assertEqual(summary(observations)['0']['replayFlips'],1)
        self.assertEqual(before,p)
        self.assertEqual(observations['decisions']['n']['mode'],'ordinary')
        disabled=dict(enabled=False)
        record(disabled,g,p,span=0,start=0,model='m',status='feasible')
        self.assertNotIn('decisions',disabled)
        json.dumps(observations,allow_nan=False)

    def test_internal_node_does_not_require_future_landing_or_region(self):
        s=scene();data=build_route_domain(s,dict(kind='landing',block='goal')).to_dict()
        adapter=AnchorAdapter(data,build_event_graph(data),settings(dict(anchors=['mid']),s))
        root=history(s,[]);cursor=adapter.initial_cursor([root])
        requests,_,_=adapter.next_goals(cursor,[root])
        self.assertEqual({r['goal']['block'] for r in requests},{'mid'})
        view=adapter.for_goal(requests[0],cursor)
        p=history(s,['start'])
        node=view.for_node(p).bind_program(p)
        self.assertFalse(hasattr(node,'contact_regions'))
        self.assertTrue(node.candidate({},dict(positions=[[.5,.5]]*2),{})['accepted'])
        self.assertFalse(view.bind_program(p).candidate({},dict(positions=[[.5,.5]]*2),{})['accepted'])
        ctx=dict(program=p);view.context(ctx)
        self.assertEqual(set(ctx),{'program','feetAllowed'})
        self.assertTrue(ctx['feetAllowed']('optional'))
        self.assertTrue(ctx['feetAllowed']('start'))
        self.assertFalse(ctx['feetAllowed']('goal'))

    def test_exact_failure_has_no_ancestor_side_effect_or_or_parent_shortcut(self):
        policy=policy_spec(dict(purpose='node',oracleRejection=True),first_witness=True)
        result=dict(best=None,status='unknown',repairState=dict(completed=[dict(branch='b',oracleRejected=True)]))
        state={'parent':dict(status='untested')}
        state['child']=outcome(result,'child-model',policy)
        self.assertEqual(state['child']['status'],'oracle_rejected')
        self.assertEqual(state['parent']['status'],'untested')
        for field,value in (('pending',[{}]),('inFlight',{}),('constraintGeneration',dict(exhausted=False))):
            other=copy.deepcopy(result);other['repairState'][field]=value
            if field=='inFlight':other['repairState'][field]={'branch':'future'}
            self.assertEqual(outcome(other,'m',policy)['status'],'suspended')
        # An unresolved branch is infeasible, not unfinished work.
        other=copy.deepcopy(result);other['repairState']['unresolved']=[dict(rows=[],passages=[],hits=[],cause='done')]
        self.assertEqual(outcome(other,'m',policy)['status'],'oracle_rejected')
        result['status']='error'
        self.assertEqual(outcome(result,'m',policy)['status'],'error')

    def test_dynamic_repair_progress_does_not_change_node_identity_but_scope_does(self):
        p=history(scene(),['mid']);q=copy.deepcopy(p);q['repairState']={'cursor':1}
        self.assertEqual(node_identity(p,'ctx'),node_identity(q,'ctx'))
        q['solveScope']={'purpose':'preliminary'}
        self.assertNotEqual(node_identity(p,'ctx'),node_identity(q,'ctx'))

    def test_empty_exhausted_constraint_domain_backtracks_without_solver_rejection(self):
        policy=policy_spec(dict(purpose='node',oracleRejection=True),first_witness=True)
        result=dict(status='unknown',best=None,termination='contact_search_exhausted',repairState=dict(
            completed=[],pending=[],unresolved=[],inFlight=None,
            constraintGeneration=dict(exhausted=True,emitted=0,pending=[])))
        original=copy.deepcopy(result)
        verdict=outcome(result,'empty-node',policy)
        self.assertEqual(verdict['status'],'exhausted')
        self.assertEqual(verdict['rejectedAlternatives'],[])
        self.assertEqual(verdict['model'],'empty-node')
        self.assertEqual(result,original)
        restored=json.loads(json.dumps(result))
        self.assertEqual(outcome(restored,'empty-node',policy),verdict)
        for field,value in (('pending',[{}]),('inFlight',dict(branch='unfinished')),
                            ('constraintGeneration',dict(exhausted=False))):
            unfinished=copy.deepcopy(result);unfinished['repairState'][field]=value
            self.assertEqual(outcome(unfinished,'empty-node',policy)['status'],'suspended')
        unresolved=copy.deepcopy(result)
        unresolved['repairState']['unresolved']=[dict(rows=[],passages=[],hits=[],cause='uncertain_interactions')]
        self.assertEqual(outcome(unresolved,'empty-node',policy)['status'],'oracle_rejected')
        no_evidence=dict(status='unknown',best=None,repairState=dict(completed=[]))
        self.assertEqual(outcome(no_evidence,'unqueried',policy)['status'],'suspended')

    def test_ceiling_and_corner_cancel_are_impulses_without_invented_support_visits(self):
        s=scene();p=history(s,['start']);p['terminalSupport']=None
        p['profile']['contactEvents']=[]
        mid=next(b for b in s['blocks'] if b['id']=='mid')
        p['profile']['caseEvents']=[dict(input=0,case='corner_jump_cancel',
            boxes=[s['collisionScene']['solids'][mid['solidIndex']]])]
        p['profile']['grounded'][-1]=False
        state=visit_state(s,p)
        self.assertEqual(state['current'],'start')
        self.assertEqual(state['departed'],[])
        self.assertEqual(local_counts(p)['J'],1)

    def test_feet_cases_cannot_touch_a_departed_owner_but_ceilings_can(self):
        s=scene();p=history(s,['start','mid']);p['profile']['position'][2]=2.
        solid=lambda name:s['collisionScene']['solids'][next(b['solidIndex'] for b in s['blocks'] if b['id']==name)]
        p['profile']['caseEvents']=[dict(input=1,type='stepping',case='blip_up',boxes=[solid('start')])]
        self.assertEqual(audit(s,p,['mid'])['reason'],'departed_owner_return')
        p['profile']['caseEvents']=[dict(input=0,type='stepping',case='blip_up',boxes=[solid('start')])]
        self.assertTrue(audit(s,p,['mid'])['accepted'])
        p['profile']['caseEvents']=[dict(input=1,type='ceiling',case='ceiling_hit',boxes=[solid('start')])]
        state=visit_state(s,p)
        self.assertEqual((state['valid'],state['current'],state['departed']),(True,'mid',['start']))
        self.assertTrue(audit(s,p,['mid'])['accepted'])

    def test_a_blip_landing_on_one_owner_and_stepping_onto_another_is_not_a_return(self):
        s=scene();p=history(s,['start','start'])
        solid=lambda name:s['collisionScene']['solids'][next(b['solidIndex'] for b in s['blocks'] if b['id']==name)]
        p['profile']['contactEvents']=p['profile']['contactEvents'][:1]
        p['profile']['position'][2]=2.
        p['profile']['caseEvents']=[dict(input=1,type='stepping',case='blip',boxes=[solid('mid')],lowerBoxes=[solid('optional')])]
        p['terminalSupport']='mid'
        self.assertEqual([v for v in visits(s,p) if v[0]==2],[(2,'optional'),(2,'mid'),(2,'mid')])
        self.assertTrue(audit(s,p,['mid'])['accepted'])

    def test_run_ticks_across_a_flush_seam_stay_on_one_face(self):
        s=flush_scene()
        p=history(s,['start','A','B','A'])
        self.assertEqual(visits(s,p),[(0,'start'),(1,'start'),(2,'A+B'),(3,'A+B'),(4,'A+B'),(4,'A+B')])
        state=visit_state(s,p)
        self.assertEqual((state['valid'],state['current'],state['departed']),(True,'A+B',['start']))
        self.assertEqual(audit(s,history(s,['start','A','B','start']),[])['reason'],'departed_owner_return')

    def test_anchors_start_and_goal_must_be_whole_faces(self):
        s=flush_scene()
        self.assertEqual(settings({},s)['anchors'],[])
        with self.assertRaisesRegex(ValueError,'^A shares its top face with B; declare them as one block$'):
            settings(dict(anchors=['A']),s)
        for name,box in (('start',[1,0,0,1.5,1,1]),('goal',[7,0,0,8,1,1])):
            with self.subTest(name), self.assertRaisesRegex(ValueError,'^'+name+' shares its top face with .*pad'):
                settings({},flush_scene(('pad',box)))

    def test_tops_less_than_the_free_width_apart_are_one_face(self):
        # Gaps are 0.6 or 0.625 in practice; 0.612 absorbs rounding between them (user, 2026-11-23).
        for gap,shared in ((.6,True),(.605,True),(.625,False)):
            s=flush_scene(('pad',[7+gap,0,0,8+gap,1,1]))
            with self.subTest(gap):
                if shared:
                    with self.assertRaisesRegex(ValueError,'^goal shares its top face with pad'):
                        settings({},s)
                else:
                    self.assertEqual(settings({},s)['anchors'],[])

    def test_face_namer_names_one_block_by_its_face_and_cut_pieces_by_their_owner(self):
        from parkour_solver.search.anchors.policy import face_namer, support_face
        # wall stands through C's top and cuts it in two; E is flush with C's right piece only.
        s=flush_scene(('C',[10,0,0,13,1,1]),('W',[11,0,0,12,3,1]),('E',[13,0,0,14,1,1]))
        solid=lambda name:s['collisionScene']['solids'][next(b['solidIndex'] for b in s['blocks'] if b['id']==name)]
        name=face_namer(s)
        self.assertEqual((name([solid('A')]),name([solid('B')]),name([solid('A'),solid('B')])),('A+B',)*3)
        self.assertEqual((name([solid('C')]),name([solid('E')]),name([solid('C'),solid('E')])),('C','C+E','C+E'))
        self.assertEqual((name([solid('start')]),name([[50,0,0,51,1,1]])),('start',None))
        self.assertEqual([support_face(s,b) for b in ('A','B','start','A+B',None)],['A+B','A+B','start','A+B',None])

    def test_a_merged_pair_gives_one_advantageous_entry_and_one_target_key(self):
        from parkour_solver.reachability.service import face_table
        from parkour_solver.search.anchors.policy import advantageous_candidates, advantageous_targets
        # P+Q is a raised merged pair beside start (a step-up or slab boost), C+D a lower one (a run-off).
        s=flush_scene(('P',[0,0,1,1,1.5,2]),('Q',[0,0,2,1,1.5,3]),('C',[0,0,-2,1,.5,-1]),('D',[0,0,-3,1,.5,-2]))
        keys=[json.loads(e['key']) for e in advantageous_candidates(s,lambda name:face_table(s,name,{}),'start',1.,'goal',
                                                                        ['normal','stepping'],0.)]
        self.assertEqual([k for k in keys if k[1] not in ('A+B','goal')],
                         [['corner_jump_cancel','P+Q'],['landing','C+D'],['slab_boost','P+Q'],['step','P+Q']])
        solid=lambda name:s['collisionScene']['solids'][next(b['solidIndex'] for b in s['blocks'] if b['id']==name)]
        base=history(s,['start']);child=history(s,['start','start'])
        child['profile']['contactEvents'].pop();child['profile']['grounded'][-1]=False;child['terminalSupport']=None
        child['profile']['caseEvents']=[dict(input=1,case='slab_boost',boxes=[solid('Q')])]
        self.assertEqual(advantageous_targets(s,base,child,'goal'),{('slab_boost','P+Q')})

    def test_next_anchor_follows_the_required_order_then_the_task_block(self):
        from parkour_solver.search.anchors.policy import next_anchor
        s=scene();anchors=['mid','optional']
        self.assertEqual(next_anchor(s,history(s,['start']),anchors,'goal'),'mid')
        self.assertEqual(next_anchor(s,history(s,['start','optional']),anchors,'goal'),'mid')
        self.assertEqual(next_anchor(s,history(s,['start','mid']),anchors,'goal'),'optional')
        self.assertEqual(next_anchor(s,history(s,['start','mid','optional']),anchors,'goal'),'goal')

    def test_advantageous_targets_are_the_next_anchor_ceilings_and_named_jump_cases(self):
        from parkour_solver.search.anchors.policy import advantageous_targets
        s=scene();base=history(s,['start'])
        solid=lambda name:s['collisionScene']['solids'][next(b['solidIndex'] for b in s['blocks'] if b['id']==name)]
        def child(landing=None,jump=True,**events):
            c=history(s,['start',landing or 'start'])
            c['profile']['jump'][1]=jump
            if landing is None:
                c['profile']['contactEvents'].pop();c['profile']['grounded'][-1]=False;c['terminalSupport']=None
            for key,value in events.items():c['profile'].setdefault(key,[]).extend(value)
            return c
        targets=lambda c:advantageous_targets(s,base,c,'mid')
        self.assertEqual(targets(child('mid')),{('anchor','mid')})
        self.assertEqual(targets(child('mid',jump=False)),{('anchor','mid')})
        self.assertEqual(targets(child('optional')),set())
        self.assertEqual(targets(child('start')),set())
        ceiling=lambda *names,offset=1:[dict(offset=offset,axis='Y',direction=1,role='ceiling',boxes=[solid(n) for n in names])]
        self.assertEqual(targets(child(contactEvents=ceiling('optional'))),{('ceiling','optional')})
        self.assertEqual(targets(child(contactEvents=ceiling('optional','goal'))),{('ceiling','goal+optional')})
        self.assertEqual(targets(child(jump=False,contactEvents=ceiling('optional'))),set())
        self.assertEqual(targets(child(contactEvents=ceiling('optional',offset=0))),set())
        for case in ('grind','slab_boost','corner_jump_cancel'):
            self.assertEqual(targets(child(caseEvents=[dict(input=1,case=case,boxes=[solid('optional')])])),{(case,'optional')})
        for case in ('normal_step','blip','blip_up','corner_blip','corner_blip_up'):
            self.assertEqual(targets(child(caseEvents=[dict(input=1,case=case,boxes=[solid('optional')])])),set())

    def test_advantageous_candidates_match_the_generators_airtimes(self):
        from parkour_solver.movement.generation import generate_choices
        from parkour_solver.movement.program import append_choice, boundary_state
        from parkour_solver.reachability.service import face_table
        from parkour_solver.search.anchors.policy import advantageous_candidates, advantageous_targets
        # From start (top 2): low is a run-off with roof over it, slab a step-up, lid a low ceiling, goal a jump;
        # ledge, flush with start past slab's Z- edge, lets a corner jump cancel land.
        def course(ledge_top):
            return prepare_scene(dict(version=1, sceneModel='collision_boxes', geometryBounds=[-10,-8,-10,20,12,10],
                initial=dict(position=[1,2,1], velocity=[0,-.0784000015258789,0], yaw=-90),
                startBlock='start', goalBlock='goal', maxWallHits=0, maxContactCombinations=0,
                blocks=[dict(id='start', boxes=[[0,1,0,2,2,2]]), dict(id='low', boxes=[[2,0,0,3,1.5,4]]),
                        dict(id='roof', boxes=[[2,3.5,2,3,4.5,4]]), dict(id='slab', boxes=[[-1,1,0,0,2.5,2]]),
                        dict(id='lid', boxes=[[0,3.9,0,1,4.9,1]]), dict(id='goal', boxes=[[6,2,0,7,3,2]])]+
                       ([dict(id='ledge', boxes=[[-2,1,-2,0,ledge_top,0]])] if ledge_top else [])))
        s=course(2)
        types=['normal','ceiling','stepping']
        root=empty_program(s['initial'],threshold=0.);root['terminalSupport']='start'
        def children(p):
            context=dict(mode='open',scene=s,program=p,state=boundary_state(p),maxInputs=40,threshold=0.,interactionGate=None)
            return [(c,child) for kind in types for c in generate_choices(kind,context)
                    for child in [append_choice(p,c)] if child is not None]
        def landed(child):
            return child['profile']['grounded'][-1] and child.get('terminalSupport') is not None
        def built(base,key):
            """Airtime from base of the generators' fastest jump with this key (ceilings/grinds through their landing)."""
            moves=[child for c,child in children(base) if c['profile']['jump'][0] and
                   tuple(key) in advantageous_targets(s,base,child,'goal')]
            ends=[c for m in moves for c in ([m] if landed(m) else [c for _,c in children(m) if landed(c)])]
            return min(c['profile']['duration'] for c in ends)-base['profile']['duration']
        def fastest(owner, jump):
            return min(child['profile']['duration'] for c,child in children(root)
                       if c['profile']['jump'][0]==jump and landed(child) and child.get('terminalSupport')==owner)
        table=lambda name:face_table(s,name,{})
        entries={e['key']:e for e in advantageous_candidates(s,table,'start',2.,'goal',types,0.)}
        for key,e in entries.items():
            kind,target=json.loads(key)
            with self.subTest(key=key):
                # A run-off's walk-off input and a step-up's step input are run ticks, not airtime.
                if kind=='landing':
                    # A run-off competes with the jump-now landing on the same block, whatever follows either.
                    self.assertEqual((e['A'],e['limit'],e['via']),(fastest(target,False)-1,fastest(target,True),target))
                elif kind=='step':
                    self.assertEqual((e['A'],'limit' in e,e['via']),(fastest(target,False)-1,False,target))
                else:
                    self.assertEqual(e['via'],None)
                    self.assertEqual(e['A'],2 if kind=='slab_boost' else built(root,[kind,target]))
        self.assertEqual(set(entries),{'["anchor", "goal"]','["ceiling", "lid"]','["grind", "lid"]','["slab_boost", "slab"]',
                                       '["corner_jump_cancel", "slab"]','["landing", "low"]','["step", "slab"]'})
        self.assertEqual(entries['["corner_jump_cancel", "slab"]']['A'],2)
        # A ledge below the take-off adds its fall to the airtime: the generators' fastest cancel landing on it.
        s=course(1.5)
        entries={e['key']:e for e in advantageous_candidates(s,table,'start',2.,'goal',types,0.)}
        cancels=[child for c,child in children(root) if c['profile']['jump'][0] and
                 ('corner_jump_cancel','slab') in advantageous_targets(s,root,child,'goal')]
        on_ledge=min(c['profile']['duration'] for m in cancels for _,c in children(m) if landed(c) and c['terminalSupport']=='ledge')
        self.assertEqual(entries['["corner_jump_cancel", "slab"]']['A'],on_ledge)
        self.assertGreater(on_ledge,2)
        # With no ledge there is no corner jump cancel.
        s=course(None)
        self.assertNotIn('["corner_jump_cancel", "slab"]',[e['key'] for e in advantageous_candidates(s,table,'start',2.,'goal',types,0.)])
        self.assertFalse([c for kind in types for c in generate_choices(kind,dict(mode='open',scene=s,program=root,
                          state=boundary_state(root),maxInputs=40,threshold=0.,interactionGate=None))
                          if any(e['case']=='corner_jump_cancel' for e in c['profile']['caseEvents'])])
        self.assertEqual({json.loads(e['key'])[0] for e in advantageous_candidates(s,table,'start',2.,'goal',['normal'],0.)},
                         {'anchor','landing'})

    def test_hard_structure_excludes_setup_hop_runoff_and_zero_jump_finish(self):
        s=scene();p=history(s,['mid'])
        self.assertTrue(transfer_structure(s,p,0,'start','mid',complete=True)['valid'])
        p['profile']['jump']=[False]
        self.assertTrue(transfer_structure(s,p,0,'start','mid')['valid'])
        self.assertFalse(transfer_structure(s,p,0,'start','mid',complete=True)['valid'])
        p=history(s,['start','mid']);p['profile']['jump']=[True,False]
        self.assertFalse(transfer_structure(s,p,0,'start','mid',complete=True)['valid'])

    def test_hard_transfer_keeps_arbitrary_source_setup_jumps_and_counts_them(self):
        s=scene()
        for hops in (1,2,4):
            p=history(s,['start']*hops+['mid'])
            result=transfer_structure(s,p,0,'start','mid',complete=True)
            self.assertTrue(result['valid'],result)
            self.assertEqual(result['preparationJumps'],list(range(hops)))
            self.assertEqual(result['transferJumps'],[hops])
            self.assertEqual(local_counts(p)['J'],hops+1)
            self.assertTrue(transfer_structure(s,history(s,['start']*hops),0,'start','mid')['valid'])
            self.assertFalse(within_caps(dict(maxJumps=hops),p))

    def test_setup_does_not_admit_an_intermediate_block_or_two_transfer_impulses(self):
        s=scene()
        self.assertFalse(transfer_structure(s,history(s,['start','optional','mid']),0,'start','mid',complete=True)['valid'])
        p=history(s,['start','start','mid'])
        p['profile']['contactEvents']=p['profile']['contactEvents'][:1]+p['profile']['contactEvents'][2:]
        result=transfer_structure(s,p,0,'start','mid',complete=True)
        self.assertFalse(result['valid'])
        self.assertEqual(result['reason'],'multiple_transfer_impulses')
        self.assertFalse(transfer_structure(s,history(s,['mid','start']),0,'start','mid')['valid'])
        self.assertFalse(transfer_structure(s,history(s,['mid','mid']),0,'start','mid',complete=True)['valid'])

    def test_transfer_uses_current_anchor_boundary_and_physical_owner_aliases(self):
        s=scene();p=history(s,['mid','mid','goal'])
        result=transfer_structure(s,p,1,'mid','goal',complete=True)
        self.assertTrue(result['valid'])
        self.assertEqual(result['preparationJumps'],[1])
        self.assertEqual(result['transferJumps'],[2])
        next(b for b in s['blocks'] if b['id']=='optional')['sourceBlock']='start'
        self.assertTrue(transfer_structure(s,history(s,['optional','mid']),0,'start','mid',complete=True)['valid'])

    def test_source_contact_at_target_arrival_is_part_of_the_same_transfer(self):
        s=scene();p=history(s,['start','mid'])
        # A named landing case can report its lower source and higher target
        # in one input; that is not a completed setup hop before this flight.
        p['profile']['contactEvents'][0]['offset']=1
        p['profile']['jump']=[True,False]
        result=transfer_structure(s,p,0,'start','mid',complete=True)
        self.assertTrue(result['valid'],result)
        self.assertEqual(result['transferJumps'],[0])

    def test_real11_initial0_setup_and_transfer_are_distinct(self):
        import gzip
        from pathlib import Path
        from parkour_solver.movement.program import truncate_program
        base=AUTONOMY_DATA/'experiments'
        fixture=json.loads((base/'anchor-feasibility/real11-anchors.json').read_text())
        s=prepare_scene(fixture['request'])
        saved=current_events(json.loads(gzip.decompress((base/'virtual-finish/evidence/initial0-cold.json.gz').read_bytes())))
        program=saved['result']['best']['movementProgram']
        setup=truncate_program(program,12)
        self.assertTrue(transfer_structure(s,setup,0,'b1','b2')['valid'])
        landing=truncate_program(program,21)
        result=transfer_structure(s,landing,0,'b1','b2',complete=True)
        self.assertTrue(result['valid'])
        self.assertEqual(result['preparationJumps'],[0])
        self.assertEqual(result['transferJumps'],[12])
        self.assertEqual(local_counts(landing)['J'],2)

    def test_declared_caps_are_not_widened_during_recovery(self):
        p=history(scene(),['start']*11);p['profile']['jump']=[False]*11
        self.assertTrue(within_caps({},p))
        self.assertTrue(within_caps(dict(maxRunTicksPerJump=11,maxTotalRunTicks=11),p))
        self.assertFalse(within_caps(dict(maxRunTicksPerJump=10),p))
        p['restoredRun']=dict(frame=0,candidate='r')
        self.assertFalse(within_caps(dict(maxTotalRunTicks=10),p))

    def test_a_leg_within_22_5_degrees_of_an_axis_is_that_axis_and_otherwise_its_diagonal(self):
        from parkour_solver.spatial.anchor_guides import leg_class
        at=lambda degrees:leg_class((math.cos(math.radians(degrees)),math.sin(math.radians(degrees))))
        self.assertEqual(at(0),((1.,0.),{('X',1),('Z',1),('Z',-1)}))
        self.assertEqual(at(22),((1.,0.),{('X',1),('Z',1),('Z',-1)}))
        self.assertEqual(at(23),((math.sqrt(.5),math.sqrt(.5)),{('X',1),('Z',1)}))
        self.assertEqual(at(100),((0.,1.),{('Z',1),('X',1),('X',-1)}))
        self.assertEqual(at(-135),((-math.sqrt(.5),-math.sqrt(.5)),{('X',-1),('Z',-1)}))

    def test_a_jump_back_onto_its_face_fits_when_the_room_ahead_exceeds_most_of_its_natural_jump(self):
        from parkour_solver.reachability.intervals import travel_radii
        sprint=[.3274]+[.026]*11,[.546]+[.91]*11
        def fit(point, velocity, leg, regions, schedule=sprint):
            return same_face_fit(point,velocity,leg,regions,*schedule)
        # Faces are player-centre regions [x0, z0, x1, z1]: a 5x1 top widened by the half-width.
        long=[-.3,-.3,5.3,1.3]
        # An 11 degree leg is along +X: the room runs to the face's +X edge from the player's position.
        leg=(math.cos(math.radians(11)),math.sin(math.radians(11)))
        back=fit([.3,.5],[0,0],leg,[long])
        self.assertEqual((back['room'],back['jump']),(long[2]-.3,travel_radii(0,*sprint)[-1]))
        self.assertTrue(back['fits'])
        # The same face from near its forward edge: the natural jump overshoots it.
        self.assertEqual(fit([4.,.5],[0,0],leg,[long])['room'],long[2]-4.)
        self.assertFalse(fit([4.,.5],[0,0],leg,[long])['fits'])
        # A merged face of two rectangles: the room reaches the farther one.
        merged=[[-.3,-.3,1.3,1.3],[1.3,.2,4.3,.8]]
        self.assertEqual(fit([.5,.5],[0,0],(1,0),merged)['room'],merged[1][2]-.5)
        # A 40 degree leg is diagonal: the room is the projection onto the quadrant's diagonal.
        leg=(math.cos(math.radians(40)),math.sin(math.radians(40)))
        cube=[-.3,-.3,1.3,1.3]
        diagonal=fit([.5,.5],[.25*leg[0],.25*leg[1]],leg,[cube])
        self.assertEqual(diagonal['room'],2*(cube[2]-.5)*math.sqrt(.5))
        self.assertEqual(diagonal['jump'],travel_radii(.25*leg[0]**2+.25*leg[1]**2,*sprint)[-1])
        self.assertFalse(diagonal['fits'])
        # Fitting is strict: room equal to the factor times the jump is held.
        exact=[.5,.25],[.5,.5]
        edge=cube[2]-SAME_FACE_FIT_FACTOR*travel_radii(0,*exact)[-1]
        self.assertEqual(fit([edge,.5],[0,0],(1,0),[cube],exact)['room'],SAME_FACE_FIT_FACTOR*travel_radii(0,*exact)[-1])
        self.assertFalse(fit([edge,.5],[0,0],(1,0),[cube],exact)['fits'])
        self.assertTrue(fit([edge-.01,.5],[0,0],(1,0),[cube],exact)['fits'])
        # A two-tick jump (a grind's jump tick and one air tick) fits a 2-block face.
        grind=fit([.5,.5],[.28,0],(1,0),[[-.3,-.3,2.3,1.3]],([.3274,.026],[.546,.91]))
        self.assertEqual(grind['jump'],travel_radii(.28,[.3274,.026],[.546,.91])[-1])
        self.assertTrue(grind['fits'])

    def test_repair_exhaustion_backtracks_without_oracle_or_ancestor_rejection(self):
        result=dict(status='unknown',best=None,repairState=dict(completed=[
            dict(branch='native-invalid',status='feasible',validationStatus='rejected')]))
        self.assertEqual(outcome(result,'model',policy_spec())['status'],'exhausted')


if __name__=='__main__':unittest.main()
