import argparse
import copy
import gzip
import json
from pathlib import Path
import subprocess
import sys
import time

from standalone.autonomy.block_search import prepare_scene
from standalone.autonomy.compiler import JavaCompiler
from .checkpoint_experiment import replay_witness
from .checkpoint_search import _motion_bound, evaluate_program
from .failure_diagnostic import ROOT, read
from .generation_diagnostic import execution_key, save
from .geometry import decompose
from .local import CorridorAdapter, SearchWork
from .routes import corridor_for_points, landmarks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,default=Path('standalone/autonomy/build/compiler.json'))
    args = parser.parse_args()
    source = read(args.input)
    cases = []
    for name,policy,tick in [('turn-blockage','shortest',12),('turn-blockage','shortest',16),
            ('grind-or-run','tick_fair',11),('multi-support','shortest',14),
            ('extra-grind','shortest',5),('extra-repeat-ceiling','complete_first',12)]:
        run = next(r for r in source['runs'] if r['name']==name and r['policy']==policy and
                   r['canonical']==(policy!='shortest'))
        program = next(c['program'] for c in run['completed'] if c['ticks']==tick)
        cases.append((name,tick,'raw',run,program))
        if name=='turn-blockage' and tick==16:
            controls = read(ROOT/'checkpoint-guidance/evidence/checkpoint-guidance-final.json.gz')
            known = next(r['result']['best']['movementProgram'] for r in controls['runs'] if r['name']==name
                         and r['repeat']==0 and r['result']['arm']=='guided')
            if execution_key(known)!=execution_key(program):
                raise ValueError('The learned-row control changed the physical decision schedule')
            conditioned = copy.deepcopy(program)
            conditioned['profile']['positionConstraints'] = copy.deepcopy(known['profile']['positionConstraints'])
            cases.append((name,tick,'learned_rows_no_yaw_seed',run,conditioned))
    report = dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),runs=[])
    with JavaCompiler(read(args.compiler)) as compiler:
        for name,tick,kind,run,program in cases:
            scene = prepare_scene(dict(run['request'],maxWallHits=0))
            if run['spatialPoints'] is not None:
                d,gates = decompose(scene),landmarks(scene)
                adapter = CorridorAdapter(scene,corridor_for_points(d,run['spatialPoints'],gates),gates,decomposition=d)
            else:
                class NoCorridor:
                    def candidate(self,*args,**kwargs):
                        return dict(accepted=True)
                adapter = NoCorridor()
            motion = _motion_bound(scene,compiler,2)
            for cap in (4,16,32):
                work = SearchWork(cap,deadline=time.monotonic()+8)
                result = evaluate_program(scene,program,compiler,adapter,work,motion,
                    seconds=8,candidate_seconds=.7,solver_calls=cap,python_executable=sys.executable)
                record = dict(name=name,ticks=tick,variant=kind,cap=cap,result=result,work=work.summary(),
                              request=run['request'],program=program,spatialPoints=run['spatialPoints'])
                if result['best']:
                    record['independentReplay'] = replay_witness(scene,result['best'],compiler)
                    if not record['independentReplay']['passed']:
                        raise RuntimeError('Repair diagnostic failed independent replay')
                report['runs'].append(record)
                print(json.dumps(dict(name=name,ticks=tick,variant=kind,cap=cap,status=result['status'],
                    termination=result['termination'],calls=result['work'].get('solverCalls',0))),flush=True)
                save(args.output,report)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
