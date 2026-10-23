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

/** Reproduces the first-30-jump, commit-one LongRun baseline with the normal robust-margin ladder. */
@Category(SlowSolverTests.class)
public class First30NormalMarginTraceTest {
    @Test
    public void traceNormalMarginSevenWindowCommitOne() throws Exception {
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
        state.setLandingTick(250);
        AngleSolverEngine engine = new AngleSolverEngine(state, Fixtures.buildBoxes(file), inputs, t -> { },
                ExactJumpModel.forMcVersion(file.mcVersion));
        JumpSpec spec = engine.debugBuildSpec();
        assertNotNull("compiled first-30 spec", spec);
        assertTrue("expected 250-tick slice", spec.asScenario().numTicks == 250);
        assertTrue("expected 229 constraints", spec.constraints.size() == 229);

        SolverTrace.enable("linkcraft-real11-first30-normal7-commit1");
        SolverTrace.solveStart("first30 normal margins windows=7,5,3,2,1 commit=1");
        try {
            double[] result = LongRunSolver.solve(ExactJumpModel.forMcVersion(file.mcVersion), spec, 0.0,
                    new AtomicBoolean(false), LongRunSolver.LongRunConfig.of(7, 1));
            SolverTrace.log("RESULT", "solved=%s yaws=%d", result != null, result == null ? 0 : result.length);
            assertNotNull("normal-margin 7 ladder should solve", result);
        } finally {
            SolverTrace.disable();
        }
    }
}
