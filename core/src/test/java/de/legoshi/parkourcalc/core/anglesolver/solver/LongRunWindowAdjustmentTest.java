package de.legoshi.parkourcalc.core.anglesolver.solver;

import org.junit.Test;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import static org.junit.Assert.*;

public class LongRunWindowAdjustmentTest {
    private JumpSpec spec(int... upperWallTicks) {
        List<JumpConstraint> rows = new ArrayList<>();
        for (int t : upperWallTicks) rows.add(new JumpConstraint(JumpConstraint.Mode.Z, t, null,
                JumpConstraint.Op.MINUS, JumpConstraint.Cmp.LE, 10, "wall"));
        return new JumpSpec(new JumpPhysicsInputs(100), rows,
                new Objective(JumpPhysicsInputs.Axis.X, Objective.Sense.MAX, 100));
    }
    private int end(JumpSpec spec, int routeEnd, LongRunSolver.WindowAdjustment policy) throws Exception {
        Method m = LongRunSolver.class.getDeclaredMethod("adjustedEnd", JumpSpec.class,
                int.class, int.class, int.class, int.class, float.class, LongRunSolver.WindowAdjustment.class);
        m.setAccessible(true);
        return (Integer) m.invoke(null, spec, 30, 68, 71, routeEnd, 0f, policy);
    }
    @Test public void prefersTheConfiguredForwardSize() throws Exception {
        assertEquals(71, end(spec(68), 100, LongRunSolver.WindowAdjustment.FORWARD_JUMP));
        assertEquals(69, end(spec(68), 100, LongRunSolver.WindowAdjustment.FORWARD_TICK));
    }
    @Test public void triesTheOtherForwardSizeIfPreferredEndIsCapped() throws Exception {
        assertEquals(69, end(spec(68, 71), 100, LongRunSolver.WindowAdjustment.FORWARD_JUMP));
        assertEquals(71, end(spec(68, 69), 100, LongRunSolver.WindowAdjustment.FORWARD_TICK));
        assertEquals(68, end(spec(68, 69, 71), 100, LongRunSolver.WindowAdjustment.FORWARD_TICK));
    }
    @Test public void leavesUncappedAndDisabledWindowsAlone() throws Exception {
        assertEquals(68, end(spec(), 100, LongRunSolver.WindowAdjustment.FORWARD_JUMP));
        assertEquals(68, end(spec(68), 100, LongRunSolver.WindowAdjustment.NONE));
    }
    @Test public void preservesTheTerminalEndpointForTheRealObjective() throws Exception {
        assertEquals(69, end(spec(68), 71, LongRunSolver.WindowAdjustment.FORWARD_JUMP));
        assertEquals(68, end(spec(68), 69, LongRunSolver.WindowAdjustment.FORWARD_TICK));
    }
    @Test public void configurationCopiesPreserveBothOverrides() throws Exception {
        for (LongRunSolver.LongRunConfig c : new LongRunSolver.LongRunConfig[]{
                LongRunSolver.LongRunConfig.of(7, 3).withFixedMargin(.0003)
                        .withWindowAdjustment(LongRunSolver.WindowAdjustment.FORWARD_JUMP),
                LongRunSolver.LongRunConfig.of(7, 3).withWindowAdjustment(LongRunSolver.WindowAdjustment.FORWARD_JUMP)
                        .withFixedMargin(.0003)}) {
            Field margin = c.getClass().getDeclaredField("fixedMargin"); margin.setAccessible(true);
            Field policy = c.getClass().getDeclaredField("adjustment"); policy.setAccessible(true);
            assertEquals(.0003, (Double) margin.get(c), 0);
            assertEquals(LongRunSolver.WindowAdjustment.FORWARD_JUMP, policy.get(c));
            assertArrayEquals(new int[]{3, 1}, c.commitLadder);
        }
    }
}
