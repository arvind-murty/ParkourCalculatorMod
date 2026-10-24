package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import org.junit.Test;
import java.util.Collections;
import static org.junit.Assert.*;

public class DualCandidateRetentionTest {
    private final ExactJumpModel exact = ExactJumpModel.forMcVersion("1.8.9");

    @Test public void retainsTheBestValidatedCandidateAndCopiesMultipliers() {
        for (Objective.Sense sense : Objective.Sense.values()) {
            JumpPhysicsInputs sc = new JumpPhysicsInputs(1);
            JumpSpec spec = new JumpSpec(sc, Collections.emptyList(), new Objective(JumpPhysicsInputs.Axis.Z, sense, 1));
            JumpLinearModel lin = new JumpLinearModel(sc);
            FacingPrefold pre = FacingPrefold.analyze(spec.constraints, lin);
            DualCandidateRetention retained = new DualCandidateRetention(exact, spec, lin, pre, 1, 0.0);
            double sign = sense == Objective.Sense.MAX ? 1.0 : -1.0;
            double[] weights = {2.0};
            retained.observe(5, new double[]{.8}, new double[]{.6 * sign}, weights);
            assertNotNull(retained.yaws);
            double first = retained.score;
            retained.observe(10, new double[]{0}, new double[]{sign}, weights);
            assertEquals(10, retained.iteration);
            assertTrue(sense == Objective.Sense.MAX ? retained.score > first : retained.score < first);
            double[] best = retained.yaws.clone();
            retained.observe(20, new double[]{0}, new double[]{-sign}, weights);
            assertArrayEquals(best, retained.yaws, 0.0);
            weights[0] = 99.0;
            assertArrayEquals(new double[]{2.0}, retained.multipliers, 0.0);
            assertTrue(retained.preferredTo(best, 1.0));
            assertFalse(retained.preferredTo(best, 0.0));
        }
    }

    @Test public void doesNotRetainAPathWhoseFeasibilityDependsOnFictitiousZeroing() {
        JumpPhysicsInputs sc = new JumpPhysicsInputs(3);
        sc.startPos = new Vec3dCore(0,0,0);
        sc.initialVelocity = new Vec3dCore(.02,0,0);
        sc.forwardInputPerTick = new float[3];
        sc.strafeInputPerTick = new float[3];
        JumpConstraint wall = new JumpConstraint(JumpConstraint.Mode.X,3,null,JumpConstraint.Op.MINUS,JumpConstraint.Cmp.LE,.001,"X");
        JumpSpec spec = new JumpSpec(sc,Collections.singletonList(wall),new Objective(JumpPhysicsInputs.Axis.Z,Objective.Sense.MAX,3));
        JumpLinearModel lin = new JumpLinearModel(sc,new boolean[]{true,false,false},null);
        FacingPrefold pre = FacingPrefold.analyze(spec.constraints,lin);
        DualCandidateRetention retained = new DualCandidateRetention(exact,spec,lin,pre,1,0.0);
        retained.observe(1,new double[3],new double[]{1,1,1},new double[0]);
        assertNull(retained.yaws);
        assertEquals(0,retained.improvements);
    }
}
