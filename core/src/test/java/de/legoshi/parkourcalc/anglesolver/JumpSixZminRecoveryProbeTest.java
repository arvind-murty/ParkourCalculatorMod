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
import static org.junit.Assert.*;

/** Inspect a single failed fixed-pattern dual iterate before/after its angle recovery. No production change. */
@Category(SlowSolverTests.class)
public class JumpSixZminRecoveryProbeTest {
    private static Method method(Class<?> type,String name,Class<?>...args)throws Exception{
        Method m=type.getDeclaredMethod(name,args);m.setAccessible(true);return m;
    }
    private static String csv(Object...values){StringBuilder s=new StringBuilder();for(Object v:values){
        if(s.length()>0)s.append(',');s.append('"').append(String.valueOf(v).replace("\"","\"\"")).append('"');}return s.toString();}
    @Test @SuppressWarnings("unchecked") public void inspectFailedInertiaRetry() throws Exception {
        Assume.assumeTrue("opt in with -Dpkc.marginProbe=true",Boolean.getBoolean("pkc.marginProbe"));
        String input=System.getenv("PKC_TRACE_INPUT");if(input==null)input="/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json";
        SaveFile file=SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get(input)),StandardCharsets.UTF_8));assertNotNull(file);
        InputData inputs=new InputData();SaveIO.applyRowsTo(file,inputs);
        AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);
        ExactJumpModel exact=ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full=new AngleSolverEngine(state,Fixtures.buildBoxes(file),inputs,t->{},exact).debugBuildSpec();
        Vec3dCore pos=new Vec3dCore(730.2967883104711,0,-1096.123960468676);
        Vec3dCore vel=new Vec3dCore(-0.23385825069855395,0,0.15986372215028655);
        float yaw=93.82391357421875f;
        JumpPhysicsInputs sc=(JumpPhysicsInputs)method(LongRunSolver.class,"sliceScenario",JumpPhysicsInputs.class,int.class,int.class,
                Vec3dCore.class,Vec3dCore.class,float.class).invoke(null,full.asScenario(),55,90,pos,vel,yaw);
        List<JumpConstraint> rows=(List<JumpConstraint>)method(LongRunSolver.class,"sliceConstraints",JumpSpec.class,int.class,int.class,float.class)
                .invoke(null,full,55,90,yaw);
        assertEquals(57,rows.size());for(JumpConstraint c:rows)assertTrue(c.mode==JumpConstraint.Mode.X||c.mode==JumpConstraint.Mode.Z);
        Objective objective=new Objective(JumpPhysicsInputs.Axis.Z,Objective.Sense.MIN,35);
        JumpSpec spec=new JumpSpec(sc,rows,objective);
        JumpLinearModel unpatterned=new JumpLinearModel(sc);double[] cx=new double[35],cz=new double[35];
        unpatterned.objectiveVectors(objective,cx,cz);
        CostateDualSolver first=new CostateDualSolver(35,cx,cz,unpatterned.mMagAll(),unpatterned.compileWalls(rows,0,new boolean[1]));
        CostateDualSolver.Result initial=first.solve(.0003,null);assertNotNull(initial);assertEquals(319,first.lastIters);
        double[] initialYaws=unpatterned.recoverAlongCostate(objective,initial.gx,initial.gz);
        boolean[] zx=new boolean[35],zz=new boolean[35];unpatterned.zeroingPattern(initialYaws,.005,true,zx,zz);
        JumpLinearModel lin=new JumpLinearModel(sc,zx,zz);
        List<JumpLinearModel.Wall> walls=lin.compileWalls(rows,0,new boolean[1]);walls.addAll(lin.velocityWalls(.005));
        lin.objectiveVectors(objective,cx,cz);
        CostateDualSolver dual=new CostateDualSolver(35,cx,cz,lin.mMagAll(),walls);
        long start=System.nanoTime();CostateDualSolver.Result result=dual.solve(.0003,null);
        double elapsed=(System.nanoTime()-start)/1e6;
        assertNotNull(result);assertEquals(625,dual.lastIters);assertEquals("NO_DESCENT",dual.lastStopReason);
        int z89Index=-1;
        for(int j=0;j<walls.size();j++)if(walls.get(j).name.equals("Z@89lo"))z89Index=j;
        assertTrue(z89Index>=0);
        double z89Lambda=result.lambda[z89Index];assertEquals(1,z89Lambda,1e-9);
        for(int t=0;t<27;t++)assertEquals("pre82 inputs shift Z89/Z90 equally",lin.coefAxis(1,t,34),lin.coefAxis(1,t,35),0);
        double[][] ux=new double[3][35],uz=new double[3][35];double[][] yaws=new double[3][];
        yaws[1]=new double[35];yaws[2]=lin.recoverAlongCostate(objective,result.gx,result.gz);
        List<String> controls=new ArrayList<>(),residuals=new ArrayList<>(),summary=new ArrayList<>(),thresholds=new ArrayList<>();
        controls.add("tick,gx,gz,norm,fallback,acceleration_magnitude,derivative_ux,derivative_uz,ray_yaw,production_yaw,ray_ux,ray_uz,production_ux,production_uz,z89_coefficient,z90_coefficient,z89_lower_lambda,objective_plus_z89wall_gz");
        residuals.add("variant,row,is_inertia,lambda,tightened_bound,row_value,tightened_violation,original_violation");
        summary.add("variant,position_violation,tightened_wall_violation,tightened_inertia_violation,actual_continuous_violation,prefix_replay_violation");
        thresholds.add("variant,tick,axis,assumed_zero,velocity_before_zero,absolute_threshold_violation");
        int fallbackCount=0;
        for(int t=0;t<35;t++){
            double gx=result.gx[t],gz=result.gz[t],norm=StrictMath.hypot(gx,gz),denom=StrictMath.sqrt(gx*gx+gz*gz+1e-24);
            boolean fallback=gx*gx+gz*gz<1e-18;if(fallback)fallbackCount++;
            ux[0][t]=lin.mMag(t)*gx/denom;uz[0][t]=lin.mMag(t)*gz/denom;
            yaws[1][t]=lin.recoverYawDeg(t,gx,gz);
            for(int v=1;v<3;v++){
                double angle=lin.baseArg(t)+Angles.rad(yaws[v][t]);ux[v][t]=lin.mMag(t)*StrictMath.cos(angle);uz[v][t]=lin.mMag(t)*StrictMath.sin(angle);
            }
            controls.add(csv(55+t,gx,gz,norm,fallback,lin.mMag(t),ux[0][t],uz[0][t],yaws[1][t],yaws[2][t],ux[1][t],uz[1][t],ux[2][t],uz[2][t],lin.coefAxis(1,t,34),lin.coefAxis(1,t,35),z89Lambda,
                    cz[t]+z89Lambda*lin.coefAxis(1,t,34)));
        }
        assertEquals(27,fallbackCount);
        String[] names={"smoothed_derivative_variable_length","full_length_costate_direction_no_fallback","production_objective_direction_fallback"};
        JumpConstraintCompiler.Compiled compiled=JumpConstraintCompiler.compile(spec);
        for(int v=0;v<3;v++){
            ForwardPath p=new ForwardPath(new double[36],new double[36],new double[36]);p.posX[0]=pos.x;p.posZ[0]=pos.z;
            double vx=vel.x,vz=vel.z;
            for(int t=0;t<35;t++){
                if(zx[t])thresholds.add(csv(names[v],55+t,"X",true,vx,Math.max(0,Math.abs(vx)-.005)));
                if(zz[t])thresholds.add(csv(names[v],55+t,"Z",true,vz,Math.max(0,Math.abs(vz)-.005)));
                if(zx[t])vx=0;if(zz[t])vz=0;
                vx+=ux[v][t];vz+=uz[v][t];p.posX[t+1]=p.posX[t]+vx;p.posZ[t+1]=p.posZ[t]+vz;
                vx*=lin.friction(t);vz*=lin.friction(t);
            }
            double wallViolation=0,inertiaViolation=0;
            for(int j=0;j<walls.size();j++){
                JumpLinearModel.Wall w=walls.get(j);double dot=0;
                for(int t=0;t<35;t++){if(w.coefX!=null)dot+=w.coefX[t]*ux[v][t];if(w.coefZ!=null)dot+=w.coefZ[t]*uz[v][t];}
                double violation=dot-(w.bPrime-.0003);boolean isInertia=w.name.startsWith("inertia");
                if(isInertia)inertiaViolation=Math.max(inertiaViolation,violation);else wallViolation=Math.max(wallViolation,violation);
                residuals.add(csv(names[v],w.name,isInertia,result.lambda[j],w.bPrime-.0003,dot,violation,dot-w.bPrime));
            }
            double actual=Double.NaN,replay=Double.NaN;
            if(v>0){WindowCandidateValidation.Result check=new WindowCandidateValidation(exact,spec,9).evaluate(yaws[v],lin);
                actual=check.continuousViolation;replay=check.replayViolation;}
            summary.add(csv(names[v],compiled.maxViolation(new double[35],p),wallViolation,inertiaViolation,actual,replay));
        }
        Path out=Paths.get("build/reports/jump-six-zmin-recovery");Files.createDirectories(out);
        String[] files={"controls.csv","wall-residuals.csv","summary.csv","inertia-velocities.csv"};List<List<String>> data=Arrays.asList(controls,residuals,summary,thresholds);
        for(int i=0;i<files.length;i++)Files.write(out.resolve(files[i]),data.get(i),StandardCharsets.UTF_8);
        Files.write(out.resolve("metadata.txt"),Arrays.asList("EPS2=1e-24; margin0.0003; cap10000; same fixed tick55 state from audit metadata",
                "firstIter="+first.lastIters+" retryIter="+dual.lastIters+" stop="+dual.lastStopReason+" finalPg="+dual.lastFinalPgres,
                "fallbackControls="+fallbackCount+" retryMs="+elapsed,"Z89lo_lambda="+z89Lambda,"Y set0; authored contact schedule and X/Z physics independent of Y"),StandardCharsets.UTF_8);
        assertEquals(16.91609160957796,new WindowCandidateValidation(exact,spec,9).evaluate(yaws[2],lin).continuousViolation,1e-9);
    }
}
