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
public class Jump20ZMaxProbeTest {
    private static final Path ROOT=Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod");
    private static final Path OUT=ROOT.resolve("docs/research/solver-traces/jump20-22-zmax-2026-10-24");
    private static String csv(Object... values){StringJoiner out=new StringJoiner(",");for(Object value:values){String text=String.valueOf(value);out.add(text.indexOf(',')>=0 || text.indexOf('"')>=0 ? "\""+text.replace("\"","\"\"")+"\"" : text);}return out.toString();}
    private static void write(String name,List<String> rows)throws Exception{Files.write(OUT.resolve(name),rows,StandardCharsets.UTF_8);}
    private static double[] array(Object object,String name)throws Exception{java.lang.reflect.Field f=object.getClass().getDeclaredField(name);f.setAccessible(true);return (double[])f.get(object);}
    @Test public void inspectZMax()throws Exception {
        org.junit.Assume.assumeTrue(Boolean.getBoolean("pkc.jump20ZMaxProbe"));Files.createDirectories(OUT);
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
        JumpPhysicsInputs win=(JumpPhysicsInputs)slice.invoke(null,full.asScenario(),132,178,pos,vel,yaw);
        @SuppressWarnings("unchecked") List<JumpConstraint> rows=(List<JumpConstraint>)constraints.invoke(null,full,132,178,yaw);
        JumpSpec spec=new JumpSpec(win,rows,new Objective(JumpPhysicsInputs.Axis.Z,Objective.Sense.MAX,46));
        List<String> outcomes=new ArrayList<>();outcomes.add("cap,accepted,continuous_violation,prefix_replay_violation,ms");
        SolverTrace.enable("jump20-22-zmax");
        try {
            for(int cap:new int[]{1000,10000,100000}) {
                SolverTrace.log("CONTROL","production Z/MAX@178 cap=%d",cap);
                ClosedFormSolve.Config cfg=new ClosedFormSolve.Config();cfg.marginsRobust=new double[]{.0003};cfg.replayTickLimit=23;cfg.dualMaxIterations=cap;
                List<WindowCandidateValidation.Diagnostics> audits=new ArrayList<>();WindowCandidateValidation.setDiagnosticObserver(audits::add);
                List<String> metrics=new ArrayList<>();metrics.add("dual_run,iteration,dual,residual,input_change,min_costate,near_zero,below_decoder_threshold,terminal");int[] run={-1};
                cfg.dualIterationObserver=(it,value,residual,change,min,near,fallback,last)->{if(it==0)run[0]++;if(it<100 || it%100==0 || last)metrics.add(csv(run[0],it,value,residual,change,min,near,fallback,last));};
                long started=System.nanoTime();double[] answer;
                try {answer=ClosedFormSolve.optimizeRobust(exact,spec,0,new AtomicBoolean(false),cfg);}finally{WindowCandidateValidation.setDiagnosticObserver(null);}
                double ms=(System.nanoTime()-started)/1e6;
                WindowCandidateValidation.Result result=answer==null?null:new WindowCandidateValidation(exact,spec,23).evaluate(answer);
                outcomes.add(csv(cap,answer!=null,result==null?Double.NaN:result.continuousViolation,result==null?Double.NaN:result.replayViolation,ms));write("outcomes.csv",outcomes);write(cap+"-iterations.csv",metrics);
                List<String> margins=new ArrayList<>();margins.add("evaluation,pattern,continuous_violation,prefix_replay_violation,tick,mode,comparison,rhs,continuous_value,continuous_margin,replay_margin,assumed_margin");
                List<String> checks=new ArrayList<>();checks.add("evaluation,pattern,tick,axis,assumed_zero,actual_zero,replay_zero,assumed_momentum,continuous_momentum,replay_momentum,assumed_threshold_margin");
                List<String> states=new ArrayList<>();states.add("evaluation,tick,yaw,game_yaw,continuous_x,continuous_z,replay_x,replay_z,assumed_x,assumed_z");
                for(int a=0;a<audits.size();a++) {
                    WindowCandidateValidation.Diagnostics d=audits.get(a);
                    for(WindowCandidateValidation.ConstraintMargin c:d.constraints)margins.add(csv(a,d.assumedPattern,d.result.continuousViolation,d.result.replayViolation,132+c.constraint.t1,c.constraint.mode,c.constraint.cmp,c.constraint.rhs,c.continuousValue,c.continuousMargin,c.replayMargin,c.assumedMargin));
                    for(WindowCandidateValidation.InertiaCheck c:d.inertiaChecks)checks.add(csv(a,d.assumedPattern,132+c.tick,c.axis,c.assumedZero,c.continuousZero,c.replayZero,c.assumedVelocity,c.continuousVelocity,c.replayVelocity,c.assumedThresholdMargin));
                    for(int t=0;t<win.numTicks;t++)states.add(csv(a,132+t,d.continuousFacings[t],d.gameFacings[t],d.continuousPath.posX[t],d.continuousPath.posZ[t],d.replayPath.posX[t],d.replayPath.posZ[t],d.assumedPath==null?Double.NaN:d.assumedPath.posX[t],d.assumedPath==null?Double.NaN:d.assumedPath.posZ[t]));
                }
                write(cap+"-margins.csv",margins);write(cap+"-inertia.csv",checks);write(cap+"-states.csv",states);
                auditInner(spec,exact,cap);
            }
            for(int pins:new int[]{1,2})runPinnedControl(spec,exact,pins);
        }finally{WindowCandidateValidation.setDiagnosticObserver(null);SolverTrace.disable();}
    }
    private static void runPinnedControl(JumpSpec spec,ExactJumpModel exact,int pins)throws Exception {
        List<String> saved=Files.readAllLines(ROOT.resolve("docs/research/solver-traces/jump20-slp-replay-validation-2026-10-24/178-last-candidate-ticks.csv"));
        List<JumpConstraint> rows=new ArrayList<>(spec.constraints);
        for(int t=0;t<pins;t++){String[] c=saved.get(t+1).split(",");double angle=Double.parseDouble(c[1]);rows.add(new JumpConstraint(JumpConstraint.Mode.F,t,null,JumpConstraint.Op.PLUS,JumpConstraint.Cmp.EQ,angle,"diagnostic pin "+t,angle));}
        JumpSpec pinned=new JumpSpec(spec.asScenario(),rows,spec.objective);
        ClosedFormSolve.Config cfg=new ClosedFormSolve.Config();cfg.marginsRobust=new double[]{.0003};cfg.replayTickLimit=23;cfg.dualMaxIterations=10000;
        List<WindowCandidateValidation.Diagnostics> audits=new ArrayList<>();WindowCandidateValidation.setDiagnosticObserver(audits::add);
        SolverTrace.log("CONTROL","diagnostic fixed first %d tick facings from SLP, cap10000",pins);
        double[] answer;long started=System.nanoTime();
        try{answer=ClosedFormSolve.optimizeRobust(exact,pinned,0,new AtomicBoolean(false),cfg);}finally{WindowCandidateValidation.setDiagnosticObserver(null);}
        List<String> summary=new ArrayList<>();summary.add("accepted,ms");summary.add(csv(answer!=null,(System.nanoTime()-started)/1e6));write("pinned"+pins+"-outcome.csv",summary);
        List<String> margins=new ArrayList<>();margins.add("evaluation,pattern,continuous_violation,prefix_replay_violation,tick,mode,continuous_margin,assumed_margin");
        for(int i=0;i<audits.size();i++){WindowCandidateValidation.Diagnostics d=audits.get(i);for(WindowCandidateValidation.ConstraintMargin c:d.constraints)margins.add(csv(i,d.assumedPattern,d.result.continuousViolation,d.result.replayViolation,132+c.constraint.t1,c.constraint.mode,c.continuousMargin,c.assumedMargin));}
        write("pinned"+pins+"-margins.csv",margins);
    }
    private static void auditInner(JumpSpec spec,ExactJumpModel exact,int cap)throws Exception {
        JumpLinearModel lin=new JumpLinearModel(spec.asScenario());int n=lin.n;
        double[] cx=new double[n],cz=new double[n];lin.objectiveVectors(spec.objective,cx,cz);
        List<JumpLinearModel.Wall> walls=lin.compileWalls(spec.constraints,0,new boolean[]{false});
        FacingPrefold pre=FacingPrefold.analyze(spec.constraints,lin);org.junit.Assert.assertTrue(pre.isIdentity());
        FacingPrefold.Reduced red=pre.reduce(cx,cz,lin.mMagAll(),walls);
        CostateDualSolver solver=new CostateDualSolver(red.n,red.cx,red.cz,red.mMag,red.walls);
        double[] gx=array(solver,"gx"),gz=array(solver,"gz"),ux=array(solver,"ux"),uz=array(solver,"uz");
        List<String> samples=new ArrayList<>();samples.add("iteration,residual,min_costate,min_radius_ratio,relaxed_original_wall_violation,decoded_fixed_violation,actual_continuous_violation");
        List<String> tickRows=new ArrayList<>();tickRows.add("iteration,tick,gx,gz,norm,radius_ratio,yaw");
        JumpConstraintCompiler.Compiled compiled=JumpConstraintCompiler.compile(spec);
        solver.iterationObserver=(it,value,residual,change,min,near,fallback,last)->{
            if(it>=100 && it%100!=0 && !last)return;
            double[] angles=pre.expand(lin,spec.objective,gx,gz);double violation=0,ratio=1;
            for(JumpLinearModel.Wall wall:red.walls){double sum=0;double[] controls=wall.axis==0?ux:uz;for(int t=0;t<n;t++)sum+=wall.coef[t]*controls[t];violation=Math.max(violation,Math.max(0,sum-wall.bPrime));}
            for(int t=0;t<n;t++){double norm=StrictMath.hypot(gx[t],gz[t]);double r=norm/StrictMath.sqrt(norm*norm+1e-24);ratio=Math.min(ratio,r);if(last)tickRows.add(csv(it,132+t,gx[t],gz[t],norm,r,angles[t]));}
            samples.add(csv(it,residual,min,ratio,violation,compiled.maxViolation(angles,lin.forwardFixed(angles)),compiled.maxViolation(angles,lin.forwardContinuous(angles,.005,true))));
        };
        CostateDualSolver.Result r=solver.solve(.0003,null,cap);org.junit.Assert.assertNotNull(r);
        double firstWeight=red.mMag[0]/StrictMath.sqrt(r.gx[0]*r.gx[0]+r.gz[0]*r.gz[0]+1e-24),otherWeight=0;
        for(int t=1;t<n;t++)otherWeight=Math.max(otherWeight,red.mMag[t]/StrictMath.sqrt(r.gx[t]*r.gx[t]+r.gz[t]*r.gz[t]+1e-24));
        write(cap+"-curvature-scale.csv",Arrays.asList("first_tick_weight,max_other_tick_weight,first_acceleration_magnitude",csv(firstWeight,otherWeight,red.mMag[0])));
        write(cap+"-unpatterned-inner.csv",samples);write(cap+"-terminal-costates.csv",tickRows);
        List<String> wallAudit=new ArrayList<>();wallAudit.add("name,lambda,relaxed_signed_violation,full_magnitude_signed_violation");
        for(int j=0;j<red.walls.size();j++){JumpLinearModel.Wall wall=red.walls.get(j);double relaxed=0,full=0;for(int t=0;t<n;t++){double norm=StrictMath.hypot(r.gx[t],r.gz[t]);double g=wall.axis==0?r.gx[t]:r.gz[t];relaxed+=wall.coef[t]*(wall.axis==0?ux[t]:uz[t]);full+=wall.coef[t]*red.mMag[t]*g/norm;}wallAudit.add(csv(wall.name,r.lambda[j],relaxed-wall.bPrime,full-wall.bPrime));}
        write(cap+"-terminal-wall-audit.csv",wallAudit);
        List<String> multipliers=new ArrayList<>();multipliers.add("row,name,axis,lambda");for(int j=0;j<red.walls.size();j++)multipliers.add(csv(j,red.walls.get(j).name,red.walls.get(j).axis,r.lambda[j]));write(cap+"-terminal-multipliers.csv",multipliers);
    }
}
