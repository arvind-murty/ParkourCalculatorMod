package de.legoshi.parkourcalc.anglesolver.harness;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.anglesolver.solver.ExactJumpModel;
import de.legoshi.parkourcalc.core.anglesolver.solver.ForwardPath;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpConstraint;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpLinearModel;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpPhysicsInputs;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpSpec;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.ui.InputData;
import de.legoshi.parkourcalc.core.ui.InputRow;
import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.zip.GZIPInputStream;
import java.io.ByteArrayOutputStream;

public final class ExportContinuousModel {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();

    public static void main(String[] args) throws Exception {
        Path root = Paths.get(args[0]).toAbsolutePath();
        JsonArray manifest = new JsonParser().parse(new String(Files.readAllBytes(Paths.get(args[1])),
                StandardCharsets.UTF_8)).getAsJsonArray();
        JsonArray cases = new JsonArray();
        for (JsonElement element : manifest) {
            JsonObject entry = element.getAsJsonObject();
            cases.add(export(root, entry));
        }
        Files.write(Paths.get(args[2]), GSON.toJson(cases).getBytes(StandardCharsets.UTF_8));
        System.out.println("Exported " + cases.size() + " continuous fixed-start cases");
    }

    private static JsonObject export(Path root, JsonObject entry) throws Exception {
        Path source = root.resolve(entry.get("source").getAsString());
        byte[] bytes = Files.readAllBytes(source);
        byte[] json = bytes;
        if (source.toString().endsWith(".gz")) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            try (GZIPInputStream in = new GZIPInputStream(new ByteArrayInputStream(bytes))) {
                byte[] buffer = new byte[8192];
                int count;
                while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            }
            json = out.toByteArray();
        }
        SaveFile file = SaveIO.parseSafe(new String(json, StandardCharsets.UTF_8));
        if (file == null || file.angleSolver == null || file.angleSolver.seed == null) {
            throw new IllegalArgumentException("Missing capture state: " + source);
        }
        if (file.mcVersion == null || !file.mcVersion.startsWith("1.8")) {
            throw new IllegalArgumentException("Expected a Minecraft 1.8 capture: " + source);
        }
        InputData inputs = new InputData();
        SaveIO.applyRowsTo(file, inputs);
        AngleSolverState state = new AngleSolverState();
        SaveIO.applyAngleSolverTo(file, state);
        state.setLegalMode(false);
        state.setSmoothLambda(0.0);
        AngleSolverEngine engine = new AngleSolverEngine(state, Fixtures.buildBoxes(file), inputs,
                tick -> { }, ExactJumpModel.forMcVersion(file.mcVersion));
        JumpSpec spec = engine.debugBuildSpec();
        if (spec == null) throw new IllegalStateException("Cannot compile " + source);
        JumpPhysicsInputs physics = spec.asScenario().copy();
        physics.startBox = null;
        JumpLinearModel linear = new JumpLinearModel(physics);
        int n = entry.has("ticks") ? entry.get("ticks").getAsInt() : physics.numTicks;
        if (n <= 0 || n > physics.numTicks) throw new IllegalArgumentException("Invalid prefix: " + n);
        double[] magnitude = new double[n];
        double[] friction = new double[n];
        double[] base = new double[n];
        double[] yaw = new double[physics.numTicks];
        double previous = physics.startYaw;
        for (int t = 0; t < physics.numTicks; t++) {
            InputRow row = inputs.getRows().get(state.getStartTick() + t);
            if (row.getYaw() != null) previous = row.isYawLocked() ? row.getYaw() : previous + row.getYaw();
            yaw[t] = previous;
            if (t < n) {
                magnitude[t] = linear.mMag(t);
                friction[t] = linear.friction(t);
                base[t] = linear.baseArg(t);
            }
        }
        List<JumpConstraint> walls = new ArrayList<>();
        int droppedFacing = 0;
        for (JumpConstraint wall : spec.constraints) {
            if (wall.t1 > n || (wall.t2 != null && wall.t2 > n)) continue;
            if (wall.mode == JumpConstraint.Mode.F) {
                droppedFacing++;
                continue;
            }
            if (wall.mode != JumpConstraint.Mode.X && wall.mode != JumpConstraint.Mode.Z) {
                throw new IllegalArgumentException("Nonlinear position row in " + source + ": " + wall.name);
            }
            walls.add(wall);
        }
        JsonObject result = new JsonObject();
        result.addProperty("name", entry.get("name").getAsString());
        result.addProperty("source", entry.get("source").getAsString());
        result.addProperty("sha256", hex(MessageDigest.getInstance("SHA-256").digest(bytes)));
        result.addProperty("ticks", n);
        result.addProperty("originalTicks", physics.numTicks);
        result.addProperty("startTick", state.getStartTick());
        result.addProperty("omittedFacingRows", droppedFacing);
        result.addProperty("inputMode", state.getDefaultInputs().name());
        result.addProperty("sprintMode", state.getDefaultSprint().name());
        result.add("position", GSON.toJsonTree(new double[]{physics.startPos.x, physics.startPos.z}));
        result.add("velocity", GSON.toJsonTree(new double[]{physics.initialVelocity.x, physics.initialVelocity.z}));
        result.add("magnitude", GSON.toJsonTree(magnitude));
        result.add("friction", GSON.toJsonTree(friction));
        result.add("base", GSON.toJsonTree(base));
        result.add("yaw", GSON.toJsonTree(Arrays.copyOf(yaw, n)));
        result.add("walls", GSON.toJsonTree(walls));
        JsonObject objective = GSON.toJsonTree(spec.objective).getAsJsonObject();
        objective.addProperty("tick", n);
        if (entry.has("axis")) {
            objective.remove("customYaw");
            objective.addProperty("axis", entry.get("axis").getAsString());
            objective.addProperty("type", "POSITION");
        }
        if (entry.has("sense")) objective.addProperty("sense", entry.get("sense").getAsString());
        result.add("objective", objective);
        JsonArray oracles = new JsonArray();
        Random random = new Random(42);
        for (int sample = 0; sample < 3; sample++) {
            double[] testYaw = yaw.clone();
            if (sample > 0) {
                for (int t = 0; t < testYaw.length; t++) testYaw[t] = sample == 1 ? 0.0 : random.nextDouble() * 360.0 - 180.0;
            }
            ForwardPath path = linear.forwardContinuous(testYaw, 0.0, true);
            double[][] positions = new double[n + 1][2];
            for (int t = 0; t <= n; t++) {
                positions[t][0] = path.posX[t];
                positions[t][1] = path.posZ[t];
            }
            JsonObject oracle = new JsonObject();
            oracle.add("yaw", GSON.toJsonTree(Arrays.copyOf(testYaw, n)));
            oracle.add("positions", GSON.toJsonTree(positions));
            oracles.add(oracle);
        }
        result.add("oracles", oracles);
        return result;
    }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder();
        for (byte value : bytes) out.append(String.format("%02x", value & 255));
        return out.toString();
    }
}
