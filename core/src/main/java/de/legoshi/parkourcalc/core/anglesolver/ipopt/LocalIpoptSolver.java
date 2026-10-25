package de.legoshi.parkourcalc.core.anglesolver.ipopt;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import de.legoshi.parkourcalc.core.anglesolver.solver.ForwardPath;
import de.legoshi.parkourcalc.core.anglesolver.solver.Objective;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

public final class LocalIpoptSolver {
    public static final double FEASIBILITY_TOLERANCE = 1e-7;
    private static final Gson GSON = new Gson();

    public static final class Result {
        public final double[] yaw;
        public final ForwardPath path;
        public final double violation;
        public final String status;

        Result(double[] yaw, ForwardPath path, double violation, String status) {
            this.yaw = yaw;
            this.path = path;
            this.violation = violation;
            this.status = status;
        }
    }

    public static String pythonExecutable() {
        String value = System.getProperty("pkc.ipopt.python");
        if (value == null || value.trim().isEmpty()) value = System.getenv("PKC_IPOPT_PYTHON");
        return value == null || value.trim().isEmpty() ? "python3" : value;
    }

    public static Result validate(IpoptProblem problem, double[] yaw, String status) {
        ForwardPath path = problem.replay(yaw);
        double violation = problem.violation(yaw, path);
        if (!Double.isFinite(violation) || violation > FEASIBILITY_TOLERANCE
                || !Double.isFinite(problem.score(yaw, path))) return null;
        return new Result(yaw.clone(), path, violation, status);
    }

    public Result solve(IpoptProblem problem, AtomicBoolean cancel, Consumer<Result> incumbent) throws IOException {
        return solve(problem, cancel, incumbent, pythonExecutable());
    }

    public Result solve(IpoptProblem problem, AtomicBoolean cancel, Consumer<Result> incumbent,
                        String python) throws IOException {
        long deadline = System.nanoTime() + (long) (problem.seconds * 1_000_000_000L);
        Path worker = Files.createTempFile("pkc-ipopt-", ".py");
        Process process = null;
        Result best = null;
        String status = "No feasible solution found";
        StringBuilder errors = new StringBuilder();
        LinkedBlockingQueue<String> messages = new LinkedBlockingQueue<>(64);
        try {
            try (InputStream resource = LocalIpoptSolver.class.getResourceAsStream("worker.py")) {
                if (resource == null) throw new IOException("Missing local IPOPT worker resource");
                Files.copy(resource, worker, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            }
            if (cancel.get()) return null;
            java.util.List<String> command = new java.util.ArrayList<>();
            String architecture = System.getProperty("pkc.ipopt.arch", System.getenv("PKC_IPOPT_ARCH"));
            if (architecture != null && !architecture.trim().isEmpty()) {
                if (!System.getProperty("os.name").startsWith("Mac")
                        || !("arm64".equals(architecture) || "x86_64".equals(architecture)))
                    throw new IOException("IPOPT worker architecture must be arm64 or x86_64 on macOS");
                command.add("/usr/bin/arch");
                command.add("-" + architecture);
            }
            command.add(python);
            command.add("-u");
            command.add(worker.toString());
            ProcessBuilder builder = new ProcessBuilder(command);
            builder.environment().put("OPENBLAS_NUM_THREADS", "1");
            builder.environment().put("OMP_NUM_THREADS", "1");
            process = builder.start();
            Thread output = reader(process.getInputStream(), line -> {
                if (!messages.offer(line)) { messages.poll(); messages.offer(line); }
            });
            Thread error = reader(process.getErrorStream(), line -> {
                synchronized (errors) {
                    if (errors.length() < 8192) errors.append(line).append('\n');
                }
            });
            Process running = process;
            Thread writer = new Thread(() -> {
                try (Writer input = new OutputStreamWriter(running.getOutputStream(), StandardCharsets.UTF_8)) {
                    input.write(GSON.toJson(problem));
                    input.write('\n');
                } catch (IOException errorWriting) {
                    synchronized (errors) {
                        if (errors.length() < 8192) errors.append(errorWriting.getMessage());
                    }
                }
            }, "ipopt-request-writer");
            writer.setDaemon(true);
            writer.start();
            while (process.isAlive() || output.isAlive() || !messages.isEmpty()) {
                if (cancel.get()) return null;
                if (System.nanoTime() >= deadline) { status = "Time limit reached"; break; }
                String line = messages.poll(25, TimeUnit.MILLISECONDS);
                if (line == null) continue;
                JsonObject message;
                try {
                    message = new JsonParser().parse(line).getAsJsonObject();
                    if (!message.has("type") || !message.get("type").isJsonPrimitive())
                        throw new IllegalArgumentException("Missing response type");
                } catch (RuntimeException badProtocol) {
                    throw new IOException("Invalid response from local IPOPT worker", badProtocol);
                }
                String type = message.get("type").getAsString();
                if ("candidate".equals(type)) {
                    Result candidate;
                    try {
                        candidate = validate(problem, GSON.fromJson(message.get("yaw"), double[].class), "Validated continuous candidate");
                    } catch (RuntimeException rejected) {
                        continue;
                    }
                    if (candidate != null && (best == null || better(problem, candidate, best))) {
                        best = candidate;
                        incumbent.accept(best);
                        if (problem.stopOnFeasible) { status = "Stopped at first validated feasible solution"; break; }
                    }
                } else if ("done".equals(type)) {
                    if (!message.has("status")) throw new IOException("Missing worker status");
                    status = message.get("status").getAsString();
                } else if ("error".equals(type)) {
                    if (!message.has("message")) throw new IOException("Missing worker error message");
                    status = "Worker error: " + message.get("message").getAsString();
                } else throw new IOException("Unknown local IPOPT worker response: " + type);
            }
            error.join(100);
            if (cancel.get()) return null;
            if (best != null) return new Result(best.yaw, best.path, best.violation, status);
            synchronized (errors) {
                if (errors.length() > 0) status += "\n" + errors.toString().trim();
            }
            return new Result(null, null, Double.NaN, status);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new IOException("Local IPOPT solve interrupted", interrupted);
        } finally {
            if (process != null) {
                process.destroy();
                try {
                    if (!process.waitFor(200, TimeUnit.MILLISECONDS)) {
                        process.destroyForcibly();
                        process.waitFor(200, TimeUnit.MILLISECONDS);
                    }
                } catch (InterruptedException interrupted) {
                    process.destroyForcibly();
                    Thread.currentThread().interrupt();
                }
                close(process.getInputStream());
                close(process.getErrorStream());
                close(process.getOutputStream());
            }
            Files.deleteIfExists(worker);
        }
    }

    private static void close(java.io.Closeable stream) {
        try { stream.close(); } catch (IOException ignored) { }
    }

    private static boolean better(IpoptProblem problem, Result next, Result previous) {
        double a = problem.score(next.yaw, next.path), b = problem.score(previous.yaw, previous.path);
        return problem.objective.sense == Objective.Sense.MAX ? a > b : a < b;
    }

    private static Thread reader(InputStream stream, Consumer<String> consume) {
        Thread thread = new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if (line.length() > 2_000_000) throw new IOException("Oversized worker response");
                    consume.accept(line);
                }
            } catch (IOException ignored) {
            }
        }, "ipopt-process-reader");
        thread.setDaemon(true);
        thread.start();
        return thread;
    }
}
