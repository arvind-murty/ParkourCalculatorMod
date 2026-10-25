package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.*;
import de.legoshi.parkourcalc.core.save.*;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import java.io.*;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.GZIPInputStream;

@Category(SlowSolverTests.class)
public class Jump20WindowComparisonProbeTest {
    private static final Path ROOT=Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod");
    private static final Path OUT=ROOT.resolve("docs/research/solver-traces/jump20-window-comparison-2026-10-24");
    private static String csv(Object... values){StringJoiner out=new StringJoiner(",");for(Object value:values){String text=String.valueOf(value);out.add(text.indexOf(',')>=0 || text.indexOf('"')>=0 ? "\""+text.replace("\"","\"\"")+"\"" : text);}return out.toString();}
    private static void write(String name,List<String> rows)throws Exception{Files.write(OUT.resolve(name),rows,StandardCharsets.UTF_8);}
    @Test public void compareWindows()throws Exception {
        org.junit.Assume.assumeTrue(Boolean.getBoolean("pkc.jump20ComparisonProbe"));Files.createDirectories(OUT);
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        try(InputStream in=new GZIPInputStream(Files.newInputStream(ROOT.resolve("docs/research/solver-traces/jump39-baseline-handoff-2026-10-24/real11-input.json.gz")))){byte[] buffer=new byte[8192];int count;while((count=in.read(buffer))!=-1)bytes.write(buffer,0,count);}
        SaveFile file=SaveIO.parseSafe(new String(bytes.toByteArray(),StandardCharsets.UTF_8));InputData input=new InputData();SaveIO.applyRowsTo(file,input);
        AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);
        ExactJumpModel exact=ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full=new AngleSolverEngine(state,Fixtures.buildBoxes(file),input,t->{},exact).debugBuildSpec();
        double[] committed=new double[full.asScenario().numTicks];
        for(String line:Files.readAllLines(ROOT.resolve("docs/research/solver-traces/full-route-commit1-staged-caps-2026-10-24/committed-ticks.csv"))){String[] c=line.split(",");if(!c[0].equals("1"))continue;int t=Integer.parseInt(c[2]);if(t<132)committed[t]=Float.parseFloat(c[7]);}
        ForwardPath prefix=exact.forward(full.asScenario(),committed);
        Vec3dCore pos=new Vec3dCore(prefix.posX[132],prefix.posY[132],prefix.posZ[132]);Vec3dCore vel=new Vec3dCore(prefix.velX[132],prefix.velY[132],prefix.velZ[132]);float yaw=(float)committed[131];
        org.junit.Assert.assertEquals(733.299391115143,pos.x,1e-10);org.junit.Assert.assertEquals(-1095.046878622428,pos.z,1e-10);
        write("entry.csv",Arrays.asList("tick,x,y,z,vx,vy,vz,yaw",csv(132,pos.x,pos.y,pos.z,vel.x,vel.y,vel.z,yaw)));
        Method slice=LongRunSolver.class.getDeclaredMethod("sliceScenario",JumpPhysicsInputs.class,int.class,int.class,Vec3dCore.class,Vec3dCore.class,float.class);slice.setAccessible(true);
        Method constraints=LongRunSolver.class.getDeclaredMethod("sliceConstraints",JumpSpec.class,int.class,int.class,float.class);constraints.setAccessible(true);
        Method solve=LongRunSolver.class.getDeclaredMethod("solveWindow",ExactJumpModel.class,JumpPhysicsInputs.class,List.class,Objective.class,boolean.class,int.class,AtomicBoolean.class,long.class,Double.class,int.class);solve.setAccessible(true);
        SolverTrace.enable("jump20-window-comparison");
        try {
            for(int end:new int[]{178,169}) {
                JumpPhysicsInputs win=(JumpPhysicsInputs)slice.invoke(null,full.asScenario(),132,end,pos,vel,yaw);
                @SuppressWarnings("unchecked") List<JumpConstraint> rows=(List<JumpConstraint>)constraints.invoke(null,full,132,end,yaw);
                List<WindowCandidateValidation.Diagnostics> audits=new ArrayList<>();WindowCandidateValidation.setDiagnosticObserver(audits::add);
                SolverTrace.log("CONTROL","start=132 end=%d prefix=23 cap=1000 constraints=%d",end,rows.size());
                double[] answer;
                try{answer=(double[])solve.invoke(null,exact,win,rows,new Objective(JumpPhysicsInputs.Axis.Z,Objective.Sense.MAX,end-132),false,23,new AtomicBoolean(false),0L,Double.valueOf(.0003),1000);}finally{WindowCandidateValidation.setDiagnosticObserver(null);}
                List<String> margins=new ArrayList<>();margins.add("evaluation,pattern,continuous_violation,prefix_replay_violation,tick,mode,comparison,rhs,committed,continuous_value,replay_value,continuous_margin,replay_margin,assumed_margin");
                for(int i=0;i<audits.size();i++){
                    WindowCandidateValidation.Diagnostics d=audits.get(i);
                    for(WindowCandidateValidation.ConstraintMargin c:d.constraints)margins.add(csv(i,d.assumedPattern,d.result.continuousViolation,d.result.replayViolation,132+c.constraint.t1,c.constraint.mode,c.constraint.cmp,c.constraint.rhs,c.committed,c.continuousValue,c.replayValue,c.continuousMargin,c.replayMargin,c.assumedMargin));
                }
                write(end+"-margins.csv",margins);
                if(!audits.isEmpty()){
                    WindowCandidateValidation.Diagnostics d=audits.get(audits.size()-1);
                    List<String> ticks=new ArrayList<>();ticks.add("tick,yaw,game_yaw,continuous_x,replay_x,continuous_z,replay_z,continuous_vx,replay_vx,continuous_vz,replay_vz");
                    for(int t=0;t<win.numTicks;t++)ticks.add(csv(132+t,d.continuousFacings[t],d.gameFacings[t],d.continuousPath.posX[t],d.replayPath.posX[t],d.continuousPath.posZ[t],d.replayPath.posZ[t],d.continuousPath.velX[t],d.replayPath.velX[t],d.continuousPath.velZ[t],d.replayPath.velZ[t]));
                    write(end+"-last-candidate-ticks.csv",ticks);
                    List<String> inertia=new ArrayList<>();inertia.add("tick,axis,assumed_zero,actual_zero,replay_zero,assumed_velocity,continuous_velocity,replay_velocity");
                    for(WindowCandidateValidation.InertiaCheck c:d.inertiaChecks)inertia.add(csv(132+c.tick,c.axis,c.assumedZero,c.continuousZero,c.replayZero,c.assumedVelocity,c.continuousVelocity,c.replayVelocity));
                    write(end+"-last-inertia.csv",inertia);
                }
                if(end==178)org.junit.Assert.assertNull(answer);else org.junit.Assert.assertNotNull(answer);
            }
        }finally{WindowCandidateValidation.setDiagnosticObserver(null);SolverTrace.disable();}
    }
}
