package de.legoshi.parkourcalc.core.anglesolver.solver;


import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/** Trust-region sequential-LP solve on the byte-exact model: the post-failure closer for windows whose
 *  cross-seam wall coupling gives the Lagrangian dual a genuine duality gap. The problem stays exactly
 *  linear in the per-tick input vectors, so linearize {@code u_t(yaw)} around the current facings and
 *  LP-step them: phase 1 reduces the worst byte-exact wall violation, phase 2 improves the objective
 *  while staying strictly inside; the trust region shrinks on rejection. Seeded from the dual recovery
 *  at margin 0. Returns absolute wrapped feasible facings, or {@code null} (caller falls back). */
public final class SlpSolve {

    public static boolean DEBUG = false;

    /** Inward clearance required after phase 1 (~ the sine-bucket lattice spacing); phase 2 may hug back to a quarter. */
    private static final double CLEARANCE = 1.0e-6;
    private static final double RAD = Math.PI / 180.0;

    public static final class Config {
        /** Phase-1 share of the LP budget; not restoring feasibility within it means infeasible. */
        public int phase1Calls = 40;
        /** Total LP budget across both phases. */
        public int totalCalls = 60;
        public double trStartDeg = 30.0;
        public double trMaxDeg = 45.0;
        public double trMinDeg = 1.0e-7;
        public int lpMaxIter = 2000;
        /** Phase-1 target for a centered solve; the result is accepted at whatever clearance was reached
         *  (>= {@value SlpSolve#CLEARANCE}), so a corridor narrower than twice this still solves. */
        public double centerClearance = 2.0e-2;
        /** Restrict exact acceptance to this input prefix; require full continuous constraint feasibility
         *  as well. All window rows still guide the LP steps. */
        public int replayTickLimit = Integer.MAX_VALUE;
    }

    private SlpSolve() {
    }

