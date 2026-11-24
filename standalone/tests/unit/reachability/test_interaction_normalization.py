import copy
import itertools
import json
import unittest
from unittest.mock import patch

from parkour_solver.constraints.variants import ConstraintVariants
from parkour_solver.reachability.service import query_interaction
from parkour_solver.reachability.intervals import reachable_envelopes


def context():
    return dict(envelopes=reachable_envelopes(dict(ticks=3, position=[0., 0.], velocity=[0., 0.],
        magnitude=[1.]*3, friction=[.9]*3, walls=[]), [1.]*4))


def source(state=None):
    return ConstraintVariants(dict(passageCaps=dict(horizontal=2, vertical=2, total=3)),
        dict(position=[1.]*4), dict(events=[dict(jump=0, runTicks=0)]),
        dict(solids=[[1., 0., -.2, 2., 3., .2]]), context()['envelopes'],
        dict(through=3, solidIndices=[0]), state=state)


class InteractionNormalizationTests(unittest.TestCase):
    def staged(self, alternatives, suffix, state=None):
        owner=source()
        owner.items[:]=[dict(rows=alternatives),dict(rows=[suffix])]
        if state is not None:
            owner.state=copy.deepcopy(state)
            owner.state['items']=owner.items
            owner.seen=set(owner.state['seen'])
            owner.partial_seen=set(owner.state['partialSeen'])
        return owner

    def test_impossible_prefix_is_rejected_before_enumerating_next_obstacle(self):
        row=lambda axis,value:dict(leg=0,offset=1,mode=axis,cmp='GE',rhs=value)
        bad=[row('X',.8),row('Z',.8)]
        good=[row('X',-.1)];suffix=[row('Z',.2)]
        owner=self.staged([bad,good],suffix)
        downstream=[];actual=owner.option
        def option(item,cursor):
            if item.get('rows') == owner.items[1]['rows']:downstream.append(True)
            return actual(item,cursor)
        with patch.object(owner,'option',side_effect=option):result=list(owner)
        self.assertEqual(len(result),1)
        self.assertEqual(len(downstream),2)  # One option, then exhaustion; never the bad prefix.
        self.assertEqual({(r['mode'],r['offset'],r['rhs']) for r in result[0]['rows']},
                         {('X',1,-.1),('Z',1,.2)})
        self.assertTrue(any(x['proof']['reason']=='norm_reach_contradiction'
                            for x in owner.state['screenExclusions']))

    def test_legacy_pending_prefix_is_screened_before_resuming_next_obstacle(self):
        rows=[dict(leg=0,offset=1,mode=axis,cmp='GE',rhs=.8) for axis in ('X','Z')]
        owner=self.staged([rows],[dict(leg=0,offset=2,mode='Z',cmp='GE',rhs=.1)])
        saved=json.loads(json.dumps(owner.state))
        saved['pending']=[dict(item=1,rows=rows,passages=[],cursor=dict(index=0))]
        restored=self.staged([rows],owner.items[1]['rows'][0],saved)
        with patch.object(restored,'option',side_effect=AssertionError('Expanded rejected legacy prefix')):
            self.assertEqual(list(restored),[])

    def test_top_contact_boundary_does_not_share_interior_absence_proof(self):
        request = dict(box=[-.5,0.,-.5,.5,1.,.5], role='unwanted_contact', windows=[dict(tick=0, phase='Y')])
        self.assertEqual(query_interaction(request, context())['status'], 'absent')
        contact = query_interaction(dict(request, role='required_contact'), context())
        self.assertEqual(contact['status'], 'not_ruled_out')

    def test_batches_preserve_complete_proofs_and_observe_later_bound_mutations(self):
        from parkour_solver.reachability.intervals import interaction_reachability, interaction_reachability_many
        envelope = context()['envelopes']
        queries = [dict(regions=[dict(tick=0, phase='X', bounds=[1.,50.,0.,2.,51.,1.])]),
                   [dict(mode='X', t1=0, cmp='GE', rhs=100.)], {}]
        before = copy.deepcopy((envelope, queries))
        first = interaction_reachability_many(envelope, queries)
        self.assertEqual(first, [interaction_reachability(envelope, q) for q in queries])
        self.assertEqual([p['ruledOut'] for p in first], [True, True, False])
        self.assertEqual((envelope, queries), before)
        envelope['phases'][0]['X']['X'] = [-1000.,1000.]
        envelope['phases'][0]['X']['Y'] = [-float('inf'),float('inf')]
        second = interaction_reachability_many(envelope, queries)
        self.assertEqual(second, [interaction_reachability(envelope, q) for q in queries])
        self.assertEqual([p['ruledOut'] for p in second], [False, True, False])
        self.assertEqual(interaction_reachability_many(envelope, []), [])

    def test_context_and_probe_uncertainty_invalidate_proofs(self):
        request = dict(box=[1.,50.,0.,2.,51.,1.], role='approach_prerequisite', windows=[dict(tick=0,phase='X')])
        a = query_interaction(request, context())
        ctx = context(); ctx['envelopes']['phases'][0]['X']['X'] = [-1000., 1000.]
        ctx['envelopes']['phases'][0]['X']['Y'] = [-float('inf'),float('inf')]
        b = query_interaction(request, ctx)
        self.assertNotEqual(a['context'], b['context'])
        self.assertEqual(b['status'], 'not_ruled_out')

    def test_interrupted_phase_cover_preparation_resumes_exactly(self):
        expected = list(itertools.islice(source(), 12))
        self.assertTrue(expected)
        for allowance in (2, 12, 40, 80):
            with self.subTest(allowance=allowance):
                current = source()
                counter = 0
                def check():
                    nonlocal counter
                    counter += 1
                    if counter > allowance:
                        raise TimeoutError('allocation')
                current.check = check
                actual = []
                try:
                    for _ in range(12):
                        actual.append(next(current))
                except TimeoutError:
                    pass
                saved = json.loads(json.dumps(current.state, allow_nan=False))
                restored = source(saved)
                actual += list(itertools.islice(restored, 12-len(actual)))
                self.assertEqual(actual, expected)

    def test_old_or_changed_constraint_state_is_explicitly_rejected(self):
        saved = copy.deepcopy(source().state)
        saved['version'] = 1
        with self.assertRaisesRegex(ValueError, 'Incompatible constraint'):
            source(saved)

if __name__ == '__main__':
    unittest.main()
