# Fixed jump6–12 rejected Z/MIN: violated rows

The shared validation observer captured this exact1264-iteration candidate after sequential reconstruction of the first5 committed states. These are violations of retained original boundaries before inward optimization margins. Replay is checked for acceptance only through tick64; full continuous feasibility is checked through88. Replay values after64 are diagnostic.

| Tick | Constraint | Continuous value | Continuous violation | Replay value | Replay violation | Replay checked for commit? |
|---:|---|---:|---:|---:|---:|---|
| 58 | X ≤ 729.699999988 | 729.711317521 | 0.011317533 | 729.711340373 | 0.011340385 | yes |
| 66 | X ≤ 729.699999988 | 729.739104806 | 0.039104817 | 729.739131773 | 0.039131785 | no |
| 74 | X ≤ 731.300000012 | 731.309594821 | 0.009594809 | 731.309554409 | 0.009554397 | no |
| 79 | X ≥ 732.699999988 | 732.669210409 | 0.030789580 | 732.669121281 | 0.030878707 | no |
| 83 | Z ≥ -1088.699999988 | -1090.205304979 | 1.505304991 | -1090.205353530 | 1.505353542 | no |
| 84 | X ≤ 732.699999988 | 732.721434752 | 0.021434764 | 732.721277309 | 0.021277321 | no |
| 85 | X ≤ 732.699999988 | 732.711968441 | 0.011968453 | 732.711795668 | 0.011795680 | no |
| 86 | X ≤ 732.699999988 | 732.703162100 | 0.003162112 | 732.702972945 | 0.002972957 | no |
| 87 | Z ≥ -1090.300000012 | -1091.523838197 | 1.223838185 | -1091.523883325 | 1.223883313 | no |

All66 per-axis inertia threshold checks agree with the unzeroed local model; no inertia assumption is violated. Assumed-model and actual continuous row values agree to around1e-12. Thus these failures are already present in the continuous optimized candidate, not introduced by Minecraft angle conversion.

Original Z88 lower bound>=-1090.300000011921 is omitted for this window’s Z/MIN solve/validation. The replay endpoint-1091.807886997006 also violates that original row by1.507886985085, but it is NOT one of the rows causing this window’s rejection. It would be restored for final full-route validation.

[All retained row margins](fixed-constraint-margins.csv), [inertia checks](fixed-inertia-checks.csv), [focused log](fixed-constraint-audit-focused.log).
