package de.legoshi.parkourcalc.core.anglesolver.solver;

import java.util.List;

/** Cheap preflight for a scalar position objective directly capped by a same-tick axis wall.
 *  A match flags possible objective/wall cancellation; it does not prove the wall will be active.
 *  This deliberately handles the common absolute X/Z case, not combinations of relative or facing rows. */
public final class ObjectiveBounds {
    private ObjectiveBounds() { }

    public static JumpConstraint directCap(JumpSpec spec) {
        return directCap(spec.objective, spec.constraints);
    }

    /** Returns the tightest matching wall, or null when this simple check finds no direct cap.
     *  Motion and custom-direction objectives are outside this check's scope. */
    public static JumpConstraint directCap(Objective objective, List<JumpConstraint> constraints) {
        if (objective.isMotion() || objective.isCustomAngle()) return null;
        JumpConstraint.Mode mode = objective.axis == JumpPhysicsInputs.Axis.X
                ? JumpConstraint.Mode.X : JumpConstraint.Mode.Z;
        JumpConstraint.Cmp cap = objective.sense == Objective.Sense.MAX
                ? JumpConstraint.Cmp.LE : JumpConstraint.Cmp.GE;
        JumpConstraint best = null;
        for (JumpConstraint row : constraints) {
            if (row.mode != mode || row.t1 != objective.tick || row.t2 != null) continue;
            if (row.cmp != cap && row.cmp != JumpConstraint.Cmp.EQ) continue;
            if (best == null || (objective.sense == Objective.Sense.MAX ? row.rhs < best.rhs : row.rhs > best.rhs)) {
                best = row;
            }
        }
        return best;
    }
}