    /** Returns absolute wrapped facings with byte-exact {@code maxViolation <= feasTol}, or {@code null}. */
    public static double[] optimize(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel) {
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, null, false, false, new Config(), null);
    }

    /** Like {@link #optimize}, but seeded from the given absolute wrapped facings instead of the dual
     *  recovery — typically a feasible point from another Solve-For direction, so phase 2 can ascend
     *  this spec's objective even where this direction's own dual recovery degenerates. */
    public static double[] optimize(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                    double[] seedAbsWrapped) {
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, seedAbsWrapped, false, false, new Config(), null);
    }

    public static double[] optimize(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                    double[] seedAbsWrapped, Config cfg) {
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, seedAbsWrapped, false, false, cfg, null);
    }

    public static double[] optimize(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                    double[] seedAbsWrapped, Config cfg, ClosestMiss miss) {
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, seedAbsWrapped, false, false, cfg, miss);
    }

    public static double[] optimize(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                    double[] seedAbsWrapped, int phase1Calls, int totalCalls) {
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, seedAbsWrapped, false, false,
                withCalls(phase1Calls, totalCalls), null);
    }

    public static double[] optimizeBestEffort(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                              double[] seedAbsWrapped, int phase1Calls, int totalCalls) {
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, seedAbsWrapped, true, true,
                withCalls(phase1Calls, totalCalls), null);
    }

    public static double[] optimizeBestEffort(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                              double[] seedAbsWrapped, int phase1Calls, int totalCalls, boolean inertiaAware) {
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, seedAbsWrapped, true, inertiaAware,
                withCalls(phase1Calls, totalCalls), null);
    }

    public static double[] optimizeBestEffort(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                              double[] seedAbsWrapped, int phase1Calls, int totalCalls, boolean inertiaAware,
                                              double trMinDeg) {
        Config cfg = withCalls(phase1Calls, totalCalls);
        cfg.trMinDeg = trMinDeg;
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, seedAbsWrapped, true, inertiaAware, cfg, null);
    }

    /** Feasibility-only centered solve: phase 1 deepens clearance toward {@link Config#centerClearance}, the
     *  hugging phase 2 is skipped. For surrogate-objective solves (lead-in windows). */
    public static double[] optimizeCentered(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel) {
        Config cfg = new Config();
        return optimize(exact, spec, feasTol, cancel, cfg.centerClearance, false, null, false, false, cfg, null);
    }

    public static double[] optimizeCentered(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                            double[] seedAbsWrapped, Config cfg) {
        return optimize(exact, spec, feasTol, cancel, cfg.centerClearance, false, seedAbsWrapped, false, false, cfg, null);
    }

    private static Config withCalls(int phase1Calls, int totalCalls) {
        Config cfg = new Config();
        cfg.phase1Calls = phase1Calls;
        cfg.totalCalls = totalCalls;
        return cfg;
    }

    public static double[] optimizeLocked(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                          double[] seedAbsWrapped, boolean[] lockVars, int phase1Calls,
                                          int totalCalls, boolean inertiaAware) {
        return optimize(exact, spec, feasTol, cancel, CLEARANCE, true, seedAbsWrapped, true, inertiaAware,
                withCalls(phase1Calls, totalCalls), null, lockVars);
    }

    private static double[] optimize(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                     double targetClearance, boolean hugObjective, double[] seedAbsWrapped,
                                     boolean bestEffort, boolean inertiaAware, Config cfg, ClosestMiss miss) {
        return optimize(exact, spec, feasTol, cancel, targetClearance, hugObjective, seedAbsWrapped,
                bestEffort, inertiaAware, cfg, miss, null);
    }

    private static double[] optimize(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                     double targetClearance, boolean hugObjective, double[] seedAbsWrapped,
                                     boolean bestEffort, boolean inertiaAware, Config cfg, ClosestMiss miss,
                                     boolean[] lockVars) {
        List<JumpConstraint> constraints = spec.constraints;
        JumpPhysicsInputs sc = spec.asScenario();
        YawTies ties = null;
        List<JumpConstraint> yawIneq = new ArrayList<>();
        if (JumpLinearModel.hasFacingWall(constraints)) {
            ties = YawTies.of(constraints, sc.numTicks);
            YawOrder order = YawOrder.carry(constraints, ties, sc.numTicks);
            if (order == null) return null;
            yawIneq.addAll(order.carried());
        }
        for (JumpConstraint c : constraints) {
            if (c.mode == JumpConstraint.Mode.F) continue;
            if (c.cmp == JumpConstraint.Cmp.EQ) return null; // UI maps EQ to a corridor; a true EQ cannot hold strict clearance
        }

        JumpLinearModel lin = new JumpLinearModel(sc);
        int n = lin.n;

        boolean[] trivialInfeasible = {false};
        List<JumpConstraint> ineq = new ArrayList<>();
        List<Integer> halves = new ArrayList<>();
        List<JumpLinearModel.Wall> walls = new ArrayList<>();
        List<JumpLinearModel.Wall> plainWalls = new ArrayList<>();
        for (JumpConstraint c : constraints) {
            if (c.mode == JumpConstraint.Mode.F) continue;
            if (JumpLinearModel.isCrossAxis(c) && c.t2 != null) {
                for (int half = -1; half <= 1; half += 2) {
                    ineq.add(c);
                    halves.add(half);
                    walls.add(null);
                }
                continue;
            }
            JumpLinearModel.Wall w = lin.compileWall(c, 0.0, trivialInfeasible);
            if (trivialInfeasible[0]) return null; // a violated constant is unfixable
            if (w != null) { ineq.add(c); halves.add(0); walls.add(w); plainWalls.add(w); }
        }
        int mPos = walls.size();
        ineq.addAll(yawIneq);
        int m = ineq.size();
        int[] halfSign = new int[m];
        for (int j = 0; j < mPos; j++) halfSign[j] = halves.get(j);
        if (m == 0) return null; // nothing to restore; the closed form already handles the unconstrained case

        double[] cx = new double[n];
        double[] cz = new double[n];
        lin.objectiveVectors(spec.objective, cx, cz); // MAX-normalized: c.u is always to maximize

        double[] theta;
        if (seedAbsWrapped != null) {
            theta = seedAbsWrapped.clone();
        } else {
            // Seed: the dual recovery at margin 0; infeasible here, but globally informed.
            if (plainWalls.isEmpty()) return null;
            CostateDualSolver dual = new CostateDualSolver(n, cx, cz, lin.mMagAll(), plainWalls);
            CostateDualSolver.Result r = dual.solve(0.0, null);
            if (r == null) {
                if (SolverTrace.on()) SolverTrace.log("SLP", "dual seed rejected stop=%s iters=%d n=%d m=%d",
                        dual.lastStopReason, dual.lastIters, n, plainWalls.size());
                return null;
            }
            theta = lin.recoverAlongCostate(spec.objective, r.gx, r.gz);
        }

        if (ties != null) theta = ties.expand(ties.reduce(theta));
        if (ties == null && !yawIneq.isEmpty()) theta = YawOrder.carryFreeTicks(lin, sc.startYaw, theta);

        long t0 = System.nanoTime();
        if (SolverTrace.on()) {
            SolverTrace.log("SLP", "start n=%d m=%d seed=%s budget=%d/%d bestEffort=%s inertiaAware=%s fold=%s",
                    n, m, seedAbsWrapped != null ? "given" : "dual", cfg.phase1Calls, cfg.totalCalls, bestEffort,
                    inertiaAware, ties == null ? "-" : String.valueOf(ties.dims()));
        }
        JumpConstraintCompiler.Compiled compiled = JumpConstraintCompiler.compile(spec, cfg.replayTickLimit);
        WindowCandidateValidation validation = cfg.replayTickLimit < n
                ? new WindowCandidateValidation(exact, spec, cfg.replayTickLimit) : null;
        double[] viol = new double[m];
        double[] candViol = new double[m];
        double[] ux = new double[n];
        double[] uz = new double[n];
        boolean max = spec.objective.sense == Objective.Sense.MAX;
        int lpCalls = 0;
        double tr = cfg.trStartDeg;
        int dims = ties == null ? n : ties.dims();
        int[] col = new int[n];
        for (int t = 0; t < n; t++) {
            int v = ties == null ? t : ties.varOf(t);
            if (v >= 0 && lockVars != null && v < lockVars.length && lockVars[v]) v = -1;
            col[t] = v;
        }

        boolean[] zeroX = new boolean[n];
        boolean[] zeroZ = new boolean[n];
        boolean[] lastZeroX = null;
        boolean[] lastZeroZ = null;
        List<JumpLinearModel.Wall> lpWalls = walls;
        JumpLinearModel lpLin = lin;
        double[] lpCx = cx;
        double[] lpCz = cz;

        for (int phase = 1; phase <= (hugObjective ? 2 : 1) && lpCalls < cfg.totalCalls; phase++) {
            int budget = phase == 1 ? cfg.phase1Calls : cfg.totalCalls;
            while (lpCalls < budget) {
                if (cancel != null && cancel.get()) {
                    offerMiss(miss, exact, sc, compiled, theta);
                    return null;
                }
                double[] gf = sc.toGameFacings(Angles.wrapAll(theta));
                ForwardPath path = exact.forward(sc, gf);
                double maxViol = exactSlacks(ineq, halfSign, gf, path, viol);
                double objNorm = normObjective(path, spec.objective, max);
                if (phase == 1 && maxViol <= -targetClearance) break;
                for (int j = mPos; j < m; j++) {
                    if (ineq.get(j).rhs != 0.0) viol[j] += YawOrder.FLOAT_MARGIN_DEG;
                }

                if (inertiaAware) lin.zeroingPattern(theta, exact.inertiaThreshold(), exact.perAxisInertia(), zeroX, zeroZ);
                if (inertiaAware && (!patternEquals(lastZeroX, zeroX) || !patternEquals(lastZeroZ, zeroZ))) {
                    List<JumpLinearModel.Wall> rebuilt = null;
                    if (anySet(zeroX) || anySet(zeroZ)) {
                        JumpLinearModel patterned = new JumpLinearModel(sc, zeroX.clone(), zeroZ.clone());
                        rebuilt = new ArrayList<>(mPos);
                        for (int j = 0; j < mPos; j++) {
                            if (halfSign[j] != 0) {
                                rebuilt.add(null);
                                continue;
                            }
                            JumpLinearModel.Wall w = patterned.compileWall(ineq.get(j), 0.0, null);
                            if (w == null) { rebuilt = null; break; }
                            rebuilt.add(w);
                        }
                        if (rebuilt != null) {
                            lpWalls = rebuilt;
                            lpLin = patterned;
                            lpCx = new double[n];
                            lpCz = new double[n];
                            patterned.objectiveVectors(spec.objective, lpCx, lpCz);
                        }
                    }
                    if (rebuilt == null) {
                        lpWalls = walls;
                        lpLin = lin;
                        lpCx = cx;
                        lpCz = cz;
                    }
                    lastZeroX = zeroX.clone();
                    lastZeroZ = zeroZ.clone();
                }

                for (int j = 0; j < m; j++) {
                    if (halfSign[j] == 0) continue;
                    JumpConstraint c = ineq.get(j);
                    lpWalls.set(j, lpLin.crossAxisHalfWall(c, JumpLinearModel.dominantSignOnPath(c, path), halfSign[j], 0.0));
                }
                for (int t = 0; t < n; t++) {
                    double phi = lin.baseArg(t) + theta[t] * RAD;
                    ux[t] = lin.mMag(t) * StrictMath.cos(phi);
                    uz[t] = lin.mMag(t) * StrictMath.sin(phi);
                }
                double[][] rows = new double[m][dims];
                for (int j = mPos; j < m; j++) {
                    JumpConstraint c = ineq.get(j);
                    double sgn = c.cmp == JumpConstraint.Cmp.GE ? -1.0 : 1.0;
                    int v1 = col[c.t1];
                    if (v1 >= 0) rows[j][v1] += sgn;
                    if (c.t2 != null) {
                        int v2 = col[c.t2];
                        if (v2 >= 0) rows[j][v2] -= sgn;
                    }
                }
                for (int j = 0; j < mPos; j++) {
                    JumpLinearModel.Wall wall = lpWalls.get(j);
                    double[] row = rows[j];
                    double[] cxj = wall.coefX;
                    double[] czj = wall.coefZ;
                    for (int t = 0; t < n; t++) {
                        int v = col[t];
                        if (v < 0) continue;
                        double du = 0.0;
                        if (cxj != null && cxj[t] != 0.0) du += cxj[t] * -uz[t];
                        if (czj != null && czj[t] != 0.0) du += czj[t] * ux[t];
                        row[v] += du * RAD;
                    }
                }
                double[] objRow = null;
                if (phase != 1) {
                    objRow = new double[dims];
                    for (int t = 0; t < n; t++) {
                        int v = col[t];
                        if (v < 0) continue;
                        objRow[v] += -(lpCx[t] * -uz[t] + lpCz[t] * ux[t]) * RAD; // maximize c.du
                    }
                }
                lpCalls++;
                TrustRegionLp.Result lp = TrustRegionLp.solve(rows, viol, objRow, tr, phase == 1,
                        Math.max(-CLEARANCE, maxViol), cfg.lpMaxIter);
                if (lp == null) {
                    if (SolverTrace.on()) SolverTrace.log("SLP", "lp#%d phase=%d LP failed, phase stop", lpCalls, phase);
                    break; // LP infeasible/degenerate at this linearization: stop the phase
                }
                double[] d = lp.d;

                double[] cand = new double[n];
                double step = 0.0;
                for (int t = 0; t < n; t++) {
                    int v = col[t];
                    cand[t] = v < 0 ? theta[t] : theta[t] + d[v];
                }
                for (int v = 0; v < dims; v++) step = Math.max(step, Math.abs(d[v]));
                double[] cgf = sc.toGameFacings(Angles.wrapAll(cand));
                ForwardPath cpath = exact.forward(sc, cgf);
                double cViol = exactSlacks(ineq, halfSign, cgf, cpath, candViol);
                double cObj = normObjective(cpath, spec.objective, max);
                // Phase 2 accepts any strictly feasible improvement; demanding extra clearance would
                // forbid hugging the very wall the objective optimizes into.
                boolean accept = phase == 1
                        ? cViol < maxViol
                        : cViol <= feasTol && cObj > objNorm;
                if (SolverTrace.on()) {
                    traceInertiaStep(lpCalls, phase, inertiaAware, lin, exact, theta, cand, path, cpath,
                            zeroX, zeroZ, accept);
                }
                if (SolverTrace.on()) {
                    SolverTrace.log("SLP", "lp#%d phase=%d tr=%.3g step=%.3g pred=%.3e viol=%.3e->%.3e worst=%s->%s t24-25=%s obj=%.9f->%.9f %s",
                            lpCalls, phase, tr, step, lp.s, maxViol, cViol,
                            worstViolation(ineq, halfSign, gf, path, viol),
                            worstViolation(ineq, halfSign, cgf, cpath, candViol),
                            tickWallSlacks(ineq, halfSign, viol, candViol, 24, 25), objNorm, cObj,
                            accept ? "accept" : "reject");
                }
                if (accept) {
                    theta = cand;
                    if (step > 0.8 * tr) tr = Math.min(tr * 2.0, cfg.trMaxDeg);
                } else {
                    tr *= 0.5;
                    if (tr < cfg.trMinDeg) break; // stalled on the float lattice: this phase is done
                }
            }
            if (phase == 1) {
                double[] gf = sc.toGameFacings(Angles.wrapAll(theta));
                ForwardPath endPath = exact.forward(sc, gf);
                double endViol = exactSlacks(ineq, halfSign, gf, endPath, viol);
                // A hugging solve keeps any strictly feasible point (a tight corridor's best clearance can
                // be shallower than CLEARANCE); a centered solve keeps the demand, its result seeds windows.
                double phase1Gate = hugObjective ? feasTol : -CLEARANCE;
                boolean prefixAccepted = validation != null
                        && validation.evaluate(Angles.wrapAll(theta), gf, endPath, lpLin).maxViolation() <= feasTol;
                if (endViol > phase1Gate && !prefixAccepted) {
                    if (DEBUG) System.out.printf("  SLP infeasible: viol=%.3e after %d LPs (%.1f ms)%n",
                            endViol, lpCalls, (System.nanoTime() - t0) / 1e6);
                    if (SolverTrace.on()) {
                        SolverTrace.log("SLP", "phase1 infeasible viol=%.3e lps=%d ms=%.1f%s",
                                endViol, lpCalls, (System.nanoTime() - t0) / 1e6, bestEffort ? " (best effort kept)" : "");
                    }
                    if (!inertiaAware && !bestEffort) {
                        return optimize(exact, spec, feasTol, cancel, targetClearance, hugObjective,
                                Angles.wrapAll(theta), false, true, cfg, miss, lockVars);
                    }
                    offerMiss(miss, exact, sc, compiled, theta);
                    return bestEffort ? Angles.wrapAll(theta) : null;
                }
                if (SolverTrace.on()) SolverTrace.log("SLP", "phase1 done viol=%.3e lps=%d", endViol, lpCalls);
                tr = 10.0; // phase 2 restarts from a workable step size (phase 1 may have collapsed it)
            }
        }

        double[] yaws = Angles.wrapAll(theta);
        double[] gf = sc.toGameFacings(yaws);
        ForwardPath path = exact.forward(sc, gf);
        double finalViol = compiled.maxViolation(gf, path);
        if (validation != null) {
            double modelViol = validation.evaluate(yaws, gf, path, lpLin).continuousViolation;
            if (SolverTrace.on()) SolverTrace.log("SLP-CHECK", "replayViol=%.12g modelViol=%.12g", finalViol, modelViol);
            finalViol = Math.max(finalViol, modelViol);
        }
        if (DEBUG) System.out.printf("  SLP viol=%.3e obj=%.7f lps=%d (%.1f ms)%n", finalViol,
                spec.objective.evaluate(path), lpCalls, (System.nanoTime() - t0) / 1e6);
        if (SolverTrace.on()) {
            SolverTrace.log("SLP", "end viol=%.3e obj=%.9f lps=%d ms=%.1f %s", finalViol,
                    spec.objective.evaluate(path), lpCalls,
                    (System.nanoTime() - t0) / 1e6, finalViol <= feasTol ? "feasible" : (bestEffort ? "best effort" : "null"));
        }
        if (finalViol <= feasTol) return yaws;
        if (miss != null) miss.offer(yaws, finalViol);
        return bestEffort ? yaws : null;
    }

    private static void offerMiss(ClosestMiss miss, ExactJumpModel exact, JumpPhysicsInputs sc,
                                  JumpConstraintCompiler.Compiled compiled, double[] theta) {
        if (miss == null) return;
        double[] yaws = Angles.wrapAll(theta);
        double[] gf = sc.toGameFacings(yaws);
        miss.offer(yaws, compiled.maxViolation(gf, exact.forward(sc, gf)));
    }

    private static boolean patternEquals(boolean[] a, boolean[] b) {
        if (a == null) return !anySet(b);
        for (int t = 0; t < b.length; t++) if (a[t] != b[t]) return false;
        return true;
    }

    /** Trace-only comparison of the smooth model's gate prediction and the exact replay's actual checks. */
    private static void traceInertiaStep(int lp, int phase, boolean inertiaAware, JumpLinearModel lin,
                                         ExactJumpModel exact, double[] theta, double[] candidate,
                                         ForwardPath currentPath, ForwardPath candidatePath,
                                         boolean[] modelZeroX, boolean[] modelZeroZ, boolean accepted) {
        int n = lin.n;
        boolean[] currentSmoothX = new boolean[n], currentSmoothZ = new boolean[n];
        boolean[] candidateSmoothX = new boolean[n], candidateSmoothZ = new boolean[n];
        boolean[] currentExactX = new boolean[n], currentExactZ = new boolean[n];
        boolean[] candidateExactX = new boolean[n], candidateExactZ = new boolean[n];
        lin.zeroingPattern(theta, exact.inertiaThreshold(), exact.perAxisInertia(), currentSmoothX, currentSmoothZ);
        lin.zeroingPattern(candidate, exact.inertiaThreshold(), exact.perAxisInertia(), candidateSmoothX, candidateSmoothZ);
        exactZeroingPattern(currentPath, n, exact, currentExactX, currentExactZ);
        exactZeroingPattern(candidatePath, n, exact, candidateExactX, candidateExactZ);
        String currentMismatch = smoothExactDifferences(currentSmoothX, currentSmoothZ,
                currentExactX, currentExactZ, currentPath, exact.inertiaThreshold());
        String mismatch = smoothExactDifferences(candidateSmoothX, candidateSmoothZ,
                candidateExactX, candidateExactZ, candidatePath, exact.inertiaThreshold());
        String changed = exactPatternChanges(currentExactX, currentExactZ,
                candidateExactX, candidateExactZ, candidatePath, exact.inertiaThreshold());
        String modeledZeroVelocity = modeledZeroVelocities(modelZeroX, modelZeroZ, currentPath, candidatePath);
        String nearGateVelocity = nearGateVelocities(currentPath, candidatePath, currentExactX, currentExactZ,
                candidateExactX, candidateExactZ, exact.inertiaThreshold());
        String yawDelta = yawDeltas(theta, candidate);
        SolverTrace.log("SLP-GATE",
                "lp#%d phase=%d %s jac=%s smooth=%s->%s replay=%s->%s current-replay-vs-smooth=%s trial-replay-vs-smooth=%s replay-changes=%s jac-zero-vel(current->trial)=%s near-gate-vel(current->trial)=%s yaw-delta=%s decision=%s",
                lp, phase, inertiaAware ? "aware" : "ordinary",
                inertiaAware ? SolverTrace.patternLabel(modelZeroX, modelZeroZ) : "unpatterned",
                SolverTrace.patternLabel(currentSmoothX, currentSmoothZ),
                SolverTrace.patternLabel(candidateSmoothX, candidateSmoothZ),
                SolverTrace.patternLabel(currentExactX, currentExactZ),
                SolverTrace.patternLabel(candidateExactX, candidateExactZ), currentMismatch, mismatch, changed,
                modeledZeroVelocity, nearGateVelocity, yawDelta,
                accepted ? "accept" : "reject");
    }

    /** Exact incoming velocities at checks the local Jacobian currently models as zeroed. */
    private static String modeledZeroVelocities(boolean[] zeroX, boolean[] zeroZ,
                                                ForwardPath current, ForwardPath trial) {
        StringBuilder out = new StringBuilder();
        for (int t = 0; t < zeroX.length; t++) {
            if (zeroX[t]) appendVelocityPair(out, t, 'X', current.velX[t], trial.velX[t]);
            if (zeroZ[t]) appendVelocityPair(out, t, 'Z', current.velZ[t], trial.velZ[t]);
        }
        return out.length() == 0 ? "-" : out.toString();
    }

    private static void appendVelocityPair(StringBuilder out, int tick, char axis,
                                           double current, double trial) {
        if (out.length() > 0) out.append(',');
        out.append(tick).append(axis).append(':')
                .append(String.format(java.util.Locale.ROOT, "%.9g->%.9g", current, trial));
    }

    private static String yawDeltas(double[] current, double[] trial) {
        StringBuilder out = new StringBuilder();
        for (int t = 0; t < current.length; t++) {
            double d = Angles.wrapDelta(trial[t] - current[t]);
            if (Math.abs(d) < 1.0e-9) continue;
            if (out.length() > 0) out.append(',');
            out.append(t).append(':').append(String.format(java.util.Locale.ROOT, "%+.6f", d));
        }
        return out.length() == 0 ? "-" : out.toString();
    }

    /** Exact incoming velocities near a threshold, including checks that are zeroed in either replay. */
    private static String nearGateVelocities(ForwardPath current, ForwardPath trial,
                                             boolean[] currentZeroX, boolean[] currentZeroZ,
                                             boolean[] trialZeroX, boolean[] trialZeroZ,
                                             double threshold) {
        StringBuilder out = new StringBuilder();
        double limit = threshold * 1.25;
        for (int t = 0; t < currentZeroX.length; t++) {
            if (currentZeroX[t] || trialZeroX[t] || Math.abs(current.velX[t]) <= limit || Math.abs(trial.velX[t]) <= limit)
                appendVelocityPair(out, t, 'X', current.velX[t], trial.velX[t]);
            if (currentZeroZ[t] || trialZeroZ[t] || Math.abs(current.velZ[t]) <= limit || Math.abs(trial.velZ[t]) <= limit)
                appendVelocityPair(out, t, 'Z', current.velZ[t], trial.velZ[t]);
        }
        return out.length() == 0 ? "-" : out.toString();
    }

    private static void exactZeroingPattern(ForwardPath path, int n, ExactJumpModel exact,
                                            boolean[] zeroX, boolean[] zeroZ) {
        double threshold = exact.inertiaThreshold();
        for (int t = 0; t < n; t++) {
            if (exact.perAxisInertia()) {
                zeroX[t] = Math.abs(path.velX[t]) < threshold;
                zeroZ[t] = Math.abs(path.velZ[t]) < threshold;
            } else {
                boolean zero = path.velX[t] * path.velX[t] + path.velZ[t] * path.velZ[t] < threshold * threshold;
                zeroX[t] = zero;
                zeroZ[t] = zero;
            }
        }
    }

    private static String smoothExactDifferences(boolean[] smoothX, boolean[] smoothZ,
                                                boolean[] exactX, boolean[] exactZ,
                                                ForwardPath path, double threshold) {
        StringBuilder out = new StringBuilder();
        for (int t = 0; t < smoothX.length; t++) {
            if (smoothX[t] != exactX[t]) appendGateDifference(out, t, 'X', smoothX[t], exactX[t], path.velX[t], threshold);
            if (smoothZ[t] != exactZ[t]) appendGateDifference(out, t, 'Z', smoothZ[t], exactZ[t], path.velZ[t], threshold);
        }
        return out.length() == 0 ? "-" : out.toString();
    }

    private static String exactPatternChanges(boolean[] beforeX, boolean[] beforeZ,
                                              boolean[] afterX, boolean[] afterZ,
                                              ForwardPath after, double threshold) {
        StringBuilder out = new StringBuilder();
        for (int t = 0; t < beforeX.length; t++) {
            if (beforeX[t] != afterX[t]) appendGateDifference(out, t, 'X', beforeX[t], afterX[t], after.velX[t], threshold);
            if (beforeZ[t] != afterZ[t]) appendGateDifference(out, t, 'Z', beforeZ[t], afterZ[t], after.velZ[t], threshold);
        }
        return out.length() == 0 ? "-" : out.toString();
    }

    private static void appendGateDifference(StringBuilder out, int tick, char axis,
                                             boolean before, boolean after, double velocity, double threshold) {
        if (out.length() > 0) out.append(',');
        out.append(tick).append(axis).append(':').append(before ? 'Z' : 'C').append('>').append(after ? 'Z' : 'C')
                .append('@').append(String.format(java.util.Locale.ROOT, "%.9g/%.9g", velocity, threshold));
    }

    private static boolean anySet(boolean[] a) {
        for (boolean v : a) if (v) return true;
        return false;
    }

    /** Signed byte-exact slack per wall into {@code out} (&lt;= 0 = feasible, by that clearance); returns the max. */
    private static double exactSlacks(List<JumpConstraint> ineq, int[] halfSign, double[] gf, ForwardPath path, double[] out) {
        double mx = Double.NEGATIVE_INFINITY;
        for (int j = 0; j < ineq.size(); j++) {
            JumpConstraint c = ineq.get(j);
            double v;
            if (halfSign[j] != 0) {
                v = JumpLinearModel.crossAxisHalfSlack(c, halfSign[j], path);
            } else {
                double e = JumpConstraintCompiler.evaluate(c, gf, path);
                v = c.cmp == JumpConstraint.Cmp.GE ? -e : e;
            }
            out[j] = v;
            if (v > mx) mx = v;
        }
        return mx;
    }

    /** Objective normalized so bigger is always better (MIN is negated), read off the byte-exact path. */
    private static double normObjective(ForwardPath path, Objective obj, boolean max) {
        double v = obj.evaluate(path);
        return max ? v : -v;
    }

    private static String worstViolation(List<JumpConstraint> constraints, int[] halfSign,
                                         double[] facings, ForwardPath path, double[] slacks) {
        int worst = 0;
        for (int j = 1; j < slacks.length; j++) if (slacks[j] > slacks[worst]) worst = j;
        JumpConstraint c = constraints.get(worst);
        String actual = halfSign[worst] != 0
                ? String.format(java.util.Locale.ROOT, "half=%d", halfSign[worst])
                : String.format(java.util.Locale.ROOT, "value=%.9f", JumpConstraintCompiler.evaluate(c, facings, path));
        return c.name + "[t=" + c.t1 + (c.t2 == null ? "" : ",t2=" + c.t2) + ",slack="
                + String.format(java.util.Locale.ROOT, "%.6g", slacks[worst]) + "," + actual + "]";
    }

    private static String tickWallSlacks(List<JumpConstraint> constraints, int[] halfSign,
                                         double[] before, double[] after, int... ticks) {
        StringBuilder out = new StringBuilder();
        for (int j = 0; j < constraints.size(); j++) {
            JumpConstraint c = constraints.get(j);
            if (c.mode != JumpConstraint.Mode.X && c.mode != JumpConstraint.Mode.Z) continue;
            boolean include = false;
            for (int tick : ticks) if (c.t1 == tick) include = true;
            if (!include) continue;
            if (out.length() > 0) out.append(',');
            out.append(c.name).append(':')
                    .append(String.format(java.util.Locale.ROOT, "%.6g->%.6g", before[j], after[j]));
        }
        return out.length() == 0 ? "-" : out.toString();
    }
}
