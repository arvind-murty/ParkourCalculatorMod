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

/** Inspect archived failed states; scalar bounds allow independent axis choices, ignoring all walls. */
@Category(SlowSolverTests.class)
public class Tick90ContinuationProbeTest {
    private static Method method(Class<?> owner, String name, Class<?>... args) throws Exception {
        Method m = owner.getDeclaredMethod(name, args); m.setAccessible(true); return m;
    }
    private static String csv(Object... values) {
        StringBuilder s = new StringBuilder();
        for (Object v : values) {
            if (s.length() > 0) s.append(',');
            s.append('"').append(String.valueOf(v).replace("\"", "\"\"")).append('"');
        }
        return s.toString();
    }
    private static final class State {
        final Vec3dCore pos, vel; final float yaw;
        State(double x,double z,double vx,double vz,float yaw) {
            pos=new Vec3dCore(x,0,z); vel=new Vec3dCore(vx,0,vz); this.yaw=yaw;
        }
    }
    @SuppressWarnings("unchecked") private static JumpSpec slice(JumpSpec full, int a, int b, State state, Objective.Sense sense) throws Exception {
        JumpPhysicsInputs sc = (JumpPhysicsInputs) method(LongRunSolver.class,"sliceScenario",JumpPhysicsInputs.class,
                int.class,int.class,Vec3dCore.class,Vec3dCore.class,float.class)
                .invoke(null, full.asScenario(),a,b,state.pos,state.vel,state.yaw);
        List<JumpConstraint> rows = (List<JumpConstraint>) method(LongRunSolver.class,"sliceConstraints",
                JumpSpec.class,int.class,int.class,float.class).invoke(null,full,a,b,state.yaw);
        return new JumpSpec(sc, rows,new Objective(JumpPhysicsInputs.Axis.Z,sense,b-a));
    }
    private static double[] window(ExactJumpModel exact, JumpSpec spec) throws Exception {
        return (double[]) method(LongRunSolver.class,"solveWindow",ExactJumpModel.class,JumpPhysicsInputs.class,
                List.class,Objective.class,boolean.class,int.class,AtomicBoolean.class,long.class,Double.class)
                .invoke(null,exact,spec.asScenario(),spec.constraints,spec.objective,false,spec.asScenario().numTicks,
                        new AtomicBoolean(false),0L,Double.valueOf(.0003));
    }
    private static State exit(ExactJumpModel exact, JumpSpec spec, double[] yaws) {
        JumpPhysicsInputs sc=spec.asScenario(); double[] gf=sc.toGameFacings(yaws);
        ForwardPath p=exact.forward(sc,gf); int n=sc.numTicks;
        return new State(p.posX[n],p.posZ[n],p.velX[n],p.velZ[n],(float)gf[n-1]);
    }
    private static double zero(double v,double threshold) { return Math.abs(v)<threshold ? 0 : v; }
    private static void bounds(String label, ExactJumpModel exact, JumpSpec spec, List<String> steps, List<String> walls) {
        JumpPhysicsInputs sc=spec.asScenario(); JumpLinearModel lin=new JumpLinearModel(sc); int n=sc.numTicks;
        double[][] lo=new double[2][n+1],hi=new double[2][n+1];
        for(int axis=0;axis<2;axis++) {
            double pos=axis==0?sc.startPos.x:sc.startPos.z, velocity=axis==0?sc.initialVelocity.x:sc.initialVelocity.z;
            lo[axis][0]=hi[axis][0]=pos;
            double lv=velocity,hv=velocity;
            for(int t=0;t<n;t++) {
                double ld=zero(lv,exact.inertiaThreshold())-lin.mMag(t);
                double hd=zero(hv,exact.inertiaThreshold())+lin.mMag(t);
                lo[axis][t+1]=lo[axis][t]+ld; hi[axis][t+1]=hi[axis][t]+hd;
                lv=ld*lin.friction(t); hv=hd*lin.friction(t);
                steps.add(csv(label,axis==0?"X":"Z",90+t,lin.mMag(t),lin.friction(t),lo[axis][t],hi[axis][t],
                        lo[axis][t+1],hi[axis][t+1],lv,hv));
            }
        }
        for(JumpConstraint c:spec.constraints) {
            if(c.t2!=null || (c.mode!=JumpConstraint.Mode.X && c.mode!=JumpConstraint.Mode.Z)) continue;
            int axis=c.mode==JumpConstraint.Mode.X?0:1,t=c.t1;
            double best=c.cmp==JumpConstraint.Cmp.LE?c.rhs-lo[axis][t]:hi[axis][t]-c.rhs;
            walls.add(csv(label,c.name,c.mode,90+t,c.cmp,c.rhs,lo[axis][t],hi[axis][t],best));
        }
    }
    @Test public void inspectShortestContinuationAndPreviousObjective() throws Exception {
        Assume.assumeTrue("opt in with -Dpkc.marginProbe=true",Boolean.getBoolean("pkc.marginProbe"));
        String input=System.getenv("PKC_TRACE_INPUT");
        if(input==null) input="/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json";
        SaveFile file=SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get(input)),StandardCharsets.UTF_8)); assertNotNull(file);
        InputData inputs=new InputData(); SaveIO.applyRowsTo(file,inputs);
        AngleSolverState solverState=new AngleSolverState(); SaveIO.applyAngleSolverTo(file,solverState); solverState.setStartTick(0);
        ExactJumpModel exact=ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full=new AngleSolverEngine(solverState,Fixtures.buildBoxes(file),inputs,t->{},exact).debugBuildSpec();
        Path archive=Paths.get("../docs/research/solver-traces/full-forward-shifts-2026-10-23");
        List<String> results=new ArrayList<>(),steps=new ArrayList<>(),walls=new ArrayList<>(),counters=new ArrayList<>(),paths=new ArrayList<>(), fastest=new ArrayList<>();
        results.add("case,start_x,start_z,start_vx,start_vz,start_yaw,production_window_accepted,validation_calls");
        steps.add("case,axis,input_tick,input_magnitude,friction,min_position,max_position,next_min_position,next_max_position,next_min_velocity,next_max_velocity");
        walls.add("case,constraint,axis,tick,comparison,wall,min_position,max_position,best_possible_signed_margin");
        counters.add("case,jump13_objective,jump13_feasible,x90,z90,vx90,vz90,yaw90,jump14_accepted");
        paths.add("case,objective,tick,x,z,vx,vz,yaw");
        fastest.add("case,tick,continuous_x,continuous_z,continuous_vx,continuous_vz,replay_x,replay_z,replay_vx,replay_vz,yaw");
        Pattern seed=Pattern.compile("LRS-WIN commit=(\\d+).*ticks=\\[(88|90),(\\d+)\\) seed=\\(([^,]+),([^;]+);([^,]+),([^\\)]+)\\) yaw=([^ ]+)");
        SolverTrace.enable("tick90-continuation-audit");
        try {
            for(String run:new String[]{"seven-forward-jump","seven-forward-tick","ten-forward-jump","ten-forward-tick"}) {
                Map<String,State> seeds=new HashMap<>();
                for(String line:Files.readAllLines(archive.resolve(run+".txt"),StandardCharsets.UTF_8)) {
                    Matcher m=seed.matcher(line); if(!m.find()) continue;
                    seeds.put(m.group(1)+"/"+m.group(2),new State(Double.parseDouble(m.group(4)),Double.parseDouble(m.group(5)),
                            Double.parseDouble(m.group(6)),Double.parseDouble(m.group(7)),Float.parseFloat(m.group(8))));
                }
                for(int commit:new int[]{3,1}) {
                    String label=run+"-commit"+commit; State at90=seeds.get(commit+"/90"); assertNotNull(at90);
                    JumpSpec continuation=slice(full,90,99,at90,Objective.Sense.MAX);
                    bounds(label,exact,continuation,steps,walls);
                    JumpLinearModel linear=new JumpLinearModel(continuation.asScenario());
                    double[] pureMinusZ=new double[linear.n];
                    for(int t=0;t<linear.n;t++) pureMinusZ[t]=Angles.deg(-StrictMath.PI/2-linear.baseArg(t));
                    ForwardPath smooth=linear.forwardContinuous(pureMinusZ,exact.inertiaThreshold(),exact.perAxisInertia());
                    double[] game=continuation.asScenario().toGameFacings(pureMinusZ);
                    ForwardPath replay=exact.forward(continuation.asScenario(),game);
                    for(int t=0;t<=linear.n;t++) fastest.add(csv(label,90+t,smooth.posX[t],smooth.posZ[t],smooth.velX[t],smooth.velZ[t],
                            replay.posX[t],replay.posZ[t],replay.velX[t],replay.velZ[t],t<linear.n?game[t]:""));
                    int[] count={0}; WindowCandidateValidation.setDiagnosticObserver(d->count[0]++);
                    double[] yaws;
                    try { SolverTrace.log("AUDIT","begin case=%s shortest-original-state",label); yaws=window(exact,continuation); }
                    finally {WindowCandidateValidation.setDiagnosticObserver(null);}
                    results.add(csv(label,at90.pos.x,at90.pos.z,at90.vel.x,at90.vel.z,at90.yaw,yaws!=null,count[0]));
                    State at88=seeds.get(commit+"/88"); assertNotNull(at88);
                    for(Objective.Sense sense:Objective.Sense.values()) {
                        SolverTrace.log("AUDIT","counterfactual case=%s jump13=%s",label,sense);
                        JumpSpec previous=slice(full,88,90,at88,sense);
                        ClosedFormSolve.Config cfg=new ClosedFormSolve.Config(); cfg.marginsRobust=new double[]{.0003};
                        double[] proposal=ClosedFormSolve.optimizeRobust(exact,previous,0,new AtomicBoolean(false),cfg);
                        if(proposal==null) {counters.add(csv(label,sense,false,"","","","","",false)); continue;}
                        State next=exit(exact,previous,proposal);
                        double[] gf=previous.asScenario().toGameFacings(proposal); ForwardPath path=exact.forward(previous.asScenario(),gf);
                        for(int t=0;t<=2;t++) paths.add(csv(label,sense,88+t,path.posX[t],path.posZ[t],path.velX[t],path.velZ[t],t<2?gf[t]:""));
                        JumpSpec nextSpec=slice(full,90,99,next,Objective.Sense.MAX);
                        bounds(label+"-previous-"+sense,exact,nextSpec,steps,walls);
                        boolean accepted=window(exact,nextSpec)!=null;
                        counters.add(csv(label,sense,true,next.pos.x,next.pos.z,next.vel.x,next.vel.z,next.yaw,accepted));
                    }
                }
            }
        } finally {
            SolverTrace.disable(); Path out=Paths.get("build/reports/tick90-continuation"); Files.createDirectories(out);
            for(int i=0;i<6;i++) Files.write(out.resolve(new String[]{"outcomes.csv","axis-bounds.csv","constraint-bounds.csv","counterfactuals.csv","previous-window-paths.csv","fastest-minus-z-paths.csv"}[i]),
                    Arrays.asList(results,steps,walls,counters,paths,fastest).get(i),StandardCharsets.UTF_8);
        }
    }
}
