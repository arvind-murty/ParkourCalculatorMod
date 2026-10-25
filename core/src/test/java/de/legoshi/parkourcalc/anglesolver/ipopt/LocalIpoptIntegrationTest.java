package de.legoshi.parkourcalc.anglesolver.ipopt;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.anglesolver.ipopt.IpoptProblem;
import de.legoshi.parkourcalc.core.anglesolver.ipopt.LocalIpoptSolver;
import de.legoshi.parkourcalc.core.anglesolver.solver.*;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Assume;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.GZIPInputStream;
import static org.junit.Assert.*;

@Category(SlowSolverTests.class)
public class LocalIpoptIntegrationTest {
    private void requireRuntime() {
        Assume.assumeNotNull(System.getenv("PKC_IPOPT_PYTHON"));
    }

    @Test public void solvesKnownOptimumWithFacingAndRelativeRows() throws Exception {
        requireRuntime();
        JumpSpec seed = IpoptValidationTest.spec(4, Collections.emptyList());
        seed.asScenario().initialVelocity = Vec3dCore.ZERO;
        JumpConstraint pin = new JumpConstraint(JumpConstraint.Mode.F, 0, null,
                JumpConstraint.Op.PLUS, JumpConstraint.Cmp.EQ, -90, "pin");
        JumpConstraint tie = new JumpConstraint(JumpConstraint.Mode.F, 1, 0,
                JumpConstraint.Op.MINUS, JumpConstraint.Cmp.EQ, 0, "tie");
        JumpConstraint relative = new JumpConstraint(JumpConstraint.Mode.Z, 4, 0,
                JumpConstraint.Op.MINUS, JumpConstraint.Cmp.EQ, 0, "relative");
        JumpSpec spec = new JumpSpec(seed.asScenario(), Arrays.asList(pin, tie, relative), seed.objective);
        IpoptProblem problem = new IpoptProblem(spec, new double[] {-90, -90, -90, -90},
                Collections.emptyList(), 0.005, 5, false);
        LocalIpoptSolver.Result solved = new LocalIpoptSolver().solve(problem, new AtomicBoolean(), best -> { });
        assertNotNull(solved.yaw);
        assertTrue(solved.violation <= 1e-7);
        double expected = new JumpLinearModel(spec.asScenario()).forwardContinuous(
                new double[] {-90, -90, -90, -90}, 0.005, true).posX[4];
        assertEquals(expected, solved.path.posX[4], 1e-6);
    }

    @Test public void real11Through400UsesAllFacingRowsAndApplyCallback() throws Exception {
        solveReal11(400);
    }

    @Test public void real11Through200UsesTheSameProductionPipeline() throws Exception {
        solveReal11(200);
    }

    private void solveReal11(int ticks) throws Exception {
        requireRuntime();
        String source = System.getenv("PKC_IPOPT_CAPTURE");
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (GZIPInputStream input = new GZIPInputStream(source == null
                ? getClass().getResourceAsStream("/ipopt/real11.json.gz")
                : Files.newInputStream(Paths.get(source)))) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) bytes.write(buffer, 0, count);
        }
        SaveFile file = SaveIO.parseSafe(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
        InputData inputs = new InputData();
        SaveIO.applyRowsTo(file, inputs);
        AngleSolverState state = new AngleSolverState();
        SaveIO.applyAngleSolverTo(file, state);
        state.setBackend(AngleSolverState.Backend.IPOPT_LOCAL);
        state.setLandingTick(state.getStartTick() + ticks);
        state.setLegalMode(false);
        state.setSmoothLambda(0);
        state.setBudgetMs(10000);
        AtomicBoolean applied = new AtomicBoolean();
        AngleSolverEngine engine = new AngleSolverEngine(state, Fixtures.buildBoxes(file), inputs,
                tick -> applied.set(true), ExactJumpModel.forMcVersion(file.mcVersion));
        JumpSpec spec = engine.debugBuildSpec();
        assertNotNull(spec);
        assertTrue(spec.constraints.stream().anyMatch(c -> c.mode == JumpConstraint.Mode.F));
        String artifactDirectory = System.getenv("PKC_IPOPT_ARTIFACT_DIR");
        if (artifactDirectory != null) {
            double[] yaw = new double[ticks];
            double previous = spec.asScenario().startYaw;
            for (int t = 0; t < yaw.length; t++) {
                de.legoshi.parkourcalc.core.ui.InputRow row = inputs.getRows().get(state.getStartTick() + t);
                if (row.getYaw() != null) previous = row.isYawLocked() ? row.getYaw() : previous + row.getYaw();
                yaw[t] = previous;
            }
            IpoptProblem exported = new IpoptProblem(spec, yaw,
                    IpoptProblem.recordedContacts(Fixtures.buildBoxes(file).getStates(), state.getStartTick(), ticks),
                    0.005, 10, false);
            java.nio.file.Path directory = Paths.get(artifactDirectory);
            Files.createDirectories(directory);
            Files.write(directory.resolve("real11-" + ticks + "-request.json"),
                    new com.google.gson.Gson().toJson(exported).getBytes(StandardCharsets.UTF_8));
        }
        engine.solve();
        long deadline = System.nanoTime() + 15_000_000_000L;
        while (engine.isSolving() && System.nanoTime() < deadline) {
            engine.poll();
            Thread.sleep(10);
        }
        engine.poll();
        assertFalse("Worker exceeded engine deadline", engine.isSolving());
        assertNotNull(state.getResult());
        assertTrue(state.getResult().getNotice(), state.getResult().isSuccess());
        assertEquals(ticks, state.getResult().getYaws().size());
        assertTrue(StrictMath.abs(Angles.wrap(state.getResult().getYaws().get(187).yaw - 125.80603)) <= 0.0001001);
        System.out.println("IPOPT real11 " + ticks + ": " + state.getResult().getDurationMs() + " ms, X="
                + state.getResult().getObjectiveValue());
        engine.apply();
        assertTrue(applied.get());
    }
}
