package de.legoshi.parkourcalc.core.anglesolver.solver;

import org.junit.Test;

import java.util.Arrays;
import java.util.Collections;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.Assert.*;

public class ReplayPrefixValidationTest {
    private static JumpConstraint row(JumpConstraint.Mode mode, int tick, Integer other, JumpConstraint.Cmp cmp,
                                       double rhs, String name) {
        return new JumpConstraint(mode, tick, other, JumpConstraint.Op.MINUS, cmp, rhs, name);
    }

    @Test
    public void prefixIncludesExitPositionButNotTheNextInputAndDefersCrossBoundaryRows() {
        JumpPhysicsInputs sc = new JumpPhysicsInputs(4);
        JumpConstraint exit = row(JumpConstraint.Mode.X, 2, null, JumpConstraint.Cmp.LE, 1, "exit position");
        JumpConstraint facing = row(JumpConstraint.Mode.F, 1, null, JumpConstraint.Cmp.LE, 90, "committed facing");
        JumpConstraint nextFacing = row(JumpConstraint.Mode.F, 2, null, JumpConstraint.Cmp.LE, 90, "next facing");
        JumpConstraint later = row(JumpConstraint.Mode.X, 3, null, JumpConstraint.Cmp.LE, 1, "later position");
        JumpConstraint across = row(JumpConstraint.Mode.X, 1, 3, JumpConstraint.Cmp.LE, 1, "crossing position");
        JumpConstraint turn = row(JumpConstraint.Mode.F, 1, 0, JumpConstraint.Cmp.LE, 90, "committed turn");
        JumpConstraint nextTurn = row(JumpConstraint.Mode.F, 2, 1, JumpConstraint.Cmp.LE, 90, "next turn");
        JumpConstraint equality = row(JumpConstraint.Mode.Z, 2, null, JumpConstraint.Cmp.EQ, .5, "exit equality");
        JumpSpec spec = new JumpSpec(sc, Arrays.asList(exit, facing, nextFacing, later, across, turn, nextTurn, equality),
                new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 4));
        JumpConstraintCompiler.Compiled prefix = JumpConstraintCompiler.compile(spec, 2);
        assertEquals(Arrays.asList(exit, facing, turn), prefix.ineq);
        assertEquals(Collections.singletonList(equality), prefix.eq);
        assertEquals(7, JumpConstraintCompiler.compile(spec).ineq.size());
        assertEquals(8, spec.constraints.size());
    }

    @Test
    public void aViolationAtTheCommitExitIsRejected() {
        JumpPhysicsInputs sc = new JumpPhysicsInputs(4);
        JumpSpec spec = new JumpSpec(sc, Collections.singletonList(
                row(JumpConstraint.Mode.X, 2, null, JumpConstraint.Cmp.LE, -1, "bad exit")),
                new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 4));
        double[] game = sc.toGameFacings(new double[4]);
        ForwardPath path = ExactJumpModel.forMcVersion("1.8.9").forward(sc, game);
        assertEquals(0, JumpConstraintCompiler.compile(spec, 1).maxViolation(game, path), 0);
        assertTrue(JumpConstraintCompiler.compile(spec, 2).maxViolation(game, path) > 0);
    }

    @Test
    public void futureConstraintsStillRestrictTheOptimizationWithPrefixOnlyReplay() {
        JumpPhysicsInputs sc = new JumpPhysicsInputs(4);
        sc.forwardInputPerTick = new float[4];
        sc.strafeInputPerTick = new float[4];
        JumpSpec spec = new JumpSpec(sc, Collections.singletonList(
                row(JumpConstraint.Mode.Z, 4, null, JumpConstraint.Cmp.GE, 2, "unreachable future")),
                new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 4));
        ClosedFormSolve.Config cfg = new ClosedFormSolve.Config();
        cfg.replayTickLimit = 1;
        assertTrue(JumpConstraintCompiler.compile(spec, 1).ineq.isEmpty());
        assertNull(ClosedFormSolve.optimizeRobust(ExactJumpModel.forMcVersion("1.8.9"), spec, 0,
                new AtomicBoolean(false), cfg));
    }

    @Test
    public void centeredSlpRejectsAFeasiblePrefixWhenTheReplayLookaheadMisses() {
        JumpPhysicsInputs sc = new JumpPhysicsInputs(4);
        JumpSpec spec = new JumpSpec(sc, Arrays.asList(
                row(JumpConstraint.Mode.X, 1, null, JumpConstraint.Cmp.LE, 1, "committed position"),
                row(JumpConstraint.Mode.X, 4, null, JumpConstraint.Cmp.GE, .6, "lookahead position")),
                new Objective(JumpPhysicsInputs.Axis.Z, Objective.Sense.MAX, 4));
        ExactJumpModel exact = ExactJumpModel.forMcVersion("1.8.9");
        SlpSolve.Config cfg = new SlpSolve.Config();
        // Hold the seed fixed to isolate the phase-1 replay acceptance rule from LP improvements.
        cfg.phase1Calls = 0;
        cfg.totalCalls = 1;
        double[] yaws = SlpSolve.optimizeCentered(exact, spec, 0, new AtomicBoolean(false), new double[4], cfg);
        assertNull("SLP requires full-window replay feasibility", yaws);
    }
}
