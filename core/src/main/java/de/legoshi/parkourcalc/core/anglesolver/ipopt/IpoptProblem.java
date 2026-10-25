package de.legoshi.parkourcalc.core.anglesolver.ipopt;

import de.legoshi.parkourcalc.core.anglesolver.solver.*;
import de.legoshi.parkourcalc.core.sim.TickState;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class IpoptProblem {
    public final int version = 1;
    public final int ticks;
    public final double[] position;
    public final double[] velocity;
    public final double[] magnitude;
    public final double[] friction;
    public final double[] base;
    public final double[] yaw;
    public final List<JumpConstraint> walls;
    public final Objective objective;
    public final List<Contact> contacts;
    public final double threshold;
    public final double startYaw;
    public final boolean stopOnFeasible;
    public final double seconds;
    private final transient JumpSpec spec;

    public static final class Contact {
        public final int state;
        public final int axis;
        public final double plane;
        public final int direction;

        public Contact(int state, int axis, double plane, int direction) {
            if (state < 1 || axis < 0 || axis > 1 || !Double.isFinite(plane)
                    || (direction != -1 && direction != 1)) throw new IllegalArgumentException("Invalid wall contact");
            this.state = state;
            this.axis = axis;
            this.plane = plane;
            this.direction = direction;
        }
    }

    public IpoptProblem(JumpSpec spec, double[] yaw, List<Contact> contacts,
                        double threshold, double seconds, boolean stopOnFeasible) {
        this.spec = spec;
        JumpPhysicsInputs sc = spec.asScenario();
        ticks = sc.numTicks;
        if (ticks <= 0 || yaw == null || yaw.length != ticks || !Double.isFinite(threshold)
                || threshold < 0 || !Double.isFinite(seconds) || seconds <= 0)
            throw new IllegalArgumentException("Invalid IPOPT solve request");
        if (spec.objective == null || spec.objective.tick < 0 || spec.objective.tick > ticks
                || spec.objective.smoothLambda != 0 || (spec.objective.customYaw != null
                && !Double.isFinite(spec.objective.customYaw)))
            throw new IllegalArgumentException("Invalid or unsupported IPOPT objective");
        this.yaw = yaw.clone();
        this.contacts = new ArrayList<>(contacts);
        this.threshold = threshold;
        this.seconds = seconds;
        this.stopOnFeasible = stopOnFeasible;
        position = new double[] {sc.startPos.x, sc.startPos.z};
        velocity = new double[] {sc.initialVelocity.x, sc.initialVelocity.z};
        for (double value : new double[] {position[0], position[1], velocity[0], velocity[1], sc.startYaw})
            if (!Double.isFinite(value)) throw new IllegalArgumentException("Non-finite start state");
        startYaw = sc.startYaw;
        magnitude = new double[ticks];
        friction = new double[ticks];
        base = new double[ticks];
        JumpLinearModel linear = new JumpLinearModel(sc);
        for (int t = 0; t < ticks; t++) {
            if (!Double.isFinite(yaw[t])) throw new IllegalArgumentException("Non-finite seed facing");
            magnitude[t] = linear.mMag(t);
            friction[t] = linear.friction(t);
            base[t] = linear.baseArg(t);
            if (!Double.isFinite(magnitude[t]) || !Double.isFinite(friction[t]) || !Double.isFinite(base[t]))
                throw new IllegalArgumentException("Non-finite movement coefficients");
        }
        walls = new ArrayList<>(spec.constraints);
        objective = spec.objective;
        JumpConstraintCompiler.compile(spec);
        for (JumpConstraint c : walls) {
            if (!Double.isFinite(c.rhs) || (c.mode == JumpConstraint.Mode.F
                    && (c.t1 >= ticks || (c.t2 != null && c.t2 >= ticks))))
                throw new IllegalArgumentException("Invalid constraint: " + c.name);
        }
        boolean[][] seen = new boolean[ticks + 1][2];
        for (Contact c : contacts) {
            if (c.state > ticks || seen[c.state][c.axis]) throw new IllegalArgumentException("Repeated or out-of-range wall contact");
            seen[c.state][c.axis] = true;
        }
    }

    public ForwardPath replay(double[] facing) {
        if (facing == null || facing.length != ticks) throw new IllegalArgumentException("Wrong facing count");
        double[][] p = new double[2][ticks + 1];
        double[][] v = new double[2][ticks + 1];
        double[] y = new double[ticks + 1];
        p[0][0] = position[0]; p[1][0] = position[1];
        v[0][0] = velocity[0]; v[1][0] = velocity[1];
        Contact[][] schedule = new Contact[ticks + 1][2];
        for (Contact c : contacts) schedule[c.state][c.axis] = c;
        for (int t = 0; t < ticks; t++) {
            if (!Double.isFinite(facing[t])) throw new IllegalArgumentException("Non-finite returned facing");
            double angle = base[t] + Angles.rad(facing[t]);
            for (int a = 0; a < 2; a++) {
                double carry = StrictMath.abs(v[a][t]) < threshold ? 0 : v[a][t];
                double move = carry + magnitude[t] * (a == 0 ? StrictMath.cos(angle) : StrictMath.sin(angle));
                p[a][t + 1] = p[a][t] + move;
                v[a][t + 1] = friction[t] * move;
                Contact c = schedule[t + 1][a];
                if (c != null) {
                    if (c.direction * (p[a][t] - c.plane) > 1e-7
                            || c.direction * (p[a][t + 1] - c.plane) < 0)
                        throw new IllegalArgumentException("Scheduled wall was not reached from its allowed side at state " + c.state);
                    p[a][t + 1] = c.plane;
                    v[a][t + 1] = 0;
                }
                if (!Double.isFinite(p[a][t + 1]) || !Double.isFinite(v[a][t + 1]))
                    throw new IllegalArgumentException("Non-finite trajectory");
            }
        }
        java.util.Arrays.fill(y, spec.asScenario().startPos.y);
        return new ForwardPath(p[0], y, p[1], v[0], new double[ticks + 1], v[1]);
    }

    public double violation(double[] facing, ForwardPath path) {
        return JumpConstraintCompiler.compile(spec).maxViolation(facing, path);
    }

    public double score(double[] facing, ForwardPath path) {
        return objective.scored(objective.evaluate(path), startYaw, facing);
    }

    public static List<Contact> recordedContacts(List<TickState> states, int from, int ticks) {
        List<Contact> result = new ArrayList<>();
        Map<String, Integer> directions = new HashMap<>();
        for (int s = 1; s <= from + ticks && s < states.size(); s++) {
            TickState now = states.get(s), prev = states.get(s - 1);
            if (!now.wallCollision) continue;
            int found = 0;
            for (int a = 0; a < 2; a++) {
                double velocity = a == 0 ? now.velocity.x : now.velocity.z;
                if (velocity != 0) continue;
                double plane = a == 0 ? now.position.x : now.position.z;
                double change = plane - (a == 0 ? prev.position.x : prev.position.z);
                String key = a + ":" + Double.toHexString(plane);
                if (change != 0) directions.put(key, change > 0 ? 1 : -1);
                Integer direction = directions.get(key);
                if (direction == null) continue;
                found++;
                if (s > from) result.add(new Contact(s - from, a, plane, direction));
            }
            if (s > from && found == 0) throw new IllegalArgumentException("Cannot infer recorded wall direction at state " + s);
        }
        return result;
    }
}
