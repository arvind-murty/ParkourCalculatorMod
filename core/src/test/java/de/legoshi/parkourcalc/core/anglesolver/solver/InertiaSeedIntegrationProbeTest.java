package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import static org.junit.Assert.*;

@Category(SlowSolverTests.class)
public class InertiaSeedIntegrationProbeTest {
    @Test public void compareProductionEndpoints() throws Exception {
        org.junit.Assume.assumeTrue(Boolean.getBoolean("pkc.inertiaIntegrationProbe"));
        Path root=Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod");
        Path out=root.resolve("docs/research/solver-traces/full-route-seeded-inventory-2026-10-24");
        Files.createDirectories(out);
        SaveFile file=SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get("/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json")),StandardCharsets.UTF_8));
        InputData input=new InputData();SaveIO.applyRowsTo(file,input);
        AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);
        ExactJumpModel exact=ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full=new AngleSolverEngine(state,Fixtures.buildBoxes(file),input,t->{},exact).debugBuildSpec();
        Properties saved=new Properties();
        try(java.io.Reader reader=Files.newBufferedReader(root.resolve("docs/research/solver-traces/manual-coupled-window-2026-10-24/entry.txt"))){saved.load(reader);}
        ForwardPath vertical=exact.forward(full.asScenario(),new double[full.asScenario().numTicks]);
        Vec3dCore pos=new Vec3dCore(Double.valueOf(saved.getProperty("x")),vertical.posY[55],Double.valueOf(saved.getProperty("z")));
        Vec3dCore vel=new Vec3dCore(Double.valueOf(saved.getProperty("vx")),vertical.velY[55],Double.valueOf(saved.getProperty("vz")));
        float yaw=Float.valueOf(saved.getProperty("yaw"));
        Method slice=LongRunSolver.class.getDeclaredMethod("sliceScenario",JumpPhysicsInputs.class,int.class,int.class,Vec3dCore.class,Vec3dCore.class,float.class);
        Method constraints=LongRunSolver.class.getDeclaredMethod("sliceConstraints",JumpSpec.class,int.class,int.class,float.class);
        slice.setAccessible(true);constraints.setAccessible(true);
        List<String> result=new ArrayList<>();result.add("end_tick,seeding,accepted,continuous_violation,prefix_replay_violation,full_replay_violation,objective,ms");
        SolverTrace.enable("seeded-production-endpoints");
        try {
            for(int end:new int[]{90,103}) {
                JumpPhysicsInputs win=(JumpPhysicsInputs)slice.invoke(null,full.asScenario(),55,end,pos,vel,yaw);
                @SuppressWarnings("unchecked") List<JumpConstraint> rows=(List<JumpConstraint>)constraints.invoke(null,full,55,end,yaw);
                JumpSpec spec=new JumpSpec(win,rows,new Objective(JumpPhysicsInputs.Axis.Z,end==90?Objective.Sense.MIN:Objective.Sense.MAX,end-55));
                for(boolean seed:new boolean[]{false,true}) {
                    ClosedFormSolve.Config cfg=new ClosedFormSolve.Config();cfg.marginsRobust=new double[]{.0003};cfg.replayTickLimit=9;cfg.seedInertiaRetries=seed;
                    SolverTrace.log("CONTROL","end=%d seeding=%s retention=true",end,seed);
                    long started=System.nanoTime();
                    double[] angles=ClosedFormSolve.optimizeRobust(exact,spec,0,new AtomicBoolean(false),cfg);
                    double ms=(System.nanoTime()-started)/1e6;
                    WindowCandidateValidation.Result check=angles==null?null:new WindowCandidateValidation(exact,spec,9).evaluate(angles);
                    ForwardPath path=angles==null?null:new JumpLinearModel(win).forwardContinuous(angles,.005,true);
                    double[] game=angles==null?null:win.toGameFacings(angles);
                    double replay=angles==null?Double.NaN:JumpConstraintCompiler.compile(spec).maxViolation(game,exact.forward(win,game));
                    result.add(String.format(Locale.ROOT,"%d,%s,%s,%.17g,%.17g,%.17g,%.17g,%.3f",end,seed,angles!=null,check==null?Double.NaN:check.continuousViolation,check==null?Double.NaN:check.replayViolation,replay,path==null?Double.NaN:spec.objective.evaluate(path),ms));
                    Files.write(out.resolve("production-endpoints.csv"),result,StandardCharsets.UTF_8);
                    if(seed){assertNotNull("seeded production endpoint "+end,angles);assertEquals(0,check.maxViolation(),0);}
                }
            }
        } finally {SolverTrace.disable();}
    }
}
