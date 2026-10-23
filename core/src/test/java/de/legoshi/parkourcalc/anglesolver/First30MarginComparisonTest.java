package de.legoshi.parkourcalc.anglesolver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.anglesolver.solver.*;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Assume;
import org.junit.Test;
import org.junit.experimental.categories.Category;

import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.Assert.*;

/** Opt-in investigation: replay both margin policies, then cross them with identical window entry states.
 * Uses the production slicing and closed-form implementation through reflection so production defaults stay intact.
 * Run with -Dpkc.marginProbe=true -PslowTests --tests '*First30MarginComparisonTest'. */
@Category(SlowSolverTests.class)
public class First30MarginComparisonTest {
    private static final AtomicBoolean CANCEL = new AtomicBoolean(false);
    private static final double[] NORMAL = new ClosedFormSolve.Config().marginsRobust;
    private static final double[] FIXED = {0.0003};
    private final List<String> attempts = new ArrayList<>();
    private final List<String> states = new ArrayList<>();
    private ExactJumpModel exact;
    private JumpSpec full;
    private int[] bounds;
    private String label;

    private static Method method(Class<?> cls, String name, Class<?>... types) throws Exception {
        Method m = cls.getDeclaredMethod(name, types);
        m.setAccessible(true);
        return m;
    }

    private static final class Seed {
        final Vec3dCore pos, vel;
        final float yaw;
        Seed(Vec3dCore pos, Vec3dCore vel, float yaw) { this.pos = pos; this.vel = vel; this.yaw = yaw; }
    }

    private static final class Run {
        final List<Seed> entries = new ArrayList<>();
        boolean solved;
    }

    @Test
    public void compareMarginsAtIdenticalEntries() throws Exception {
        Assume.assumeTrue("opt in with -Dpkc.marginProbe=true", Boolean.getBoolean("pkc.marginProbe"));
        loadCapture();
        attempts.add("run,start_index,end_index,start_tick,end_tick,objective,result,violation,worst,objective_value");
        states.add("run,start_index,tick,x,z,vx,vz,incoming_committed_yaw");
        comparePreparedCapture();
    }

