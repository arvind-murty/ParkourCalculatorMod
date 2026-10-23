package de.legoshi.parkourcalc.core.anglesolver.solver;

/** Shared acceptance rule for a lookahead window: continuous feasibility over the entire window
 *  with actual inertia checks, plus Minecraft replay feasibility over the committed prefix.
 *  The solver's assumed inertia pattern is deliberately not an input to this check. */
public final class WindowCandidateValidation {
    /** Optional per-thread audit hook. No detailed audit arrays are built unless an observer is installed. */
    public interface DiagnosticObserver { void evaluated(Diagnostics diagnostics); }
    private static final ThreadLocal<DiagnosticObserver> OBSERVER = new ThreadLocal<>();

    public static void setDiagnosticObserver(DiagnosticObserver observer) {
        if (observer == null) OBSERVER.remove();
        else OBSERVER.set(observer);
    }

    public static final class ConstraintMargin {
        public final JumpConstraint constraint;
        public final boolean committed;
        public final double continuousValue, continuousMargin, replayValue, replayMargin;
        public final double assumedValue, assumedMargin;

        private ConstraintMargin(JumpConstraint c, boolean committed, double[] angles, double[] game,
                                 ForwardPath smooth, ForwardPath replay, ForwardPath assumed) {
            this.constraint = c;
            this.committed = committed;
            double continuousResidual = JumpConstraintCompiler.evaluate(c, angles, smooth);
            double replayResidual = JumpConstraintCompiler.evaluate(c, game, replay);
            double assumedResidual = assumed == null ? Double.NaN : JumpConstraintCompiler.evaluate(c, angles, assumed);
            continuousValue = continuousResidual + c.rhs;
            replayValue = replayResidual + c.rhs;
            assumedValue = assumedResidual + c.rhs;
            continuousMargin = signedMargin(c, continuousResidual);
            replayMargin = signedMargin(c, replayResidual);
            assumedMargin = signedMargin(c, assumedResidual);
        }
    }

    public static final class InertiaCheck {
        public final int tick, axis;
        public final double continuousVelocity, replayVelocity, assumedVelocity, assumedThresholdMargin;
        public final boolean continuousZero, replayZero, assumptionKnown, assumedZero, assumptionSatisfied;

        private InertiaCheck(int tick, int axis, ExactJumpModel exact, double[] angles,
                             ForwardPath smooth, ForwardPath replay, JumpLinearModel assumed) {
            this.tick = tick;
            this.axis = axis;
            continuousVelocity = axis == 0 ? smooth.velX[tick] : smooth.velZ[tick];
            replayVelocity = axis == 0 ? replay.velX[tick] : replay.velZ[tick];
            continuousZero = belowThreshold(exact, continuousVelocity, smooth.velX[tick], smooth.velZ[tick]);
            replayZero = belowThreshold(exact, replayVelocity, replay.velX[tick], replay.velZ[tick]);
            assumptionKnown = assumed != null;
            assumedZero = assumed != null && assumed.assumesZero(axis, tick);
            assumedVelocity = assumed == null ? Double.NaN : assumed.velocityBeforeCheck(axis, tick, angles);
            double measure = assumedVelocity;
            if (!exact.perAxisInertia() && assumed != null) {
                measure = StrictMath.hypot(assumed.velocityBeforeCheck(0, tick, angles),
                        assumed.velocityBeforeCheck(1, tick, angles));
            }
            assumedThresholdMargin = assumedZero ? exact.inertiaThreshold() - Math.abs(measure)
                    : Math.abs(measure) - exact.inertiaThreshold();
            assumptionSatisfied = assumptionKnown && (assumedZero ? assumedThresholdMargin > 0 : assumedThresholdMargin >= 0);
        }
    }

    public static final class Diagnostics {
        public final Result result;
        public final String assumedPattern;
        public final double[] continuousFacings, gameFacings;
        public final ForwardPath continuousPath, replayPath, assumedPath;
        public final java.util.List<ConstraintMargin> constraints;
        public final java.util.List<InertiaCheck> inertiaChecks;

        private Diagnostics(Result result, ExactJumpModel exact, JumpConstraintCompiler.Compiled full,
                            JumpConstraintCompiler.Compiled prefix, double[] angles, double[] game,
                            ForwardPath smooth, ForwardPath replay, JumpLinearModel assumed) {
            this.result = result;
            assumedPattern = assumed == null ? "not supplied" : assumed.patternDescription();
            continuousFacings = angles.clone();
            gameFacings = game.clone();
            continuousPath = smooth;
            replayPath = replay;
            assumedPath = assumed == null ? null : assumed.forwardFixed(angles);
            java.util.List<ConstraintMargin> margins = new java.util.ArrayList<>();
            for (JumpConstraint c : full.ineq) margins.add(new ConstraintMargin(c, prefix.ineq.contains(c), angles, game, smooth, replay, assumedPath));
            for (JumpConstraint c : full.eq) margins.add(new ConstraintMargin(c, prefix.eq.contains(c), angles, game, smooth, replay, assumedPath));
            constraints = java.util.Collections.unmodifiableList(margins);
            java.util.List<InertiaCheck> checks = new java.util.ArrayList<>();
            for (int t = 0; t < angles.length; t++) for (int axis = 0; axis < 2; axis++) {
                checks.add(new InertiaCheck(t, axis, exact, angles, smooth, replay, assumed));
            }
            inertiaChecks = java.util.Collections.unmodifiableList(checks);
        }
    }

    private static boolean belowThreshold(ExactJumpModel exact, double component, double vx, double vz) {
        double threshold = exact.inertiaThreshold();
        return exact.perAxisInertia() ? Math.abs(component) < threshold : vx * vx + vz * vz < threshold * threshold;
    }

    private static double signedMargin(JumpConstraint c, double residual) {
        return c.cmp == JumpConstraint.Cmp.GE ? residual : c.cmp == JumpConstraint.Cmp.LE ? -residual : -Math.abs(residual);
    }

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
        return evaluate(continuousFacings, (JumpLinearModel) null);
    }

    /** The optional assumed model is used only for diagnostics, never for acceptance. */
    public Result evaluate(double[] continuousFacings, JumpLinearModel assumed) {
        double[] game = scenario.toGameFacings(continuousFacings);
        return evaluate(continuousFacings, game, exact.forward(scenario, game), assumed);
    }

    /** Reuse a replay already computed by the caller, without converting or simulating it again. */
    public Result evaluate(double[] continuousFacings, double[] gameFacings, ForwardPath replay) {
        return evaluate(continuousFacings, gameFacings, replay, null);
    }

    public Result evaluate(double[] continuousFacings, double[] gameFacings, ForwardPath replay,
                           JumpLinearModel assumed) {
        ForwardPath smooth = continuous.forwardContinuous(continuousFacings,
                exact.inertiaThreshold(), exact.perAxisInertia());
        Result result = new Result(prefix.maxViolation(gameFacings, replay),
                full.maxViolation(continuousFacings, smooth));
        DiagnosticObserver observer = OBSERVER.get();
        if (observer != null) observer.evaluated(new Diagnostics(result, exact, full, prefix,
                continuousFacings, gameFacings, smooth, replay, assumed));
        return result;
    }
}
