package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import static org.junit.Assert.*;

@Category(SlowSolverTests.class)
public class ManualCoupledWindowProbeTest {
    private static final Path ROOT = Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod");
    private static final Path OUT = ROOT.resolve("docs/research/solver-traces/manual-coupled-window-2026-10-24");

    static final class CoupledSection {
        final int firstJump, lastJump;
        CoupledSection(int firstJump, int lastJump) {
            if (firstJump < 1 || lastJump < firstJump) throw new IllegalArgumentException();
            this.firstJump = firstJump;
            this.lastJump = lastJump;
        }
        boolean contains(int endingJump) {
            return endingJump >= firstJump && endingJump <= lastJump;
        }
        int[] alternatives(int firstUncommittedJump, int nominalEnd, int routeJumps) {
            if (!contains(nominalEnd)) return new int[]{nominalEnd};
            List<Integer> ends = new ArrayList<>();
            if (lastJump < routeJumps) ends.add(lastJump + 1);
            if (firstJump - 1 >= firstUncommittedJump) ends.add(firstJump - 1);
            int[] result = new int[ends.size()];
            for (int k = 0; k < result.length; k++) result[k] = ends.get(k);
            return result;
        }
    }

    @Test public void manualSectionSelectsOnlyOutsideEndpoints() {
        CoupledSection section = new CoupledSection(10, 14);
        assertArrayEquals(new int[]{15, 9}, section.alternatives(6, 12, 100));
        assertArrayEquals(new int[]{15}, section.alternatives(11, 12, 100));
        assertArrayEquals(new int[]{9}, section.alternatives(6, 12, 14));
        assertArrayEquals(new int[]{8}, section.alternatives(6, 8, 100));
        assertArrayEquals(new int[0], section.alternatives(11, 12, 14));
    }

