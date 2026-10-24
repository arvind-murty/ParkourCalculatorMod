# Solve-type comparison against the successful normal-margin reference

## Reference and scope

Use the successful **shared actual-inertia validation** run as the reference: `shared-inertia-validation-2026-10-23/normal7-commit1.txt`, ending `solved=true yaws=250`. This is the successful first30-jump run, not the earlier prefix-only or fixed-pattern variants. Compare its commit1 windows with the latest fixed0.0003 forward-jump and forward-tick runs, also commit1. This compares observed solver paths, not positional reachability from already-failed prefixes.

The saved reference used EPS2=1e-14 and endpoint250; latest fixed runs used EPS2=1e-24 and endpoint808. The reported prefix windows end no later than155, so none encounters either final endpoint. Entry states and selected window ends differ. These are observed route comparisons, not same-entry margin-only controls. No new solver run was needed for this extraction.

## How to read the compact table

Every intermediate window tries **closed-form Z/MAX → closed-form Z/MIN → closed-form X/MAX → closed-form X/MIN → centered SLP**, stopping at the first accepted result.

- `7: Zmin`: nominal7-jump window; Z/MAX failed, Z/MIN succeeded; X/MAX, X/MIN and SLP were not attempted.
- `7: Xmin`: Z/MAX, Z/MIN and X/MAX failed; X/MIN succeeded; SLP was not attempted.
- `7: SLP`: all four closed-form objectives failed; centered SLP succeeded.
- `7×`: all four closed-form objectives **and** centered SLP failed on that window, followed by the next window size.
- Numbers are **nominal window sizes**. Forward adjustment may extend the actual endpoint by a jump or tick; the expanded table gives actual end ticks.
- All fixed closed-form attempts use0.0003. Normal accepted closed-form margins are shown explicitly. SLP is feasibility/centering, not an accepted Z/MAX optimization result.

| Starting jump | Successful normal margins | Fixed +forward jump | Fixed +forward tick |
|---:|---|---|---|
| 1 | 7: Zmax @0.05 | 7: Zmax | 7: Zmax |
| 2 | 7: Zmax @0.05 | 7: Zmin | 7: Zmin |
| 3 | 7: Zmax @0.02 | 7: Zmax | 7: Zmin |
| 4 | 7: Zmax @0.02 | 7: SLP | 7: SLP |
| 5 | 7: Zmax @0.02 | 7: Zmax | 7: SLP |
| 6 | 7: Zmin @0.0003 | 7×<br>5: Zmax | 7×<br>5: Zmax |
| 7 | 7: Zmin @0.01 | 7: SLP | 7: Zmin |
| 8 | 7: Zmin @0.0003 | 7×<br>5: Zmin | 7×<br>5: SLP |
| 9 | 7: Zmax @0.0003 | 7×<br>5: Zmin | 7×<br>5: Zmin |
| 10 | 7: Zmax @0.0012 | 7×<br>5×<br>3: SLP | 7×<br>5×<br>3: Zmin |
| 11 | 7: Xmax @0.0012 | 7×<br>5×<br>3: Zmin | 7×<br>5×<br>3: Zmin |
| 12 | 7×<br>5: Zmin @0.0012 | 7×<br>5×<br>3×<br>2: Zmin | 7×<br>5×<br>3×<br>2: Zmin |
| 13 | 7: Xmin @0.0003 | 7×<br>5×<br>3×<br>2×<br>1: Zmax | 7×<br>5×<br>3×<br>2×<br>1: Zmax |
| 14 | 7×<br>5: SLP | 7×<br>5×<br>3×<br>2×<br>1× | 7×<br>5×<br>3×<br>2×<br>1× |

## Where the solve types diverge

1. **Starting jump2:** normal accepts primary Z/MAX; both shifted fixed runs reject it and accept Z/MIN on the same nominal7-jump size (actual end71 in all three). This is the first accepted-objective difference, although their earlier first-window endpoints and committed states already differ.
2. **Starting jump4:** normal still accepts primary closed-form Z/MAX; both fixed runs reject all four closed-form objectives and need centered SLP. Actual end77 in all three. This is the first CF-versus-SLP difference.
3. **Starting jump6:** normal accepts Z/MIN on the7-jump window ending88; both fixed runs miss the extended nominal7 window ending90 and shrink to5 jumps/end77, accepting Z/MAX.
4. **Starting jump10:** normal still accepts closed-form Z/MAX on7 jumps/end117. Fixed+jump misses7/end128 and5/end99, then accepts SLP on3/end90. Fixed+tick misses7/end118 and5/end99, then accepts Z/MIN on3/end90.
5. **Starting jump12:** normal misses7/end130 but accepts closed-form Z/MIN on5/end117 at0.0012. Both fixed runs also miss their5-jump attempts (actual end128 or118), miss3/end99, and only accept Z/MIN on2/end90. The direct comparison must account for those different endpoints, rather than describing them as the same five-jump problem.
6. **Starting jump13:** normal accepts **closed-form X/MIN on7 jumps/end132 at0.0003**. Both fixed runs fail their7/end155,5/end128,3/end113 and2/end99 windows, then accept Z/MAX on1/end90. The normal reference already demonstrates that0.0003 can yield the desired larger-window solve here; the observed difference includes entry state, endpoint, continuation history and epsilon.
7. **Starting jump14:** normal misses7/end155, then all four closed-form objectives fail on5/end130 but centered SLP succeeds. Both fixed runs miss every window down to1/end99.

