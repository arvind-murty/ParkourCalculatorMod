import copy
import json
import itertools
import math
import os
import random
import unittest
from unittest.mock import patch

from parkour_solver.reachability.intervals import reachable_envelopes, interaction_reachability
from parkour_solver.search.identity import solve_scope, processing_key, motion_record
from parkour_solver.reachability.viability import prefix_viability
from parkour_solver.search.frontier import persistent_programs
from parkour_solver.movement.settings import movement_settings
from parkour_solver.movement.program import empty_program, program_physics_key
from tests.support.checkpoint_guidance import flat_scene, programs
from parkour_solver.numerics import prepare, validate


class InteractionReachabilityTests(unittest.TestCase):
    def test_diagonal_distance_bound_rejects_box_relaxation_false_positive(self):
        envelope=reachable_envelopes(dict(ticks=1,position=[0.,0.],velocity=[0.,0.],magnitude=[1.],friction=[.9],
            walls=[dict(mode=axis,t1=1,cmp='GE',rhs=.9) for axis in ('X','Z')]))
        self.assertEqual(envelope['contradiction']['reason'],'norm_reach_contradiction')
    def test_all_acceleration_directions_and_swept_phases_remain_in_the_envelope(self):
        rng = random.Random(17923)
        for _ in range(40):
            n = 12
            model = prepare(dict(version=1, ticks=n, position=[rng.uniform(-3, 3), rng.uniform(-3, 3)],
                velocity=[rng.uniform(-.5, .5), rng.uniform(-.5, .5)], magnitude=[rng.uniform(.01, .35) for _ in range(n)],
                friction=[rng.uniform(.5, .95) for _ in range(n)], base=[0.]*n,
                yaw=[rng.uniform(-180, 180) for _ in range(n)], threshold=0., walls=[],
                objective=dict(axis='X', sense='MAX', tick=n)))
            candidate = validate(model, model['yaw'])
            for t in (4, 8, 12):
                for axis, coordinate in zip(('X', 'Z'), candidate['positions'][t]):
                    model['walls'].extend([dict(mode=axis,t1=t,cmp='GE',rhs=coordinate-.01),
                                           dict(mode=axis,t1=t,cmp='LE',rhs=coordinate+.01)])
            envelope = reachable_envelopes(model, [2.]*(n+1))
            self.assertIsNone(envelope['contradiction'])
            self.assertFalse(interaction_reachability(envelope,{},refine=True)['ruledOut'])
            for t, (a, b) in enumerate(zip(candidate['positions'], candidate['positions'][1:])):
                for phase, points in [('Y',[a]), ('X',[a,[b[0],a[1]]]), ('Z',[[b[0],a[1]],b])]:
                    for point in points:
                        for axis, value in zip(('X','Z'),point):
                            lo, hi = envelope['phases'][t][phase][axis]
                            self.assertLessEqual(lo-1e-7, value)
                            self.assertGreaterEqual(hi+1e-7, value)

    def test_required_and_unwanted_interactions_use_the_same_query(self):
        envelope = reachable_envelopes(dict(ticks=1,position=[0.,0.],velocity=[0.,0.],magnitude=[.1],friction=[.9],walls=[]), [1.,1.])
        self.assertTrue(interaction_reachability(envelope,[dict(mode='X',t1=1,cmp='GE',rhs=3.)])['ruledOut'])
        distant = dict(regions=[dict(tick=0,phase='X',bounds=[3.,0.,-1.,4.,2.,1.],interior=True)])
        self.assertTrue(interaction_reachability(envelope,distant)['ruledOut'])
        nearby = dict(regions=[dict(tick=0,phase='X',bounds=[-.2,0.,-.2,.2,2.,.2],interior=True)])
        self.assertFalse(interaction_reachability(envelope,nearby)['ruledOut'])
        self.assertFalse(interaction_reachability(envelope,dict(alternatives=[distant,nearby]))['ruledOut'])

    def test_disk_relaxation_rules_out_velocity_position_coupling(self):
        model=dict(ticks=2,position=[0.,0.],velocity=[0.,0.],magnitude=[1.,0.],friction=[1.,1.],threshold=0.,
            walls=[dict(mode='X',t1=1,cmp='GE',rhs=.9),dict(mode='X',t1=2,cmp='LE',rhs=1.5)])
        envelope=reachable_envelopes(model)
        self.assertFalse(interaction_reachability(envelope,{})['ruledOut'])
        self.assertEqual(interaction_reachability(envelope,{},refine=True)['reason'],'disk_control_relaxation_infeasible')
        model['walls'][-1]['rhs']=2.1
        self.assertFalse(interaction_reachability(reachable_envelopes(model),{},refine=True)['ruledOut'])

    def test_selected_clips_do_not_exclude_their_feasible_controls(self):
        rng=random.Random(37091)
        for axis in (0,1):
            for _ in range(15):
                model=prepare(dict(version=1,ticks=8,position=[0.,0.],velocity=[.2,-.1],
                    magnitude=[.2]*8,friction=[.91]*8,base=[0.]*8,yaw=[rng.uniform(-180,180) for _ in range(8)],
                    threshold=0.,walls=[],objective=dict(axis='X',sense='MAX',tick=8)))
                free=validate(model,model['yaw'])['positions']
                a,b=free[3][axis],free[4][axis]
                model['contacts']=[dict(state=4,axis=axis,plane=(a+b)/2,direction=1 if b>a else -1)]
                clipped=validate(model,model['yaw'])
                self.assertTrue(clipped['feasible'])
                for t in (4,8):
                    for name,value in zip(('X','Z'),clipped['positions'][t]):
                        model['walls'] += [dict(mode=name,t1=t,cmp='LE',rhs=value+1e-5),
                                           dict(mode=name,t1=t,cmp='GE',rhs=value-1e-5)]
                self.assertFalse(interaction_reachability(reachable_envelopes(model),{},refine=True)['ruledOut'])


