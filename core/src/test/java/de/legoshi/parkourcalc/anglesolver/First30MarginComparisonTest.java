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
        attempts.add("run,start_index,end_index,start_tick,end_tick,objective,result,violation,worst,objective_value");
        states.add("run,start_index,tick,x,z,vx,vz,incoming_committed_yaw");
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
