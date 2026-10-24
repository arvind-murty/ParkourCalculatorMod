from pathlib import Path
import csv,re
root=Path('docs/research/solver-traces')
sources={
 'normal-success':root/'shared-inertia-validation-2026-10-23/normal7-commit1.txt',
 'fixed-forward-jump':root/'full-forward-shifts-2026-10-23/seven-forward-jump.txt',
 'fixed-forward-tick':root/'full-forward-shifts-2026-10-23/seven-forward-tick.txt',
}
windows=[];objectives=[]
for run,path in sources.items():
 current=None;active=None;margin=None
 for number,line in enumerate(path.read_text().splitlines(),1):
  if 'LRS-WIN' in line:
   m=re.search(r'commit=(\d+) jumps=\[(\d+),(\d+)\) ticks=\[(\d+),(\d+)\)',line)
   current=None
   if m and m[1]=='1':
    current=dict(run=run,first_jump=int(m[2])+1,nominal_window=int(m[3])-int(m[2]),
                 start_tick=int(m[4]),end_tick=int(m[5]),accepted_solver='miss',margin='',calls=[],source=str(path),line=number)
    windows.append(current)
  if current is None:continue
  if 'LRS-OBJ' in line and 'begin ' in line:
   active=line.split('begin ',1)[1];margin=None
  if 'CF ' in line and 'certified' in line:
   m=re.search(r'margin=([^ ]+)',line)
   if m:margin=m[1]
  if 'LRS-OBJ' in line and 'end ' in line:
   m=re.search(r'result=(\w+)',line)
   if m:
    name='SLP' if 'SLP' in active else re.search(r'[XZ]/(?:MAX|MIN)',active)[0]
    status='pass' if m[1]=='solved' else 'fail'
    current['calls'].append(name+' '+status)
    objectives.append(dict(run=run,first_jump=current['first_jump'],nominal_window=current['nominal_window'],
        start_tick=current['start_tick'],end_tick=current['end_tick'],solver='centered SLP' if name=='SLP' else 'closed form',
        objective=name if name!='SLP' else 'feasibility/centering',outcome=status,
        accepted_margin=margin if name!='SLP' and status=='pass' and margin else '',source=str(path),line=number))
    if status=='pass':current['accepted_solver']=name;current['margin']=margin if name!='SLP' else ''
out=root/'normal-vs-forward-solve-types-2026-10-23';out.mkdir(exist_ok=True)
for filename,rows in [('windows.csv',[dict(r, calls='; '.join(r['calls'])) for r in windows if r['first_jump']<=14]),
                      ('objectives.csv',[r for r in objectives if r['first_jump']<=14])]:
 with (out/filename).open('w') as f:
  w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
def cell(run,jump,detail=False):
 vals=[]
 for r in windows:
  if r['run']!=run or r['first_jump']!=jump:continue
  w=r['nominal_window'];name=r['accepted_solver']
  if detail:
   val=f"{w} jumps, end{r['end_tick']}: "+' → '.join(x.replace(' pass',' ✓').replace(' fail',' ×') for x in r['calls'])
  elif name=='miss':val=f'{w}×'
  else:
   val=f"{w}: {name.replace('/MAX','max').replace('/MIN','min')}"
   if run=='normal-success' and r['margin']:val+=f" @{float(r['margin']):g}"
  vals.append(val)
 return '<br>'.join(vals)
short=['| Starting jump | Successful normal margins | Fixed +forward jump | Fixed +forward tick |','|---:|---|---|---|']
expanded=['| Starting jump | Successful normal margins | Fixed +forward jump | Fixed +forward tick |','|---:|---|---|---|']
for j in range(1,15):
 short.append('| '+str(j)+' | '+' | '.join(cell(r,j) for r in sources)+' |')
 expanded.append('| '+str(j)+' | '+' | '.join(cell(r,j,True) for r in sources)+' |')
(out/'compact-table.md').write_text('\n'.join(short)+'\n')
(out/'every-objective-table.md').write_text('\n'.join(expanded)+'\n')
print('\n'.join(short))