class ScopeAndViabilityTests(unittest.TestCase):
    def test_complete_scope_cannot_silently_omit_a_block(self):
        scene=flat_scene();p=programs(scene,2)[0]
        with self.assertRaises(ValueError):
            solve_scope(scene,p,dict(purpose='complete',modeledBlocks=[scene['startBlock']]))
        value=solve_scope(scene,p,dict(purpose='preliminary',modeledBlocks=[scene['startBlock']]))
        self.assertEqual(value['solidIndices'],[0])

    def test_fall_below_every_floor_is_dead_only_when_a_contact_is_required(self):
        scene=flat_scene();p=copy.deepcopy(programs(scene,2)[0])
        p['profile']['position'][-1]=-3.
        p['profile']['velocity'][-1]=-.5
        p['profile']['grounded'][-1]=False
        self.assertTrue(prefix_viability(scene,p,required_contacts=['goal'],terminal_tick=20)['dead'])
        self.assertFalse(prefix_viability(scene,p,required_contacts=[],terminal_tick=20)['dead'])
        scene['blocks'].append(dict(id='lower',sourceBlock='lower',solidIndex=2,top=-4.,bounds=[-2.,2.,-2.,2.]))
        self.assertFalse(prefix_viability(scene,p,required_contacts=['goal'],terminal_tick=20)['dead'])

    def test_unsupported_first_event_family_is_not_proof_of_death(self):
        scene=flat_scene();p=copy.deepcopy(programs(scene,2)[0])
        p['profile']['position'][-1]=-3.;p['profile']['velocity'][-1]=-.5;p['profile']['grounded'][-1]=False
        self.assertFalse(prefix_viability(scene,p,required_contacts=['goal'],terminal_tick=20,enabled_types=['ladder_climb'])['dead'])


