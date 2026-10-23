package de.legoshi.parkourcalc.anglesolver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.anglesolver.SolveResult;
import de.legoshi.parkourcalc.core.anglesolver.solver.ExactJumpModel;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import org.junit.experimental.categories.Category;

import static org.junit.Assert.assertNotNull;

/** Runs the actual Optimize graph on j153 to trace RelaxationRecovery. */
@Category(SlowSolverTests.class)
public class OptimizeInertiaTraceTest {

    @Test
    public void optimizeJ153() throws Exception {
        SaveFile file = SaveIO.parseSafe(Fixtures.rawPool("hpk/d10/j153_Flat_Momentum_Head_to_Head_Neo"));
        assertNotNull("capture did not parse", file);
        InputData inputs = new InputData();
        SaveIO.applyRowsTo(file, inputs);
        AngleSolverState state = new AngleSolverState();
        SaveIO.applyAngleSolverTo(file, state);
        state.setEffort(AngleSolverState.Effort.THOROUGH);
        // Leave enough of the Optimize budget for RecoveryLadder's 3 s
        // minimum-remaining-time gate and for the recovery itself.
        state.setOptimizeSeconds(30);
        state.clearResult();

        AngleSolverEngine engine = new AngleSolverEngine(state, Fixtures.buildBoxes(file), inputs,
                t -> { }, ExactJumpModel.forMcVersion(file.mcVersion));
        engine.solve();
        long deadline = System.currentTimeMillis() + 90_000;
        while (engine.isSolving() && System.currentTimeMillis() < deadline) {
            engine.poll();
            Thread.sleep(2);
        }
        engine.poll();

        SolveResult result = state.getResult();
        assertNotNull("Optimize graph did not finish in time", result);
        System.out.printf("OPTIMIZE j153 success=%s solver=%s objective=%s%n", result.isSuccess(),
                result.getSolver(), result.hasObjective() ? result.getObjectiveValue() : "none");
    }
}
