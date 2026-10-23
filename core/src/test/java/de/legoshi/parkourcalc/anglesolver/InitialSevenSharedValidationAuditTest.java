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

/** Diagnostic observation of the production acceptance helper, without reproducing the solve loop. */
@Category(SlowSolverTests.class)
public class InitialSevenSharedValidationAuditTest {
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
    public void compareFirstWindowFixedAndNormalUsingSharedDiagnostics() throws Exception {
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
        JumpPhysicsInputs sc = (JumpPhysicsInputs) method(LongRunSolver.class, "sliceScenario", JumpPhysicsInputs.class,
                int.class, int.class, Vec3dCore.class, Vec3dCore.class, float.class)
                .invoke(null, source, 0, 68, source.startPos, source.initialVelocity, source.startYaw);
        List<JumpConstraint> constraints = (List<JumpConstraint>) method(LongRunSolver.class, "sliceConstraints",
                JumpSpec.class, int.class, int.class, float.class).invoke(null, full, 0, 68, source.startYaw);
        assertEquals(33, constraints.size());
        java.lang.reflect.Field epsField = CostateDualSolver.class.getDeclaredField("EPS2");
        epsField.setAccessible(true);
        double eps2 = epsField.getDouble(null);
        List<String> summary = new ArrayList<>(), margins = new ArrayList<>(), inertia = new ArrayList<>(), paths = new ArrayList<>();
        summary.add("case,candidate,objective,optimization_margin,replay_input_ticks,assumed_pattern,replay_violation,continuous_violation,accepted,continuous_objective,replay_objective");
        margins.add("case,candidate,constraint,axis,tick,comparison,wall,committed,assumed_value,assumed_signed_margin,continuous_value,continuous_signed_margin,replay_value,replay_signed_margin");
        inertia.add("case,candidate,tick,axis,threshold,assumed_zero,assumed_velocity,assumed_threshold_margin,assumption_satisfied,continuous_zero,continuous_velocity,replay_zero,replay_velocity");
        paths.add("case,candidate,tick,continuous_yaw,game_yaw,assumed_x,assumed_z,continuous_x,continuous_z,continuous_vx,continuous_vz,replay_x,replay_z,replay_vx,replay_vz");
        SolverTrace.enable("initial-seven-shared-validation-audit");
        try {
            for (int scenario = 0; scenario < 3; scenario++) {
                final double margin = scenario == 2 ? .05 : .0003;
                Objective objective = new Objective(JumpPhysicsInputs.Axis.Z,
                        scenario == 1 ? Objective.Sense.MIN : Objective.Sense.MAX, 68);
                JumpSpec spec = new JumpSpec(sc, constraints, objective);
                double[] first = null;
                for (int prefix : new int[]{30, 12}) {
                    final String label = (scenario == 2 ? "normal-zmax" : scenario == 1 ? "fixed-zmin" : "fixed-zmax") + "-prefix" + prefix;
                    final int replayTicks = prefix;
                    int[] count = {0};
                    WindowCandidateValidation.setDiagnosticObserver(d -> {
                        int candidate = count[0]++;
                        summary.add(csv(label, candidate, objective.axis + "/" + objective.sense, margin, replayTicks,
                                d.assumedPattern, d.result.replayViolation, d.result.continuousViolation,
                                d.result.maxViolation() <= 0, objective.evaluate(d.continuousPath), objective.evaluate(d.replayPath)));
                        for (WindowCandidateValidation.ConstraintMargin c : d.constraints) {
                            margins.add(csv(label, candidate, c.constraint.name, c.constraint.mode, c.constraint.t1,
                                    c.constraint.cmp, c.constraint.rhs, c.committed, c.assumedValue, c.assumedMargin,
                                    c.continuousValue, c.continuousMargin, c.replayValue, c.replayMargin));
                        }
                        for (WindowCandidateValidation.InertiaCheck c : d.inertiaChecks) {
                            inertia.add(csv(label, candidate, c.tick, c.axis == 0 ? "X" : "Z", exact.inertiaThreshold(),
                                    c.assumedZero, c.assumedVelocity, c.assumedThresholdMargin, c.assumptionSatisfied,
                                    c.continuousZero, c.continuousVelocity, c.replayZero, c.replayVelocity));
                        }
                        for (int t = 0; t <= sc.numTicks; t++) {
                            paths.add(csv(label, candidate, t, t < sc.numTicks ? d.continuousFacings[t] : "",
                                    t < sc.numTicks ? d.gameFacings[t] : "", d.assumedPath.posX[t], d.assumedPath.posZ[t],
                                    d.continuousPath.posX[t], d.continuousPath.posZ[t], d.continuousPath.velX[t], d.continuousPath.velZ[t],
                                    d.replayPath.posX[t], d.replayPath.posZ[t], d.replayPath.velX[t], d.replayPath.velZ[t]));
                        }
                    });
                    SolverTrace.log("AUDIT", "begin case=%s margin=%.5g prefix=%d", label, margin, prefix);
                    ClosedFormSolve.Config cfg = new ClosedFormSolve.Config();
                    cfg.replayTickLimit = prefix;
                    ClosedFormSolve.Result result;
                    try {
                        result = (ClosedFormSolve.Result) method(ClosedFormSolve.class, "optimizeReturning",
                                ExactJumpModel.class, JumpSpec.class, double.class, AtomicBoolean.class, double[].class,
                                boolean.class, ClosedFormSolve.Config.class).invoke(null, exact, spec, 0.0,
                                new AtomicBoolean(false), new double[]{margin}, false, cfg);
                    } finally { WindowCandidateValidation.setDiagnosticObserver(null); }
                    assertNotNull(result);
                    if (eps2 == 1e-14) assertEquals("historical EPS2=1e-14 outcome", scenario != 0, result.feasible);
                    if (eps2 == 1e-14 && first != null) assertArrayEquals("prefix length does not change the selected angles here", first, result.yaws, 0);
                    first = result.yaws;
                    SolverTrace.log("AUDIT", "end case=%s candidateCount=%d accepted=%s violation=%.12g", label, count[0], result.feasible, result.violation);
                }
            }
        } finally {
            WindowCandidateValidation.setDiagnosticObserver(null);
            SolverTrace.disable();
            Path output = Paths.get("build/reports/initial-seven-shared-validation");
            Files.createDirectories(output);
            Files.write(output.resolve("metadata.txt"), Collections.singletonList("EPS2=" + eps2), StandardCharsets.UTF_8);
            Files.write(output.resolve("summary.csv"), summary, StandardCharsets.UTF_8);
            Files.write(output.resolve("constraint-margins.csv"), margins, StandardCharsets.UTF_8);
            Files.write(output.resolve("inertia-checks.csv"), inertia, StandardCharsets.UTF_8);
            Files.write(output.resolve("paths.csv"), paths, StandardCharsets.UTF_8);
        }
    }
}
