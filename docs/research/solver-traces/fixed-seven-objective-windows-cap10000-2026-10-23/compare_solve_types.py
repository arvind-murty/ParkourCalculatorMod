from pathlib import Path
import re,csv
root=Path('docs/research/solver-traces');out=root/'fixed-seven-objective-windows-cap10000-2026-10-23'
sources={
 'normal-success':root/'shared-inertia-validation-2026-10-23/normal7-commit1.txt',
 'fixed1000-global-extension':root/'fixed-seven-forward-jump-cap1000-2026-10-23/solver-trace.txt',
 'fixed10000-per-objective':out/'solver-trace.txt'}
windows=[];attempts=[];cf=[];stops=[]
for run,path in sources.items():
 cur=None;active=None;pendingEnd=None
 for lineno,line in enumerate(path.read_text().splitlines(),1):
  if 'LRS-WIN' in line:
   m=re.search(r'commit=(\d+) jumps=\[(\d+),(\d+)\) ticks=\[(\d+),(\d+)\).*replayTicks=(\d+)',line)
   cur=None;active=None;pendingEnd=None
   if m:
    cur=dict(run=run,commit=int(m[1]),first_jump=int(m[2])+1,nominal_size=int(m[3])-int(m[2]),
             window_start=int(m[4]),window_end=int(m[5]),commit_end=int(m[4])+int(m[6]),accepted_end='',accepted_method='miss',
             accepted_margin='',calls='',elapsed_ms='',source_line=lineno)
    windows.append(cur)
  if 'LRS-STUCK' in line:
   m=re.search(r'commit=(\d+) nextJump=(\d+) tick=(\d+)',line)
   if m:stops.append(dict(run=run,commit=m[1],next_jump=m[2],tick=m[3],source_line=lineno))
  if cur is None:continue
  if 'LRS-ADJUST' in line:
   m=re.search(r'selectedEnd=(\d+)',line)
   if m:pendingEnd=int(m[1])
  if 'LRS-OBJ' in line and 'begin ' in line:
   stage=line.split('begin ',1)[1]
   isSlp='SLP' in stage;isTerminal='terminal' in stage
   method='centered SLP' if isSlp else 'terminal recovery' if isTerminal else 'CF '+re.search(r'[XZ]/(?:MAX|MIN)',stage)[0]
   selectedEnd=cur['window_end'] if isSlp or pendingEnd is None else pendingEnd
   active=dict(run=run,commit=cur['commit'],first_jump=cur['first_jump'],nominal_size=cur['nominal_size'],
               window_start=cur['window_start'],window_end=cur['window_end'],selected_end=selectedEnd,method=method,
               outcome='',accepted_margin='',elapsed_ms='',source_line=lineno)
   attempts.append(active);pendingEnd=None
  if active is not None and 'CF ' in line and 'rung margin=' in line:
   m=re.search(r'margin=([^ ]+).*iters=(\d+).*viol=([^ ]+) ms=([\d.]+)',line)
   if m:
    cf.append(dict(run=run,commit=cur['commit'],first_jump=cur['first_jump'],nominal_size=cur['nominal_size'],method=active['method'],
                   selected_end=active['selected_end'],margin=m[1],iterations=m[2],violation=m[3],elapsed_ms=m[4],source_line=lineno))
    if 'certified' in line:active['accepted_margin']=m[1]
  if active is not None and 'LRS-OBJ' in line and 'end ' in line:
   m=re.search(r'result=(\w+) ms=([\d.]+)',line)
   if m:
    active['outcome']='pass' if m[1]=='solved' else 'fail';active['elapsed_ms']=m[2]
    cur['calls']+=('; ' if cur['calls'] else '')+active['method']+'@'+str(active['selected_end'])+' '+active['outcome']
    if active['outcome']=='pass':
     cur['accepted_method']=active['method'];cur['accepted_end']=active['selected_end'];cur['accepted_margin']=active['accepted_margin']
  if 'LRS ' in line and 'window i=' in line:
   m=re.search(r' -> (solved|miss) ms=([\d.]+)',line)
   if m:cur['elapsed_ms']=m[2]
   if 'cached -> solved' in line:cur['accepted_method']='cached'
  if 'LRS-SELECT' in line:
   m=re.search(r'acceptedEnd=(\d+)',line)
   if m:cur['accepted_end']=int(m[1])
for name,data in [('windows.csv',windows),('objectives.csv',attempts),('cf-rungs.csv',cf),('stops.csv',stops)]:
 with (out/name).open('w') as f:
  w=csv.DictWriter(f,fieldnames=data[0].keys());w.writeheader();w.writerows(data)
lines=['| Starting jump | Successful normal margins | Fixed1000, shared extension | Fixed10000, per-objective extension |','|---:|---|---|---|']
for j in range(1,15):
 vals=[]
 for run in sources:
  texts=[]
  for r in windows:
   if r['run']!=run or r['commit']!=1 or r['first_jump']!=j:continue
   size=str(r['nominal_size']);method=r['accepted_method']
   t=size+'×' if method=='miss' else size+': '+method.replace('CF ','').replace('centered SLP','SLP')
   if run=='normal-success' and r['accepted_margin']:t+=' @'+str(float(r['accepted_margin']))
   texts.append(t)
  vals.append('<br>'.join(texts))
 lines.append('| '+str(j)+' | '+' | '.join(vals)+' |')
(out/'solve-types.md').write_text('\n'.join(lines)+'\n')
# Check the parsed selected endpoint against every accepted-window replay log.
for r in windows:
 if r['run']!='fixed10000-per-objective':continue
 accepted=[a for a in attempts if a['run']==r['run'] and a['commit']==r['commit'] and a['first_jump']==r['first_jump'] and a['nominal_size']==r['nominal_size'] and a['outcome']=='pass']
 if r['accepted_method']!='miss':assert accepted and accepted[-1]['selected_end']==int(r['accepted_end']),r
print('\n'.join(lines))
