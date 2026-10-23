package de.legoshi.parkourcalc.core.anglesolver.solver;

import org.junit.Test;
import java.lang.reflect.Field;
import java.util.Collections;
import static org.junit.Assert.*;

public class CostateSmoothingScaleTest {
    @Test public void derivativeLengthBiasIsSmallAboveTheDirectionFallback() throws Exception {
        Field field = CostateDualSolver.class.getDeclaredField("EPS2");
        field.setAccessible(true);
        double eps2 = field.getDouble(null);
        assertEquals(1e-24, eps2, 0);
        for (double norm : new double[]{1e-9, 2e-9, 1e-8, 1e-7, 1, 100}) {
            double shrink = 1 - norm / StrictMath.sqrt(norm * norm + eps2);
            assertTrue("relative length error at " + norm, shrink >= 0 && shrink <= 5.00001e-7);
        }
    }
    @Test public void bothRecoveryPathsUseObjectiveDirectionOnlyForVanishingCostates() {
        JumpLinearModel lin = new JumpLinearModel(new JumpPhysicsInputs(1));
        FacingPrefold folded = FacingPrefold.analyze(Collections.emptyList(), lin);
        assertNotNull(folded);
        Objective obj = new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 1);
        for (double value : new double[]{-.5e-9, -2e-9}) {
            double expected = lin.recoverYawDeg(0, 0, Math.abs(value) < 1e-9 ? 1 : -1);
            assertEquals(expected, lin.recoverAlongCostate(obj, new double[]{0}, new double[]{value})[0], 0);
            assertEquals(expected, folded.expand(lin, obj, new double[]{0}, new double[]{value})[0], 0);
        }
    }
}
