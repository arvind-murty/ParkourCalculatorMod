import copy
import json
import time
import unittest
from unittest.mock import patch

from standalone.autonomy.block_contacts import solve_contact_branches
from standalone.autonomy.cell_routes.checkpoints import retain_unresolved
from standalone.autonomy.cell_routes.checkpoint_search import _key
from standalone.autonomy.movement_composition import compile_program, program_physics_key
from standalone.autonomy.movement_search import iter_movement_programs
from standalone.autonomy.repair_state import compatible, context
from standalone.autonomy.tests.test_checkpoint_guidance import flat_scene, programs


def route():
    return dict(constraints=[], passages=[], hitEvents=[], maxTotalRunTicks=0, supportClearance=1e-6,
                initial=dict(position=[.5,1,.5],velocity=[0,0,0]), objective=dict(axis='X',sense='MAX'))


def best(value):
    return dict(allocation=[0],cost=0,plan=dict(ticks=2,events=[]),
                candidate=dict(yaw=[value]*2,positions=[[0,0]]*3,objective=value))


def outcome(value=1):
    return dict(status='feasible',termination='done',attempts=[],best=best(value))


VERTICAL=dict(feasible=True,vertical={},y=[1]*3,vy=[0]*3,onGround=[True]*3)


class RepairResumeTests(unittest.TestCase):
    def invoke(self, request, find, filter_candidate, **kwargs):
        with patch('standalone.autonomy.block_contacts.find_route',side_effect=find), \
             patch('standalone.autonomy.block_contacts.validate_vertical_schedule',return_value=VERTICAL):
            return solve_contact_branches(request,[],[],object(),seconds=1,candidate_seconds=.1,retries=0,
                max_branches=1,rank_equal_cost=True,optimize_goal=True,candidate_filter=filter_candidate,**kwargs)

    def test_pending_guards_and_facing_guesses_resume_without_repeating_root(self):
        rows=[dict(leg=0,offset=1,mode='X',cmp='GE',rhs=v) for v in (1,2)]
        def filter_candidate(*args):
            return dict(accepted=False,alternatives=[[r] for r in rows])
        first=self.invoke(route(),lambda *a,**k:outcome(7),filter_candidate)
        state=json.loads(json.dumps(first['repairState']))
        self.assertEqual(len(state['pending']),2)
        self.assertEqual(state['pending'][0]['warmYaw'],[7,7])
        requests=[]
        def resumed(request,*args,**kwargs):
            requests.append(copy.deepcopy(request))
            return dict(status='unknown',termination='done',best=None,attempts=[])
        second=self.invoke(route(),resumed,filter_candidate,resume_state=state)
        self.assertTrue(second['repairResume']['exact'])
        self.assertEqual(requests[0]['constraints'],[rows[0]])
        self.assertEqual(requests[0]['warmYaw'],[7,7])
        self.assertEqual(second['repairState']['pending'][0]['rows'],[rows[1]])
        self.assertEqual(second['repairState']['unresolved'][0]['rows'],[rows[0]])
        self.assertEqual(requests[0]['initial'],route()['initial'])

    def test_unknown_numeric_branch_is_stored_and_revalidated_on_resume(self):
        request=dict(route(),warmYaw=[8,8])
        first=self.invoke(request,lambda *a,**k:dict(status='unknown',termination='done',best=None,attempts=[]),
                          lambda *a:dict(accepted=True))
        self.assertEqual(first['repairState']['unresolved'][0]['warmYaw'],[8,8])
        second=self.invoke(request,lambda *a,**k:outcome(9),lambda *a:dict(accepted=True),resume_state=first['repairState'])
        self.assertEqual(second['status'],'feasible')
        self.assertTrue(second['repairResume']['compatible'])

    def test_changed_start_or_corridor_discards_incompatible_state(self):
        first=self.invoke(route(),lambda *a,**k:dict(status='unknown',termination='done',best=None,attempts=[]),
                          lambda *a:dict(accepted=True),repair_scope={'corridor':1})
        for change in ('start','scope'):
            request=route()
            if change=='start':request['initial']['velocity'][0]=.1
            scope={'corridor':2 if change=='scope' else 1}
            seen=[]
            def find(request,*args,**kwargs):
                seen.append(request)
                return outcome()
            result=self.invoke(request,find,lambda *a:dict(accepted=True),
                resume_state=first['repairState'],repair_scope=scope)
            self.assertFalse(result['repairResume']['compatible'])
            self.assertEqual(seen[0]['constraints'],[])

    def test_changed_constraints_cannot_reuse_prior_acceptance(self):
        first=self.invoke(route(),lambda *a,**k:outcome(),lambda *a:dict(accepted=True))
        self.assertEqual(first['status'],'feasible')
        second=self.invoke(route(),lambda *a,**k:outcome(),lambda *a:dict(accepted=False),resume_state=first['repairState'])
        self.assertIsNone(second['best'])
        self.assertEqual(second['status'],'unknown')

    def test_cancellation_keeps_queue_serializable_for_resume(self):
        import threading
        cancelled=threading.Event();cancelled.set()
        result=self.invoke(route(),lambda *a:self.fail('Cancelled search solved a branch'),
                           lambda *a:dict(accepted=True),cancel_event=cancelled)
        state=json.loads(json.dumps(result['repairState']))
        self.assertEqual(len(state['pending']),1)

    def test_compatible_extension_preserves_symbolic_state_and_has_distinct_search_identity(self):
        scene=flat_scene()
        first=next(p for p in programs(scene,2) if p['profile']['jump'][0])
        compiled=compile_program(scene,first,require_complete=False)
        ctx=context(compiled['route'],compiled['profiles'],scene['collisionScene'],{'corridor':1})
        branch=dict(score=1,rows=[dict(leg=0,offset=1,mode='Z',cmp='LE',rhs=.4)],passages=[],hits=[],warmYaw=[1,2])
        state=dict(version=1,context=ctx,pending=[branch],unresolved=[],seen=[])
        retained=retain_unresolved(first,state)
        self.assertEqual(program_physics_key(first),program_physics_key(retained))
        self.assertNotEqual(_key(first),_key(retained))
        generator=iter_movement_programs(scene,checkpoint_tick=4,threshold=0.,initial_programs=[retained],deadline=time.monotonic()+2)
        extended=next(generator);generator.close()
        self.assertEqual(extended['repairState'],state)
        self.assertEqual(extended['guideSeedYaw'],[1,2])
        later=compile_program(scene,extended,require_complete=False)
        new_ctx=context(later['route'],later['profiles'],scene['collisionScene'],{'corridor':1})
        self.assertTrue(compatible(ctx,new_ctx))
        new_ctx['profiles'][0]['jump'][0]=False
        self.assertFalse(compatible(ctx,new_ctx))
        self.assertNotIn('positions',retained)
        self.assertNotIn('velocity',retained)


if __name__=='__main__':
    unittest.main()
