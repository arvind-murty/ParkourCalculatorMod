package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import org.junit.Test;

import java.util.Collections;

import static org.junit.Assert.*;

public class WindowCandidateValidationTest {
    private final ExactJumpModel exact = ExactJumpModel.forMcVersion("1.8.9");

    private JumpPhysicsInputs idle(double vx, double vz) {
        JumpPhysicsInputs sc = new JumpPhysicsInputs(3);
        sc.startPos = new Vec3dCore(0, 0, 0);
        sc.initialVelocity = new Vec3dCore(vx, 0, vz);
        sc.forwardInputPerTick = new float[3];
        sc.strafeInputPerTick = new float[3];
        return sc;
    }

    private JumpSpec wall(JumpPhysicsInputs sc, JumpConstraint.Cmp cmp, double rhs) {
        JumpConstraint c = new JumpConstraint(JumpConstraint.Mode.X, 3, null,
                JumpConstraint.Op.MINUS, cmp, rhs, "future X");
        return new JumpSpec(sc, Collections.singletonList(c),
                new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 3));
    }

    @Test
    public void rejectsFutureFeasibilityThatDependsOnIgnoringARealThresholdCheck() {
        JumpPhysicsInputs sc = idle(.004, 0);
        JumpSpec spec = wall(sc, JumpConstraint.Cmp.GE, .001);
        double[] angles = new double[3];
        assertEquals(0, new JumpLinearModel(sc).maxConstraintViolation(JumpConstraintCompiler.compile(spec), angles), 0);
        WindowCandidateValidation.Result result = new WindowCandidateValidation(exact, spec, 1).evaluate(angles);
        assertEquals(0, result.replayViolation, 0);
        assertEquals(.001, result.continuousViolation, 0);
    }

    @Test
    public void rejectsFutureFeasibilityThatDependsOnFictitiousZeroing() {
        JumpPhysicsInputs sc = idle(.02, 0);
        JumpSpec spec = wall(sc, JumpConstraint.Cmp.LE, .001);
        double[] angles = new double[3];
        boolean[] fake = {true, false, false};
        assertEquals(0, new JumpLinearModel(sc, fake, null)
                .maxConstraintViolation(JumpConstraintCompiler.compile(spec), angles), 0);
        WindowCandidateValidation.Result result = new WindowCandidateValidation(exact, spec, 1).evaluate(angles);
        assertEquals(0, result.replayViolation, 0);
        assertTrue(result.continuousViolation > .05);
    }

    @Test
    public void continuousPathUsesIndependentStrictThresholdChecksAndRecordsCarryVelocity() {
        JumpPhysicsInputs sc = idle(.005, .004);
        JumpLinearModel lin = new JumpLinearModel(sc);
        boolean[] zx = new boolean[3], zz = new boolean[3];
        lin.zeroingPattern(new double[3], .005, true, zx, zz);
        assertArrayEquals(new boolean[]{false, true, true}, zx);
        assertArrayEquals(new boolean[]{true, true, true}, zz);
        ForwardPath path = lin.forwardContinuous(new double[3], .005, true);
        assertEquals(.005, path.posX[1], 0);
        assertEquals(.005, path.posX[3], 0);
        assertEquals(.005 * .91, path.velX[1], 0);
        assertEquals(0, path.velX[2], 0);
        assertEquals(0, path.posZ[3], 0);
    }
}
