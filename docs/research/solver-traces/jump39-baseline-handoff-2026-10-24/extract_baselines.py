from pathlib import Path
import csv,gzip,re,json,collections
out=Path(__file__).resolve().parent
runs={'single10000':'full-route-commit1-no-decoder-2026-10-24','staged':'full-route-commit1-staged-caps-2026-10-24'}
def save(name,rows):
    if not rows:return
    keys=list(dict.fromkeys(k for row in rows for k in row))
    with (out/name).open('w') as f:
        w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(rows)
def absolute(pattern):
    if pattern=='free':return 'free'
    parts=[]
    for axis in pattern.split('+'):
        name=axis[0];ranges=[]
        for term in axis[1:].split(','):
            term=term.lstrip('@');bounds=term.split('-');ranges.append('-'.join(str(351+int(x)) for x in bounds))
        parts.append(name+':'+','.join(ranges))
    return ';'.join(parts)
allrungs=[];slps=[];objectives=[];entries=[]
for label,folder in runs.items():
    src=out.parent/folder;lines=gzip.decompress((src/'solver-trace.txt.gz').read_bytes()).decode().splitlines()
    active=False;window={};method='';check={};retention={};pattern='free';pnum=0;applied=False;costate_error='';last=None;slp=None;events=[]
    for num,line in enumerate(lines,1):
        if 'LRS-WIN' in line:
            m=re.search(r'jumps=\[(\d+),(\d+)\) ticks=\[(\d+),(\d+)\).*constraints=(\d+) replayTicks=(\d+)',line)
            active=bool(m and int(m[1])==38)
            if active:
                window=dict(run=label,dual_cap=10000,nominal_jumps=int(m[2])-38,end_tick=int(m[4]),constraints=int(m[5]),commit_through=351+int(m[6]))
                entries.append(dict(window,entry_trace=line,trace_line=num))
        if not active:continue
        if 'SLP-GATE' not in line:events.append(f'{num}: {line}')
        if 'LRS-BUDGET' in line:
            window['dual_cap']=int(re.search(r'cap=(\d+)',line)[1]);entries[-1]['dual_cap']=window['dual_cap']
        if 'LRS-OBJ' in line and 'begin ' in line:
            method=line.split('begin ',1)[1];last=None
        m=re.search(r'CF\s+pass=(\d+) pattern=([^ ]+) n=',line)
        if m:pnum=int(m[1]);pattern=m[2];applied=False;costate_error='';check={};retention={}
        if 'CF-SEED applied' in line:
            applied=True;costate_error=re.search(r'costateError=([^ ]+)',line)[1]
        if 'CF-RETENTION' in line:
            m=re.search(r'iteration=(-?\d+) improvements=(\d+)',line);retention=dict(retained_iteration=int(m[1]),retained_improvements=int(m[2]))
        if 'CF-CHECK' in line:
            m=re.search(r'replayViol=([^ ]+) modelViol=([^ ]+)',line);check=dict(prefix_replay_violation=m[1],actual_continuous_violation=m[2])
        if re.search(r'CF\s+rung margin=',line):
            m=re.search(r'margin=([^ ]+) iters=(\d+).*?viol=([^ ]+) ms=([^ ]+).*?stop=(\w+) finalPg=([^ ]+)',line)
            if m:
                last=dict(window,objective=method,inertia_pass=pnum,pattern_relative=pattern,pattern_absolute=absolute(pattern),margin=m[1],iterations=int(m[2]),stop=m[5],max_acceptance_violation=m[3],ms=m[4],final_projected_residual=m[6],seed_applied=applied,seed_costate_error=costate_error,trace_line=num,**check,**retention);allrungs.append(last)
        if 'CF-SEED source' in line and last is not None:
            m=re.search(r'assumedViolation=([^ ]+) eligible=(\w+)',line);last.update(source_assumed_violation=m[1],source_seed_eligible=m[2])
        if re.search(r'CF\s+pass=.*stop',line) and last is not None:last['pattern_loop_exit']=line.split('CF',1)[1].strip()
        if re.search(r'SLP\s+start ',line):
            m=re.search(r'seed=(\w+) budget=(\d+)/(\d+).*inertiaAware=(\w+)',line)
            slp=dict(window,objective=method,seed=m[1],phase1_budget=int(m[2]),total_budget=int(m[3]),inertia_aware=m[4],trace_line=num);slps.append(slp)
        if 'SLP     phase1 infeasible' in line and slp is not None:
            m=re.search(r'viol=([^ ]+) lps=(\d+) ms=([^ ]+)',line);slp.update(phase1_terminal_violation=m[1],lp_calls=int(m[2]),lp_phase_ms=m[3])
        if 'LRS-OBJ' in line and 'end ' in line:
            m=re.search(r'result=(\w+) ms=([^ ]+)',line);objectives.append(dict(window,objective=method,outcome=m[1],ms=m[2],trace_line=num))
    (out/(label+'-events.txt')).write_text('\n'.join(events)+'\n')
save('cf-inertia-attempts.csv',allrungs);save('slp-attempts.csv',slps);save('objective-attempts.csv',objectives);save('window-entries.csv',entries)
lines=['# Jump39: closed-form inertia attempts','','Ticks in the absolute-pattern column are original0-based simulation states where the pre-input inertia threshold check is assumed to zero the axis. Patterns describe the model being solved, not proof of actual simulation outcomes. All attempts miss. Full precision fields and seed audits are in [CSV](cf-inertia-attempts.csv).','','| Run |Cap |Nominal size |Objective |Pass |Assumed zeros, absolute states |Iterations |Stop |Continuous miss |Prefix replay miss |','|---|---:|---:|---|---:|---|---:|---|---:|---:|']
for r in allrungs:
    lines.append(f'|{r["run"]} |{r["dual_cap"]} |{r["nominal_jumps"]} |{r["objective"]} |{r["inertia_pass"]} |{r["pattern_absolute"]} |{r["iterations"]} |{r["stop"]} |{r.get("actual_continuous_violation", "")} |{r.get("prefix_replay_violation", "")} |')
(out/'cf-inertia-attempts.md').write_text('\n'.join(lines)+'\n')
print('rungs',len(allrungs),'SLP',len(slps),'objective calls',len(objectives),'entries',len(entries))
for label in runs:
    rows=[r for r in allrungs if r['run']==label]
    print(label,'rungs',len(rows),'seeded',sum(r['seed_applied'] for r in rows),'eligible',sum(r.get('source_seed_eligible')=='true' for r in rows),'stops',collections.Counter(r['stop'] for r in rows),'best',min(float(r['max_acceptance_violation']) for r in rows),'retained',sum(r['retained_iteration']>=0 for r in rows))
