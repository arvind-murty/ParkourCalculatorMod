package de.legoshi.parkourcalc.core.anglesolver.solver;

/** Shared acceptance rule for a lookahead window: continuous feasibility over the entire window
 *  with actual inertia checks, plus Minecraft replay feasibility over the committed prefix.
 *  The solver's assumed inertia pattern is deliberately not an input to this check. */
public final class WindowCandidateValidation {
    private final ExactJumpModel exact;
    private final JumpPhysicsInputs scenario;
    private final JumpLinearModel continuous;
    private final JumpConstraintCompiler.Compiled full;
    private final JumpConstraintCompiler.Compiled prefix;

    public static final class Result {
        public final double replayViolation;
        public final double continuousViolation;

        private Result(double replayViolation, double continuousViolation) {
            this.replayViolation = replayViolation;
            this.continuousViolation = continuousViolation;
        }

        public double maxViolation() {
            return Math.max(replayViolation, continuousViolation);
        }
    }

    public WindowCandidateValidation(ExactJumpModel exact, JumpSpec spec, int committedInputTicks) {
        this.exact = exact;
        this.scenario = spec.asScenario();
        this.continuous = new JumpLinearModel(scenario);
        this.full = JumpConstraintCompiler.compile(spec);
        this.prefix = JumpConstraintCompiler.compile(spec, committedInputTicks);
    }

    public Result evaluate(double[] continuousFacings) {
        double[] game = scenario.toGameFacings(continuousFacings);
        return evaluate(continuousFacings, game, exact.forward(scenario, game));
    }

    /** Reuse a replay already computed by the caller, without converting or simulating it again. */
    public Result evaluate(double[] continuousFacings, double[] gameFacings, ForwardPath replay) {
        ForwardPath smooth = continuous.forwardContinuous(continuousFacings,
                exact.inertiaThreshold(), exact.perAxisInertia());
        return new Result(prefix.maxViolation(gameFacings, replay),
                full.maxViolation(continuousFacings, smooth));
    }
}
