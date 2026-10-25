package de.legoshi.parkourcalc.anglesolver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.anglesolver.solver.ExactJumpModel;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpSpec;
import de.legoshi.parkourcalc.core.anglesolver.solver.LongRunSolver;
import de.legoshi.parkourcalc.core.anglesolver.solver.SolverTrace;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import org.junit.experimental.categories.Category;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

/** Opt-in full Linkcraft route probe: configurable window ladder, fixed margin, actual-inertia window validation. */
@Category(SlowSolverTests.class)
public class FullRouteFixedMarginTraceTest {
    @Test
    public void traceFullRouteFixedMarginTenLadder() throws Exception {
        runProbe(10, "ten");
    }

    @Test
    public void traceFullRouteFixedMarginSevenLadder() throws Exception {
        runProbe(7, "seven");
    }

    private void runProbe(int window, String label) throws Exception {
        label = label + System.getProperty("pkc.fullRouteLabel", "");
        org.junit.Assume.assumeTrue("opt in with -Dpkc.fullRouteProbe=true", Boolean.getBoolean("pkc.fullRouteProbe"));
        String path = System.getenv("PKC_TRACE_INPUT");
        if (path == null) path = "/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json";
        String json = new String(Files.readAllBytes(Paths.get(path)), StandardCharsets.UTF_8);
        SaveFile file = SaveIO.parseSafe(json);
        assertNotNull("capture parse", file);
        InputData inputs = new InputData();
        SaveIO.applyRowsTo(file, inputs);
        AngleSolverState state = new AngleSolverState();
        SaveIO.applyAngleSolverTo(file, state);
        state.setStartTick(0);
        // Keep the imported endpoint and objective; this probe is not truncated to 30 jumps.
        AngleSolverEngine engine = new AngleSolverEngine(state, Fixtures.buildBoxes(file), inputs, t -> { },
                ExactJumpModel.forMcVersion(file.mcVersion));
        JumpSpec spec = engine.debugBuildSpec();
        assertNotNull("compiled full-route spec", spec);
        assertTrue("full route extends beyond the prior 250-tick probe", spec.asScenario().numTicks > 250);

        SolverTrace.enable("linkcraft-real11-full-fixed0003-" + label + "-ladder");
        SolverTrace.solveStart("full route margin=0.0003 windows="
                + (window == 10 ? "10,7,5,3,2,1" : "7,5,3,2,1") + " commits=3,1 deadline=none");
        SolverTrace.log("INPUT", "rows=%d ticks=%d constraints=%d objective=%s/%s@%d",
                inputs.size(), spec.asScenario().numTicks, spec.constraints.size(), spec.objective.axis,
                spec.objective.sense, spec.objective.tick);
        SolverTrace.log("POLICY", "nominal jump windows; extend provisional objective to first unconstrained airborne tick; retain all rows including final replay");
        long startNanos = System.nanoTime();
        try {
            double[] result = LongRunSolver.solve(ExactJumpModel.forMcVersion(file.mcVersion), spec, 0.0,
                    new AtomicBoolean(false), LongRunSolver.LongRunConfig.of(window, 3).withFixedMargin(0.0003));
            SolverTrace.log("RESULT", "solved=%s yaws=%d ms=%.3f", result != null, result == null ? 0 : result.length,
                    (System.nanoTime() - startNanos) / 1e6);
            if (result != null) {
                double[] replay = spec.asScenario().toGameFacings(
                        de.legoshi.parkourcalc.core.anglesolver.solver.Angles.wrapAll(result));
                double violation = de.legoshi.parkourcalc.core.anglesolver.solver.JumpConstraintCompiler.compile(spec)
                        .maxViolation(replay, ExactJumpModel.forMcVersion(file.mcVersion).forward(spec.asScenario(), replay));
                SolverTrace.log("VALIDATE", "fullReplayViolation=%.12g", violation);
                org.junit.Assert.assertEquals(0, violation, 0);
                java.util.List<String> rows = new java.util.ArrayList<>();
                rows.add("tick,yaw");
                for (int t = 0; t < result.length; t++) rows.add(t + "," + result[t]);
                Files.write(Paths.get("build/reports/linkcraft-real11-full-fixed0003-" + label + "-yaws.csv"), rows, StandardCharsets.UTF_8);
            }
            // A miss is a recorded experimental outcome, not a reason to alter solver behavior.
            System.out.println("full-route solved=" + (result != null));
        } finally {
            SolverTrace.disable();
        }
    }
}
