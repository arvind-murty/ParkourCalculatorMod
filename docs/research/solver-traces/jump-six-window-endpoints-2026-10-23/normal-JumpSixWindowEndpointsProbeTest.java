package de.legoshi.parkourcalc.core.anglesolver.solver;
import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.*;
import de.legoshi.parkourcalc.core.save.*;
import de.legoshi.parkourcalc.core.ui.InputData;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicBoolean;
import static org.junit.Assert.*;
@Category(SlowSolverTests.class)
public class JumpSixWindowEndpointsProbeTest {
 @Test public void endpoints() throws Exception {
  String run=System.getProperty("pkc.endpointRun"); boolean fixed="fixed".equals(run);
  Path artifacts=Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod/docs/research/solver-traces/fixed-seven-window-local-bounds-2026-10-23");
  SaveFile file=SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get("/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json")),StandardCharsets.UTF_8));
  InputData input=new InputData();SaveIO.applyRowsTo(file,input);
  AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);if(!fixed)state.setLandingTick(250);
  ExactJumpModel exact=ExactJumpModel.forMcVersion(file.mcVersion);
  JumpSpec full=new AngleSolverEngine(state,Fixtures.buildBoxes(file),input,t->{},exact).debugBuildSpec();
  Method scenario=LongRunSolver.class.getDeclaredMethod("sliceScenario",JumpPhysicsInputs.class,int.class,int.class,Vec3dCore.class,Vec3dCore.class,float.class);scenario.setAccessible(true);
  Method constraints=LongRunSolver.class.getDeclaredMethod("sliceConstraints",JumpSpec.class,int.class,int.class,float.class);constraints.setAccessible(true);
  List<String> expected=Files.readAllLines(artifacts.resolve("committed-jumps1-5-every-tick.csv"));
  String[] headers=expected.get(0).split(",");Map<String,Integer> index=new HashMap<>();for(int k=0;k<headers.length;k++)index.put(headers[k],k);
  List<String> output=new ArrayList<>();output.add("run,start_jump,end_jump,start_tick,end_tick,continuous_x,continuous_z,replay_x,replay_z,continuous_vx,continuous_vz,replay_vx,replay_vz,max_prefix_position_diff,max_prefix_yaw_diff");
  int[] ends={68,71,75,77,80,88};int[] commits={12,21,30,40,55,64};
  SolverTrace.enable("jump-six-exact-endpoints-"+run);
  try {
   Vec3dCore pos=full.asScenario().startPos,vel=full.asScenario().initialVelocity;float yaw=full.asScenario().startYaw;
   for(String row:Files.readAllLines(artifacts.resolve("committed-jumps1-5-seams.csv")).subList(1,13)) {
    String[] r=row.split(",");if(!r[0].equals(run))continue;int j=Integer.parseInt(r[1]);if(j>6)continue;int a=Integer.parseInt(r[2]),c=ends[j-1];
    JumpPhysicsInputs win=(JumpPhysicsInputs)scenario.invoke(null,full.asScenario(),a,c,pos,vel,yaw);
    @SuppressWarnings("unchecked") List<JumpConstraint> rows=(List<JumpConstraint>)constraints.invoke(null,full,a,c,yaw);
    Objective objective=new Objective(JumpPhysicsInputs.Axis.Z,j==6?Objective.Sense.MIN:Objective.Sense.MAX,c-a);
    if(fixed){List<JumpConstraint> retained=new ArrayList<>();for(JumpConstraint q:rows)if(!(q.mode==JumpConstraint.Mode.Z && q.t1==c-a && q.t2==null && ((j==6?q.cmp==JumpConstraint.Cmp.GE:q.cmp==JumpConstraint.Cmp.LE) || q.cmp==JumpConstraint.Cmp.EQ)))retained.add(q);rows=retained;}
    ClosedFormSolve.Config cfg=new ClosedFormSolve.Config();cfg.replayTickLimit=commits[j-1]-a;if(fixed){cfg.margins=new double[]{.0003};cfg.marginsRobust=new double[]{.0003};}
    double[] y=ClosedFormSolve.optimizeRobust(exact,new JumpSpec(win,rows,objective),0,new AtomicBoolean(false),cfg);assertNotNull("window"+j,y);
    double[] game=win.toGameFacings(y);ForwardPath replay=exact.forward(win,game);ForwardPath continuous=new JumpLinearModel(win).forwardContinuous(y,exact.inertiaThreshold(),exact.perAxisInertia());
    if(j==6){
     List<String> audit=new ArrayList<>();audit.add("tick,name,axis,comparison,bound,committed,continuous_value,continuous_margin,replay_value,replay_margin");
     for(JumpConstraint cc:rows){double cr=JumpConstraintCompiler.evaluate(cc,y,continuous),rr=JumpConstraintCompiler.evaluate(cc,game,replay);double cm=cc.cmp==JumpConstraint.Cmp.LE?-cr:cc.cmp==JumpConstraint.Cmp.GE?cr:-Math.abs(cr);double rm=cc.cmp==JumpConstraint.Cmp.LE?-rr:cc.cmp==JumpConstraint.Cmp.GE?rr:-Math.abs(rr);audit.add(String.format(java.util.Locale.ROOT,"%d,%s,%s,%s,%.12f,%s,%.12f,%.12f,%.12f,%.12f",a+cc.t1,cc.name,cc.mode,cc.cmp,cc.rhs,cc.t1<=commits[j-1]-a,cr+cc.rhs,cm,rr+cc.rhs,rm));}
     Files.write(Paths.get("/tmp/pkc-jump-six-normal-margins.csv"),audit,StandardCharsets.UTF_8);
    }
    double pd=0,yd=0;
    for(int t=a;t<=commits[j-1] && j<6;t++){
     String[] saved=expected.get(t+1).split(",");int k=t-a;
     pd=Math.max(pd,Math.abs(replay.posX[k]-Double.parseDouble(saved[index.get(run+"_x")])));
     pd=Math.max(pd,Math.abs(replay.posZ[k]-Double.parseDouble(saved[index.get(run+"_z")])));
     if(t<commits[j-1])yd=Math.max(yd,Math.abs(game[k]-Double.parseDouble(saved[index.get(run+"_yaw")])));
    }
    assertEquals("position matches saved "+run+" window"+j,0,pd,1e-9);assertEquals("yaw matches saved "+run+" window"+j,0,yd,1e-7);
    int ct=commits[j-1]-a;pos=new Vec3dCore(replay.posX[ct],replay.posY[ct],replay.posZ[ct]);vel=new Vec3dCore(replay.velX[ct],replay.velY[ct],replay.velZ[ct]);yaw=(float)game[ct-1];
    int n=c-a;output.add(String.format(java.util.Locale.ROOT,"%s,%d,%d,%d,%d,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12g,%.12g",run,j,j+6,a,c,continuous.posX[n],continuous.posZ[n],replay.posX[n],replay.posZ[n],continuous.velX[n],continuous.velZ[n],replay.velX[n],replay.velZ[n],pd,yd));
   }
   Files.write(Paths.get("/tmp/pkc-jump-six-exact-endpoints-"+run+".csv"),output,StandardCharsets.UTF_8);
  } finally {SolverTrace.disable();}
 }
}