Thus the solve-type divergence is visible much earlier than the jump14 rejection: Z/MAX versus Z/MIN at jump2, CF versus SLP at jump4, and loss of larger windows at jump6. For mimicking the successful run, those are the earlier differences to inspect, while jumps12–13 show the final loss of the reference's longer-lookahead solves.

## Every objective outcome, including failed attempts

✓ accepted, × failed; unlisted later methods were not called. Sizes are nominal; end is the absolute state tick. This table expands all compact shorthand above.

| Starting jump | Successful normal margins | Fixed +forward jump | Fixed +forward tick |
|---:|---|---|---|
| 1 | 7 jumps, end68: Z/MAX ✓ | 7 jumps, end71: Z/MAX ✓ | 7 jumps, end69: Z/MAX ✓ |
| 2 | 7 jumps, end71: Z/MAX ✓ | 7 jumps, end71: Z/MAX × → Z/MIN ✓ | 7 jumps, end71: Z/MAX × → Z/MIN ✓ |
| 3 | 7 jumps, end75: Z/MAX ✓ | 7 jumps, end75: Z/MAX ✓ | 7 jumps, end75: Z/MAX × → Z/MIN ✓ |
| 4 | 7 jumps, end77: Z/MAX ✓ | 7 jumps, end77: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ | 7 jumps, end77: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ |
| 5 | 7 jumps, end80: Z/MAX ✓ | 7 jumps, end80: Z/MAX ✓ | 7 jumps, end80: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ |
| 6 | 7 jumps, end88: Z/MAX × → Z/MIN ✓ | 7 jumps, end90: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end77: Z/MAX ✓ | 7 jumps, end90: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end77: Z/MAX ✓ |
| 7 | 7 jumps, end90: Z/MAX × → Z/MIN ✓ | 7 jumps, end90: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ | 7 jumps, end90: Z/MAX × → Z/MIN ✓ |
| 8 | 7 jumps, end99: Z/MAX × → Z/MIN ✓ | 7 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end90: Z/MAX × → Z/MIN ✓ | 7 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end90: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ |
| 9 | 7 jumps, end113: Z/MAX ✓ | 7 jumps, end113: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end90: Z/MAX × → Z/MIN ✓ | 7 jumps, end113: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end90: Z/MAX × → Z/MIN ✓ |
| 10 | 7 jumps, end117: Z/MAX ✓ | 7 jumps, end128: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end90: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ | 7 jumps, end118: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end90: Z/MAX × → Z/MIN ✓ |
| 11 | 7 jumps, end128: Z/MAX × → Z/MIN × → X/MAX ✓ | 7 jumps, end128: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end113: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end90: Z/MAX × → Z/MIN ✓ | 7 jumps, end128: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end113: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end90: Z/MAX × → Z/MIN ✓ |
| 12 | 7 jumps, end130: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end117: Z/MAX × → Z/MIN ✓ | 7 jumps, end130: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end128: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>2 jumps, end90: Z/MAX × → Z/MIN ✓ | 7 jumps, end130: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end118: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>2 jumps, end90: Z/MAX × → Z/MIN ✓ |
| 13 | 7 jumps, end132: Z/MAX × → Z/MIN × → X/MAX × → X/MIN ✓ | 7 jumps, end155: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end128: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end113: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>2 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>1 jumps, end90: Z/MAX ✓ | 7 jumps, end155: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end128: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end113: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>2 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>1 jumps, end90: Z/MAX ✓ |
| 14 | 7 jumps, end155: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end130: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ✓ | 7 jumps, end155: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end130: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end128: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>2 jumps, end113: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>1 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP × | 7 jumps, end155: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>5 jumps, end130: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>3 jumps, end118: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>2 jumps, end113: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP ×<br>1 jumps, end99: Z/MAX × → Z/MIN × → X/MAX × → X/MIN × → SLP × |

## Sources and extraction

- [Successful normal trace](shared-inertia-validation-2026-10-23/normal7-commit1.txt).
- [Latest fixed forward-jump trace](full-forward-shifts-2026-10-23/seven-forward-jump.txt), [latest fixed forward-tick trace](full-forward-shifts-2026-10-23/seven-forward-tick.txt).
- [Every objective outcome CSV](normal-vs-forward-solve-types-2026-10-23/objectives.csv), with original trace paths and line numbers.
- [Every window CSV](normal-vs-forward-solve-types-2026-10-23/windows.csv), with complete call sequences, accepted margins and actual endpoints.
- [Reproducible extractor](normal-vs-forward-solve-types-2026-10-23/compare_solve_types.py). Execute from the repository root. The extracted sequences were checked to be prefixes of the production method order, with only the final call permitted to succeed and every failed window containing all five failures.
- `LongRunSolver.solveWindow` is the source of the objective call order and centered-SLP fallback; `LRS-OBJ` records each actual result. Accepted CF margins are taken from the corresponding certified `CF rung` line.

No solver code, margins, epsilon or validation changed in this comparison; no tests ran. The full explicit objective table and compact table are also saved separately in the evidence directory.
