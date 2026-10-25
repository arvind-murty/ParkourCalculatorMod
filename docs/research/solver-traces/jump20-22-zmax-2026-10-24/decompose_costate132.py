from pathlib import Path
import csv,math
out=Path(__file__).resolve().parent
source=out.parent/'jump30-50-route-map-2026-10-24'
inputs={int(row['tick']):row for row in csv.DictReader((source/'physics-inputs.csv').open())}
constraints={row['name']:row for row in csv.DictReader((source/'compiled-constraints.csv').open())}
friction=[]
for tick in range(132,178):
    slip=float(inputs[tick]['slipperiness']);friction.append(.91 if math.isnan(slip) else slip*.91)
fpre=[1.];spre=[0.]
for f in friction:spre.append(spre[-1]+fpre[-1]);fpre.append(fpre[-1]*f)
rows=[dict(source='objective Z178',axis='Z',coefficient=spre[46],multiplier='',contribution=spre[46])]
g=[0.,spre[46]]
for row in csv.DictReader((out/'1000-terminal-multipliers.csv').open()):
    c=constraints[row['name']];lam=float(row['lambda']);coef=spre[int(c['t1'])-132]*(-1 if c['comparison']=='GE' else 1);delta=-lam*coef;axis=int(row['axis']);g[axis]+=delta
    rows.append(dict(source=row['name'],axis='X' if axis==0 else 'Z',coefficient=coef,multiplier=lam,contribution=delta))
with (out/'costate132-contributions.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
record=next(row for row in csv.DictReader((out/'1000-terminal-costates.csv').open()) if row['tick']=='132')
assert g[0]==float(record['gx']) and g[1]==float(record['gz'])
print('exact reconstructed gx,gz',g,'Z160/Z178 first-input coupling',spre[28]/spre[46])
lines=['# Why tick132 has a tiny costate','','Offline decomposition of the saved1000-iteration unpatterned run. No optimization rerun. [Exact terms](costate132-contributions.csv), [reconstruction script](decompose_costate132.py), [original diagnostics](README.md).','','The code computes `g = objective coefficient − sum(multiplier × normalized wall coefficient)`. The multipliers below are the saved current values, not a certificate that the dual is optimal. The script reconstructs the Java values bit-for-bit from the exported friction schedule and saved multipliers. Ground friction is slip×.91; air friction.91, matching the default double-precision JumpLinearModel constructor used by this solve.','','## Z component','','| Term |Contribution to gZ132 |','|---|---:|']
for row in rows:
    if row['axis']=='Z' and abs(row['contribution'])>1e-10:lines.append(f'|{row["source"]} |{row["contribution"]:+.15f} |')
lines+=['',f'Result: **gZ132={g[1]:.17g}**. The objective plus lower Z136 contribution sum to4.659266021956938; the upper-Z contributions cancel that to approximately13 decimal places.','','## X component','','The objective has X coefficient0. Lower X136 contributes+.054150090632665, opposed by X upper walls.','','| Term |Contribution to gX132 |','|---|---:|']
for row in rows:
    if row['axis']=='X' and abs(row['contribution'])>1e-10:lines.append(f'|{row["source"]} |{row["contribution"]:+.15f} |')
lines +=['',f'Including smaller nonzero terms, **gX132={g[0]:.17g}**, norm1.588269351191274e-13.','','## Interpretation','','The costate is the NET objective incentive after subtracting constraint prices. Its tiny norm does not mean132 is physically unimportant or that its acceleration is small. Positive X is opposed by early/future upper-X walls; negative X is opposed by lower X136. Positive Z helps the final objective but is opposed by earlier upper-Z walls, particularly160; negative Z is opposed by lower Z136. The dual search brings these priced demands into near cancellation for this first control.','','First input132 affects Z160 with coefficient4.4875168130357395 and Z178 with4.511200743155278. Almost99.5% of its final-position effect is already present at160. The multiplier on upper Z160 is1.0163076270735563, so that wall alone contributes-4.56069756370904, slightly more than the final objective contributes+4.511200743155278. The early Z133..136 walls balance the remainder. This is an earlier position cap, not a direct cap at the objective state178.','','Tick132 also uniquely affects state133. Later controls cannot adjust that first state. Every tick has a different set of wall coefficients;133 therefore does not share the cancellation: gX133=.01022714569596829, gZ133=.0013856566278287463, norm.010320588809598435.','','Near zero, the support norm has an ambiguous maximizing direction: at exactly g=0 any full-magnitude control has the same Lagrangian contribution. Our smoothing produces shortened derivative controls there, while decoding requires a full acceleration. That explains why tiny g is numerically problematic. It does NOT prove an original primal solution is indifferent to tick132: the constraints still depend on it strongly. It also does not establish a dual optimum, unavoidable cancellation or a duality gap; the current projected residual is still.1495.','','The exact numeric cancellation is verified. The broader question of why this Newton/damping trajectory stays near this nonsmooth point rather than reaching a feasible primal path remains a targeted numerical investigation.','']
(out/'costate132-explanation.md').write_text('\n'.join(lines))
