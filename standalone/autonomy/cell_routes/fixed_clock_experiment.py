import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

from standalone.autonomy.block_search import prepare_scene
from standalone.autonomy.compiler import JavaCompiler
from .checkpoints import Checkpoint
from .checkpoint_search import find_checkpoint_route
from .checkpoint_experiment import replay_witness
from .geometry import decompose
from .routes import corridor_for_points,landmarks
from .support_order import SupportOrder
from .two_goal_experiment import witnesses
from .window_experiment import save


def run(fixture,compiler,*,on_window=None,python_executable=None):
    scene=prepare_scene(fixture['request'])
    d,gates=decompose(scene),landmarks(scene)
    corridor=corridor_for_points(d,fixture['spatialPoints'],gates)
    initial=tuple(scene['initial']['position'])
    target=next(b for b in scene['blocks'] if b['id']==fixture['requiredSupports'][-1])
    end=((target['bounds'][0]+target['bounds'][1])/2,target['top'],(target['bounds'][2]+target['bounds'][3])/2)
    dx,dz=end[0]-initial[0],end[2]-initial[2]
    length=math.hypot(dx,dz)
    intermediate=(dx/length,dz/length)
    checkpoints=[Checkpoint(t,-1,(0.,1.) if t==fixture['comparisonTicks'][-1] else intermediate,initial,end,())
                 for t in fixture['comparisonTicks']]
    direction=tuple(fixture['terminalDirection'])
    terminal=Checkpoint(fixture['terminalTick'],-1,direction,initial,tuple(corridor.points[-1]),corridor.signature)
    policy=SupportOrder(scene,fixture['requiredSupports'],fixture['terminalTick'])
    result=find_checkpoint_route(fixture['request'],compiler,corridor=corridor,checkpoints=checkpoints,
        terminal_checkpoint=terminal,program_filter=policy,python_executable=python_executable,on_window=on_window,
        **fixture['settings'])
    result['supportOrder']=fixture['requiredSupports']
    if result['best'] is not None:
        result['bestSupportAudit']=policy.audit(result['best']['movementProgram'])
    validation=[]
    for witness in witnesses(result):
        checked=replay_witness(scene,witness,compiler)
        validation.append(checked)
        if not checked['passed']:raise RuntimeError('Fixed-clock witness failed independent replay')
    return result,validation


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,default=Path('standalone/autonomy/build/compiler.json'))
    args=parser.parse_args()
    fixture=json.loads(args.input.read_text())
    report=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),fixture=fixture,windows=[])
    def progress(window):
        report['windows'].append(window)
        save(args.output,report)
        print(json.dumps(dict(tick=window['checkpoint']['tick'],scored=window['scored'],unknown=window['unknown'],
            retained=len(window['retained']),generation=window['generation']['termination'],
            progress=[c['value'] for c in window['candidates'] if c['id'] in window['retained']])),flush=True)
    with JavaCompiler(json.loads(args.compiler.read_text())) as compiler:
        result,validation=run(fixture,compiler,on_window=progress,python_executable=sys.executable)
    report.update(result=result,validation=validation)
    save(args.output,report)
    best=result['best']
    print(json.dumps(dict(status=result['status'],termination=result['termination'],elapsed=result['elapsedSeconds'],
        work=result['work'],position=[best['candidate']['positions'][-1][0],best['candidate']['y'][-1],
        best['candidate']['positions'][-1][1]] if best else None,replays=len(validation))),flush=True)


if __name__=='__main__':main()
