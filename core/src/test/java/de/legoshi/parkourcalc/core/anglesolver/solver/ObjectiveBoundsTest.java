package de.legoshi.parkourcalc.core.anglesolver.solver;

import org.junit.Test;
import java.util.Arrays;
import java.util.Collections;
import static org.junit.Assert.*;

public class ObjectiveBoundsTest {
    private JumpConstraint row(JumpConstraint.Mode mode, int tick, Integer other, JumpConstraint.Cmp cmp, double rhs) {
        return new JumpConstraint(mode, tick, other, JumpConstraint.Op.MINUS, cmp, rhs, "row");
    }
    @Test public void maxZFindsOnlyUpperWallsOnTheObjectiveState() {
        Objective objective = new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 68);
        JumpConstraint match = row(JumpConstraint.Mode.Z, 68, null, JumpConstraint.Cmp.LE, 5);
        assertSame(match, ObjectiveBounds.directCap(objective, Arrays.asList(
                row(JumpConstraint.Mode.Z, 68, null, JumpConstraint.Cmp.GE, 1),
                row(JumpConstraint.Mode.X, 68, null, JumpConstraint.Cmp.LE, 2),
                row(JumpConstraint.Mode.Z, 67, null, JumpConstraint.Cmp.LE, 3),
                row(JumpConstraint.Mode.Z, 68, 67, JumpConstraint.Cmp.LE, 4), match)));
    }
    @Test public void minXFindsTheStrongestLowerWall() {
        Objective objective = new Objective(JumpPhysicsInputs.Axis.X, Objective.Sense.MIN, 5);
        JumpConstraint match = row(JumpConstraint.Mode.X, 5, null, JumpConstraint.Cmp.GE, 3);
        assertSame(match, ObjectiveBounds.directCap(objective, Arrays.asList(
                row(JumpConstraint.Mode.X, 5, null, JumpConstraint.Cmp.GE, 1), match,
                row(JumpConstraint.Mode.X, 5, null, JumpConstraint.Cmp.LE, 4))));
    }
    @Test public void maxXFindsTheStrongestUpperWall() {
        Objective objective = new Objective(JumpPhysicsInputs.Axis.X, Objective.Sense.MAX, 5);
        JumpConstraint match = row(JumpConstraint.Mode.X, 5, null, JumpConstraint.Cmp.LE, 2);
        assertSame(match, ObjectiveBounds.directCap(objective, Arrays.asList(
                row(JumpConstraint.Mode.X, 5, null, JumpConstraint.Cmp.LE, 4), match)));
    }
    @Test public void equalityCapsEitherSenseAndTheSpecOverloadUsesIt() {
        JumpPhysicsInputs sc = new JumpPhysicsInputs(5);
        JumpConstraint pin = row(JumpConstraint.Mode.Z, 5, null, JumpConstraint.Cmp.EQ, 2);
        for (Objective.Sense sense : Objective.Sense.values()) {
            assertSame(pin, ObjectiveBounds.directCap(new JumpSpec(sc, Collections.singletonList(pin),
                    new Objective(JumpPhysicsInputs.Axis.Z, sense, 5))));
        }
    }
    @Test public void oppositeWallAndPairedRowsDoNotMatch() {
        Objective objective = new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 5);
        assertNull(ObjectiveBounds.directCap(objective, Arrays.asList(
                row(JumpConstraint.Mode.Z, 5, null, JumpConstraint.Cmp.GE, 1),
                row(JumpConstraint.Mode.Z, 5, 4, JumpConstraint.Cmp.LE, 2))));
    }
    @Test public void motionAndCustomDirectionsAreOutsideThisSimpleCheck() {
        JumpConstraint wall = row(JumpConstraint.Mode.Z, 5, null, JumpConstraint.Cmp.LE, 2);
        assertNull(ObjectiveBounds.directCap(new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX,
                5, 0, null, Objective.Type.MOTION), Collections.singletonList(wall)));
        assertNull(ObjectiveBounds.directCap(new Objective(0, 5, 0), Collections.singletonList(wall)));
    }
}
