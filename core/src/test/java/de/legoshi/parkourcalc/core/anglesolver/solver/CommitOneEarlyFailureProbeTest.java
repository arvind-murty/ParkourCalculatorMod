package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.*;
import de.legoshi.parkourcalc.core.save.*;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;

@Category(SlowSolverTests.class)
public class CommitOneEarlyFailureProbeTest {
    private static final Path ROOT=Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod");
    private static final Path OUT=ROOT.resolve(System.getProperty("pkc.commitOneEarlyOutput", "docs/research/solver-traces/commit1-early-failure-2026-10-24"));
    private static void write(String name,List<String> rows) throws Exception {Files.write(OUT.resolve(name),rows,StandardCharsets.UTF_8);}
    private static String csv(Object... values) {StringJoiner s=new StringJoiner(",");for(Object v:values)s.add(String.valueOf(v));return s.toString();}
    private static double[] array(Object obj,String name)throws Exception {Field f=obj.getClass().getDeclaredField(name);f.setAccessible(true);return (double[])f.get(obj);}

    @Test public void traceShortestWindow() throws Exception {
        org.junit.Assume.assumeTrue(Boolean.getBoolean("pkc.commitOneEarlyProbe"));
        Files.createDirectories(OUT);
        SaveFile file=SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get("/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json")),StandardCharsets.UTF_8));
        InputData input=new InputData();SaveIO.applyRowsTo(file,input);
        AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);
        ExactJumpModel exact=ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full=new AngleSolverEngine(state,Fixtures.buildBoxes(file),input,t->{},exact).debugBuildSpec();
        ForwardPath vert=exact.forward(full.asScenario(),new double[full.asScenario().numTicks]);
        double[] committed=new double[full.asScenario().numTicks];
        for(String line:Files.readAllLines(ROOT.resolve("docs/research/solver-traces/full-route-free-air-ten-ladder-2026-10-24/committed-ticks.csv"))) {
            String[] c=line.split(",");if(!c[0].equals("1"))continue;
            int tick=Integer.parseInt(c[2]);if(tick<77)committed[tick]=Double.parseDouble(c[7]);
        }
        ForwardPath reconstructed=exact.forward(full.asScenario(),committed);
        List<String> entry=new ArrayList<>();entry.add("x,z,vx,vz");entry.add(csv(reconstructed.posX[77],reconstructed.posZ[77],reconstructed.velX[77],reconstructed.velZ[77]));write("reconstructed-entry.csv",entry);
        org.junit.Assert.assertEquals(732.184309547831,reconstructed.posX[77],1e-10);
        org.junit.Assert.assertEquals(-1089.644531181534,reconstructed.posZ[77],1e-10);
        org.junit.Assert.assertEquals(.226853724943,reconstructed.velX[77],1e-10);
        org.junit.Assert.assertEquals(.255584609936,reconstructed.velZ[77],1e-10);
        Vec3dCore pos=new Vec3dCore(reconstructed.posX[77],vert.posY[77],reconstructed.posZ[77]);
        Vec3dCore vel=new Vec3dCore(reconstructed.velX[77],vert.velY[77],reconstructed.velZ[77]);
        float yaw=3.081829071f;
        Method slice=LongRunSolver.class.getDeclaredMethod("sliceScenario",JumpPhysicsInputs.class,int.class,int.class,Vec3dCore.class,Vec3dCore.class,float.class);
        Method constraints=LongRunSolver.class.getDeclaredMethod("sliceConstraints",JumpSpec.class,int.class,int.class,float.class);
        slice.setAccessible(true);constraints.setAccessible(true);
        JumpPhysicsInputs win=(JumpPhysicsInputs)slice.invoke(null,full.asScenario(),77,91,pos,vel,yaw);
        @SuppressWarnings("unchecked") List<JumpConstraint> rows=(List<JumpConstraint>)constraints.invoke(null,full,77,91,yaw);
        JumpSpec spec=new JumpSpec(win,rows,new Objective(JumpPhysicsInputs.Axis.Z,Objective.Sense.MIN,14));
        SolverTrace.enable("commit1-early-failure");
        try {
            ClosedFormSolve.Config cfg=new ClosedFormSolve.Config();cfg.marginsRobust=new double[]{.0003};cfg.replayTickLimit=3;
            List<String> controls=new ArrayList<>();controls.add("seeded,accepted,continuous_violation,prefix_violation,ms");
            for(boolean enabled:new boolean[]{true,false}) {
                cfg.seedInertiaRetries=enabled;long start=System.nanoTime();
                double[] answer=ClosedFormSolve.optimizeRobust(exact,spec,0,new AtomicBoolean(false),cfg);
                WindowCandidateValidation.Result accepted=answer==null?null:new WindowCandidateValidation(exact,spec,3).evaluate(answer);
                controls.add(csv(enabled,answer!=null,accepted==null?Double.NaN:accepted.continuousViolation,accepted==null?Double.NaN:accepted.replayViolation,(System.nanoTime()-start)/1e6));
                if(answer!=null) {
                    List<String> acceptedTicks=new ArrayList<>();acceptedTicks.add("tick,yaw,x,z,vx,vz");
                    ForwardPath path=new JumpLinearModel(win).forwardContinuous(answer,.005,true);
                    for(int t=0;t<14;t++)acceptedTicks.add(csv(77+t,answer[t],path.posX[t],path.posZ[t],path.velX[t],path.velZ[t]));
                    write("accepted-cold-ticks.csv",acceptedTicks);
                }
            }
            write("production-controls.csv",controls);
            for(boolean seeded:new boolean[]{true,false}) {
                boolean[] zx=null,zz=null;
                InertiaRetrySeed seed=new InertiaRetrySeed();
                List<String> passes=new ArrayList<>();passes.add("pass,pattern,iters,stop,continuous_violation,prefix_violation,fixed_violation");
                for(int pass=0;pass<2;pass++) {
                    final int p=pass;
                    JumpLinearModel lin=new JumpLinearModel(win,zx,zz);
                    double[] cx=new double[14],cz=new double[14];lin.objectiveVectors(spec.objective,cx,cz);
                    List<JumpLinearModel.Wall> walls=lin.compileWalls(rows,0,new boolean[]{false});int authored=walls.size();walls.addAll(lin.velocityWalls(.005));
                    FacingPrefold pre=FacingPrefold.analyze(rows,lin);
                    FacingPrefold.Reduced red=pre.reduce(cx,cz,lin.mMagAll(),walls);
                    CostateDualSolver solver=new CostateDualSolver(red.n,red.cx,red.cz,red.mMag,red.walls);
                    final double[] gx=array(solver,"gx"),gz=array(solver,"gz");
                    List<String> iterations=new ArrayList<>();iterations.add("iteration,residual,min_costate,fallback,raw_fixed_violation,raw_continuous_violation,decoded_fixed_violation,decoded_continuous_violation,max_decoder_angle_change");
                    List<String> ticks=new ArrayList<>();ticks.add("iteration,tick,gx,gz,raw_yaw,decoded_yaw,assumed_vx,assumed_vz");
                    solver.iterationObserver=(it,value,residual,change,min,near,fallback,last)->{
                        double[] raw=new double[14],decoded=pre.expand(lin,spec.objective,gx,gz);double max=0;
                        for(int t=0;t<14;t++) {raw[t]=lin.recoverYawDeg(t,gx[t],gz[t]);max=Math.max(max,Math.abs(Angles.wrap(raw[t]-decoded[t])));
                            ticks.add(csv(it,77+t,gx[t],gz[t],raw[t],decoded[t],lin.velocityBeforeCheck(0,t,decoded),lin.velocityBeforeCheck(1,t,decoded)));}
                        JumpConstraintCompiler.Compiled comp=JumpConstraintCompiler.compile(spec);
                        iterations.add(csv(it,residual,min,fallback,comp.maxViolation(raw,lin.forwardFixed(raw)),comp.maxViolation(raw,lin.forwardContinuous(raw,.005,true)),comp.maxViolation(decoded,lin.forwardFixed(decoded)),comp.maxViolation(decoded,lin.forwardContinuous(decoded,.005,true)),max));
                    };
                    DualCandidateRetention retained=new DualCandidateRetention(exact,spec,lin,pre,3,0);solver.candidateObserver=retained;
                    CostateDualSolver.Result result=solver.solve(.0003,seeded?seed.forModel(red,authored):null,10000);
                    org.junit.Assert.assertNotNull(result);
                    double[] angles=pre.expand(lin,spec.objective,result);
                    WindowCandidateValidation.setDiagnosticObserver(d->{
                        try {
                            List<String> margins=new ArrayList<>();margins.add("tick,axis,comparison,bound,committed,continuous_value,continuous_margin,replay_margin,assumed_margin");
                            for(WindowCandidateValidation.ConstraintMargin c:d.constraints)margins.add(csv(77+c.constraint.t1,c.constraint.mode,c.constraint.cmp,c.constraint.rhs,c.committed,c.continuousValue,c.continuousMargin,c.replayMargin,c.assumedMargin));
                            write(seeded+"-pass"+p+"-constraints.csv",margins);
                            List<String> checks=new ArrayList<>();checks.add("tick,axis,assumed_zero,actual_zero,assumed_velocity,actual_velocity,assumed_threshold_margin");
                            for(WindowCandidateValidation.InertiaCheck c:d.inertiaChecks)checks.add(csv(77+c.tick,c.axis,c.assumedZero,c.continuousZero,c.assumedVelocity,c.continuousVelocity,c.assumedThresholdMargin));
                            write(seeded+"-pass"+p+"-inertia.csv",checks);
                        }catch(Exception e){throw new RuntimeException(e);}
                    });
                    WindowCandidateValidation.Result check;
                    try {check=new WindowCandidateValidation(exact,spec,3).evaluate(angles,lin);}finally{WindowCandidateValidation.setDiagnosticObserver(null);}
                    passes.add(csv(pass,lin.patternDescription(),solver.lastIters,solver.lastStopReason,check.continuousViolation,check.replayViolation,JumpConstraintCompiler.compile(spec).maxViolation(angles,lin.forwardFixed(angles))));
                    write(seeded+"-passes.csv",passes);write(seeded+"-pass"+pass+"-iterations.csv",iterations);write(seeded+"-pass"+pass+"-ticks.csv",ticks);
                    seed.capture(exact,spec,lin,pre,red,authored,angles,result.lambda);
                    zx=new boolean[14];zz=new boolean[14];new JumpLinearModel(win).zeroingPattern(angles,.005,true,zx,zz);
                }
            }
        }finally{SolverTrace.disable();WindowCandidateValidation.setDiagnosticObserver(null);}
    }
}
