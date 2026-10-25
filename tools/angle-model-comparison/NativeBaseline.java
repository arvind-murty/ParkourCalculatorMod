package de.legoshi.parkourcalc.core.anglesolver.solver;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

public final class NativeBaseline {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static final Map<JumpPhysicsInputs, JumpLinearModel> MODELS = new IdentityHashMap<>();

    static ForwardPath continuousForward(JumpPhysicsInputs scenario, double[] yaws) {
        return MODELS.computeIfAbsent(scenario, JumpLinearModel::new).forwardContinuous(yaws, 0.0, true);
    }

    public static void main(String[] args) throws Exception {
        JsonArray cases = new JsonParser().parse(new String(Files.readAllBytes(Paths.get(args[0])),
                StandardCharsets.UTF_8)).getAsJsonArray();
        JsonObject report = new JsonObject();
        report.addProperty("baselineRef", args[2]);
        report.add("problemsSha256", cases.get(0).getAsJsonObject().get("problemsSha256"));
        report.addProperty("java", System.getProperty("java.version"));
        report.addProperty("scope", "main dual and SLP adapted to shared continuous no-threshold replay");
        report.addProperty("slpPhase1Calls", 10000);
        report.addProperty("slpTotalCalls", 20000);
        JsonArray results = new JsonArray();
        report.add("results", results);
        for (JsonElement element : cases) {
            JsonObject entry = element.getAsJsonObject();
            JsonObject scenarioJson = entry.getAsJsonObject("scenario");
            JsonArray slip = scenarioJson.remove("slipPerTick").getAsJsonArray();
            JumpPhysicsInputs scenario = GSON.fromJson(scenarioJson, JumpPhysicsInputs.class);
            scenario.slipPerTick = new double[scenario.numTicks];
            for (int t = 0; t < scenario.numTicks; t++) {
                scenario.slipPerTick[t] = slip.get(t).isJsonNull() ? Double.NaN : slip.get(t).getAsDouble();
            }
            List<JumpConstraint> constraints = Arrays.asList(GSON.fromJson(entry.get("walls"), JumpConstraint[].class));
            Objective objective = GSON.fromJson(entry.get("objective"), Objective.class);
            JumpSpec spec = new JumpSpec(scenario, constraints, objective);
            JumpLinearModel linear = new JumpLinearModel(scenario);
            MODELS.put(scenario, linear);
            int n = scenario.numTicks;
            double[] cx = new double[n], cz = new double[n];
            linear.objectiveVectors(objective, cx, cz);
            boolean[] contradiction = {false};
            List<JumpLinearModel.Wall> walls = linear.compileWalls(constraints, 0.0, contradiction);
            long started = System.nanoTime();
            CostateDualSolver dual = new CostateDualSolver(n, cx, cz, linear.mMagAll(), walls);
            CostateDualSolver.Result solved = dual.solve(0.0, null);
            double[] dualYaw = null;
            if (solved != null) {
                dualYaw = new double[n];
                for (int t = 0; t < n; t++) {
                    double x = solved.gx[t], z = solved.gz[t];
                    if (x * x + z * z < 1.0e-18) {
                        double sign = objective.sense == Objective.Sense.MAX ? 1.0 : -1.0;
                        if (objective.isCustomAngle()) {
                            x = -sign * StrictMath.sin(Angles.rad(objective.customYaw));
                            z = sign * StrictMath.cos(Angles.rad(objective.customYaw));
                        } else {
                            x = objective.axis == JumpPhysicsInputs.Axis.X ? sign : 0.0;
                            z = objective.axis == JumpPhysicsInputs.Axis.Z ? sign : 0.0;
                        }
                    }
                    dualYaw[t] = linear.recoverYawDeg(t, x, z);
                }
            }
            results.add(result(entry, spec, "main-dual", -1, dualYaw, started, dual.lastIters));
            JsonArray seeds = entry.getAsJsonArray("nativeSeeds");
            for (int seed = 0; seed <= seeds.size(); seed++) {
                double[] yaw = seed == seeds.size() ? dualYaw : GSON.fromJson(seeds.get(seed), double[].class);
                if (yaw == null) continue;
                started = System.nanoTime();
                double[] answer = MainContinuousSlp.optimizeBestEffort(new ExactJumpModel(0.0, true, false),
                        spec, 1.0e-7, new AtomicBoolean(false), yaw, 10000, 20000, false);
                results.add(result(entry, spec, "main-slp", seed == seeds.size() ? -1 : seed,
                        answer, started, -1));
            }
            Files.write(Paths.get(args[1]), GSON.toJson(report).getBytes(StandardCharsets.UTF_8));
        }
    }

    private static JsonObject result(JsonObject entry, JumpSpec spec, String method, int seed,
                                     double[] yaw, long started, int iterations) {
        double elapsed = (System.nanoTime() - started) / 1.0e9;
        JsonObject out = new JsonObject();
        out.addProperty("case", entry.get("name").getAsString());
        out.addProperty("method", method);
        out.addProperty("seed", seed);
        out.addProperty("solveSeconds", elapsed);
        out.addProperty("iterations", iterations);
        out.addProperty("returned", yaw != null);
        if (yaw != null) {
            ForwardPath path = continuousForward(spec.asScenario(), yaw);
            double violation = JumpConstraintCompiler.compile(spec).maxViolation(yaw, path);
            out.addProperty("wallViolation", violation);
            out.addProperty("feasible", violation <= 1.0e-7);
            out.addProperty("objective", spec.objective.evaluate(path));
            out.add("yaw", GSON.toJsonTree(yaw));
            double[][] positions = new double[yaw.length + 1][2];
            for (int t = 0; t < positions.length; t++) positions[t] = new double[]{path.posX[t], path.posZ[t]};
            out.add("positions", GSON.toJsonTree(positions));
        }
        System.out.println(out.get("case") + " " + method + " seed=" + seed + " feasible="
                + out.get("feasible") + " seconds=" + elapsed);
        return out;
    }
}
