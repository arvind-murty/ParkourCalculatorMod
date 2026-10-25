package de.legoshi.parkourcalc.anglesolver.ipopt;

import de.legoshi.parkourcalc.core.anglesolver.ipopt.IpoptProblem;
import de.legoshi.parkourcalc.core.anglesolver.ipopt.LocalIpoptSolver;
import org.junit.Assume;
import org.junit.Test;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Collections;
import java.util.concurrent.atomic.AtomicBoolean;
import static org.junit.Assert.*;

public class IpoptProcessTest {
    private Path executable(String body) throws Exception {
        Assume.assumeTrue(!System.getProperty("os.name").startsWith("Windows"));
        Path file = Files.createTempFile("pkc-worker-test-", ".sh");
        Files.write(file, ("#!/bin/sh\n" + body).getBytes(StandardCharsets.UTF_8));
        assertTrue(file.toFile().setExecutable(true));
        return file;
    }

    @Test public void timeoutKillsWorkerThatDoesNotReadLargeRequest() throws Exception {
        Path executable = executable("exec sleep 20\n");
        try {
            IpoptProblem problem = new IpoptProblem(IpoptValidationTest.spec(4000, Collections.emptyList()),
                    new double[4000], Collections.emptyList(), 0, 0.15, false);
            long start = System.nanoTime();
            LocalIpoptSolver.Result result = new LocalIpoptSolver().solve(problem, new AtomicBoolean(),
                    best -> fail("Unexpected incumbent"), executable.toString());
            assertNull(result.yaw);
            assertTrue(result.status, result.status.contains("Time limit"));
            assertTrue((System.nanoTime() - start) / 1e9 < 2);
        } finally { Files.deleteIfExists(executable); }
    }

    @Test public void cancellationStopsAnActiveWorker() throws Exception {
        Path executable = executable("exec sleep 20\n");
        AtomicBoolean cancel = new AtomicBoolean();
        Thread canceller = new Thread(() -> {
            try { Thread.sleep(100); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
            cancel.set(true);
        });
        try {
            IpoptProblem problem = new IpoptProblem(IpoptValidationTest.spec(1, Collections.emptyList()),
                    new double[1], Collections.emptyList(), 0, 10, false);
            canceller.start();
            long start = System.nanoTime();
            assertNull(new LocalIpoptSolver().solve(problem, cancel, best -> { }, executable.toString()));
            assertTrue((System.nanoTime() - start) / 1e9 < 2);
        } finally { canceller.join(); Files.deleteIfExists(executable); }
    }

    @Test public void malformedWorkerOutputFailsClearly() throws Exception {
        Path executable = executable("echo broken\n");
        try {
            IpoptProblem problem = new IpoptProblem(IpoptValidationTest.spec(1, Collections.emptyList()),
                    new double[1], Collections.emptyList(), 0, 2, false);
            IOException failure = assertThrows(IOException.class, () -> new LocalIpoptSolver().solve(problem,
                    new AtomicBoolean(), best -> { }, executable.toString()));
            assertTrue(failure.getMessage().contains("Invalid response"));
        } finally { Files.deleteIfExists(executable); }
    }

    @Test public void timeoutKeepsOnlyIndependentlyValidatedIncumbent() throws Exception {
        Path executable = executable("echo '{\"type\":\"candidate\",\"yaw\":[0]}'\nexec sleep 20\n");
        try {
            IpoptProblem problem = new IpoptProblem(IpoptValidationTest.spec(1, Collections.emptyList()),
                    new double[1], Collections.emptyList(), 0, 0.2, false);
            AtomicBoolean offered = new AtomicBoolean();
            LocalIpoptSolver.Result result = new LocalIpoptSolver().solve(problem, new AtomicBoolean(),
                    best -> offered.set(true), executable.toString());
            assertTrue(offered.get());
            assertNotNull(result.yaw);
            assertTrue(result.status.contains("Time limit"));
        } finally { Files.deleteIfExists(executable); }
    }

    @Test public void workerSuccessCannotOverrideConstraintViolation() throws Exception {
        Path executable = executable("echo '{\"type\":\"candidate\",\"yaw\":[0]}'\necho '{\"type\":\"done\",\"status\":\"Solve_Succeeded\"}'\n");
        try {
            de.legoshi.parkourcalc.core.anglesolver.solver.JumpConstraint pin =
                    new de.legoshi.parkourcalc.core.anglesolver.solver.JumpConstraint(
                            de.legoshi.parkourcalc.core.anglesolver.solver.JumpConstraint.Mode.F, 0, null,
                            de.legoshi.parkourcalc.core.anglesolver.solver.JumpConstraint.Op.PLUS,
                            de.legoshi.parkourcalc.core.anglesolver.solver.JumpConstraint.Cmp.EQ, 90, "pin");
            IpoptProblem problem = new IpoptProblem(IpoptValidationTest.spec(1, Collections.singletonList(pin)),
                    new double[1], Collections.emptyList(), 0, 2, false);
            LocalIpoptSolver.Result result = new LocalIpoptSolver().solve(problem, new AtomicBoolean(),
                    best -> fail("Invalid candidate was accepted"), executable.toString());
            assertNull(result.yaw);
        } finally { Files.deleteIfExists(executable); }
    }
}
