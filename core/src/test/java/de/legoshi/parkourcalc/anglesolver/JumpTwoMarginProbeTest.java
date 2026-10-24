package de.legoshi.parkourcalc.anglesolver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.anglesolver.solver.*;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Assume;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.regex.*;
import static org.junit.Assert.*;

/** Same-entry controls for jump2, reconstructing exact first-commit state from logged float game facings. */
@Category(SlowSolverTests.class)
public class JumpTwoMarginProbeTest {
    private static Method method(Class<?> owner,String name,Class<?>... args) throws Exception {
        Method m=owner.getDeclaredMethod(name,args);m.setAccessible(true);return m;
    }
    private static String csv(Object... values) {
        StringBuilder s=new StringBuilder();
        for(Object v:values) {if(s.length()>0)s.append(',');s.append('"').append(String.valueOf(v).replace("\"","\"\"")).append('"');}
        return s.toString();
    }
    private static final class Entry {
        final Vec3dCore pos,vel;final float yaw;
        Entry(Vec3dCore pos,Vec3dCore vel,float yaw){this.pos=pos;this.vel=vel;this.yaw=yaw;}
    }
    private static JumpPhysicsInputs scenario(JumpPhysicsInputs full,int a,int b,Entry e) throws Exception {
        return (JumpPhysicsInputs)method(LongRunSolver.class,"sliceScenario",JumpPhysicsInputs.class,int.class,int.class,
                Vec3dCore.class,Vec3dCore.class,float.class).invoke(null,full,a,b,e.pos,e.vel,e.yaw);
    }
    private static Entry reconstruct(JumpSpec full,ExactJumpModel exact,Path source) throws Exception {
        boolean active=false;double[] game=new double[12];boolean[] seen=new boolean[12];
        Pattern row=Pattern.compile("LRS-COMMIT-PATH jump=0 tick=(\\d+).*yaw=([^ ]+)");
        Pattern seed=Pattern.compile("LRS-WIN commit=1 jumps=\\[1,8\\).*seed=\\(([^,]+),([^;]+);([^,]+),([^\\)]+)\\) yaw=([^ ]+)");
        double[] archived=null;
        for(String line:Files.readAllLines(source,StandardCharsets.UTF_8)) {
            if(line.contains("LRS-WIN"))active=line.contains("commit=1 ");
            Matcher m=row.matcher(line);
            if(active && m.find()) {int t=Integer.parseInt(m.group(1));if(t<12){game[t]=(float)Double.parseDouble(m.group(2));seen[t]=true;}}
            m=seed.matcher(line);
            if(m.find()){archived=new double[5];for(int i=0;i<5;i++)archived[i]=Double.parseDouble(m.group(i+1));break;}
        }
        for(boolean value:seen)assertTrue("all12 committed game facings present",value);
        JumpPhysicsInputs sc=full.asScenario();Entry start=new Entry(sc.startPos,sc.initialVelocity,sc.startYaw);
        ForwardPath p=exact.forward(scenario(sc,0,12,start),game);assertNotNull(archived);
        assertEquals(archived[0],p.posX[12],1e-10);assertEquals(archived[1],p.posZ[12],1e-10);
        assertEquals(archived[2],p.velX[12],1e-10);assertEquals(archived[3],p.velZ[12],1e-10);
        assertEquals((float)archived[4],(float)game[11],0);
        return new Entry(new Vec3dCore(p.posX[12],p.posY[12],p.posZ[12]),
                new Vec3dCore(p.velX[12],p.velY[12],p.velZ[12]),(float)game[11]);
    }
    @Test @SuppressWarnings("unchecked")
    public void compareIdenticalEntriesAndIterationCaps() throws Exception {
        Assume.assumeTrue("opt in with -Dpkc.marginProbe=true",Boolean.getBoolean("pkc.marginProbe"));
        String input=System.getenv("PKC_TRACE_INPUT");if(input==null)input="/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json";
        SaveFile file=SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get(input)),StandardCharsets.UTF_8));assertNotNull(file);
        InputData inputs=new InputData();SaveIO.applyRowsTo(file,inputs);
        AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);
        ExactJumpModel exact=ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full=new AngleSolverEngine(state,Fixtures.buildBoxes(file),inputs,t->{},exact).debugBuildSpec();
        java.lang.reflect.Field field=CostateDualSolver.class.getDeclaredField("EPS2");field.setAccessible(true);
        double eps=field.getDouble(null);String tag=String.valueOf(eps);
        List<String> results=new ArrayList<>(),candidates=new ArrayList<>(),constraints=new ArrayList<>(),inertia=new ArrayList<>(),iterations=new ArrayList<>(),paths=new ArrayList<>();
        results.add("case,entry,objective,policy,cap,x12,z12,vx12,vz12,yaw12,feasible,violation,validation_calls,elapsed_ms");
        candidates.add("case,candidate,pattern,continuous_violation,prefix_replay_violation,accepted,last_iteration,min_costate_norm,below_fallback_costates,continuous_objective");
        constraints.add("case,candidate,name,axis,tick,comparison,wall,committed,assumed_value,assumed_margin,continuous_value,continuous_margin,replay_value,replay_margin");
        inertia.add("case,candidate,tick,axis,assumed_zero,assumed_velocity,assumed_threshold_margin,assumption_satisfied,continuous_zero,continuous_velocity,replay_zero,replay_velocity");
        iterations.add("case,event,iteration,dual,projected_residual,input_change,min_costate_norm,near_zero_costates,fallback_costates,final_sample");
        paths.add("case,candidate,tick,continuous_yaw,game_yaw,assumed_x,assumed_z,continuous_x,continuous_z,continuous_vx,continuous_vz,replay_x,replay_z,replay_vx,replay_vz");
        String[] names={"normal-reference","fixed-forward-jump","fixed-forward-tick"};
        Path root=Paths.get("../docs/research/solver-traces");
        Path[] sources={root.resolve("shared-inertia-validation-2026-10-23/normal7-commit1.txt"),
                root.resolve("full-forward-shifts-2026-10-23/seven-forward-jump.txt"),root.resolve("full-forward-shifts-2026-10-23/seven-forward-tick.txt")};
        SolverTrace.enable("jump-two-margin-eps"+tag);
        try {
            for(int entryIndex=0;entryIndex<3;entryIndex++) {
                Entry entry=reconstruct(full,exact,sources[entryIndex]);
                JumpPhysicsInputs sc=scenario(full.asScenario(),12,71,entry);assertEquals(59,sc.numTicks);
                List<JumpConstraint> rows=(List<JumpConstraint>)method(LongRunSolver.class,"sliceConstraints",JumpSpec.class,int.class,int.class,float.class)
                        .invoke(null,full,12,71,entry.yaw);assertEquals(37,rows.size());
                for(Objective.Sense sense:Objective.Sense.values())for(int policy=0;policy<(sense==Objective.Sense.MAX?3:2);policy++) {
                    final String name=names[entryIndex]+"-z"+sense+"-"+(policy==0?"normal":policy==1?"fixed100":"fixed1000");
                    Objective objective=new Objective(JumpPhysicsInputs.Axis.Z,sense,59);JumpSpec spec=new JumpSpec(sc,rows,objective);
                    ClosedFormSolve.Config cfg=new ClosedFormSolve.Config();cfg.replayTickLimit=9;cfg.dualMaxIterations=policy==2?1000:100;
                    double[] margins=policy==0?cfg.marginsRobust:new double[]{.0003};
                    int[] count={0},events={0},lastIteration={0},fallback={0};double[] minNorm={Double.NaN};
                    cfg.dualIterationObserver=(iteration,dual,residual,change,minCostate,nearZero,fallbackCount,finalSample)->{
                        iterations.add(csv(name,events[0]++,iteration,dual,residual,change,minCostate,nearZero,fallbackCount,finalSample));
                        lastIteration[0]=iteration;minNorm[0]=minCostate;fallback[0]=fallbackCount;
                    };
                    WindowCandidateValidation.setDiagnosticObserver(d->{
                        int c=count[0]++;
                        candidates.add(csv(name,c,d.assumedPattern,d.result.continuousViolation,d.result.replayViolation,
                                d.result.maxViolation()<=0,lastIteration[0],minNorm[0],fallback[0],objective.evaluate(d.continuousPath)));
                        for(WindowCandidateValidation.ConstraintMargin x:d.constraints)constraints.add(csv(name,c,x.constraint.name,x.constraint.mode,
                                12+x.constraint.t1,x.constraint.cmp,x.constraint.rhs,x.committed,x.assumedValue,x.assumedMargin,
                                x.continuousValue,x.continuousMargin,x.replayValue,x.replayMargin));
                        for(WindowCandidateValidation.InertiaCheck x:d.inertiaChecks)inertia.add(csv(name,c,12+x.tick,x.axis==0?"X":"Z",x.assumedZero,x.assumedVelocity,
                                x.assumedThresholdMargin,x.assumptionSatisfied,x.continuousZero,x.continuousVelocity,x.replayZero,x.replayVelocity));
                        for(int t=0;t<=59;t++)paths.add(csv(name,c,12+t,t<59?d.continuousFacings[t]:"",t<59?d.gameFacings[t]:"",
                                d.assumedPath.posX[t],d.assumedPath.posZ[t],d.continuousPath.posX[t],d.continuousPath.posZ[t],d.continuousPath.velX[t],d.continuousPath.velZ[t],
                                d.replayPath.posX[t],d.replayPath.posZ[t],d.replayPath.velX[t],d.replayPath.velZ[t]));
                    });
                    long start=System.nanoTime();ClosedFormSolve.Result result;
                    try {
                        SolverTrace.log("J2","begin case=%s eps=%g marginPolicy=%s cap=%d",name,eps,Arrays.toString(margins),cfg.dualMaxIterations);
                        result=(ClosedFormSolve.Result)method(ClosedFormSolve.class,"optimizeReturning",ExactJumpModel.class,JumpSpec.class,double.class,
                                AtomicBoolean.class,double[].class,boolean.class,ClosedFormSolve.Config.class).invoke(null,exact,spec,0.0,new AtomicBoolean(false),margins,false,cfg);
                    } finally {WindowCandidateValidation.setDiagnosticObserver(null);}
                    results.add(csv(name,names[entryIndex],sense,policy==0?"normal":"fixed",cfg.dualMaxIterations,entry.pos.x,entry.pos.z,
                            entry.vel.x,entry.vel.z,entry.yaw,result!=null&&result.feasible,result==null?"no candidate":result.violation,count[0],(System.nanoTime()-start)/1e6));
                    SolverTrace.log("J2","end case=%s feasible=%s violation=%s checks=%d",name,result!=null&&result.feasible,result==null?"none":result.violation,count[0]);
                }
            }
        } finally {
            WindowCandidateValidation.setDiagnosticObserver(null);SolverTrace.disable();
            Path out=Paths.get("build/reports/jump-two-margin-eps"+tag);Files.createDirectories(out);
            String[] files={"outcomes.csv","candidates.csv","constraint-margins.csv","inertia-checks.csv","iterations.csv","paths.csv"};
            List<List<String>> data=Arrays.asList(results,candidates,constraints,inertia,iterations,paths);
            for(int i=0;i<files.length;i++)Files.write(out.resolve(files[i]),data.get(i),StandardCharsets.UTF_8);
            Files.write(out.resolve("metadata.txt"),Arrays.asList("EPS2="+eps,"input="+input,"window inputs12..70; end71;37constraints;9committed inputs",
                    "Entries reconstructed exactly from logged float game facings0..11 and checked against archived12-decimal state logs."),StandardCharsets.UTF_8);
        }
    }
}
