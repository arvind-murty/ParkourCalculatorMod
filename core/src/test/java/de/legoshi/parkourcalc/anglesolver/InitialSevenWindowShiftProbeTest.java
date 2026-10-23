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
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import static org.junit.Assert.*;

/** Opt-in endpoint comparison using production slicing, solving and shared acceptance. */
@Category(SlowSolverTests.class)
public class InitialSevenWindowShiftProbeTest {
    private static Method method(Class<?> owner, String name, Class<?>... args) throws Exception {
        Method m = owner.getDeclaredMethod(name, args);
        m.setAccessible(true);
        return m;
    }
    private static String csv(Object... values) {
        StringBuilder s = new StringBuilder();
        for (Object v : values) {
            if (s.length() > 0) s.append(',');
            s.append('"').append(String.valueOf(v).replace("\"", "\"\"")).append('"');
        }
        return s.toString();
    }
    @Test @SuppressWarnings("unchecked")
    public void compareJumpAndTickShifts() throws Exception {
        Assume.assumeTrue("opt in with -Dpkc.marginProbe=true", Boolean.getBoolean("pkc.marginProbe"));
        String input = System.getenv("PKC_TRACE_INPUT");
        if (input == null) input = "/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json";
        SaveFile file = SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get(input)), StandardCharsets.UTF_8));
        assertNotNull(file);
        InputData rows = new InputData();
        SaveIO.applyRowsTo(file, rows);
        AngleSolverState state = new AngleSolverState();
        SaveIO.applyAngleSolverTo(file, state);
        state.setStartTick(0);
        ExactJumpModel exact = ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full = new AngleSolverEngine(state, Fixtures.buildBoxes(file), rows, t -> {}, exact).debugBuildSpec();
        JumpPhysicsInputs source = full.asScenario();
        List<String> summary = new ArrayList<>(), candidates = new ArrayList<>(), margins = new ArrayList<>();
        summary.add("end,prefix,constraints,direct_cap,primary_accepted,primary_ms,window_accepted,window_ms");
        candidates.add("end,prefix,phase,candidate,pattern,continuous_violation,replay_violation,accepted");
        margins.add("end,prefix,phase,candidate,name,axis,tick,comparison,wall,committed,continuous_margin,replay_margin");
        SolverTrace.enable("initial-seven-window-shifts");
        try {
            for (int end : new int[]{68, 64, 71, 67, 69}) {
                JumpPhysicsInputs sc = (JumpPhysicsInputs) method(LongRunSolver.class, "sliceScenario",
                        JumpPhysicsInputs.class, int.class, int.class, Vec3dCore.class, Vec3dCore.class, float.class)
                        .invoke(null, source, 0, end, source.startPos, source.initialVelocity, source.startYaw);
                List<JumpConstraint> cons = (List<JumpConstraint>) method(LongRunSolver.class, "sliceConstraints",
                        JumpSpec.class, int.class, int.class, float.class).invoke(null, full, 0, end, source.startYaw);
                Objective obj = new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, end);
                for (int prefix : new int[]{30, 12}) {
                    final int e = end, p = prefix;
                    final String[] phase = {"primary"};
                    final int[] count = {0};
                    WindowCandidateValidation.setDiagnosticObserver(d -> {
                        int n = count[0]++;
                        candidates.add(csv(e, p, phase[0], n, d.assumedPattern, d.result.continuousViolation,
                                d.result.replayViolation, d.result.maxViolation() <= 0));
                        for (WindowCandidateValidation.ConstraintMargin c : d.constraints)
                            margins.add(csv(e, p, phase[0], n, c.constraint.name, c.constraint.mode, c.constraint.t1,
                                    c.constraint.cmp, c.constraint.rhs, c.committed, c.continuousMargin, c.replayMargin));
                    });
                    try {
                        SolverTrace.log("SHIFT", "begin end=%d prefix=%d", end, prefix);
                        ClosedFormSolve.Config cfg = new ClosedFormSolve.Config();
                        cfg.replayTickLimit = prefix;
                        cfg.marginsRobust = new double[]{.0003};
                        long start = System.nanoTime();
                        double[] primary = ClosedFormSolve.optimizeRobust(exact, new JumpSpec(sc, cons, obj),
                                0.0, new AtomicBoolean(false), cfg);
                        double primaryMs = (System.nanoTime() - start) / 1e6;
                        phase[0] = "full-window";
                        count[0] = 0;
                        start = System.nanoTime();
                        double[] window = (double[]) method(LongRunSolver.class, "solveWindow", ExactJumpModel.class,
                                JumpPhysicsInputs.class, List.class, Objective.class, boolean.class, int.class,
                                AtomicBoolean.class, long.class, Double.class).invoke(null, exact, sc, cons, obj,
                                false, prefix, new AtomicBoolean(false), 0L, Double.valueOf(.0003));
                        double windowMs = (System.nanoTime() - start) / 1e6;
                        summary.add(csv(end, prefix, cons.size(), ObjectiveBounds.directCap(obj, cons) != null,
                                primary != null, primaryMs, window != null, windowMs));
                        SolverTrace.log("SHIFT", "end end=%d prefix=%d primary=%s window=%s", end, prefix,
                                primary != null, window != null);
                    } finally { WindowCandidateValidation.setDiagnosticObserver(null); }
                }
            }
        } finally {
            SolverTrace.disable();
            Path out = Paths.get("build/reports/initial-seven-window-shifts");
            Files.createDirectories(out);
            java.lang.reflect.Field eps = CostateDualSolver.class.getDeclaredField("EPS2");
            eps.setAccessible(true);
            Files.write(out.resolve("metadata.txt"), Arrays.asList("input=" + input, "EPS2=" + eps.getDouble(null),
                    "fixedMargin=0.0003; dualMaxIterations=100; same initial state; prefixes=30,12"), StandardCharsets.UTF_8);
            Files.write(out.resolve("summary.csv"), summary, StandardCharsets.UTF_8);
            Files.write(out.resolve("candidates.csv"), candidates, StandardCharsets.UTF_8);
            Files.write(out.resolve("constraint-margins.csv"), margins, StandardCharsets.UTF_8);
        }
    }
}
