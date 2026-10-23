package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.core.sim.Vec3dCore;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

/** Receding-horizon solver for long multi-jump spans. Each window optimizes all its constraints and requires full
 *  continuous constraint feasibility. Exact replay acceptance covers only the first few jumps that will be committed. The remaining
 *  lookahead is provisional and is solved again from the committed prefix's exact exit state.
 *
 *  <p>Windows use {@link ClosedFormSolve} with centered margins and fall back to SLP. Smaller windows
 *  are tried when a solve fails; a stuck chain retries with a smaller commit count. A final window
 *  commits its whole remainder and checks every remaining constraint.
 *
 *  <p>The completed route is checked against every original constraint after the same float-delta
 *  reconstruction used by Apply: {@code toGameFacings(wrapAll(gf))}. A prefix success does not guarantee
 *  a feasible continuation; a stuck chain or a failed final replay returns {@code null}.
 *  Uses the authored inputs and start state, never the capture's recorded trajectory. */
public final class LongRunSolver {

    /** Jumps solved together per window (the dual converges to ~10-13; 10 leaves margin). */
    private static final int WINDOW = 10;
    /** Jumps committed before sliding, tried in order; a SMALLER commit = MORE lookahead (WINDOW − commit),
     *  used as a retry when a larger commit gets the chain stuck. The measured seam-coupling horizon on the
     *  desert-hard maps is ~5 jumps, so the first try (commit 3 = lookahead 7) carries margin; the retry
     *  (commit 1 = lookahead 9) is the most lookahead a 10-jump window can give. */
    private static final int[] COMMIT_LADDER = {3, 1};
    /** Window sizes tried, largest first; a smaller window is the fallback when a larger one cannot be solved. */
    private static final int[] WINDOW_LADDER = {WINDOW, 7, 5, 3, 2, 1};
    private static final int[] FALLBACK_RUNGS = {7, 5, 3, 2, 1};

    public static boolean DEBUG = false;

    /** Opt-in endpoint policy for fixed-start, nonterminal windows with a direct objective cap. */
    public enum WindowAdjustment { NONE, FORWARD_JUMP, FORWARD_TICK }

    public static final class LongRunConfig {
        final int[] windowLadder;
        final int[] commitLadder;
        /** Optional probe override for closed-form margins; null preserves the standard ladders. */
        private Double fixedMargin;
        private WindowAdjustment adjustment = WindowAdjustment.NONE;

        public LongRunConfig withWindowAdjustment(WindowAdjustment adjustment) {
            if (adjustment == null) throw new IllegalArgumentException("null window adjustment");
            LongRunConfig copy = new LongRunConfig(windowLadder.clone(), commitLadder.clone());
            copy.fixedMargin = fixedMargin;
            copy.adjustment = adjustment;
            return copy;
        }

        public LongRunConfig withFixedMargin(double margin) {
            if (!Double.isFinite(margin) || margin < 0) throw new IllegalArgumentException("invalid margin");
            LongRunConfig copy = new LongRunConfig(windowLadder.clone(), commitLadder.clone());
            copy.fixedMargin = margin;
            copy.adjustment = adjustment;
            return copy;
        }

        LongRunConfig(int[] windowLadder, int[] commitLadder) {
            this.windowLadder = windowLadder;
            this.commitLadder = commitLadder;
        }

        public static LongRunConfig defaults() {
            return new LongRunConfig(WINDOW_LADDER, COMMIT_LADDER);
        }

        public static LongRunConfig of(int window, int commit) {
            int w = Math.max(2, window);
            int c = Math.max(1, Math.min(commit, w - 1));
            int[] commits = (c == 1) ? new int[]{1} : new int[]{c, 1};
            return new LongRunConfig(windowLadderFor(w), commits);
        }

        public static LongRunConfig of(int window, int commit, int[] windowLadder, int[] commitLadder) {
            LongRunConfig base = of(window, commit);
            int[] wl = sanitizeLadder(windowLadder);
            int[] cl = sanitizeLadder(commitLadder);
            return new LongRunConfig(wl != null ? wl : base.windowLadder, cl != null ? cl : base.commitLadder);
        }

