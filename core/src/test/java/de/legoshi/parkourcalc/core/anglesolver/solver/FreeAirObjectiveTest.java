package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import static org.junit.Assert.*;

public class FreeAirObjectiveTest {
    private JumpConstraint row(JumpConstraint.Mode mode,int tick,Integer other) {
        return new JumpConstraint(mode,tick,other,JumpConstraint.Op.MINUS,JumpConstraint.Cmp.LE,1,"row");
    }

    private JumpSpec spec(JumpPhysicsInputs scenario,List<JumpConstraint> rows) {
        return new JumpSpec(scenario,rows,new Objective(JumpPhysicsInputs.Axis.Z,Objective.Sense.MAX,scenario.numTicks));
    }

    @Test public void skipsEveryConstraintKindAndBothReferencesOfRelativeRows() {
        JumpSpec full=spec(new JumpPhysicsInputs(10),Arrays.asList(row(JumpConstraint.Mode.X,3,null),
                row(JumpConstraint.Mode.Z,4,null),row(JumpConstraint.Mode.F,5,null),row(JumpConstraint.Mode.DXZ,7,6)));
        assertEquals(8,LongRunSolver.freeAirObjectiveTick(full,3));
        assertEquals(8,LongRunSolver.freeAirObjectiveTick(full,6));
    }

    @Test public void skipsGroundTicksEvenWhenTheyHaveNoConstraint() {
        JumpPhysicsInputs scenario=new JumpPhysicsInputs(8);
        scenario.slipPerTick=new double[]{Double.NaN,Double.NaN,.6,.6,Double.NaN,Double.NaN,Double.NaN,Double.NaN};
        assertEquals(4,LongRunSolver.freeAirObjectiveTick(spec(scenario,Collections.emptyList()),2));
    }

    @Test public void keepsAnAlreadyFreeAirborneTick() {
        assertEquals(3,LongRunSolver.freeAirObjectiveTick(spec(new JumpPhysicsInputs(8),Collections.emptyList()),3));
    }

    @Test public void selectionDoesNotDependOnObjectiveAxisOrSense() {
        JumpPhysicsInputs scenario=new JumpPhysicsInputs(8);
        List<JumpConstraint> rows=Collections.singletonList(row(JumpConstraint.Mode.Z,3,null));
        for(JumpPhysicsInputs.Axis axis:new JumpPhysicsInputs.Axis[]{JumpPhysicsInputs.Axis.X,JumpPhysicsInputs.Axis.Z})
            for(Objective.Sense sense:Objective.Sense.values()) {
                JumpSpec full=new JumpSpec(scenario,rows,new Objective(axis,sense,8));
                assertEquals(4,LongRunSolver.freeAirObjectiveTick(full,3));
            }
    }

    @Test public void doesNotInventTicksBeyondTheAuthoredRoute() {
        JumpPhysicsInputs scenario=new JumpPhysicsInputs(4);
        assertEquals(-1,LongRunSolver.freeAirObjectiveTick(spec(scenario,Collections.singletonList(row(JumpConstraint.Mode.X,3,null))),3));
        assertEquals(-1,LongRunSolver.freeAirObjectiveTick(spec(scenario,Collections.emptyList()),4));
    }

    @Test public void probeRealRouteObjectiveTicks() throws Exception {
        org.junit.Assume.assumeTrue(Boolean.getBoolean("pkc.freeAirObjectiveProbe"));
        SaveFile file=SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get("/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json")),StandardCharsets.UTF_8));
        InputData input=new InputData();SaveIO.applyRowsTo(file,input);
        AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);
        JumpSpec full=new AngleSolverEngine(state,Fixtures.buildBoxes(file),input,t->{},ExactJumpModel.forMcVersion(file.mcVersion)).debugBuildSpec();
        List<String> rows=new ArrayList<>();rows.add("nominal_end,objective_tick,extension");
        for(int tick:new int[]{64,68,71,75,77,80,88,90,99,113,117,128,130,132,155}) {
            int selected=LongRunSolver.freeAirObjectiveTick(full,tick);
            assertTrue(selected>=tick);
            assertTrue(Double.isNaN(full.asScenario().slipAt(selected)));
            assertFalse(full.constraints.stream().anyMatch(c->c.t1==selected || (c.t2!=null && c.t2==selected)));
            rows.add(tick+","+selected+","+(selected-tick));
        }
        Path out=Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/free-air-objective-2026-10-24");
        Files.createDirectories(out);Files.write(out.resolve("selected-ticks.csv"),rows,StandardCharsets.UTF_8);
    }
}
