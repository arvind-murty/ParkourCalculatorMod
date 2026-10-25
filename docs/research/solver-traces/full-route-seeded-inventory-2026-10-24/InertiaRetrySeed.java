package de.legoshi.parkourcalc.core.anglesolver.solver;

final class InertiaRetrySeed {
    private static final double MODEL_TOLERANCE = 1e-8;
    private FacingPrefold.Reduced source;
    private double[] multipliers;
    private double[] gx, gz;
    private int authoredCount;

    void capture(ExactJumpModel exact, JumpSpec spec, JumpLinearModel model,
                 FacingPrefold prefold, FacingPrefold.Reduced reduced, int authoredCount,
                 double[] angles, double[] lambda) {
        source = null;
        if (!exact.perAxisInertia() || !prefold.isIdentity()) return;
        double violation = JumpConstraintCompiler.compile(spec).maxViolation(angles, model.forwardFixed(angles));
        for (int t = 0; t < model.n; t++) for (int axis = 0; axis < 2; axis++) {
            if (model.assumesZero(axis, t)) violation = Math.max(violation,
                    Math.max(0.0, Math.abs(model.velocityBeforeCheck(axis, t, angles)) - exact.inertiaThreshold()));
        }
        if (SolverTrace.on()) SolverTrace.log("CF-SEED", "source assumedViolation=%.12g eligible=%s", violation,
                Double.isFinite(violation) && violation <= MODEL_TOLERANCE);
        if (!Double.isFinite(violation) || violation > MODEL_TOLERANCE) return;
        double[] x = reduced.cx.clone(), z = reduced.cz.clone();
        for (int j = 0; j < reduced.walls.size(); j++) {
            JumpLinearModel.Wall wall = reduced.walls.get(j);
            if (wall.axis > 1 || !Double.isFinite(lambda[j]) || (!wall.eq && lambda[j] < 0)) return;
            for (int t = 0; t < reduced.n; t++) {
                if (wall.axis == 0) x[t] -= lambda[j] * wall.coef[t];
                else z[t] -= lambda[j] * wall.coef[t];
            }
        }
        this.source = reduced;
        this.multipliers = lambda.clone();
        this.gx = x;
        this.gz = z;
        this.authoredCount = authoredCount;
    }

    double[] forModel(FacingPrefold.Reduced next, int nextAuthoredCount) {
        if (source == null || source.n != next.n || authoredCount != nextAuthoredCount) return null;
        double[] seed = new double[next.walls.size()];
        double[] dx = next.cx.clone(), dz = next.cz.clone();
        for (int j = 0; j < authoredCount; j++) {
            JumpLinearModel.Wall old = source.walls.get(j), wall = next.walls.get(j);
            if (wall.axis > 1 || old.axis != wall.axis || old.eq != wall.eq || !old.name.equals(wall.name)) return null;
            seed[j] = multipliers[j];
            for (int t = 0; t < next.n; t++) {
                if (wall.axis == 0) dx[t] -= seed[j] * wall.coef[t];
                else dz[t] -= seed[j] * wall.coef[t];
            }
        }
        for (int t = 0; t < next.n; t++) { dx[t] -= gx[t]; dz[t] -= gz[t]; }
        if ((next.walls.size() - authoredCount) % 2 != 0) return null;
        for (int j = authoredCount; j < next.walls.size(); j += 2) {
            JumpLinearModel.Wall hi = next.walls.get(j), lo = next.walls.get(j + 1);
            if (hi.axis > 1 || hi.axis != lo.axis || hi.eq || lo.eq) return null;
            double[] delta = hi.axis == 0 ? dx : dz;
            double dot = 0, norm2 = 0;
            for (int t = 0; t < next.n; t++) {
                if (hi.coef[t] != -lo.coef[t]) return null;
                dot += hi.coef[t] * delta[t];
                norm2 += hi.coef[t] * hi.coef[t];
            }
            double price = norm2 == 0 ? 0 : dot / norm2;
            if (!Double.isFinite(price)) return null;
            seed[j] = Math.max(price, 0);
            seed[j + 1] = Math.max(-price, 0);
            for (int t = 0; t < next.n; t++) delta[t] -= price * hi.coef[t];
        }
        double error = 0;
        for (int t = 0; t < next.n; t++) {
            if (!Double.isFinite(dx[t]) || !Double.isFinite(dz[t])) return null;
            error = Math.max(error, StrictMath.hypot(dx[t], dz[t]));
        }
        if (SolverTrace.on()) SolverTrace.log("CF-SEED", "applied rows=%d costateError=%.3e", seed.length, error);
        return seed;
    }
}
