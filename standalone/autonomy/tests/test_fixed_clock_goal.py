import copy
import itertools
import json
import os
from pathlib import Path
import time
import unittest

from standalone.autonomy.block_search import prepare_scene
from standalone.autonomy.cell_routes.checkpoints import Checkpoint, checkpoint
from standalone.autonomy.cell_routes.checkpoint_search import find_checkpoint_route
from standalone.autonomy.cell_routes.checkpoint_experiment import replay_witness
from standalone.autonomy.cell_routes.routes import Corridor
from standalone.autonomy.compiler import JavaCompiler
from standalone.autonomy.movement_search import iter_movement_programs
from standalone.autonomy.tests.test_checkpoint_guidance import flat_scene, programs


class FixedClockTests(unittest.TestCase):
    def test_explicit_clock_preserves_direction_without_goal_region(self):
        route=Corridor(0,[],[],[[0,1,0],[2,1,0]],(),[],2.)
        cp=Checkpoint(8,-1,(0.,1.),(0.,1.,0.),(0.,1.,10.),())
        self.assertIs(checkpoint(route,[],cp),cp)
        self.assertEqual(cp.rows(),[])
        self.assertAlmostEqual(cp.objective['customYaw'],0.)

    def test_clock_generation_continues_after_goal_tag(self):
        scene=flat_scene()
        seed=next(p for p in programs(scene,1) if p['profile']['grounded'][-1])
        seed['terminalSupport']=scene['goalBlock']
        old=list(itertools.islice(iter_movement_programs(scene,initial_programs=[seed],threshold=0.,
            checkpoint_tick=2,deadline=time.monotonic()+2),1))
        new=list(itertools.islice(iter_movement_programs(scene,initial_programs=[seed],threshold=0.,
            checkpoint_tick=2,deadline=time.monotonic()+2,stop_on_goal=False),1))
        self.assertEqual(old[0]['profile']['airtime'],1)
        self.assertEqual(new[0]['profile']['airtime'],2)


@unittest.skipUnless(os.environ.get('PKC_RUN_TICKS_COMPILER') and os.environ.get('PKC_IPOPT_PYTHON'),
                     'Set compiler and IPOPT Python for fixed-clock solves')
class FixedClockNumericalTests(unittest.TestCase):
    def test_terminal_clock_can_overshoot_goal_and_finish_airborne(self):
        request=dict(version=1,sceneModel='collision_boxes',geometryBounds=[-3,-3,-3,12,7,3],
            initial=dict(position=[.5,1,.5],velocity=[.3,-.0784000015258789,0],yaw=-90),
            startBlock='start',goalBlock='goal',maxWallHits=0,maxContactCombinations=0,
            blocks=[dict(id='start',boxes=[[0,0,0,1,1,1]]),dict(id='goal',boxes=[[1.5,0,0,2.5,1,1]])],
            movementSearch=dict(types=['normal'],maxInputs=16,maxPrefixes=64,maxQueuedPrefixes=64,maxChoicesPerType=16))
        original=copy.deepcopy(request)
        cp=Checkpoint(8,-1,(1.,0.),(.5,1.,.5),(8.,3.,.5),())
        with JavaCompiler(json.loads(Path(os.environ['PKC_RUN_TICKS_COMPILER']).read_text())) as compiler:
            result=find_checkpoint_route(request,compiler,terminal_checkpoint=cp,checkpoints=[],
                spatial_points=[[.5,1,.5],[.5,3,.5],[8,3,.5]],seconds=10,
                max_candidates=12,max_solver_calls=32,candidate_seconds=.7,score_seconds=2,
                python_executable=os.environ['PKC_IPOPT_PYTHON'])
            self.assertEqual(result['status'],'feasible',result['termination'])
            best=result['best']
            self.assertEqual(best['plan']['ticks'],8)
            self.assertGreater(best['candidate']['positions'][-1][0],2.8)
            self.assertFalse(best['candidate']['onGround'][-1])
            self.assertFalse(best['route']['terminalSupportRequired'])
            self.assertTrue(replay_witness(prepare_scene(request),best,compiler)['passed'])
        self.assertEqual(request,original)
        self.assertEqual(result['goalType'],'fixed_absolute_tick_objective')


class SupportOrderTests(unittest.TestCase):
    def test_route_order_preserves_timings_and_does_not_require_terminal_landing(self):
        from standalone.autonomy.cell_routes.support_order import SupportOrder
        scene=flat_scene()
        policy=SupportOrder(scene,['goal'],8)
        goal=scene['collisionScene']['solids'][1]
        program=dict(profile=dict(airtime=8,contactEvents=[dict(offset=4,axis='Y',direction=-1,box=goal)]))
        self.assertTrue(policy(program))

        self.assertEqual(policy.audit(program)['observed'],[dict(tick=5,block='goal')])
        program['profile']['contactEvents'][0]['offset']=6
        self.assertTrue(policy(program))
        program['profile']['contactEvents']=[]
        self.assertFalse(policy(program))
        program['profile']['airtime']=4
        self.assertTrue(policy(program))


if __name__=='__main__':unittest.main()
