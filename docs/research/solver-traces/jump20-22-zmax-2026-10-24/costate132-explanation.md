# Why tick132 has a tiny costate

Offline decomposition of the saved1000-iteration unpatterned run. No optimization rerun. [Exact terms](costate132-contributions.csv), [reconstruction script](decompose_costate132.py), [original diagnostics](README.md).

The code computes `g = objective coefficient − sum(multiplier × normalized wall coefficient)`. The multipliers below are the saved current values, not a certificate that the dual is optimal. The script reconstructs the Java values bit-for-bit from the exported friction schedule and saved multipliers. Ground friction is slip×.91; air friction.91, matching the default double-precision JumpLinearModel constructor used by this solve.

## Z component

| Term |Contribution to gZ132 |
|---|---:|
|objective Z178 |+4.511200743155278 |
|Z@135 |-0.091899888625649 |
|Z@134 |-0.005832350984457 |
|Z@133 |-0.000836218637700 |
|Z@160hi |-4.560697563709040 |
|Z@136 |+0.148065278801660 |

Result: **gZ132=9.1621155107191043e-14**. The objective plus lower Z136 contribution sum to4.659266021956938; the upper-Z contributions cancel that to approximately13 decimal places.

## X component

The objective has X coefficient0. Lower X136 contributes+.054150090632665, opposed by X upper walls.

| Term |Contribution to gX132 |
|---|---:|
|X@134 |-0.045055562101701 |
|X@156hi |-0.000045562313201 |
|X@166hi |-0.000000000337561 |
|X@157hi |-0.000007363463288 |
|X@158hi |-0.000001378537420 |
|X@154hi |-0.002842858267917 |
|X@159hi |-0.000000208880503 |
|X@160hi |-0.000000001941410 |
|X@176 |-0.006197154789533 |
|X@136lo |+0.054150090632665 |

Including smaller nonzero terms, **gX132=1.2973649932135345e-13**, norm1.588269351191274e-13.

## Interpretation

The costate is the NET objective incentive after subtracting constraint prices. Its tiny norm does not mean132 is physically unimportant or that its acceleration is small. Positive X is opposed by early/future upper-X walls; negative X is opposed by lower X136. Positive Z helps the final objective but is opposed by earlier upper-Z walls, particularly160; negative Z is opposed by lower Z136. The dual search brings these priced demands into near cancellation for this first control.

First input132 affects Z160 with coefficient4.4875168130357395 and Z178 with4.511200743155278. Almost99.5% of its final-position effect is already present at160. The multiplier on upper Z160 is1.0163076270735563, so that wall alone contributes-4.56069756370904, slightly more than the final objective contributes+4.511200743155278. The early Z133..136 walls balance the remainder. This is an earlier position cap, not a direct cap at the objective state178.

Tick132 also uniquely affects state133. Later controls cannot adjust that first state. Every tick has a different set of wall coefficients;133 therefore does not share the cancellation: gX133=.01022714569596829, gZ133=.0013856566278287463, norm.010320588809598435.

Near zero, the support norm has an ambiguous maximizing direction: at exactly g=0 any full-magnitude control has the same Lagrangian contribution. Our smoothing produces shortened derivative controls there, while decoding requires a full acceleration. That explains why tiny g is numerically problematic. It does NOT prove an original primal solution is indifferent to tick132: the constraints still depend on it strongly. It also does not establish a dual optimum, unavoidable cancellation or a duality gap; the current projected residual is still.1495.

The exact numeric cancellation is verified. The broader question of why this Newton/damping trajectory stays near this nonsmooth point rather than reaching a feasible primal path remains a targeted numerical investigation.