        private static int[] sanitizeLadder(int[] ladder) {
            if (ladder == null || ladder.length == 0) return null;
            int[] out = new int[ladder.length];
            for (int i = 0; i < ladder.length; i++) out[i] = Math.max(1, ladder[i]);
            return out;
        }

        public int window() { return windowLadder[0]; }

        public int commit() { return commitLadder[0]; }
    }

    private static int[] windowLadderFor(int w) {
        List<Integer> out = new ArrayList<>();
        out.add(w);
        for (int r : FALLBACK_RUNGS) if (r < w) out.add(r);
        int[] a = new int[out.size()];
        for (int i = 0; i < a.length; i++) a[i] = out.get(i);
        return a;
    }

    private LongRunSolver() {
    }

    public static final class WindowCache {
        private final Map<String, double[]> map = new HashMap<>();
    }

    private static final double[] WINDOW_MISS = new double[0];

    public static double[] solve(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel) {
        return solve(exact, spec, feasTol, cancel, LongRunConfig.defaults());
    }

    public static final class FreeRun {
        public final double[] gf;
        public final double startX;
        public final double startZ;

        FreeRun(double[] gf, double startX, double startZ) {
            this.gf = gf;
            this.startX = startX;
            this.startZ = startZ;
        }
    }

    public static FreeRun solveFree(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                    LongRunConfig cfg, StartBox freeBox, WindowCache windows, long deadlineNanos) {
        if (freeBox == null || !freeBox.startFree()) return null;
        JumpPhysicsInputs sc = spec.asScenario();
        JumpConstraintCompiler.Compiled compiled = JumpConstraintCompiler.compile(spec);
        int[] bounds = jumpBoundaries(sc);
        int jumps = bounds.length - 1;
        if (jumps < 1) return null;

        double[] chosen = new double[2];
        Map<Integer, Object> retryCache = new HashMap<>();
        for (int commit : cfg.commitLadder) {
            if (cancel != null && cancel.get()) return null;
            if (deadlineNanos > 0L && System.nanoTime() >= deadlineNanos) return null;
            chosen[0] = Double.NaN;
            chosen[1] = Double.NaN;
            double[] gf = runHorizon(exact, sc, spec, bounds, jumps, commit, cfg, cancel,
                    freeBox, chosen, retryCache, windows, deadlineNanos);
            if (gf == null || Double.isNaN(chosen[0])) continue;
            JumpPhysicsInputs at = FreeStartSolve.copyWithStart(sc, chosen[0], chosen[1]);
            double[] replay = at.toGameFacings(Angles.wrapAll(gf));
            double viol = compiled.maxViolation(replay, exact.forward(at, replay));
            if (viol <= feasTol) return new FreeRun(gf, chosen[0], chosen[1]);
        }
        return null;
    }

    public static double[] solve(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                 LongRunConfig cfg) {
        return solve(exact, spec, feasTol, cancel, cfg, new WindowCache());
    }

    public static double[] solve(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                 LongRunConfig cfg, WindowCache windows) {
        return solve(exact, spec, feasTol, cancel, cfg, windows, 0L);
    }

    public static double[] solve(ExactJumpModel exact, JumpSpec spec, double feasTol, AtomicBoolean cancel,
                                 LongRunConfig cfg, WindowCache windows, long deadlineNanos) {
        JumpPhysicsInputs sc = spec.asScenario();
        JumpConstraintCompiler.Compiled compiled = JumpConstraintCompiler.compile(spec);
        int[] bounds = jumpBoundaries(sc);
        int jumps = bounds.length - 1;
        if (jumps < 1) return null;
        if (DEBUG) System.err.printf("LRS receding-horizon: %d jumps, %d ticks%n", jumps, sc.numTicks);

        for (int commit : cfg.commitLadder) {
            if (cancel != null && cancel.get()) return null;
            if (deadlineNanos > 0L && System.nanoTime() >= deadlineNanos) return null;
            double[] gf = runHorizon(exact, sc, spec, bounds, jumps, commit, cfg, cancel, null, null, null,
                    windows, deadlineNanos);
            if (gf == null) continue;
            // Certify the chain Apply will actually realize, not the window-chained facings themselves:
            // the plan stores the wrapped facings and the game re-accumulates float deltas from them,
            // which is not guaranteed bit-identical to the seam-chained gf (a delta can re-round when
            // consecutive facings straddle float scales, e.g. crossing 0 deg). Verifying the replay makes
            // the verified, reported, and applied trajectories one and the same object: the engine and
            // Apply both recompute exactly toGameFacings(wrapAll(gf)).
            double[] replay = sc.toGameFacings(Angles.wrapAll(gf));
            double viol = compiled.maxViolation(replay, exact.forward(sc, replay));
            if (DEBUG) System.err.printf("LRS commit=%d -> full viol=%.6f%n", commit, viol);
            if (viol <= feasTol) return gf;
        }
        return null;
    }

