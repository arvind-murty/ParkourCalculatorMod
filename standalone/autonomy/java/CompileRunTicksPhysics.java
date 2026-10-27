package de.legoshi.parkourcalc.autonomy;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import de.legoshi.parkourcalc.core.anglesolver.ConstraintDeriver;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpLinearModel;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpPhysicsInputs;
import de.legoshi.parkourcalc.core.sim.AABB;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.replay.StaticScene;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.ArrayList;
import java.util.List;

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
        JsonObject scene;
        double[][] positions;
        double[][] movement;
        boolean[] expectedGround;
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
                result = request != null && "sceneCheck".equals(request.action) ? sceneCheck(request)
                        : request != null && ("footprints".equals(request.action)
                        || "standingFootprints".equals(request.action)) ? footprints(request) : compile(request);
            } catch (RuntimeException error) {
                result = new LinkedHashMap<String, Object>();
                result.put("error", error.toString());
            }
            System.out.println(GSON.toJson(result));
            System.out.flush();
        }
    }

    private static Map<String, Object> sceneCheck(Request request) {
        StaticScene scene = new StaticScene(request.scene);
        int n = request.movement.length;
        if (request.positions.length != n + 1 || request.expectedGround.length != n + 1) {
            throw new IllegalArgumentException("Scene check arrays have inconsistent lengths");
        }
        Map<String, Object> result = new LinkedHashMap<>();
        AABB body = scene.startBox(vector(request.positions[0]));
        for (int t = 0; t < n; t++) {
            StaticScene.Contact hit;
            try {
                hit = scene.move(body, vector(request.movement[t]), request.expectedGround[t], false);
            } catch (IllegalArgumentException error) {
                result.put("feasible", false);
                result.put("tick", t);
                result.put("reason", "scene_query");
                result.put("message", error.getMessage());
                return result;
            }
            Vec3dCore expected = vector(request.positions[t + 1]);
            double error = StrictMath.max(StrictMath.abs(hit.position.x - expected.x),
                    StrictMath.max(StrictMath.abs(hit.position.y - expected.y), StrictMath.abs(hit.position.z - expected.z)));
            if (hit.horizontalCollision || error > 1e-7 || hit.onGround != request.expectedGround[t + 1]) {
                result.put("feasible", false);
                result.put("tick", t);
                result.put("reason", hit.stepped ? "unplanned_step" : "scene_collision");
                result.put("positionError", error);
                result.put("actualPosition", new double[]{hit.position.x, hit.position.y, hit.position.z});
                List<Map<String, Object>> clips = new ArrayList<>();
                for (StaticScene.Clip clip : hit.clips) {
                    if (clip.axis == 1 && StrictMath.abs(hit.position.y - expected.y) <= 1e-7
                            && hit.onGround == request.expectedGround[t + 1]) continue;
                    Map<String, Object> item = new LinkedHashMap<>();
                    AABB b = clip.obstacle;
                    item.put("box", new double[]{b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z});
                    item.put("axis", clip.axis);
                    clips.add(item);
                }
                result.put("clips", clips);
                return result;
            }
            body = hit.box;
        }
        result.put("feasible", true);
        result.put("ticks", n);
        return result;
    }

    private static Vec3dCore vector(double[] values) {
        if (values.length != 3) throw new IllegalArgumentException("Expected X/Y/Z");
        for (double value : values) if (!Double.isFinite(value)) throw new IllegalArgumentException("Nonfinite scene check");
        return new Vec3dCore(values[0], values[1], values[2]);
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
