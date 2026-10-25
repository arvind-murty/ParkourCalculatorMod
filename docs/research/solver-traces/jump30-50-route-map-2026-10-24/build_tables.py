from pathlib import Path
import csv
p=Path(__file__).resolve().parent
cs=list(csv.DictReader((p/'compiled-constraints.csv').open()))
bs=list(csv.DictReader((p/'jump-boundaries.csv').open()))
def fmt(v):return f'{float(v):.3f}'.rstrip('0').rstrip('.')
def desc(rows):
    result=[]
    for axis in ['X','Z','F']:
        current=[r for r in rows if r['mode']==axis]
        plain=[r for r in current if not r['t2']]
        lo=[float(r['rhs']) for r in plain if r['comparison']=='GE']
        hi=[float(r['rhs']) for r in plain if r['comparison']=='LE']
        eq=[float(r['rhs']) for r in plain if r['comparison']=='EQ']
        if lo and hi:result.append(axis+' ∈ ['+fmt(max(lo))+', '+fmt(min(hi))+']')
        elif lo:result.append(axis+' ≥ '+fmt(max(lo)))
        elif hi:result.append(axis+' ≤ '+fmt(min(hi)))
        for value in eq:result.append(axis+' = '+fmt(value))
        for r in current:
            if r['t2']:result.append(f"{axis}[{r['t1']}] {'−' if r['op']=='MINUS' else '+'} {axis}[{r['t2']}] {r['comparison']} {fmt(r['rhs'])}")
    return '; '.join(result)
lines=['# Constraints for jumps30–50','','Input ticks are inclusive in this table. Position constraints refer to pre-input states. At a shared boundary, a state constraint is listed with the jump just ending: jump j owns states (start,end] in this presentation. All636 compiled rows are preserved separately; duplicate rows are collapsed to their strongest equivalent bound for display. Values are rounded to3 decimals; the exact compiled CSV retains floating-point boundaries.','','| Jump | Input ticks | Constraint states and rows |','|---:|---|---|']
for b in bs:
    j=int(b['jump']);a=int(b['start_tick']);z=int(b['end_state'])
    if not 30<=j<=50:continue
    groups=[]
    for t in range(a+1,z+1):
        text=desc([r for r in cs if int(r['t1'])==t])
        if not text:continue
        if groups and groups[-1][1]+1==t and groups[-1][2]==text:groups[-1]=(groups[-1][0],t,text)
        else:groups.append((t,t,text))
    label='<br>'.join((str(x) if x==y else f'{x}–{y}')+': '+text for x,y,text in groups)
    lines.append(f'| {j} | {a}–{z-1} | {label or "None"} |')
(p/'constraints-table.md').write_text('\n'.join(lines)+'\n')
source=p.parent/'full-route-commit1-staged-caps-2026-10-24'
w=list(csv.DictReader((source/'windows.csv').open()))
lines=['# Accepted windows leading into39, current staged baseline','','Every accepted row uses cap1000 and commits one nominal jump. All larger windows listed as failed below were tried first from the same row entry. Their objective calls and inertia attempts are in the original run. Actual inputs are [start,end); objective is evaluated at end.','','| Starting jump |Failed nominal window sizes first |Accepted nominal window |Actual input horizon |Accepted method/objective |Committed exit state |','|---:|---|---|---|---|---:|']
for j in range(30,39):
    rows=[r for r in w if int(r['first_jump'])==j and r['dual_cap']=='1000']
    a=next(r for r in rows if r['method']!='miss')
    failed=', '.join(str(int(r['last_jump'])-j+1) for r in rows if r['method']=='miss')
    lines.append(f'|{j} |{failed or "None"} |{j}–{a["last_jump"]} |{a["start_tick"]}–{a["end_tick"]} |{a["method"]} |{a["commit_end"]} |')
lines+=['','At39, both cap1000 and10000 try window sizes10,7,5,3,2,1; all fail. Actual endpoints493,450,441,412,388,373. Proposed commit exit365, no inputs committed.','']
(p/'leading-windows.md').write_text('\n'.join(lines))