    /** One full receding-horizon sweep committing {@code commitJumps} jumps per window. Returns the chained
     *  game facings, or {@code null} if it gets stuck (no window solvable from some seam). */
    private static final Object FREE_RETRY_MISS = new Object();

    private static double[] runHorizon(ExactJumpModel exact, JumpPhysicsInputs sc, JumpSpec spec, int[] bounds,
                                       int jumps, int commitJumps, LongRunConfig config, AtomicBoolean cancel,
                                       StartBox freeBox, double[] chosenStart, Map<Integer, Object> retryCache,
                                       WindowCache windows, long deadlineNanos) {
        int n = sc.numTicks;
        double[] gf = new double[n];
        Vec3dCore seedPos = sc.startPos, seedVel = sc.initialVelocity;
        float seedYaw = sc.startYaw;

        int i = 0;
        while (i < jumps) {
            if (cancel != null && cancel.get()) return null;
            if (deadlineNanos > 0L && System.nanoTime() >= deadlineNanos) return null;
            boolean advanced = false;
            // Try the largest window that solves; shrink on failure for robustness near the run's end / hard spots.
            int prevWe = -1;
            for (int w : config.windowLadder) {
                int we = Math.min(i + w, jumps);
                if (we == prevWe) continue; // several ladder sizes clamp to the same window near the end: solve it once
                prevWe = we;
                boolean last = (we == jumps);
                int a = bounds[i], c = bounds[we];
                int ce = last ? we : Math.min(i + Math.min(commitJumps, w), jumps);
                int commitTicks = bounds[ce] - a;
                // Keep the nominal commit span. Only the provisional lookahead endpoint moves.
                if (!last && freeBox == null && config.adjustment != WindowAdjustment.NONE) {
                    int nominalEnd = c;
                    int jumpEnd = we + 1 < bounds.length ? bounds[we + 1] : c;
                    c = adjustedEnd(spec, a, c, jumpEnd, sc.numTicks, seedYaw, config.adjustment);
                    if (SolverTrace.on()) SolverTrace.log("LRS-ADJUST",
                            "policy=%s commit=%d start=%d nominalEnd=%d selectedEnd=%d replayTicks=%d",
                            config.adjustment, commitJumps, a, nominalEnd, c, commitTicks);
                }
                JumpPhysicsInputs win = sliceScenario(sc, a, c, seedPos, seedVel, seedYaw);
                List<JumpConstraint> cons = sliceConstraints(spec, a, c, seedYaw);
                if (SolverTrace.on()) {
                    SolverTrace.log("LRS-WIN", "commit=%d jumps=[%d,%d) ticks=[%d,%d) seed=(%.12f,%.12f;%.12f,%.12f) yaw=%.9f constraints=%d replayTicks=%d",
                            commitJumps, i, we, a, c, seedPos.x, seedPos.z, seedVel.x, seedVel.z, seedYaw, cons.size(), commitTicks);
                    if (c - a <= 10) for (JumpConstraint jc : cons) {
                        SolverTrace.log("LRS-CONS", "%s %s t=%d%s %s rhs=%.12f",
                                jc.name, jc.mode, jc.t1, jc.t2 == null ? "" : "/" + jc.t2,
                                jc.cmp, jc.rhs);
                    }
                }
                Objective obj = last
                        ? new Objective(spec.objective.axis, spec.objective.sense, c - a)   // last window: real objective
                        : new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, c - a); // lead-in: any feasible
                String winKey = windowKey(a, c, last, commitTicks, seedPos, seedVel, seedYaw) + ":margin=" + config.fixedMargin;
                double[] cachedWin = windows.map.get(winKey);
                double[] yaws;
                if (cachedWin != null) {
                    yaws = cachedWin == WINDOW_MISS ? null : cachedWin.clone();
                    if (SolverTrace.on()) {
                        SolverTrace.log("LRS", "commit=%d window i=%d we=%d cached -> %s",
                                commitJumps, i, we, yaws != null ? "solved" : "miss");
                    }
                } else {
                    long winT0 = SolverTrace.on() ? System.nanoTime() : 0L;
                    yaws = solveWindow(exact, win, cons, obj, last, commitTicks, cancel, deadlineNanos, config.fixedMargin);
                    if (cancel == null || !cancel.get()) {
                        windows.map.put(winKey, yaws == null ? WINDOW_MISS : yaws.clone());
                    }
                    if (SolverTrace.on()) {
                        SolverTrace.log("LRS", "commit=%d window i=%d we=%d ticks=%d cons=%d last=%s -> %s ms=%.1f",
                                commitJumps, i, we, c - a, cons.size(), last, yaws != null ? "solved" : "miss",
                                (System.nanoTime() - winT0) / 1.0e6);
                    }
                }
                if (i == 0 && freeBox != null) {
                    Object cached = retryCache.get(we);
                    FreeStartSolve.Result fr;
                    if (cached != null) {
                        fr = cached == FREE_RETRY_MISS ? null : (FreeStartSolve.Result) cached;
                    } else {
                        JumpPhysicsInputs freeSc = FreeStartSolve.copyWithStart(sc, seedPos.x, seedPos.z);
                        freeSc.startBox = new StartBox(seedPos.x, seedPos.z, seedVel.x, seedVel.z,
                                freeBox.pxLo, freeBox.pxHi, freeBox.pzLo, freeBox.pzHi,
                                seedVel.x, seedVel.x, seedVel.z, seedVel.z);
                        List<JumpConstraint> upTo = new ArrayList<>();
                        int hiTick = c;
                        for (JumpConstraint jc : spec.constraints) {
                            int lim = jc.mode == JumpConstraint.Mode.F ? hiTick - 1 : hiTick;
                            if (jc.t1 <= lim && (jc.t2 == null || jc.t2 <= lim)) upTo.add(jc);
                        }
                        Objective freeObj = last ? spec.objective
                                : new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, c);
                        fr = FreeStartSolve.solveJoint(exact, new JumpSpec(freeSc, upTo, freeObj), 0.0, cancel,
                                deadlineNanos);
                        retryCache.put(we, fr == null ? FREE_RETRY_MISS : fr);
                        if (SolverTrace.on()) {
                            SolverTrace.log("FREE", "window free retry we=%d cons=%d -> %s",
                                    we, upTo.size(), fr != null && fr.feasible ? "solved" : "miss");
                        }
                    }
                    if (fr != null && fr.feasible) {
                        chosenStart[0] = fr.startX;
                        chosenStart[1] = fr.startZ;
                        win = FreeStartSolve.copyWithStart(sc, fr.startX, fr.startZ);
                        yaws = Angles.wrapAll(fr.yaws);
                    }
                }
                if (yaws == null) continue;

                // Commit the first commitJumps jumps (all of them for the final window), chaining the exact exit.
                double[] wgf = win.toGameFacings(yaws);
                ForwardPath wp = exact.forward(win, wgf);
                if (SolverTrace.on()) for (int k = 0; k <= commitTicks; k++) {
                    int tick = a + k;
                    SolverTrace.log("LRS-COMMIT-PATH", "jump=%d tick=%d x=%.12f z=%.12f vx=%.12f vz=%.12f yaw=%s",
                            i, tick, wp.posX[k], wp.posZ[k], wp.velX[k], wp.velZ[k],
                            k < wgf.length ? SolverTrace.fmt("%.9f", wgf[k]) : "-");
                }
                System.arraycopy(wgf, 0, gf, a, commitTicks);
                seedPos = new Vec3dCore(wp.posX[commitTicks], wp.posY[commitTicks], wp.posZ[commitTicks]);
                seedVel = new Vec3dCore(wp.velX[commitTicks], wp.velY[commitTicks], wp.velZ[commitTicks]);
                seedYaw = (float) wgf[commitTicks - 1];
                i = ce;
                advanced = true;
                break;
            }
            if (!advanced) {
                if (SolverTrace.on()) SolverTrace.log("LRS-STUCK", "commit=%d nextJump=%d tick=%d",
                        commitJumps, i + 1, bounds[i]);
                if (DEBUG) System.err.printf("  commit=%d stuck at jump %d (tick %d)%n", commitJumps, i, bounds[i]);
                return null;
            }
        }
        return gf;
    }

    /** Extend only when the nominal +Z objective is directly capped. Never move the final goal
     *  or consume the endpoint reserved for a terminal solve. Try the other forward size if needed. */
    private static int adjustedEnd(JumpSpec spec, int start, int end, int jumpEnd, int routeEnd,
                                   float yaw, WindowAdjustment policy) {
        if (policy == WindowAdjustment.NONE || !hasDirectZCap(spec, start, end, yaw)) return end;
        int[] choices = policy == WindowAdjustment.FORWARD_JUMP
                ? new int[]{jumpEnd, end + 1} : new int[]{end + 1, jumpEnd};
        for (int candidate : choices) {
            if (candidate > end && candidate < routeEnd && !hasDirectZCap(spec, start, candidate, yaw))
                return candidate;
        }
        return end;
    }

    private static boolean hasDirectZCap(JumpSpec spec, int start, int end, float yaw) {
        Objective objective = new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, end - start);
        return ObjectiveBounds.directCap(objective, sliceConstraints(spec, start, end, yaw)) != null;
    }

    private static String windowKey(int a, int c, boolean last, int commitTicks, Vec3dCore pos, Vec3dCore vel, float yaw) {
        return a + ":" + c + ":" + last + ":" + commitTicks
                + ":" + Double.doubleToLongBits(pos.x) + ":" + Double.doubleToLongBits(pos.y)
                + ":" + Double.doubleToLongBits(pos.z)
                + ":" + Double.doubleToLongBits(vel.x) + ":" + Double.doubleToLongBits(vel.y)
                + ":" + Double.doubleToLongBits(vel.z)
                + ":" + Float.floatToIntBits(yaw);
    }

    private static double[] solveWindow(ExactJumpModel exact, JumpPhysicsInputs win, List<JumpConstraint> cons,
                                        Objective first, boolean last, int commitTicks, AtomicBoolean cancel, long deadlineNanos) {
        return solveWindow(exact, win, cons, first, last, commitTicks, cancel, deadlineNanos, null);
    }

    private static double[] solveWindow(ExactJumpModel exact, JumpPhysicsInputs win, List<JumpConstraint> cons,
                                        Objective first, boolean last, int commitTicks, AtomicBoolean cancel,
                                        long deadlineNanos, Double fixedMargin) {
        traceObjectiveBound(last ? "terminal" : "primary", first, cons);
        ClosedFormSolve.Config closed = new ClosedFormSolve.Config();
        if (fixedMargin != null) {
            closed.margins = new double[]{fixedMargin};
            closed.marginsRobust = new double[]{fixedMargin};
        }
        if (last) {
            AtomicBoolean token = cancel != null ? cancel : new AtomicBoolean(false);
            long recoveryT0 = SolverTrace.on() ? System.nanoTime() : 0L;
            if (SolverTrace.on()) SolverTrace.log("LRS-OBJ", "begin terminal recovery %s/%s@%d",
                    first.axis, first.sense, first.tick);
            double[] primary = RecoveryLadder.solve(exact, new JumpSpec(win, cons, first), win, token, new String[1],
                    deadlineNanos, new SlpSolve.Config(), closed, new RelaxationRecovery.Config(),
                    null);
            double[] result = hugBestObjective(exact, win, cons, first, primary, token, deadlineNanos, closed);
            if (SolverTrace.on()) SolverTrace.log("LRS-OBJ", "end terminal recovery result=%s ms=%.3f",
                    result != null ? "solved" : "miss", (System.nanoTime() - recoveryT0) / 1.0e6);
            return result;
        }
        closed.replayTickLimit = commitTicks;
        long objectiveT0 = SolverTrace.on() ? System.nanoTime() : 0L;
        if (SolverTrace.on()) SolverTrace.log("LRS-OBJ", "begin primary %s/%s@%d", first.axis, first.sense, first.tick);
        double[] y = ClosedFormSolve.optimizeRobust(exact, new JumpSpec(win, cons, first), 0.0, cancel, closed);
        if (SolverTrace.on()) SolverTrace.log("LRS-OBJ", "end primary result=%s ms=%.3f",
                y != null ? "solved" : "miss", (System.nanoTime() - objectiveT0) / 1.0e6);
        if (y != null) return y;
        for (Objective alt : alternates(first, win.numTicks)) {
            if (cancel != null && cancel.get()) return null;
            objectiveT0 = SolverTrace.on() ? System.nanoTime() : 0L;
            if (SolverTrace.on()) SolverTrace.log("LRS-OBJ", "begin alternate %s/%s@%d", alt.axis, alt.sense, alt.tick);
            traceObjectiveBound("alternate", alt, cons);
            y = ClosedFormSolve.optimizeRobust(exact, new JumpSpec(win, cons, alt), 0.0, cancel, closed);
            if (SolverTrace.on()) SolverTrace.log("LRS-OBJ", "end alternate %s/%s@%d result=%s ms=%.3f",
                    alt.axis, alt.sense, alt.tick, y != null ? "solved" : "miss",
                    (System.nanoTime() - objectiveT0) / 1.0e6);
            if (y != null) return y;
        }
        if (cancel != null && cancel.get()) return null;
        objectiveT0 = SolverTrace.on() ? System.nanoTime() : 0L;
        if (SolverTrace.on()) SolverTrace.log("LRS-OBJ", "begin fallback SLP %s/%s@%d",
                first.axis, first.sense, first.tick);
        SlpSolve.Config slp = new SlpSolve.Config();
        slp.replayTickLimit = commitTicks;
        y = SlpSolve.optimizeCentered(exact, new JumpSpec(win, cons, first), 0.0, cancel, null, slp);
        if (SolverTrace.on()) SolverTrace.log("LRS-OBJ", "end fallback SLP result=%s ms=%.3f",
                y != null ? "solved" : "miss", (System.nanoTime() - objectiveT0) / 1.0e6);
        return y;
    }

    private static void traceObjectiveBound(String stage, Objective objective, List<JumpConstraint> constraints) {
        if (!SolverTrace.on()) return;
        JumpConstraint cap = ObjectiveBounds.directCap(objective, constraints);
        if (cap == null) {
            SolverTrace.log("LRS-PREFLIGHT", "%s %s/%s@%d directCap=false", stage,
                    objective.axis, objective.sense, objective.tick);
        } else {
            SolverTrace.log("LRS-PREFLIGHT", "%s %s/%s@%d directCap=true constraint=%s %s rhs=%.12f", stage,
                    objective.axis, objective.sense, objective.tick, cap.name, cap.cmp, cap.rhs);
        }
    }

    private static double[] hugBestObjective(ExactJumpModel exact, JumpPhysicsInputs win, List<JumpConstraint> cons,
                                             Objective first, double[] primary, AtomicBoolean cancel, long deadlineNanos,
                                             ClosedFormSolve.Config closed) {
        JumpSpec spec = new JumpSpec(win, cons, first);
        JumpConstraintCompiler.Compiled compiled = JumpConstraintCompiler.compile(spec);
        boolean max = first.sense == Objective.Sense.MAX;
        double[] best = primary;
        double bestObj = feasibleObjective(exact, win, compiled, first, primary);
        for (Objective alt : alternates(first, win.numTicks)) {
            if (cancel != null && cancel.get()) break;
            if (deadlineNanos > 0L && System.nanoTime() >= deadlineNanos) break;
            double[] seed = ClosedFormSolve.optimize(exact, new JumpSpec(win, cons, alt), 0.0, cancel, closed);
            if (seed == null) continue;
            double[] hugged = hugObjective(exact, win, cons, first, seed, cancel);
            double obj = feasibleObjective(exact, win, compiled, first, hugged);
            if (Double.isNaN(obj)) continue;
            if (Double.isNaN(bestObj) || (max ? obj > bestObj : obj < bestObj)) {
                best = hugged;
                bestObj = obj;
            }
        }
        return best;
    }

    private static double[] hugObjective(ExactJumpModel exact, JumpPhysicsInputs win, List<JumpConstraint> cons,
                                         Objective first, double[] feasible, AtomicBoolean cancel) {
        JumpSpec spec = new JumpSpec(win, cons, first);
        double[] hugged = SlpSolve.optimize(exact, spec, 0.0, cancel, feasible);
        double[] base = hugged != null ? hugged : feasible;
        double[] laddered = LevelSetAscent.improve(exact, spec, base, 0.0, cancel);
        return laddered != null ? laddered : base;
    }

    private static double feasibleObjective(ExactJumpModel exact, JumpPhysicsInputs win,
                                            JumpConstraintCompiler.Compiled compiled, Objective first, double[] yaws) {
        if (yaws == null) return Double.NaN;
        double[] gf = win.toGameFacings(yaws);
        ForwardPath path = exact.forward(win, gf);
        if (compiled.maxViolation(gf, path) > 0.0) return Double.NaN;
        return path.getPos(first.tick, first.axis);
    }

    private static List<Objective> alternates(Objective first, int len) {
        List<Objective> out = new ArrayList<>();
        for (JumpPhysicsInputs.Axis ax : new JumpPhysicsInputs.Axis[]{JumpPhysicsInputs.Axis.Z, JumpPhysicsInputs.Axis.X}) {
            for (Objective.Sense se : Objective.Sense.values()) {
                if (ax == first.axis && se == first.sense) continue;
                out.add(new Objective(ax, se, len));
            }
        }
        return out;
    }

    /** Jump launch boundaries: the grounded ticks that begin an airborne arc, plus both endpoints, with
     *  sub-2-tick pieces merged. Ground/air is the input-specified per-tick slip annotation (NaN = air). */
    private static int[] jumpBoundaries(JumpPhysicsInputs sc) {
        int n = sc.numTicks;
        List<Integer> bl = new ArrayList<>();
        bl.add(0);
        for (int t = 1; t < n; t++) {
            boolean g = !Double.isNaN(sc.slipAt(t)), gp = !Double.isNaN(sc.slipAt(t - 1));
            if (g && !gp) bl.add(t);
        }
        if (bl.get(bl.size() - 1) != n) bl.add(n);
        List<Integer> m = new ArrayList<>();
        m.add(bl.get(0));
        for (int k = 1; k < bl.size(); k++) {
            if (bl.get(k) - m.get(m.size() - 1) < 2 && k < bl.size() - 1) continue;
            m.add(bl.get(k));
        }
        int[] o = new int[m.size()];
        for (int k = 0; k < o.length; k++) o[k] = m.get(k);
        return o;
    }

    public static JumpSpec suffixSpec(JumpSpec full, int from, Vec3dCore pos, Vec3dCore vel, float yaw) {
        JumpPhysicsInputs sc = full.asScenario();
        JumpPhysicsInputs win = sliceScenario(sc, from, sc.numTicks, pos, vel, yaw);
        win.incomingSprint = from == 0 ? sc.incomingSprint : sc.sprintAt(from - 1);
        win.incomingAmp = from == 0 ? sc.incomingAmp : sc.speedAmplifierAt(from - 1);
        List<JumpConstraint> cons = sliceConstraints(full, from, sc.numTicks, yaw);
        Objective obj = new Objective(full.objective.axis, full.objective.sense, full.objective.tick - from);
        return new JumpSpec(win, cons, obj);
    }

    /** A window's physics inputs: the masks for ticks [a, c), seeded with the chained exit state. */
    private static JumpPhysicsInputs sliceScenario(JumpPhysicsInputs sc, int a, int c,
                                                   Vec3dCore pos, Vec3dCore vel, float yaw) {
        int len = c - a;
        JumpPhysicsInputs p = new JumpPhysicsInputs(len);
        p.startPos = pos;
        p.initialVelocity = vel;
        p.startYaw = yaw;
        p.strafeSign = sc.strafeSign;
        p.jumpPerTick = sliceBool(sc.jumpPerTick, a, len);
        p.strafePerTick = sliceBool(sc.strafePerTick, a, len);
        p.yawLockedPerTick = sliceBool(sc.yawLockedPerTick, a, len);
        p.speedAmplifier = sliceInt(sc.speedAmplifier, a, len);
        p.slipPerTick = sliceDouble(sc.slipPerTick, a, len);
        p.surfacePerTick = sliceKind(sc.surfacePerTick, a, len);
        p.soulsandCellsPerTick = sliceInt(sc.soulsandCellsPerTick, a, len);
        p.sneakPerTick = sliceBool(sc.sneakPerTick, a, len);
        // null arrays stay null so the slice keeps the source's legacy fallbacks (always-sprint, W held).
        p.sprintPerTick = sliceBool(sc.sprintPerTick, a, len);
        p.liveAirSprintFactor = sc.liveAirSprintFactor;
        p.forwardInputPerTick = sliceFloat(sc.forwardInputPerTick, a, len, 1.0F * 0.98F);
        p.strafeInputPerTick = sliceFloat(sc.strafeInputPerTick, a, len, 0.0F);
        return p;
    }

    /** The full spec's constraints that fall entirely within [a, c], remapped to window-local ticks. */
    private static List<JumpConstraint> sliceConstraints(JumpSpec full, int a, int c, float seedYaw) {
        List<JumpConstraint> out = new ArrayList<>();
        for (JumpConstraint jc : full.constraints) {
            int hi = jc.mode == JumpConstraint.Mode.F ? c - 1 : c;
            boolean in1 = jc.t1 >= a && jc.t1 <= hi;
            boolean in2 = jc.t2 == null || (jc.t2 >= a && jc.t2 <= hi);
            if (a > 0 && jc.mode == JumpConstraint.Mode.F && jc.op == JumpConstraint.Op.MINUS && jc.t2 != null
                    && jc.t1 == a && jc.t2 == a - 1) {
                out.add(new JumpConstraint(JumpConstraint.Mode.F, 0, null, JumpConstraint.Op.PLUS, jc.cmp,
                        Angles.wrap(seedYaw + jc.rhs), jc.name));
                continue;
            }
            if (in1 && in2) {
                Integer t2 = jc.t2 == null ? null : (jc.t2 - a);
                out.add(new JumpConstraint(jc.mode, jc.t1 - a, t2, jc.op, jc.cmp, jc.rhs, jc.name));
            }
        }
        return out;
    }

    private static boolean[] sliceBool(boolean[] x, int from, int len) {
        if (x == null) return null;
        boolean[] o = new boolean[len];
        for (int i = 0; i < len; i++) o[i] = from + i < x.length && x[from + i];
        return o;
    }

    private static int[] sliceInt(int[] x, int from, int len) {
        if (x == null) return null;
        int[] o = new int[len];
        for (int i = 0; i < len; i++) o[i] = from + i < x.length ? x[from + i] : 0;
        return o;
    }

    private static double[] sliceDouble(double[] x, int from, int len) {
        if (x == null) return null;
        double[] o = new double[len];
        for (int i = 0; i < len; i++) o[i] = from + i < x.length ? x[from + i] : Double.NaN;
        return o;
    }

    private static SurfaceKind[] sliceKind(SurfaceKind[] x, int from, int len) {
        if (x == null) return null;
        SurfaceKind[] o = new SurfaceKind[len];
        for (int i = 0; i < len; i++) o[i] = from + i < x.length ? x[from + i] : SurfaceKind.NORMAL;
        return o;
    }

    private static float[] sliceFloat(float[] x, int from, int len, float dflt) {
        if (x == null) return null;
        float[] o = new float[len];
        for (int i = 0; i < len; i++) o[i] = from + i < x.length ? x[from + i] : dflt;
        return o;
    }
}
