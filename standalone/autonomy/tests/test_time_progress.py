import copy
import unittest

from standalone.autonomy.cell_routes.time_progress import retain_two_goals


def label(name,tick,score,*,clock='clock:10',goal='landing:G'):
    record=dict(id=name,required_spatial=[goal],required_clocks=[clock],
        spatial_results={goal:dict(status='feasible' if tick is not None else 'unknown',ticks=tick)},
        clock_results={clock:dict(status='feasible' if score is not None else 'unknown',objective=score)})
    return record,dict(name=name)


class TimeProgressTests(unittest.TestCase):
    def retained(self,items,**kwargs):
        kept,report=retain_two_goals(items,cap=20,**kwargs)
        return [r['id'] for r,_ in kept],report

    def test_fast_and_far_are_different_and_tradeoffs_survive(self):
        names,_=self.retained([label('fast',12,3),label('far',14,4)])
        self.assertEqual(set(names),{'fast','far'})

    def test_similar_candidates_are_not_strictly_frontier_pruned(self):
        names,_=self.retained([label('best',12,4),label('similar',13,3.8)])
        self.assertEqual(set(names),{'best','similar'})

    def test_only_marked_inferiority_is_discarded(self):
        names,report=self.retained([label('better',12,4),label('worse',15,3)])
        self.assertEqual(names,['better'])
        self.assertEqual(report['decisions']['worse'],dict(reason='markedly_worse',peer='better'))

    def test_unrelated_fastest_and_farthest_do_not_dominate_middle(self):
        names,_=self.retained([label('fast',10,1),label('far',20,5),label('middle',15,3)])
        self.assertEqual(set(names),{'fast','far','middle'})

    def test_distance_at_different_clocks_is_not_compared(self):
        names,_=self.retained([label('at10',10,5,clock='clock:10'),label('at15',15,3,clock='clock:15')])
        self.assertEqual(set(names),{'at10','at15'})

    def test_different_spatial_goals_are_not_compared(self):
        names,_=self.retained([label('a',10,5,goal='landing:A'),label('b',15,3,goal='landing:B')])
        self.assertEqual(set(names),{'a','b'})

    def test_unknown_measurement_does_not_mean_bad_candidate(self):
        names,_=self.retained([label('known',10,5),label('unknown_time',None,3),label('unknown_clock',15,None)])
        self.assertEqual(set(names),{'known','unknown_time','unknown_clock'})

    def test_full_reference_keeps_would_be_discards_despite_cap(self):
        items=[label('best',10,5),label('worse',15,3),label('unknown',None,None)]
        kept,report=retain_two_goals(items,cap=1,prune=False)
        self.assertEqual(len(kept),3)
        self.assertEqual(report['markedlyWorse'],0)
        self.assertEqual(report['resourceDrops'],0)

    def test_hard_cap_is_reported_separately(self):
        kept,report=retain_two_goals([label('fast',10,1),label('far',20,5),label('middle',15,3)],cap=2)
        self.assertEqual({r['id'] for r,_ in kept},{'fast','far'})
        self.assertEqual(report['markedlyWorse'],0)
        self.assertEqual(report['decisions']['middle']['reason'],'resource_cap')

    def test_translation_does_not_change_classification(self):
        a=[label('best',10,5),label('worse',15,3)]
        b=copy.deepcopy(a)
        for r,_ in b:r['clock_results']['clock:10']['objective']+=1000
        self.assertEqual(self.retained(a)[0],self.retained(b)[0])


class ComparisonAuditTests(unittest.TestCase):
    def test_retaining_decisions_does_not_commit_clock_optimum_guards_or_states(self):
        from standalone.autonomy.cell_routes.checkpoints import retain_decisions
        program=dict(initial=dict(position=[1,2,3],velocity=[.2,0,.1]),
            profile=dict(airtime=2,positionConstraints=[dict(mode='X',rhs=1)]))
        before=copy.deepcopy(program)
        result=dict(best=dict(candidate=dict(yaw=[20,30],positions=[[1,3],[2,3],[3,3]]),
            plan=dict(walls=[dict(mode='F',rhs=20),dict(mode='X',rhs=9)])),repairState=dict(pending=[],unresolved=[]))
        retained=retain_decisions(program,result)
        self.assertEqual(program,before)
        self.assertEqual(retained['initial'],before['initial'])
        self.assertEqual(retained['profile'],before['profile'])
        self.assertEqual(retained['guideSeedYaw'],[20,30])
        self.assertEqual(retained['repairState'],result['repairState'])
        self.assertIsNot(retained['repairState'],result['repairState'])

    def test_completed_candidate_in_window_is_already_final_evaluated(self):
        from standalone.autonomy.cell_routes.comparison_audit import source_entries
        result=dict(windows=[dict(retained=[],retention=dict(decisions={}),
            candidates=[dict(id='finished',complete=True,decision_program={})])])
        self.assertEqual(source_entries(result)[0]['selection'],'final_evaluated')

    def test_regression_really_continues_discarded_candidate_that_wins(self):
        from standalone.autonomy.cell_routes.comparison_audit import audit_pool
        entries=[dict(id='kept',selection='retained'),dict(id='omitted',selection='markedly_worse')]
        seen=[]
        def best(t):return dict(plan=dict(ticks=t),candidate=dict(objective=3.))
        def evaluate(entry):
            seen.append(entry['id'])
            return dict(best=best(12 if entry['id']=='omitted' else 13),complete=True)
        audit=audit_pool(entries,evaluate,objective=dict(sense='MAX'),incumbent=best(13))
        self.assertEqual(seen,['kept','omitted'])
        self.assertEqual(audit['discardedBeatsHeuristic'],['omitted'])
        self.assertTrue(audit['heuristicQualityLoss'])
        self.assertTrue(audit['continuationAuditComplete'])

    def test_unknown_reference_cannot_claim_full_audit(self):
        from standalone.autonomy.cell_routes.comparison_audit import audit_pool
        audit=audit_pool([dict(id='unknown',selection='markedly_worse')],lambda e:dict(best=None,complete=False),
                         objective=dict(sense='MAX'))
        self.assertTrue(audit['allCandidatesProcessed'])
        self.assertFalse(audit['continuationAuditComplete'])


if __name__=='__main__':unittest.main()
