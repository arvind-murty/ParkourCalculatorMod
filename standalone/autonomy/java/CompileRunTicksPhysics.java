package de.legoshi.parkourcalc.autonomy;

import com.google.gson.Gson;
import de.legoshi.parkourcalc.core.anglesolver.ConstraintDeriver;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpLinearModel;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpPhysicsInputs;
import de.legoshi.parkourcalc.core.sim.AABB;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.LinkedHashMap;
import java.util.Map;

public final class CompileRunTicksPhysics {
    private static final Gson GSON = new Gson();

    public static final class Policy {
        float forward;
        float strafe;
        boolean sprint;
        boolean incomingSprint;
        boolean force45;
    }

    public static final class Request {
        String action;
        Block[] blocks;
        boolean[] grounded;
        boolean[] jump;
        Policy inputs;
    }

    public static final class Block {
        double[] bounds;
        double[] centerBounds;
        double[] standingBounds;
        double top;
    }

    public static void main(String[] args) throws Exception {
        BufferedReader reader = new BufferedReader(new InputStreamReader(System.in, "UTF-8"));
        String line;
        while ((line = reader.readLine()) != null) {
            Map<String, Object> result;
            try {
                Request request = GSON.fromJson(line, Request.class);
                result = request != null && ("footprints".equals(request.action)
                        || "standingFootprints".equals(request.action)) ? footprints(request) : compile(request);
            } catch (RuntimeException error) {
                result = new LinkedHashMap<String, Object>();
                result.put("error", error.toString());
            }
            System.out.println(GSON.toJson(result));
            System.out.flush();
        }
    }

    private static Map<String, Object> footprints(Request request) {
        if (request.blocks == null || request.blocks.length < 2) {
            throw new IllegalArgumentException("Supply a route of support blocks");
        }
        double[][] footprints = new double[request.blocks.length][];
        for (int i = 0; i < request.blocks.length; i++) {
            Block block = request.blocks[i];
            double[] standing = "standingFootprints".equals(request.action) ? block.standingBounds : null;
            if (standing == null && block.centerBounds != null) {
                footprints[i] = block.centerBounds.clone();
            } else {
                double[] b = standing != null ? standing : block.bounds;
                AABB support = new AABB(new Vec3dCore(b[0], block.top - 1, b[2]),
                                        new Vec3dCore(b[1], block.top, b[3]));
                footprints[i] = ConstraintDeriver.deriveFootprint(support, (b[0] + b[1]) / 2,
                        (b[2] + b[3]) / 2, null, false, 0f);
            }
        }
        Map<String, Object> result = new LinkedHashMap<String, Object>();
        result.put("footprints", footprints);
        result.put("geometryModel", "ConstraintDeriver.deriveFootprint:legacy");
        return result;
    }

    private static Map<String, Object> compile(Request request) {
        if (request == null || request.grounded == null || request.jump == null || request.inputs == null
                || request.grounded.length == 0 || request.jump.length != request.grounded.length) {
            throw new IllegalArgumentException("Invalid movement schedule");
        }
        int n = request.grounded.length;
        JumpPhysicsInputs physics = new JumpPhysicsInputs(n);
        physics.slipPerTick = new double[n];
        physics.jumpPerTick = request.jump.clone();
        physics.forwardInputPerTick = new float[n];
        physics.strafeInputPerTick = new float[n];
        physics.sprintPerTick = new boolean[n];
        physics.strafePerTick = new boolean[n];
        physics.incomingSprint = request.inputs.incomingSprint;
        for (int t = 0; t < n; t++) {
            physics.slipPerTick[t] = request.grounded[t] ? 0.6 : Double.NaN;
            physics.forwardInputPerTick[t] = request.inputs.forward;
            physics.strafeInputPerTick[t] = request.inputs.strafe;
            physics.sprintPerTick[t] = request.inputs.sprint;
            physics.strafePerTick[t] = request.inputs.force45;
        }
        JumpLinearModel model = new JumpLinearModel(physics, null, null, true);
        double[] magnitude = new double[n], friction = new double[n], base = new double[n];
        for (int t = 0; t < n; t++) {
            magnitude[t] = model.mMag(t);
            friction[t] = model.friction(t);
            base[t] = model.baseArg(t);
        }
        Map<String, Object> result = new LinkedHashMap<String, Object>();
        result.put("magnitude", magnitude);
        result.put("friction", friction);
        result.put("base", base);
        result.put("model", "JumpLinearModel:1.8.9:float-friction");
        return result;
    }
}
