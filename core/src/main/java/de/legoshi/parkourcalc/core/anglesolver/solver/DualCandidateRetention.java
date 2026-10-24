package de.legoshi.parkourcalc.core.anglesolver.solver;

final class DualCandidateRetention implements CostateDualSolver.CandidateObserver {
    private final JumpSpec spec;
    private final JumpLinearModel assumed;
    private final JumpLinearModel physical;
    private final FacingPrefold prefold;
    private final ExactJumpModel exact;
    private final JumpConstraintCompiler.Compiled constraints;
    private final WindowCandidateValidation validation;
    private final double tolerance;
    double[] yaws;
    double[] multipliers;
    double score;
    double violation;
    double replayViolation;
    double continuousViolation;
    int iteration = -1;
    int improvements;
    long evaluationNanos;

    DualCandidateRetention(ExactJumpModel exact, JumpSpec spec, JumpLinearModel assumed,
                           FacingPrefold prefold, int replayTicks, double tolerance) {
        this.exact = exact;
        this.spec = spec;
        this.assumed = assumed;
        this.physical = new JumpLinearModel(spec.asScenario());
        this.prefold = prefold;
        this.constraints = JumpConstraintCompiler.compile(spec);
        this.validation = new WindowCandidateValidation(exact, spec, replayTicks);
        this.tolerance = tolerance;
    }

    @Override public void observe(int iteration, double[] gx, double[] gz, double[] lambda) {
        long started = System.nanoTime();
        try {
            double[] candidate = prefold.expand(assumed, spec.objective, gx, gz);
            ForwardPath path = physical.forwardContinuous(candidate, exact.inertiaThreshold(), exact.perAxisInertia());
            double continuousViolation = constraints.maxViolation(candidate, path);
            if (!Double.isFinite(continuousViolation) || continuousViolation > tolerance) return;
            double candidateScore = score(candidate, path);
            if (!Double.isFinite(candidateScore) || !better(candidateScore)) return;
            WindowCandidateValidation.Result check = validation.evaluate(candidate, assumed);
            if (!Double.isFinite(check.maxViolation()) || check.maxViolation() > tolerance) return;
            this.yaws = candidate;
            this.multipliers = lambda.clone();
            this.score = candidateScore;
            this.violation = check.maxViolation();
            this.replayViolation = check.replayViolation;
            this.continuousViolation = check.continuousViolation;
            this.iteration = iteration;
            this.improvements++;
            if (SolverTrace.on()) SolverTrace.log("CF-INCUMBENT", "iteration=%d score=%.12g violation=%.3e", iteration, score, violation);
        } finally {
            evaluationNanos += System.nanoTime() - started;
        }
    }

    boolean better(double candidateScore) {
        return yaws == null || (spec.objective.sense == Objective.Sense.MAX
                ? candidateScore > score : candidateScore < score);
    }

    double score(double[] candidate, ForwardPath path) {
        return spec.objective.scored(spec.objective.evaluate(path), spec.asScenario().startYaw, candidate);
    }

    boolean preferredTo(double[] candidate, double candidateViolation) {
        if (yaws == null) return false;
        if (!Double.isFinite(candidateViolation) || candidateViolation > tolerance) return true;
        ForwardPath path = physical.forwardContinuous(candidate, exact.inertiaThreshold(), exact.perAxisInertia());
        double candidateScore = score(candidate, path);
        return !Double.isFinite(candidateScore) || (spec.objective.sense == Objective.Sense.MAX
                ? score > candidateScore : score < candidateScore);
    }
}
