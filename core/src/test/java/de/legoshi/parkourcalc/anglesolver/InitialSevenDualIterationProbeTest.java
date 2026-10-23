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

/** Cold-start cap sweep on the initial seven-jump Z/MAX window, with all other stopping rules retained. */
@Category(SlowSolverTests.class)
public class InitialSevenDualIterationProbeTest {
    private static Method method(Class<?> type, String name, Class<?>... args) throws Exception {
        Method method = type.getDeclaredMethod(name, args);
        method.setAccessible(true);
        return method;
    }
    private static String csv(Object... values) {
        StringBuilder line = new StringBuilder();
        for (Object value : values) {
            if (line.length() > 0) line.append(',');
            line.append('"').append(String.valueOf(value).replace("\"", "\"\"")).append('"');
        }
        return line.toString();
    }

    @Test
    @SuppressWarnings("unchecked")
    public void sweepCapsWithoutChangingOtherStoppingRules() throws Exception {
        Assume.assumeTrue("opt in with -Dpkc.marginProbe=true", Boolean.getBoolean("pkc.marginProbe"));
        String input = System.getenv("PKC_TRACE_INPUT");
        if (input == null) input = "/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json";
        SaveFile file = SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get(input)), StandardCharsets.UTF_8));
        InputData inputs = new InputData();
        SaveIO.applyRowsTo(file, inputs);
        AngleSolverState state = new AngleSolverState();
        SaveIO.applyAngleSolverTo(file, state);
        state.setStartTick(0);
        ExactJumpModel exact = ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full = new AngleSolverEngine(state, Fixtures.buildBoxes(file), inputs, t -> {}, exact).debugBuildSpec();
        JumpPhysicsInputs source = full.asScenario();
        JumpPhysicsInputs sc = (JumpPhysicsInputs) method(LongRunSolver.class, "sliceScenario", JumpPhysicsInputs.class,
                int.class, int.class, Vec3dCore.class, Vec3dCore.class, float.class)
                .invoke(null, source, 0, 68, source.startPos, source.initialVelocity, source.startYaw);
        List<JumpConstraint> constraints = (List<JumpConstraint>) method(LongRunSolver.class, "sliceConstraints",
                JumpSpec.class, int.class, int.class, float.class).invoke(null, full, 0, 68, source.startYaw);
        JumpSpec spec = new JumpSpec(sc, constraints, new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 68));
        assertEquals(33, constraints.size());
        java.lang.reflect.Field smoothingField = CostateDualSolver.class.getDeclaredField("EPS2");
        smoothingField.setAccessible(true);
        double eps2 = smoothingField.getDouble(null);
        double[][] referenceYaws = {null};
        List<String> outcomes = new ArrayList<>(), candidates = new ArrayList<>(), iterations = new ArrayList<>(), margins = new ArrayList<>();
        List<String> recovery = new ArrayList<>(), recoveryMargins = new ArrayList<>();
        outcomes.add("case,cap,margin,prefix,accepted,best_violation,dual_calls,elapsed_ms");
        candidates.add("case,pass,assumed_pattern,replay_violation,continuous_violation,assumed_max_violation,assumed_tightened_max_violation,continuous_objective,min_costate,near_zero_costates,final_dual_residual,final_iterations,direction_fallback_costates");
        iterations.add("case,pass,iteration,dual_value,projected_residual,input_change,min_costate,near_zero_costates,direction_fallback_costates,final_sample");
        margins.add("case,pass,constraint,committed,assumed_margin,continuous_margin,replay_margin");
        SolverTrace.enable("initial-seven-dual-iteration-sweep");
        try {
            for (double margin : new double[]{.0003, .05}) {
                int[] caps = margin == .0003 ? new int[]{100, 200, 500, 1000, 5000, 20000} : new int[]{100, 1000};
                for (int cap : caps) for (int prefix : new int[]{30, 12}) {
                    final String label = "margin" + margin + "-cap" + cap + "-prefix" + prefix;
                    int[] pass = {-1};
                    double[] finalState = new double[5];
                    ClosedFormSolve.Config cfg = new ClosedFormSolve.Config();
                    cfg.dualMaxIterations = cap;
                    cfg.replayTickLimit = prefix;
                    cfg.dualIterationObserver = (it, value, pg, change, minCostate, tiny, fallback, last) -> {
                        if (it == 0 && !last) pass[0]++;
                        iterations.add(csv(label, pass[0], it, value, pg, change, minCostate, tiny, fallback, last));
                        if (last) { finalState[0] = minCostate; finalState[1] = tiny; finalState[2] = pg; finalState[3] = it; finalState[4] = fallback; }
                    };
                    WindowCandidateValidation.setDiagnosticObserver(d -> {
                        if (margin == .0003 && cap == 200 && prefix == 30 && pass[0] == 0) {
                            referenceYaws[0] = d.continuousFacings.clone();
                        }
                        double assumedViolation = 0, tightenedViolation = 0;
                        for (WindowCandidateValidation.ConstraintMargin c : d.constraints) {
                            assumedViolation = Math.max(assumedViolation, -c.assumedMargin);
                            tightenedViolation = Math.max(tightenedViolation,
                                    (c.constraint.cmp == JumpConstraint.Cmp.EQ ? 0 : margin) - c.assumedMargin);
                            margins.add(csv(label, pass[0], c.constraint.name, c.committed,
                                    c.assumedMargin, c.continuousMargin, c.replayMargin));
                        }
                        candidates.add(csv(label, pass[0], d.assumedPattern, d.result.replayViolation,
                                d.result.continuousViolation, assumedViolation, tightenedViolation,
                                spec.objective.evaluate(d.continuousPath), finalState[0], finalState[1], finalState[2], finalState[3], finalState[4]));
                    });
                    SolverTrace.log("CAP-PROBE", "begin case=%s", label);
                    long begin = System.nanoTime();
                    ClosedFormSolve.Result result;
                    try {
                        result = (ClosedFormSolve.Result) method(ClosedFormSolve.class, "optimizeReturning", ExactJumpModel.class,
                                JumpSpec.class, double.class, AtomicBoolean.class, double[].class, boolean.class,
                                ClosedFormSolve.Config.class).invoke(null, exact, spec, 0.0, new AtomicBoolean(false),
                                new double[]{margin}, false, cfg);
                    } finally { WindowCandidateValidation.setDiagnosticObserver(null); }
                    double ms = (System.nanoTime() - begin) / 1e6;
                    assertNotNull(result);
                    if (eps2 == 1e-14 && cap == 100 && margin == .0003) assertEquals(.011663350626349711, result.violation, 1e-12);
                    outcomes.add(csv(label, cap, margin, prefix, result.feasible, result.violation, pass[0] + 1, ms));
                    SolverTrace.log("CAP-PROBE", "end case=%s accepted=%s violation=%.12g calls=%d ms=%.3f", label, result.feasible, result.violation, pass[0] + 1, ms);
                }
            }
            // Compare the derivative's smoothed controls with the full-length controls recovered as angles.
            JumpLinearModel lin = new JumpLinearModel(sc);
            double[] cx = new double[68], cz = new double[68];
            lin.objectiveVectors(spec.objective, cx, cz);
            CostateDualSolver dual = new CostateDualSolver(68, cx, cz, lin.mMagAll(),
                    lin.compileWalls(constraints, 0, new boolean[1]));
            CostateDualSolver.Result dualResult = dual.solve(.0003, null, 200);
            assertNotNull(dualResult);
            double[] angles = lin.recoverAlongCostate(spec.objective, dualResult.gx, dualResult.gz);
            assertArrayEquals("direct dual reconstruction matches production first pass", referenceYaws[0], angles, 0);
            if (eps2 == 1e-14) assertEquals(.07214943605640656,
                    new WindowCandidateValidation(exact, spec, 30).evaluate(angles).continuousViolation, 1e-12);
            double[] smoothX = new double[68], smoothZ = new double[68];
            recovery.add("tick,costate_x,costate_z,costate_norm,input_magnitude,smoothed_length_ratio,smoothed_input_x,smoothed_input_z,full_input_x,full_input_z,extra_z_at68");
            for (int t = 0; t < 68; t++) {
                double gx = dualResult.gx[t], gz = dualResult.gz[t];
                double norm = StrictMath.hypot(gx, gz);
                // Read the actual smoothing constant, rather than duplicating a probe-selected epsilon.
                java.lang.reflect.Field epsField = CostateDualSolver.class.getDeclaredField("EPS2");
                epsField.setAccessible(true);
                double denominator = StrictMath.sqrt(gx * gx + gz * gz + epsField.getDouble(null));
                smoothX[t] = lin.mMag(t) * gx / denominator;
                smoothZ[t] = lin.mMag(t) * gz / denominator;
                double phi = lin.baseArg(t) + Angles.rad(angles[t]);
                double fullX = lin.mMag(t) * StrictMath.cos(phi), fullZ = lin.mMag(t) * StrictMath.sin(phi);
                recovery.add(csv(t, gx, gz, norm, lin.mMag(t), norm / denominator,
                        smoothX[t], smoothZ[t], fullX, fullZ, lin.coefAxis(1, t, 68) * (fullZ - smoothZ[t])));
            }
            double[][] relaxed = new double[2][69];
            for (int axis = 0; axis < 2; axis++) for (int k = 0; k <= 68; k++) {
                double value = lin.constPos(k, axis);
                for (int t = 0; t < k; t++) value += lin.coefAxis(axis, t, k) * (axis == 0 ? smoothX[t] : smoothZ[t]);
                relaxed[axis][k] = value;
            }
            ForwardPath shortened = new ForwardPath(relaxed[0], new double[69], relaxed[1]);
            ForwardPath fullLength = lin.forwardFixed(angles);
            recoveryMargins.add("constraint,tick,wall,smoothed_original_margin,smoothed_tightened_margin,recovered_original_margin,recovered_tightened_margin");
            for (JumpConstraint c : constraints) {
                double smoothed = JumpConstraintCompiler.evaluate(c, angles, shortened);
                double recovered = JumpConstraintCompiler.evaluate(c, angles, fullLength);
                smoothed = c.cmp == JumpConstraint.Cmp.LE ? -smoothed : smoothed;
                recovered = c.cmp == JumpConstraint.Cmp.LE ? -recovered : recovered;
                recoveryMargins.add(csv(c.name, c.t1, c.rhs, smoothed, smoothed - .0003, recovered, recovered - .0003));
            }
        } finally {
            WindowCandidateValidation.setDiagnosticObserver(null);
            SolverTrace.disable();
            Path output = Paths.get("build/reports/initial-seven-dual-iteration-sweep");
            Files.createDirectories(output);
            Files.write(output.resolve("metadata.txt"), Collections.singletonList("EPS2=" + eps2), StandardCharsets.UTF_8);
            Files.write(output.resolve("outcomes.csv"), outcomes, StandardCharsets.UTF_8);
            Files.write(output.resolve("candidates.csv"), candidates, StandardCharsets.UTF_8);
            Files.write(output.resolve("iterations.csv"), iterations, StandardCharsets.UTF_8);
            Files.write(output.resolve("constraint-margins.csv"), margins, StandardCharsets.UTF_8);
            Files.write(output.resolve("recovery.csv"), recovery, StandardCharsets.UTF_8);
            Files.write(output.resolve("recovery-constraint-margins.csv"), recoveryMargins, StandardCharsets.UTF_8);
        }
    }
}