class PersistentGenerationTests(unittest.TestCase):
    def test_choice_and_queue_sizes_do_not_erase_a_late_candidate_after_reload(self):
        scene=flat_scene();cfg=dict(movement_settings(scene),maxInputs=1,maxPrefixes=1,maxQueuedPrefixes=2,maxChoicesPerType=2,types=['normal'])
        scene=dict(scene,movementSearch=cfg.copy())
        root=empty_program(scene['initial'],threshold=0.)
        def child(program, choice, **kwargs):
            p=copy.deepcopy(root);q=p['profile'];q.update(duration=1,position=[1.,1.],velocity=[0.,0.],grounded=[True,True],
                jump=[False],sneak=[False],ladderTicks=[False,False],positionConstraints=[dict(mode='X',t1=1,cmp='GE',rhs=float(choice))])
            p['marker']=choice;p['terminalSupport']='goal'
            return p
        def generated(*args):return iter(range(30))
        with patch('parkour_solver.search.frontier.generate_choices',side_effect=generated), \
             patch('parkour_solver.movement.program.append_choice',side_effect=child):
            report={}
            gen=persistent_programs(scene,settings=cfg,threshold=0.,report=report)
            first=next(gen);saved=json.loads(json.dumps(report['continuation']));gen.close()
            # The automatic route queue shrinks a route's final allocation.
            # Allocation fields can appear in both settings and the scene.
            cfg=dict(cfg,maxPrefixes=2,maxQueuedPrefixes=1,maxChoicesPerType=3)
            scene=dict(scene,movementSearch=cfg.copy())
            resumed={}
            rest=list(persistent_programs(scene,settings=cfg,threshold=0.,report=resumed,continuation=saved))
        self.assertEqual({p['marker'] for p in [first]+rest},set(range(30)))
        self.assertEqual(len(rest),29)
        self.assertEqual(resumed['queueDrops'],0)
        self.assertTrue(resumed['continuation']['exhausted'])
        changed=dict(scene,movementSearch=dict(cfg,maxInputs=2))
        with self.assertRaisesRegex(ValueError,'Incompatible movement generation'):
            list(persistent_programs(changed,settings=movement_settings(changed),threshold=0.,report={},continuation=saved))

    def test_interrupted_screen_keeps_the_unprocessed_choice(self):
        scene=flat_scene();cfg=dict(movement_settings(scene),maxPrefixes=1,types=['normal'])
        report={}
        def interrupted(p):raise TimeoutError('screen_deadline')
        list(persistent_programs(scene,settings=cfg,threshold=0.,report=report,screen=interrupted,
                                 context_adapter=lambda c:c.update(interactionGate=None)))
        state=json.loads(json.dumps(report['continuation']))
        self.assertIn('choice',state['expansion'])
        resumed={}
        values=list(persistent_programs(scene,settings=cfg,threshold=0.,report=resumed,continuation=state,
                                        context_adapter=lambda c:c.update(interactionGate=None)))
        self.assertTrue(values)

    def test_constraint_generation_cursor_survives_a_new_facing_guess(self):
        from parkour_solver.constraints.variants import ConstraintVariants
        route=dict(passageCaps=dict(horizontal=2,vertical=2,total=3))
        profile=dict(duration=4,position=[1.]*5)
        plan=dict(events=[dict(jump=0,landing=4,runTicks=0)])
        geometry=dict(solids=[[0.,0.,-.2,1.,5.,.2]])
        envelope=reachable_envelopes(dict(ticks=4,position=[-2.,0.],velocity=[1.,0.],magnitude=[.3]*4,friction=[.9]*4,walls=[]),[1.]*5)
        scope=dict(through=4,solidIndices=[0])
        source=ConstraintVariants(route,profile,plan,geometry,envelope,scope)
        first=next(source);saved=json.loads(json.dumps(source.state,allow_nan=False))
        expected=list(itertools.islice(source,6))
        restored=ConstraintVariants(route,profile,plan,geometry,envelope,scope,state=saved,reference=[[99.,99.]]*5)
        actual=list(itertools.islice(restored,6))
        self.assertEqual(actual,expected)
        self.assertTrue(actual)
        self.assertNotEqual((first['rows'], first['selectedPassages']),
                            (actual[0]['rows'], actual[0]['selectedPassages']))


class QueueKeyTests(unittest.TestCase):
    def test_queue_key_counts_effective_grounded_inputs(self):
        p=dict(profile=dict(duration=4,grounded=[True,True,False,False,False],jump=[False,True,True,False]))
        self.assertEqual(processing_key(p,'x'),(4,-1,1,'x'))


if __name__ == '__main__':
    unittest.main()
