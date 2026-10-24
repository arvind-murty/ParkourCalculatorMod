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

/** Reproduce existing fixed jump6 CF calls with shared diagnostics, without changing entry or settings. */
@Category(SlowSolverTests.class)
public class JumpSixCandidateAuditTest {
    private static Method method(Class<?> owner,String name,Class<?>... args) throws Exception {
        Method m=owner.getDeclaredMethod(name,args);m.setAccessible(true);return m;
    }
    private static String csv(Object... values) {
        StringBuilder s=new StringBuilder();
        for(Object v:values){if(s.length()>0)s.append(',');s.append('"').append(String.valueOf(v).replace("\"","\"\"")).append('"');}
        return s.toString();
    }
    private static JumpPhysicsInputs slice(JumpPhysicsInputs sc,int a,int b,Vec3dCore pos,Vec3dCore vel,float yaw) throws Exception {
        return (JumpPhysicsInputs)method(LongRunSolver.class,"sliceScenario",JumpPhysicsInputs.class,int.class,int.class,
                Vec3dCore.class,Vec3dCore.class,float.class).invoke(null,sc,a,b,pos,vel,yaw);
    }
    @Test @SuppressWarnings("unchecked")
    public void auditExistingCalls() throws Exception {
        Assume.assumeTrue("opt in with -Dpkc.marginProbe=true",Boolean.getBoolean("pkc.marginProbe"));
        String input=System.getenv("PKC_TRACE_INPUT");if(input==null)input="/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json";
        SaveFile file=SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get(input)),StandardCharsets.UTF_8));assertNotNull(file);
        InputData inputs=new InputData();SaveIO.applyRowsTo(file,inputs);
        AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);
        ExactJumpModel exact=ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full=new AngleSolverEngine(state,Fixtures.buildBoxes(file),inputs,t->{},exact).debugBuildSpec();
        JumpPhysicsInputs source=full.asScenario();
        Path archive=Paths.get("../docs/research/solver-traces/fixed-seven-forward-jump-cap1000-2026-10-23/solver-trace.txt");
        boolean active=false;double[] prefix=new double[55];boolean[] seen=new boolean[55];double[] expected=null;
        Pattern row=Pattern.compile("LRS-COMMIT-PATH jump=\\d+ tick=(\\d+).*yaw=([^ ]+)");
        Pattern seed=Pattern.compile("LRS-WIN commit=1 jumps=\\[5,12\\).*seed=\\(([^,]+),([^;]+);([^,]+),([^\\)]+)\\) yaw=([^ ]+)");
        for(String line:Files.readAllLines(archive,StandardCharsets.UTF_8)) {
            if(line.contains("LRS-WIN"))active=line.contains("commit=1 ");
            Matcher m=row.matcher(line);
            if(active&&m.find()) {int t=Integer.parseInt(m.group(1));if(t<55){prefix[t]=(float)Double.parseDouble(m.group(2));seen[t]=true;}}
            m=seed.matcher(line);if(m.find()){expected=new double[5];for(int i=0;i<5;i++)expected[i]=Double.parseDouble(m.group(i+1));break;}
        }
        for(boolean x:seen)assertTrue(x);assertNotNull(expected);
        ForwardPath before=exact.forward(slice(source,0,55,source.startPos,source.initialVelocity,source.startYaw),prefix);
        Vec3dCore pos=new Vec3dCore(before.posX[55],before.posY[55],before.posZ[55]);
        Vec3dCore vel=new Vec3dCore(before.velX[55],before.velY[55],before.velZ[55]);float yaw=(float)prefix[54];
        assertEquals(expected[0],pos.x,1e-10);assertEquals(expected[1],pos.z,1e-10);
        assertEquals(expected[2],vel.x,1e-10);assertEquals(expected[3],vel.z,1e-10);assertEquals((float)expected[4],yaw,0);
        List<String> summary=new ArrayList<>(),margins=new ArrayList<>(),inertia=new ArrayList<>(),paths=new ArrayList<>(),outcomes=new ArrayList<>();
        summary.add("case,candidate,pattern,continuous_violation,replay_violation,accepted,last_iteration,min_costate_norm,fallback_costates");
        margins.add("case,candidate,name,axis,tick,comparison,wall,committed,assumed_value,assumed_margin,continuous_value,continuous_margin,replay_value,replay_margin");
        inertia.add("case,candidate,tick,axis,assumed_zero,assumed_velocity,threshold_margin,assumption_satisfied,continuous_zero,continuous_velocity,replay_zero,replay_velocity");
        paths.add("case,candidate,tick,yaw,game_yaw,assumed_x,assumed_z,continuous_x,continuous_z,vx,vz,replay_x,replay_z,replay_vx,replay_vz");
        outcomes.add("case,feasible,violation,candidates,elapsed_ms");
        SolverTrace.enable("jump-six-fixed-cap1000-audit");
        try {
            for(int end:new int[]{90,77}) {
                JumpPhysicsInputs sc=slice(source,55,end,pos,vel,yaw);
                List<JumpConstraint> rows=(List<JumpConstraint>)method(LongRunSolver.class,"sliceConstraints",JumpSpec.class,int.class,int.class,float.class)
                        .invoke(null,full,55,end,yaw);
                assertEquals(end==90?57:32,rows.size());
                for(int o=0;o<(end==90?4:3);o++) {
                    Objective objective=new Objective(o<2?JumpPhysicsInputs.Axis.Z:JumpPhysicsInputs.Axis.X,
                            o%2==0?Objective.Sense.MAX:Objective.Sense.MIN,end-55);
                    String label="end"+end+"-"+objective.axis+objective.sense;
                    ClosedFormSolve.Config cfg=new ClosedFormSolve.Config();cfg.dualMaxIterations=1000;cfg.replayTickLimit=9;
                    int[] count={0},iteration={0},fallback={0};double[] minCostate={Double.NaN};
                    cfg.dualIterationObserver=(i,d,r,c,n,z,f,fin)->{iteration[0]=i;fallback[0]=f;minCostate[0]=n;};
                    WindowCandidateValidation.setDiagnosticObserver(d->{
                        int c=count[0]++;
                        summary.add(csv(label,c,d.assumedPattern,d.result.continuousViolation,d.result.replayViolation,d.result.maxViolation()<=0,
                                iteration[0],minCostate[0],fallback[0]));
                        for(WindowCandidateValidation.ConstraintMargin x:d.constraints)margins.add(csv(label,c,x.constraint.name,x.constraint.mode,55+x.constraint.t1,
                                x.constraint.cmp,x.constraint.rhs,x.committed,x.assumedValue,x.assumedMargin,x.continuousValue,x.continuousMargin,x.replayValue,x.replayMargin));
                        for(WindowCandidateValidation.InertiaCheck x:d.inertiaChecks)inertia.add(csv(label,c,55+x.tick,x.axis==0?"X":"Z",x.assumedZero,x.assumedVelocity,
                                x.assumedThresholdMargin,x.assumptionSatisfied,x.continuousZero,x.continuousVelocity,x.replayZero,x.replayVelocity));
                        for(int t=0;t<=sc.numTicks;t++)paths.add(csv(label,c,55+t,t<sc.numTicks?d.continuousFacings[t]:"",t<sc.numTicks?d.gameFacings[t]:"",
                                d.assumedPath.posX[t],d.assumedPath.posZ[t],d.continuousPath.posX[t],d.continuousPath.posZ[t],d.continuousPath.velX[t],d.continuousPath.velZ[t],
                                d.replayPath.posX[t],d.replayPath.posZ[t],d.replayPath.velX[t],d.replayPath.velZ[t]));
                    });
                    long start=System.nanoTime();ClosedFormSolve.Result result;
                    try {
                        SolverTrace.log("J6","begin case=%s",label);
                        result=(ClosedFormSolve.Result)method(ClosedFormSolve.class,"optimizeReturning",ExactJumpModel.class,JumpSpec.class,double.class,
                                AtomicBoolean.class,double[].class,boolean.class,ClosedFormSolve.Config.class)
                                .invoke(null,exact,new JumpSpec(sc,rows,objective),0.0,new AtomicBoolean(false),new double[]{.0003},false,cfg);
                    } finally {WindowCandidateValidation.setDiagnosticObserver(null);}
                    boolean feasible=result!=null&&result.feasible;
                    assertEquals("reproduce archived acceptance "+label,end==77&&o==2,feasible);
                    outcomes.add(csv(label,feasible,result==null?"none":result.violation,count[0],(System.nanoTime()-start)/1e6));
                    SolverTrace.log("J6","end case=%s feasible=%s",label,feasible);
                }
            }
        } finally {
            WindowCandidateValidation.setDiagnosticObserver(null);SolverTrace.disable();
            Path out=Paths.get("build/reports/jump-six-fixed-cap1000");Files.createDirectories(out);
            String[] names={"candidates.csv","constraint-margins.csv","inertia-checks.csv","paths.csv","outcomes.csv"};
            List<List<String>> data=Arrays.asList(summary,margins,inertia,paths,outcomes);
            for(int i=0;i<names.length;i++)Files.write(out.resolve(names[i]),data.get(i),StandardCharsets.UTF_8);
            Files.write(out.resolve("metadata.txt"),Arrays.asList("entry reconstructed from committed float game facings0..54",
                    "x55="+pos.x+" z55="+pos.z+" vx55="+vel.x+" vz55="+vel.z+" yaw="+yaw,
                    "fixed CF margin0.0003; cap1000; EPS2=1e-24; prefix9; endpoints90 and77; no altered entry/margin controls"),StandardCharsets.UTF_8);
        }
    }
}