    @Test public void compareExactJumpSixEntry() throws Exception {
        org.junit.Assume.assumeTrue(Boolean.getBoolean("pkc.coupledWindowProbe"));
        Files.createDirectories(OUT);
        byte[] raw = Files.readAllBytes(Paths.get("/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json"));
        SaveFile file = SaveIO.parseSafe(new String(raw, StandardCharsets.UTF_8));
        InputData inputs = new InputData(); SaveIO.applyRowsTo(file, inputs);
        AngleSolverState state = new AngleSolverState(); SaveIO.applyAngleSolverTo(file, state); state.setStartTick(0);
        ExactJumpModel exact = ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full = new AngleSolverEngine(state, Fixtures.buildBoxes(file), inputs, t -> {}, exact).debugBuildSpec();
        Method slice = LongRunSolver.class.getDeclaredMethod("sliceScenario", JumpPhysicsInputs.class, int.class, int.class, Vec3dCore.class, Vec3dCore.class, float.class);
        Method rowsAt = LongRunSolver.class.getDeclaredMethod("sliceConstraints", JumpSpec.class, int.class, int.class, float.class);
        Method partition = LongRunSolver.class.getDeclaredMethod("jumpBoundaries", JumpPhysicsInputs.class);
        slice.setAccessible(true); rowsAt.setAccessible(true); partition.setAccessible(true);
        int[] bounds = (int[]) partition.invoke(null, full.asScenario());
        Files.write(OUT.resolve("boundaries.txt"), Arrays.asList(Arrays.toString(bounds)), StandardCharsets.UTF_8);
        Vec3dCore pos = full.asScenario().startPos, vel = full.asScenario().initialVelocity;
        float yaw = full.asScenario().startYaw;
        List<String> saved = Files.readAllLines(ROOT.resolve("docs/research/solver-traces/fixed-seven-window-local-bounds-2026-10-23/committed-jumps1-5-every-tick.csv"));
        String[] columns = saved.get(0).split(",");
        int xcol = Arrays.asList(columns).indexOf("fixed_x"), zcol = Arrays.asList(columns).indexOf("fixed_z");
        int yawcol = Arrays.asList(columns).indexOf("fixed_yaw");
        SolverTrace.enable("manual-coupled-window");
        try {
            for (int j = 0; j < 5; j++) {
                int a = bounds[j], end = bounds[j + 7];
                JumpPhysicsInputs win = (JumpPhysicsInputs) slice.invoke(null, full.asScenario(), a, end, pos, vel, yaw);
                @SuppressWarnings("unchecked") List<JumpConstraint> rows = (List<JumpConstraint>) rowsAt.invoke(null, full, a, end, yaw);
                Objective obj = new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, end - a);
                JumpSpec spec = new JumpSpec(win, retained(rows, obj), obj);
                double[] result = ClosedFormSolve.optimizeRobust(exact, spec, 0.0, new AtomicBoolean(false), config(bounds[j + 1] - a));
                assertNotNull("reconstruct prefix jump " + (j + 1), result);
                double[] game = win.toGameFacings(result); ForwardPath path = exact.forward(win, game);
                int commit = bounds[j + 1] - a;
                for (int k = 0; k <= commit; k++) {
                    String[] ref = saved.get(a + k + 1).split(",");
                    assertEquals(Double.parseDouble(ref[xcol]), path.posX[k], 1e-9);
                    assertEquals(Double.parseDouble(ref[zcol]), path.posZ[k], 1e-9);
                    if (k < commit) assertEquals(Double.parseDouble(ref[yawcol]), game[k], 1e-7);
                }
                pos = new Vec3dCore(path.posX[commit], path.posY[commit], path.posZ[commit]);
                vel = new Vec3dCore(path.velX[commit], path.velY[commit], path.velZ[commit]); yaw = (float) game[commit - 1];
            }
            assertEquals(55, bounds[5]);
            assertEquals(730.296913235570, pos.x, 1e-10);
            assertEquals(-1096.123854818772, pos.z, 1e-10);
            Files.write(OUT.resolve("entry.txt"), Arrays.asList("tick=" + bounds[5],
                    "x=" + Double.toHexString(pos.x), "z=" + Double.toHexString(pos.z),
                    "vx=" + Double.toHexString(vel.x), "vz=" + Double.toHexString(vel.z),
                    "yaw=" + Float.toHexString(yaw)), StandardCharsets.UTF_8);
            List<String> summary = new ArrayList<>();
            summary.add("case,end_jump,end_tick,ticks,objective,rows,accepted,elapsed_ms,best_continuous_violation,best_prefix_replay_violation,full_replay_violation,j6_exit_x,j6_exit_z,j6_exit_vx,j6_exit_vz,j6_carried_yaw,evaluated_candidates");
            runCase("baseline-max", 12, Objective.Sense.MAX, full, bounds, pos, vel, yaw, slice, rowsAt, exact, summary);
            runCase("baseline-min", 12, Objective.Sense.MIN, full, bounds, pos, vel, yaw, slice, rowsAt, exact, summary);
            CoupledSection section = new CoupledSection(10, 14);
            for (int endJump : section.alternatives(6, 12, bounds.length - 1)) {
                runCase(endJump > 14 ? "forward" : "backward", endJump,
                        endJump > 14 ? Objective.Sense.MIN : Objective.Sense.MAX,
                        full, bounds, pos, vel, yaw, slice, rowsAt, exact, summary);
            }
            CoupledSection narrow = new CoupledSection(11, 13);
            for (int endJump : narrow.alternatives(6, 12, bounds.length - 1)) {
                runCase(endJump > 13 ? "forward-narrow" : "backward-narrow", endJump,
                        endJump > 13 ? Objective.Sense.MIN : Objective.Sense.MAX,
                        full, bounds, pos, vel, yaw, slice, rowsAt, exact, summary);
            }
            Files.write(OUT.resolve("summary.csv"), summary, StandardCharsets.UTF_8);
        } finally { WindowCandidateValidation.setDiagnosticObserver(null); SolverTrace.disable(); }
    }

    private static ClosedFormSolve.Config config(int committedTicks) {
        ClosedFormSolve.Config cfg = new ClosedFormSolve.Config();
        cfg.margins = new double[]{0.0003}; cfg.marginsRobust = new double[]{0.0003};
        cfg.replayTickLimit = committedTicks; return cfg;
    }
    private static List<JumpConstraint> retained(List<JumpConstraint> rows, Objective obj) {
        List<JumpConstraint> result = new ArrayList<>();
        for (JumpConstraint c : rows) if (!ObjectiveBounds.isDirectCap(obj, c)) result.add(c);
        return result;
    }
    private static void runCase(String label, int endJump, Objective.Sense sense, JumpSpec full, int[] bounds,
                                Vec3dCore pos, Vec3dCore vel, float yaw, Method slice, Method rowsAt,
                                ExactJumpModel exact, List<String> summary) throws Exception {
        int a = bounds[5], end = bounds[endJump], commit = bounds[6] - a;
        JumpPhysicsInputs win = (JumpPhysicsInputs) slice.invoke(null, full.asScenario(), a, end, pos, vel, yaw);
        @SuppressWarnings("unchecked") List<JumpConstraint> original = (List<JumpConstraint>) rowsAt.invoke(null, full, a, end, yaw);
        Objective obj = new Objective(JumpPhysicsInputs.Axis.Z, sense, end - a);
        JumpSpec spec = new JumpSpec(win, retained(original, obj), obj);
        List<WindowCandidateValidation.Diagnostics> observations = new ArrayList<>();
        List<String> iterations = new ArrayList<>();
        iterations.add("iteration,dual,residual,input_change,min_costate,near_zero,fallback,final_sample");
        ClosedFormSolve.Config cfg = config(commit);
        cfg.dualIterationObserver = (it, value, residual, change, min, near, fallback, last) -> {
            if (last || it % 100 == 0) iterations.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%.17g,%.17g,%d,%d,%s",it,value,residual,change,min,near,fallback,last));
        };
        WindowCandidateValidation.setDiagnosticObserver(observations::add);
        SolverTrace.log("SPIKE", "case=%s endJump=%d endTick=%d objective=Z/%s rows=%d", label, endJump, end, sense, spec.constraints.size());
        long t0 = System.nanoTime();
        double[] result;
        try { result = ClosedFormSolve.optimizeRobust(exact, spec, 0.0, new AtomicBoolean(false), cfg); }
        finally { WindowCandidateValidation.setDiagnosticObserver(null); }
        double ms = (System.nanoTime() - t0) / 1e6;
        WindowCandidateValidation.Diagnostics best = null;
        for (WindowCandidateValidation.Diagnostics d : observations) if (best == null || d.result.maxViolation() < best.result.maxViolation()) best = d;
        double[] chosen = result != null ? result : best == null ? null : best.continuousFacings;
        if (result != null) assertEquals(0.0, new WindowCandidateValidation(exact, spec, commit).evaluate(result).maxViolation(), 0.0);
        double fullViol = Double.NaN;
        double ex = Double.NaN, ez = ex, evx = ex, evz = ex, eyaw = ex;
        if (chosen != null) {
            double[] game = win.toGameFacings(chosen); ForwardPath path = exact.forward(win, game);
            fullViol = JumpConstraintCompiler.compile(new JumpSpec(win, original, obj)).maxViolation(game, path);
            ex = path.posX[commit]; ez = path.posZ[commit]; evx = path.velX[commit]; evz = path.velZ[commit]; eyaw = game[commit - 1];
            List<String> ticks = new ArrayList<>(); ticks.add("tick,x,z,vx,vz,yaw,committed");
            for (int k = 0; k <= win.numTicks; k++) ticks.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%.17g,%.17g,%s,%s",a+k,path.posX[k],path.posZ[k],path.velX[k],path.velZ[k],k < game.length ? Double.toString(game[k]) : "",k<=commit));
            Files.write(OUT.resolve(label+"-path.csv"),ticks,StandardCharsets.UTF_8);
        }
        List<String> candidates = new ArrayList<>(); candidates.add("index,pattern,continuous_violation,prefix_replay_violation,failed_inertia_assumptions");
        for (int i = 0; i < observations.size(); i++) {
            WindowCandidateValidation.Diagnostics d = observations.get(i); int failed = 0;
            for (WindowCandidateValidation.InertiaCheck check : d.inertiaChecks) if (check.assumptionKnown && !check.assumptionSatisfied) failed++;
            candidates.add(String.format(Locale.ROOT,"%d,%s,%.17g,%.17g,%d",i,d.assumedPattern,d.result.continuousViolation,d.result.replayViolation,failed));
        }
        Files.write(OUT.resolve(label+"-candidates.csv"),candidates,StandardCharsets.UTF_8);
        Files.write(OUT.resolve(label+"-iterations.csv"),iterations,StandardCharsets.UTF_8);
        if (best != null) {
            List<String> margins = new ArrayList<>();
            margins.add("tick,name,axis,comparison,bound,committed,continuous_margin,replay_margin,assumed_margin");
            for (WindowCandidateValidation.ConstraintMargin m : best.constraints) margins.add(String.format(Locale.ROOT,
                    "%d,%s,%s,%s,%.17g,%s,%.17g,%.17g,%.17g", a+m.constraint.t1,m.constraint.name,
                    m.constraint.mode,m.constraint.cmp,m.constraint.rhs,m.committed,m.continuousMargin,m.replayMargin,m.assumedMargin));
            Files.write(OUT.resolve(label+"-margins.csv"),margins,StandardCharsets.UTF_8);
            List<String> checks = new ArrayList<>();
            checks.add("tick,axis,assumed_velocity,continuous_velocity,replay_velocity,assumed_zero,assumption_satisfied");
            for (WindowCandidateValidation.InertiaCheck c : best.inertiaChecks) checks.add(String.format(Locale.ROOT,
                    "%d,%s,%.17g,%.17g,%.17g,%s,%s",a+c.tick,c.axis==0?"X":"Z",c.assumedVelocity,
                    c.continuousVelocity,c.replayVelocity,c.assumedZero,c.assumptionSatisfied));
            Files.write(OUT.resolve(label+"-inertia.csv"),checks,StandardCharsets.UTF_8);
        }
        summary.add(String.format(Locale.ROOT,"%s,%d,%d,%d,Z/%s,%d,%s,%.3f,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d",label,endJump,end,end-a,sense,spec.constraints.size(),result!=null,ms,best==null?Double.NaN:best.result.continuousViolation,best==null?Double.NaN:best.result.replayViolation,fullViol,ex,ez,evx,evz,eyaw,observations.size()));
        Files.write(OUT.resolve("summary.csv"),summary,StandardCharsets.UTF_8);
        System.out.println(summary.get(summary.size()-1));
    }
}
