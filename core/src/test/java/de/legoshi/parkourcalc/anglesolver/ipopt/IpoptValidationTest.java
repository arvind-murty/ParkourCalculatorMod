package de.legoshi.parkourcalc.anglesolver.ipopt;

import de.legoshi.parkourcalc.core.anglesolver.ipopt.IpoptProblem;
import de.legoshi.parkourcalc.core.anglesolver.ipopt.LocalIpoptSolver;
import de.legoshi.parkourcalc.core.anglesolver.solver.*;
import de.legoshi.parkourcalc.core.sim.TickState;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import org.junit.Test;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import static org.junit.Assert.*;

public class IpoptValidationTest {
    static JumpSpec spec(int n, List<JumpConstraint> constraints) {
        JumpPhysicsInputs sc = new JumpPhysicsInputs(n);
        sc.startPos = Vec3dCore.ZERO;
        sc.initialVelocity = new Vec3dCore(0.004, 0, 0.1);
        return new JumpSpec(sc, constraints, new Objective(JumpPhysicsInputs.Axis.X, Objective.Sense.MAX, n));
    }

    @Test public void thresholdAndNormalCarryAreIndependent() {
        JumpSpec spec = spec(2, Collections.emptyList());
        IpoptProblem free = new IpoptProblem(spec, new double[2], Collections.emptyList(), 0, 2, false);
        IpoptProblem clamped = new IpoptProblem(spec, new double[2], Collections.emptyList(), 0.005, 2, false);
        ForwardPath a = free.replay(new double[2]), b = clamped.replay(new double[2]);
        assertEquals(0.004, a.posX[1] - b.posX[1], 1e-14);
        assertEquals(a.posZ[1], b.posZ[1], 0);
    }

    @Test public void continuousRecurrenceMatchesExistingLinearModel() {
        JumpSpec spec = spec(5, Collections.emptyList());
        double[] yaw = {15, -20, 90, -170, 179};
        IpoptProblem problem = new IpoptProblem(spec, yaw, Collections.emptyList(), 0, 2, false);
        ForwardPath expected = new JumpLinearModel(spec.asScenario()).forwardContinuous(yaw, 0, true);
        ForwardPath actual = problem.replay(yaw);
        assertArrayEquals(expected.posX, actual.posX, 1e-14);
        assertArrayEquals(expected.posZ, actual.posZ, 1e-14);
        assertArrayEquals(expected.velX, actual.velX, 1e-14);
    }

    @Test public void wallStopsOnlyNormalComponent() {
        JumpSpec spec = spec(2, Collections.emptyList());
        IpoptProblem problem = new IpoptProblem(spec, new double[2],
                Collections.singletonList(new IpoptProblem.Contact(1, 0, 0.002, 1)), 0, 2, false);
        ForwardPath path = problem.replay(new double[2]);
        assertEquals(0.002, path.posX[1], 0);
        assertEquals(0, path.velX[1], 0);
        assertTrue(path.velZ[1] > 0);
        assertTrue(path.posZ[2] > path.posZ[1]);
    }

    @Test public void subtoleranceWallShortfallCannotTriggerReset() {
        JumpSpec spec = spec(1, Collections.emptyList());
        IpoptProblem problem = new IpoptProblem(spec, new double[1],
                Collections.singletonList(new IpoptProblem.Contact(1, 0, 0.004 + 1e-9, 1)), 0, 2, false);
        assertThrows(IllegalArgumentException.class, () -> problem.replay(new double[1]));
    }

    @Test public void allFacingAndRelativeRowsAreValidated() {
        JumpConstraint facing = new JumpConstraint(JumpConstraint.Mode.F, 0, null,
                JumpConstraint.Op.PLUS, JumpConstraint.Cmp.EQ, 15, "facing");
        JumpConstraint turn = new JumpConstraint(JumpConstraint.Mode.F, 1, 0,
                JumpConstraint.Op.MINUS, JumpConstraint.Cmp.EQ, 0, "turn");
        JumpSpec spec = spec(2, Arrays.asList(facing, turn));
        IpoptProblem problem = new IpoptProblem(spec, new double[] {15, 15}, Collections.emptyList(), 0, 2, false);
        assertNotNull(LocalIpoptSolver.validate(problem, new double[] {375, 15}, "test"));
        assertNull(LocalIpoptSolver.validate(problem, new double[] {15, 16}, "test"));
    }

    @Test public void wrongSizeAndNonFiniteCandidatesAreRejected() {
        IpoptProblem problem = new IpoptProblem(spec(1, Collections.emptyList()), new double[1],
                Collections.emptyList(), 0, 2, false);
        assertThrows(IllegalArgumentException.class, () -> problem.replay(new double[2]));
        assertThrows(IllegalArgumentException.class, () -> problem.replay(new double[] {Double.NaN}));
        assertThrows(IllegalArgumentException.class, () -> problem.replay(new double[] {Double.POSITIVE_INFINITY}));
    }

    @Test public void contactInferenceCarriesDirectionAcrossStationaryHitsAndWindowSeam() {
        List<TickState> states = Arrays.asList(state(0, false), state(0.002, true), state(0.002, true));
        List<IpoptProblem.Contact> contacts = IpoptProblem.recordedContacts(states, 1, 1);
        assertEquals(1, contacts.size());
        assertEquals(1, contacts.get(0).state);
        assertEquals(1, contacts.get(0).direction);
        assertEquals(0, contacts.get(0).axis);
    }

    private static TickState state(double x, boolean wall) {
        return new TickState(new Vec3dCore(x, 0, 0), false, false, wall, 0,
                Collections.emptyList(), new Vec3dCore(0, 0, 0.1), false, Double.NaN);
    }
}
