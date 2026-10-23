package de.legoshi.parkourcalc.anglesolver.graph;

import de.legoshi.parkourcalc.core.anglesolver.graph.GraphBuilder;
import de.legoshi.parkourcalc.core.anglesolver.graph.GraphContext;
import de.legoshi.parkourcalc.core.anglesolver.graph.GraphRunner;
import de.legoshi.parkourcalc.core.anglesolver.graph.Guarantee;
import de.legoshi.parkourcalc.core.anglesolver.graph.NodeStatus;
import de.legoshi.parkourcalc.core.anglesolver.graph.SolverGraph;
import de.legoshi.parkourcalc.core.anglesolver.solver.BoundPrunedRecovery;
import de.legoshi.parkourcalc.core.anglesolver.solver.CertifiedBnb;
import de.legoshi.parkourcalc.core.anglesolver.solver.ExactJumpModel;
import de.legoshi.parkourcalc.core.anglesolver.solver.JumpSpec;
import de.legoshi.parkourcalc.core.anglesolver.solver.SolverTrace;
import org.junit.Test;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

/** Ensures the over-cap guard keeps the certifying and pattern B&B searches off long routes. */
public class LongRouteBnbCapTest {

    private static final int BUILTIN_BNB_TICK_CAP = 256;
    private static final String TRACE_TAG = "long-route-bnb-cap-test";

    @Test
    public void routeOver256TicksDoesNotStartEitherBnbSearch() throws Exception {
        JumpSpec spec = TestScenarios.spec(BUILTIN_BNB_TICK_CAP + 1, null);
        assertTrue("scenario must exceed the built-in B&B cap", spec.asScenario().numTicks > BUILTIN_BNB_TICK_CAP);
        ExactJumpModel model = ExactJumpModel.forMcVersion("1.8.9");
        assertTrue("fixture must otherwise be supported by certBnb", CertifiedBnb.accepts(spec));
        assertTrue("fixture must otherwise be supported by bnb", BoundPrunedRecovery.accepts(spec));

        SolverGraph graph = new GraphBuilder("long route B&B cap", false)
                .add("entry", "entry")
                .add("cert", "certBnb")
                .add("bnb", "bnb")
                .add("emit", "emit")
                .chainAll("entry", "cert")
                .chainAll("cert", "bnb")
                .chainAll("bnb", "emit")
                .build();
        GraphContext ctx = new GraphContext(spec, model, null, null, 0.0,
                new AtomicBoolean(false), null, true, null);

        File traceFile = new File("build/reports/solver-trace-" + TRACE_TAG + ".txt");
        SolverTrace.enable(TRACE_TAG);
        try {
            GraphRunner.run(graph, ctx);
        } finally {
            SolverTrace.disable();
        }

        NodeStatus cert = ctx.runState.status("cert");
        NodeStatus bnb = ctx.runState.status("bnb");
        assertNotNull("certBnb graph node should be reached and reject by tick cap", cert);
        assertNotNull("bnb graph node should be reached and reject by tick cap", bnb);
        assertEquals(1, cert.visits);
        assertEquals(1, bnb.visits);
        assertEquals(Guarantee.NONE, cert.taken);
        assertEquals(Guarantee.NONE, bnb.taken);

        String trace = new String(Files.readAllBytes(traceFile.toPath()), StandardCharsets.UTF_8);
        assertFalse("CertifiedBnb search explored nodes on an over-cap route", trace.contains("CERT    node="));
        assertFalse("pattern B&B search started on an over-cap route",
                trace.contains("bnb rescue budgetMs=") || trace.contains("bnb optimize budgetMs="));
        Files.deleteIfExists(traceFile.toPath());
    }
}