    private void loadCapture() throws Exception {
        String capture = System.getenv("PKC_TRACE_INPUT");
        if (capture == null) capture = "/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json";
        SaveFile file = SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get(capture)), StandardCharsets.UTF_8));
        assertNotNull(file);
        InputData inputs = new InputData();
        SaveIO.applyRowsTo(file, inputs);
        AngleSolverState state = new AngleSolverState();
        SaveIO.applyAngleSolverTo(file, state);
        state.setStartTick(0);
        state.setLandingTick(250);
        exact = ExactJumpModel.forMcVersion(file.mcVersion);
        full = new AngleSolverEngine(state, Fixtures.buildBoxes(file), inputs, t -> { }, exact).debugBuildSpec();
        assertEquals(250, full.asScenario().numTicks);
        assertEquals(229, full.constraints.size());
        bounds = (int[]) method(LongRunSolver.class, "jumpBoundaries", JumpPhysicsInputs.class)
                .invoke(null, full.asScenario());
        assertEquals(31, bounds.length);
    }

    private void comparePreparedCapture() throws Exception {
        Run normal = run("normal-full", NORMAL);
        Run fixed = run("fixed-full", FIXED);
        assertTrue("normal policy completes", normal.solved);
        assertFalse("fixed policy stalls", fixed.solved);
        assertEquals("fixed entry states through stuck index 13", 14, fixed.entries.size());
        // Historical logs round state components to 12 decimals; these checks validate reproduction before controls.
        assertEquals(732.813498656565, normal.entries.get(11).pos.x, 1e-11);
        assertEquals(-1088.466968184592, normal.entries.get(11).pos.z, 1e-11);
        assertEquals(732.819704258099, fixed.entries.get(11).pos.x, 1e-11);
        assertEquals(-1088.639926044153, fixed.entries.get(11).pos.z, 1e-11);
        for (int i : new int[]{0, 11, 12}) {
            for (boolean fixedEntry : new boolean[]{false, true}) {
                Seed entry = (fixedEntry ? fixed : normal).entries.get(i);
                int end = i == 0 ? 7 : i + 5;
                for (boolean fixedPolicy : new boolean[]{false, true}) {
                    label = (fixedEntry ? "fixed-entry" : "normal-entry") + "-i" + i
                            + (fixedPolicy ? "-fixed-policy" : "-normal-policy");
                    SolverTrace.enable("margin-control-" + label);
                    try {
                        double[] result = window(entry, i, end, fixedPolicy ? FIXED : NORMAL);
                        boolean expected = i == 0 || (!fixedEntry && (i == 12 || !fixedPolicy));
                        assertEquals("controlled window " + label, expected, result != null);
                    }
                    finally { SolverTrace.disable(); }
                }
            }
        }
        for (boolean fixedEntry : new boolean[]{false, true}) {
            Seed entry = (fixedEntry ? fixed : normal).entries.get(11);
            label = (fixedEntry ? "fixed-entry" : "normal-entry") + "-i11-rungs";
            SolverTrace.enable("margin-control-" + label);
            try { diagnoseRungs(entry); }
            finally { SolverTrace.disable(); }
        }
        Path reports = Paths.get("build/reports");
        Files.write(reports.resolve("margin-comparison-attempts.csv"), attempts, StandardCharsets.UTF_8);
        Files.write(reports.resolve("margin-comparison-states.csv"), states, StandardCharsets.UTF_8);
    }

    private static final class AuditCandidate {
        final int pass;
        final boolean[] zeroX, zeroZ;
        final ClosedFormSolve.Result result;
        AuditCandidate(int pass, boolean[] zeroX, boolean[] zeroZ, ClosedFormSolve.Result result) {
            this.pass = pass; this.zeroX = zeroX; this.zeroZ = zeroZ; this.result = result;
        }
    }

    /** Only the initial seven-jump window: every fixed-margin pass, every original position constraint. */
    @Test
    @SuppressWarnings("unchecked")
    public void initialSevenConstraintAudit() throws Exception {
        Assume.assumeTrue("opt in with -Dpkc.marginProbe=true", Boolean.getBoolean("pkc.marginProbe"));
        loadCapture();
        JumpPhysicsInputs source = full.asScenario();
        Seed initial = new Seed(source.startPos, source.initialVelocity, source.startYaw);
        JumpPhysicsInputs sc = slice(initial, 0, 68);
        assertEquals(68, bounds[7]);
        List<JumpConstraint> cons = (List<JumpConstraint>) method(LongRunSolver.class, "sliceConstraints",
                JumpSpec.class, int.class, int.class, float.class).invoke(null, full, 0, 68, initial.yaw);
        assertEquals(33, cons.size());
        assertEquals(0, JumpLinearModel.crossAxisCount(cons));
        FacingPrefold pre = FacingPrefold.analyze(cons, new JumpLinearModel(sc));
        assertNotNull(pre);
        assertTrue(pre.isIdentity());
        List<String> rows = new ArrayList<>();
        rows.add("objective,margin,pass,pattern,best_for_objective,constraint,axis,tick,comparison,wall,model_value,model_clearance,model_violation,model_tightened_violation,replay_value,replay_clearance,replay_violation,replay_minus_model");
        SolverTrace.enable("initial-seven-constraint-audit");
        try {
            for (int k = 0; k < 5; k++) {
                boolean normalControl = k == 4;
                Objective obj = new Objective(k < 2 || normalControl ? JumpPhysicsInputs.Axis.Z : JumpPhysicsInputs.Axis.X,
                        k % 2 == 0 ? Objective.Sense.MAX : Objective.Sense.MIN, 68);
                JumpSpec spec = new JumpSpec(sc, cons, obj);
                double margin = normalControl ? 0.05 : 0.0003;
                ClosedFormSolve.Config cfg = new ClosedFormSolve.Config();
                JumpConstraintCompiler.Compiled compiled = JumpConstraintCompiler.compile(spec);
                List<AuditCandidate> candidates = new ArrayList<>();
                boolean[] zeroX = null, zeroZ = null;
                AuditCandidate best = null;
                for (int pass = 0; pass < cfg.maxInertiaPasses; pass++) {
                    SolverTrace.log("AUDIT-PASS", "objective=%s/%s margin=%.5g pass=%d pattern=%s", obj.axis, obj.sense,
                            margin, pass, SolverTrace.patternLabel(zeroX, zeroZ));
                    ClosedFormSolve.Result r = (ClosedFormSolve.Result) method(ClosedFormSolve.class, "runLadder",
                            ExactJumpModel.class, JumpSpec.class, JumpPhysicsInputs.class, JumpConstraintCompiler.Compiled.class,
                            double.class, AtomicBoolean.class, double[].class, boolean.class, boolean[].class,
                            boolean[].class, long.class, ClosedFormSolve.Config.class, FacingPrefold.class, int[].class)
                            .invoke(null, exact, spec, sc, compiled, 0.0, CANCEL, new double[]{margin}, false, zeroX,
                                    zeroZ, System.nanoTime(), cfg, pre, new int[0]);
                    assertNotNull(r);
                    AuditCandidate candidate = new AuditCandidate(pass, zeroX, zeroZ, r);
                    candidates.add(candidate);
                    boolean improved = best == null || r.violation < best.result.violation;
                    if (improved) best = candidate;
                    if (r.feasible || (pass > 0 && !improved)) break;
                    boolean[] nx = new boolean[68], nz = new boolean[68];
                    new JumpLinearModel(sc).zeroingPattern(r.yaws, exact.inertiaThreshold(), exact.perAxisInertia(), nx, nz);
                    boolean effective = (Boolean) method(ClosedFormSolve.class, "patternEffective",
                            JumpPhysicsInputs.class, boolean[].class, boolean[].class).invoke(null, sc, nx, nz);
                    if (!effective || (java.util.Arrays.equals(zeroX, nx) && java.util.Arrays.equals(zeroZ, nz))) break;
                    zeroX = nx; zeroZ = nz;
                }
                assertNotNull(best);
                ClosedFormSolve.Result reference = (ClosedFormSolve.Result) method(ClosedFormSolve.class, "optimizeReturning",
                        ExactJumpModel.class, JumpSpec.class, double.class, AtomicBoolean.class, double[].class,
                        boolean.class, ClosedFormSolve.Config.class).invoke(null, exact, spec, 0.0, CANCEL,
                        new double[]{margin}, false, cfg);
                assertNotNull(reference);
                assertArrayEquals("audit matches production candidate", reference.yaws, best.result.yaws, 0.0);
                assertEquals(normalControl, best.result.feasible);
                for (AuditCandidate candidate : candidates) {
                    auditConstraints(sc, spec, candidate, candidate == best, margin, rows);
                }
            }
        } finally { SolverTrace.disable(); }
        Files.write(Paths.get("build/reports/initial-seven-constraints.csv"), rows, StandardCharsets.UTF_8);
    }

    private void auditConstraints(JumpPhysicsInputs sc, JumpSpec spec, AuditCandidate candidate, boolean best,
                                  double margin, List<String> rows) {
        JumpLinearModel lin = new JumpLinearModel(sc, candidate.zeroX, candidate.zeroZ);
        double[] yaws = candidate.result.yaws;
        double[] game = sc.toGameFacings(yaws);
        ForwardPath replay = exact.forward(sc, game);
        double[][] position = new double[2][sc.numTicks + 1];
        for (int axis = 0; axis < 2; axis++) for (int tick = 0; tick <= sc.numTicks; tick++) {
            double value = lin.constPos(tick, axis);
            for (int t = 0; t < tick; t++) {
                double phi = lin.baseArg(t) + Angles.rad(yaws[t]);
                value += lin.coefAxis(axis, t, tick) * lin.mMag(t)
                        * (axis == 0 ? StrictMath.cos(phi) : StrictMath.sin(phi));
            }
            position[axis][tick] = value;
        }
        for (JumpConstraint c : spec.constraints) {
            assertTrue(c.mode == JumpConstraint.Mode.X || c.mode == JumpConstraint.Mode.Z);
            int axis = c.mode == JumpConstraint.Mode.X ? 0 : 1;
            double modelValue = position[axis][c.t1];
            if (c.t2 != null) modelValue += (c.op == JumpConstraint.Op.PLUS ? 1 : -1) * position[axis][c.t2];
            double replayValue = JumpConstraintCompiler.evaluate(c, game, replay) + c.rhs;
            double modelClearance = clearance(c, modelValue);
            double replayClearance = clearance(c, replayValue);
            double tightened = c.cmp == JumpConstraint.Cmp.EQ ? -modelClearance : margin - modelClearance;
            rows.add(String.format(Locale.ROOT,
                    "%s/%s,%.9g,%d,\"%s\",%s,%s,%s,%d,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",
                    spec.objective.axis, spec.objective.sense, margin, candidate.pass,
                    SolverTrace.patternLabel(candidate.zeroX, candidate.zeroZ), best, c.name, c.mode, c.t1, c.cmp,
                    c.rhs, modelValue, modelClearance, Math.max(0, -modelClearance), Math.max(0, tightened),
                    replayValue, replayClearance, Math.max(0, -replayClearance), replayValue - modelValue));
        }
    }

    private static double clearance(JumpConstraint c, double value) {
        if (c.cmp == JumpConstraint.Cmp.EQ) return -StrictMath.abs(value - c.rhs);
        return c.cmp == JumpConstraint.Cmp.LE ? c.rhs - value : value - c.rhs;
    }

    private Run run(String name, double[] margins) throws Exception {
        label = name;
        SolverTrace.enable("margin-control-" + name);
        Run run = new Run();
        JumpPhysicsInputs sc = full.asScenario();
        Seed entry = new Seed(sc.startPos, sc.initialVelocity, sc.startYaw);
        int jumps = bounds.length - 1;
        double[] chained = new double[sc.numTicks];
        try {
            int i = 0;
            while (i < jumps) {
                run.entries.add(entry);
                states.add(String.format(Locale.ROOT, "%s,%d,%d,%.17g,%.17g,%.17g,%.17g,%.9f",
                        label, i, bounds[i], entry.pos.x, entry.pos.z, entry.vel.x, entry.vel.z, entry.yaw));
                boolean advanced = false;
                for (int w : new int[]{7, 5, 3, 2, 1}) {
                    int end = Math.min(i + w, jumps);
                    double[] yaws = window(entry, i, end, margins);
                    if (yaws == null) continue;
                    JumpPhysicsInputs win = slice(entry, bounds[i], bounds[end]);
                    double[] game = win.toGameFacings(yaws);
                    ForwardPath path = exact.forward(win, game);
                    int next = end == jumps ? end : i + 1;
                    int ticks = bounds[next] - bounds[i];
                    System.arraycopy(game, 0, chained, bounds[i], ticks);
                    entry = new Seed(new Vec3dCore(path.posX[ticks], path.posY[ticks], path.posZ[ticks]),
                            new Vec3dCore(path.velX[ticks], path.velY[ticks], path.velZ[ticks]), (float) game[ticks - 1]);
                    SolverTrace.log("PROBE-COMMIT", "i=%d next=%d tick=%d x=%.17g z=%.17g vx=%.17g vz=%.17g incomingYaw=%.9f",
                            i, next, bounds[next], entry.pos.x, entry.pos.z, entry.vel.x, entry.vel.z, entry.yaw);
                    i = next;
                    advanced = true;
                    break;
                }
                if (!advanced) break;
                if (i == jumps) run.solved = true;
            }
            if (run.solved) {
                double[] replay = sc.toGameFacings(Angles.wrapAll(chained));
                assertEquals("full chained replay " + label, 0.0,
                        JumpConstraintCompiler.compile(full).maxViolation(replay, exact.forward(sc, replay)), 0.0);
            }
        } finally { SolverTrace.disable(); }
        return run;
    }

    private JumpPhysicsInputs slice(Seed entry, int start, int end) throws Exception {
        return (JumpPhysicsInputs) method(LongRunSolver.class, "sliceScenario", JumpPhysicsInputs.class,
                int.class, int.class, Vec3dCore.class, Vec3dCore.class, float.class)
                .invoke(null, full.asScenario(), start, end, entry.pos, entry.vel, entry.yaw);
    }

    @SuppressWarnings("unchecked")
    private double[] window(Seed entry, int i, int end, double[] margins) throws Exception {
        int a = bounds[i], c = bounds[end];
        JumpPhysicsInputs sc = slice(entry, a, c);
        List<JumpConstraint> cons = (List<JumpConstraint>) method(LongRunSolver.class, "sliceConstraints",
                JumpSpec.class, int.class, int.class, float.class).invoke(null, full, a, c, entry.yaw);
        SolverTrace.log("PROBE-WINDOW", "%s i=%d end=%d ticks=[%d,%d) x=%.17g z=%.17g vx=%.17g vz=%.17g yaw=%.9f",
                label, i, end, a, c, entry.pos.x, entry.pos.z, entry.vel.x, entry.vel.z, entry.yaw);
        if (end == bounds.length - 1) {
            Objective obj = new Objective(full.objective.axis, full.objective.sense, c - a);
            double[] y = (double[]) method(LongRunSolver.class, "solveWindow", ExactJumpModel.class,
                    JumpPhysicsInputs.class, List.class, Objective.class, boolean.class, AtomicBoolean.class, long.class)
                    .invoke(null, exact, sc, cons, obj, true, CANCEL, 0L);
            record(i, end, new JumpSpec(sc, cons, obj), "terminal", y);
            return y;
        }
        for (int k = 0; k < 4; k++) {
            Objective obj = new Objective(k < 2 ? JumpPhysicsInputs.Axis.Z : JumpPhysicsInputs.Axis.X,
                    k % 2 == 0 ? Objective.Sense.MAX : Objective.Sense.MIN, c - a);
            JumpSpec spec = new JumpSpec(sc, cons, obj);
            SolverTrace.log("PROBE-OBJ", "begin %s/%s", obj.axis, obj.sense);
            ClosedFormSolve.Config cfg = new ClosedFormSolve.Config();
            ClosedFormSolve.Result result = (ClosedFormSolve.Result) method(ClosedFormSolve.class, "optimizeReturning",
                    ExactJumpModel.class, JumpSpec.class, double.class, AtomicBoolean.class, double[].class,
                    boolean.class, ClosedFormSolve.Config.class).invoke(null, exact, spec, 0.0, CANCEL, margins, false, cfg);
            record(i, end, spec, obj.axis + "/" + obj.sense,
                    result == null ? null : result.yaws);
            if (result != null && result.feasible) return result.yaws;
        }
        JumpSpec spec = new JumpSpec(sc, cons, new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, c - a));
        SolverTrace.log("PROBE-OBJ", "begin fallback SLP");
        double[] yaws = SlpSolve.optimizeCentered(exact, spec, 0.0, CANCEL);
        record(i, end, spec, "SLP", yaws);
        return yaws;
    }

    private void record(int i, int end, JumpSpec spec, String objective, double[] yaws) {
        double violation = Double.NaN, value = Double.NaN;
        String worst = "none";
        if (yaws != null) {
            JumpPhysicsInputs sc = spec.asScenario();
            double[] game = sc.toGameFacings(yaws);
            ForwardPath path = exact.forward(sc, game);
            violation = 0;
            for (JumpConstraint c : spec.constraints) {
                double slack = JumpConstraintCompiler.slack(c, game, path);
                if (slack > violation) { violation = slack; worst = c.name; }
            }
            value = spec.objective.evaluate(path);
            for (int t = 0; t <= sc.numTicks; t++) {
                SolverTrace.log("PROBE-PATH", "objective=%s tick=%d x=%.12f z=%.12f vx=%.12f vz=%.12f yaw=%s",
                        objective, bounds[i] + t, path.posX[t], path.posZ[t], path.velX[t], path.velZ[t],
                        t < game.length ? SolverTrace.fmt("%.9f", game[t]) : "-");
            }
        }
        String result = yaws != null && violation <= 0 ? "solved" : "miss";
        attempts.add(String.format(Locale.ROOT, "%s,%d,%d,%d,%d,%s,%s,%.17g,%s,%.17g",
                label, i, end, bounds[i], bounds[end], objective, result, violation, worst, value));
        SolverTrace.log("PROBE-OBJ", "end %s %s violation=%.12g worst=%s obj=%.12f", objective, result, violation, worst, value);
    }

    /** Same dual construction as ClosedFormSolve.runLadder; continue after certification to compare the
     * next smaller margin, and also try each relevant margin cold to separate warm starts from clearance. */
    @SuppressWarnings("unchecked")
    private void diagnoseRungs(Seed entry) throws Exception {
        JumpPhysicsInputs sc = slice(entry, 80, 117);
        List<JumpConstraint> cons = (List<JumpConstraint>) method(LongRunSolver.class, "sliceConstraints",
                JumpSpec.class, int.class, int.class, float.class).invoke(null, full, 80, 117, entry.yaw);
        Objective obj = new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MIN, sc.numTicks);
        assertEquals(0, JumpLinearModel.crossAxisCount(cons));
        for (int pattern = 0; pattern < 3; pattern++) {
            boolean[] zx = pattern == 0 ? null : new boolean[sc.numTicks];
            boolean[] zz = pattern == 0 ? null : new boolean[sc.numTicks];
            if (pattern != 0) zz[2] = true;
            if (pattern == 1) for (int t = 4; t <= 10; t++) zx[t] = true;
            JumpLinearModel lin = new JumpLinearModel(sc, zx, zz);
            double[] cx = new double[lin.n], cz = new double[lin.n];
            lin.objectiveVectors(obj, cx, cz);
            List<JumpLinearModel.Wall> walls = lin.compileWalls(cons, 0, new boolean[1]);
            walls.addAll(lin.velocityWalls(exact.inertiaThreshold()));
            FacingPrefold pre = FacingPrefold.analyze(cons, new JumpLinearModel(sc));
            assertNotNull(pre);
            assertTrue(pre.isIdentity());
            FacingPrefold.Reduced reduced = pre.reduce(cx, cz, lin.mMagAll(), walls);
            for (double[] sequence : new double[][]{FIXED, {0.0012}, NORMAL}) {
                String sequenceName = sequence == NORMAL ? "warm-ladder" : "cold-" + sequence[0];
                CostateDualSolver dual = new CostateDualSolver(reduced.n, reduced.cx, reduced.cz, reduced.mMag, reduced.walls);
                double[] warm = null;
                for (double margin : sequence) {
                    CostateDualSolver.Result r = dual.solve(margin, warm);
                    if (r == null) {
                        SolverTrace.log("RUNG-DIAG", "pattern=%s sequence=%s margin=%.5g unbounded", SolverTrace.patternLabel(zx, zz), sequenceName, margin);
                        continue;
                    }
                    warm = r.lambda;
                    double[] yaws = pre.expand(lin, obj, r);
                    double[] game = sc.toGameFacings(yaws);
                    ForwardPath path = exact.forward(sc, game);
                    double exactViol = 0;
                    String exactWorst = "none";
                    for (JumpConstraint c : cons) {
                        double slack = JumpConstraintCompiler.slack(c, game, path);
                        if (slack > exactViol) { exactViol = slack; exactWorst = c.name; }
                    }
                    double smoothViol = 0;
                    String smoothWorst = "none";
                    for (JumpLinearModel.Wall wall : walls) {
                        double lhs = 0;
                        for (int t = 0; t < lin.n; t++) {
                            double phi = lin.baseArg(t) + Angles.rad(yaws[t]);
                            lhs += lin.mMag(t) * ((wall.coefX == null ? 0 : wall.coefX[t] * StrictMath.cos(phi))
                                    + (wall.coefZ == null ? 0 : wall.coefZ[t] * StrictMath.sin(phi)));
                        }
                        double slack = wall.eq ? StrictMath.abs(lhs - wall.bPrime) : lhs - wall.bPrime + margin;
                        if (slack > smoothViol) { smoothViol = slack; smoothWorst = wall.name; }
                    }
                    SolverTrace.log("RUNG-DIAG", "pattern=%s sequence=%s margin=%.5g iters=%d pg=%.9g smoothViolation=%.12g smoothWorst=%s exactViolation=%.12g exactWorst=%s",
                            SolverTrace.patternLabel(zx, zz), sequenceName, margin, dual.lastIters, dual.lastPgres,
                            smoothViol, smoothWorst, exactViol, exactWorst);
                    if (pattern == 1 && margin <= 0.0012) {
                        for (int t : new int[]{2, 4, 5, 6, 7, 8, 9, 10}) {
                            SolverTrace.log("RUNG-INERTIA", "sequence=%s margin=%.5g tick=%d axis=%s velocity=%.12g",
                                    sequenceName, margin, 80 + t, t == 2 ? "Z" : "X", t == 2 ? path.velZ[t] : path.velX[t]);
                        }
                    }
                }
            }
        }
    }
}
