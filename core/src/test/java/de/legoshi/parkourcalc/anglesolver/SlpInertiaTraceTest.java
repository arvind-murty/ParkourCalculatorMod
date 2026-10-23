package de.legoshi.parkourcalc.anglesolver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.ProblemFixture;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.solver.Angles;
import de.legoshi.parkourcalc.core.anglesolver.solver.ExactJumpModel;
import de.legoshi.parkourcalc.core.anglesolver.solver.ForwardPath;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpConstraintCompiler;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpPhysicsInputs;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpSpec;
import org.junit.Test;
import org.junit.experimental.categories.Category;

import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

/** Focused trace fixture: legacy 1.8 inertia, ClosedForm miss, then dual-chain SLP and level-set SLP. */
@Category(SlowSolverTests.class)
public class SlpInertiaTraceTest {

    @Test
    public void loopmmShowsSlpProposalsAgainstExactInertiaReplay() {
        ProblemFixture pf = ProblemFixture.load("dualrecovery", "loopmm-3jump-lands");
        ExactJumpModel exact = pf.model;
        JumpSpec spec = pf.specFor(null, null);
        JumpPhysicsInputs scenario = spec.asScenario();
        String[] route = new String[1];
        double[] yaws = AngleSolverEngine.dualChain(exact, spec, scenario,
                new AtomicBoolean(false), route);

        assertNotNull("dual chain should recover loopmm", yaws);
        assertTrue("expected route to exercise SLP, got " + route[0], route[0].contains("SLP"));
        double[] gameFacings = scenario.toGameFacings(Angles.wrapAll(yaws));
        ForwardPath path = exact.forward(scenario, gameFacings);
        double violation = JumpConstraintCompiler.compile(spec).maxViolation(gameFacings, path);
        assertTrue("returned SLP path must pass exact replay: " + violation, violation <= 0.0);

        boolean sawGate = false;
        for (int t = 0; t < scenario.numTicks; t++) {
            sawGate |= Math.abs(path.velX[t]) < exact.inertiaThreshold()
                    || Math.abs(path.velZ[t]) < exact.inertiaThreshold();
        }
        assertTrue("fixture should exercise at least one 1.8 per-axis inertia threshold check", sawGate);
        System.out.printf("SLP_INERTIA_PROBE route=%s ticks=%d exactViolation=%.3e%n",
                route[0], scenario.numTicks, violation);
    }
}
