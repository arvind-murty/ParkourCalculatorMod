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
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.Properties;
import java.util.concurrent.atomic.AtomicBoolean;
import static org.junit.Assert.*;

@Category(SlowSolverTests.class)
public class InertiaMultiplierSeedProbeTest {
    private static final Path ROOT = Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod");
    private static final String CASES = System.getProperty("pkc.inertiaSeedCases", "baseline");
    private static final boolean REPAIR = "j14-near-repair".equals(CASES);
    private static final boolean J14 = REPAIR || "j14-retained".equals(CASES);
    private static final boolean RETAIN = J14 || "j13-retained".equals(CASES);
    private static final boolean DETAIL = (!J14 && RETAIN) || "j13-inertia-detail".equals(CASES);
    private static final boolean CLEAR_CAP = RETAIN || DETAIL || "post-turn-clear-cap".equals(CASES);
    private static final boolean POST_TURN = CLEAR_CAP || "post-turn".equals(CASES);
    private static final Path OUT = ROOT.resolve("docs/research/solver-traces/"+(REPAIR?"j14-near-repair-2026-10-24":J14?"j14-retained-2026-10-24":RETAIN?"j13-retained-2026-10-24":DETAIL?"j13-inertia-detail-2026-10-24":CLEAR_CAP?"post-turn-clear-cap-2026-10-24":POST_TURN?"post-turn-objectives-2026-10-24":"inertia-multiplier-seeding-2026-10-24"));

    @Test public void compareColdAndCarriedMultipliers() throws Exception {
        org.junit.Assume.assumeTrue(Boolean.getBoolean("pkc.inertiaSeedProbe"));
        Files.createDirectories(OUT);
        SaveFile file = SaveIO.parseSafe(new String(Files.readAllBytes(Paths.get("/Users/arvindmurty/Downloads/LinkcraftII_Competition_01/real/11.json")), StandardCharsets.UTF_8));
        InputData input = new InputData(); SaveIO.applyRowsTo(file, input);
        AngleSolverState state = new AngleSolverState(); SaveIO.applyAngleSolverTo(file, state); state.setStartTick(0);
        ExactJumpModel exact = ExactJumpModel.forMcVersion(file.mcVersion);
        JumpSpec full = new AngleSolverEngine(state, Fixtures.buildBoxes(file), input, t -> {}, exact).debugBuildSpec();
        Properties saved = new Properties();
        saved.load(Files.newBufferedReader(ROOT.resolve("docs/research/solver-traces/manual-coupled-window-2026-10-24/entry.txt"), StandardCharsets.UTF_8));
        ForwardPath vertical = exact.forward(full.asScenario(), new double[full.asScenario().numTicks]);
        Vec3dCore pos = new Vec3dCore(Double.valueOf(saved.getProperty("x")), vertical.posY[55], Double.valueOf(saved.getProperty("z")));
        Vec3dCore vel = new Vec3dCore(Double.valueOf(saved.getProperty("vx")), vertical.velY[55], Double.valueOf(saved.getProperty("vz")));
        float yaw = Float.valueOf(saved.getProperty("yaw"));
        Method slice = LongRunSolver.class.getDeclaredMethod("sliceScenario", JumpPhysicsInputs.class, int.class, int.class, Vec3dCore.class, Vec3dCore.class, float.class);
        Method rowsAt = LongRunSolver.class.getDeclaredMethod("sliceConstraints", JumpSpec.class, int.class, int.class, float.class);
        slice.setAccessible(true); rowsAt.setAccessible(true);
        List<String> result = new ArrayList<>();
        result.add("end_jump,objective,seed_scale,pass,pattern,rows,seeded_rows,initial_dual,initial_residual,initial_min_costate,iters,stop,final_residual,final_min_costate,fallback_ticks,continuous_violation,prefix_replay_violation,assumed_violation,failed_inertia_assumptions,elapsed_ms");
        List<String> outcomes = new ArrayList<>(); outcomes.add("end_jump,objective,seed_scale,accepted,passes,best_violation,total_solve_ms");
        int[] ends = J14 ? new int[]{103} : DETAIL ? new int[]{90} : CLEAR_CAP ? new int[]{90,103} : POST_TURN ? new int[]{90,99} : new int[]{75,77,99,113};
        int[] jumps = J14 ? new int[]{14} : POST_TURN ? new int[]{13,14} : new int[]{9,10,14,15};
        SolverTrace.enable("inertia-multiplier-seeding");
        try {
            for (int k = 0; k < ends.length; k++) {
                Objective.Sense sense = J14 ? Objective.Sense.MAX : POST_TURN ? (k==0?Objective.Sense.MIN:Objective.Sense.MAX) : (k<2?Objective.Sense.MAX:Objective.Sense.MIN);
                JumpPhysicsInputs win = (JumpPhysicsInputs) slice.invoke(null, full.asScenario(), 55, ends[k], pos, vel, yaw);
                @SuppressWarnings("unchecked") List<JumpConstraint> rows = (List<JumpConstraint>) rowsAt.invoke(null, full, 55, ends[k], yaw);
                Objective objective = new Objective(JumpPhysicsInputs.Axis.Z, sense, win.numTicks);
                List<JumpConstraint> keep = new ArrayList<>();
                for (JumpConstraint c : rows) if (POST_TURN || !ObjectiveBounds.isDirectCap(objective,c)) keep.add(c);
                JumpSpec spec = new JumpSpec(win, keep, objective);
                assertFalse(JumpLinearModel.hasFacingWall(spec.constraints));
                if(CLEAR_CAP) {
                    assertNull("objective tick is outside the repeated capping Z rows",ObjectiveBounds.directCap(spec));
                    assertEquals("all authored rows retained",rows.size(),spec.constraints.size());
                    int expectedLast=J14?102:k==0?89:102;
                    assertTrue("last capped state retained",spec.constraints.stream().anyMatch(c->c.mode==JumpConstraint.Mode.Z && c.t1==expectedLast-55 && c.cmp==JumpConstraint.Cmp.LE));
                }
                for (double scale : REPAIR ? new double[]{0.0,1.0,-1.0,-2.0} : DETAIL ? new double[]{-1.0} : new double[]{0.0,1.0,0.25,-1.0}) run(exact,spec,jumps[k],scale,result,outcomes);
                if(RETAIN && !REPAIR) runProductionControl(exact,spec);
            }
        } finally { SolverTrace.disable(); }
    }

    private static void run(ExactJumpModel exact, JumpSpec spec, int endJump, double scale,
                            List<String> result, List<String> outcomes) throws Exception {
        int n = spec.asScenario().numTicks;
        boolean[] zx = null, zz = null;
        List<JumpLinearModel.Wall> previousWalls = null;
        JumpLinearModel previousModel = null;
        double[] previousLambda = null;
        double[] previousAngles = null;
        double[] previousGx = null, previousGz = null;
        double best = Double.POSITIVE_INFINITY, solveMs = 0.0;
        boolean accepted = false;
        int passes = 0;
        String label = "j"+endJump+"-seed"+scale;
        for (int pass = 0; pass < 4; pass++) {
            JumpLinearModel lin = new JumpLinearModel(spec.asScenario(),zx,zz);
            double[] cx = new double[n], cz = new double[n]; lin.objectiveVectors(spec.objective,cx,cz);
            boolean[] bad = {false}; List<JumpLinearModel.Wall> walls = lin.compileWalls(spec.constraints,0.0,bad);
            assertFalse(bad[0]); int authoredRows = walls.size();
            walls.addAll(lin.velocityWalls(exact.inertiaThreshold()));
            FacingPrefold pre = FacingPrefold.analyze(spec.constraints,lin); assertNotNull(pre); assertTrue(pre.isIdentity());
            FacingPrefold.Reduced reduced = pre.reduce(cx,cz,lin.mMagAll(),walls);
            if(DETAIL) {
                List<String> coefficients=new ArrayList<>();coefficients.add("tick,objective_x,objective_z,Z89lo_normal,objective_minus_Z89lo");
                JumpLinearModel.Wall z89=null;
                for(JumpLinearModel.Wall w:reduced.walls)if(w.name.equals("Z@89lo"))z89=w;
                assertNotNull(z89);
                for(int t=0;t<n;t++)coefficients.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%.17g,%.17g",55+t,reduced.cx[t],reduced.cz[t],z89.coef[t],reduced.cz[t]-z89.coef[t]));
                Files.write(OUT.resolve(label+"-pass"+pass+"-coefficients.csv"),coefficients,StandardCharsets.UTF_8);
            }
            if(REPAIR && pass==3) {
                final WindowCandidateValidation.Diagnostics[] near={null};
                WindowCandidateValidation.setDiagnosticObserver(d->near[0]=d);
                try {new WindowCandidateValidation(exact,spec,9).evaluate(previousAngles,lin);}
                finally {WindowCandidateValidation.setDiagnosticObserver(null);}
                saveDiagnostics(label+"-same-angles",pass,near[0]);
                ForwardPath oldPath=previousModel.forwardFixed(previousAngles),newPath=lin.forwardFixed(previousAngles);
                List<String> positions=new ArrayList<>();positions.add("tick,old_fixed_x,new_fixed_x,actual_x,old_fixed_z,new_fixed_z,actual_z");
                for(int t=0;t<=n;t++)positions.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",55+t,oldPath.posX[t],newPath.posX[t],near[0].continuousPath.posX[t],oldPath.posZ[t],newPath.posZ[t],near[0].continuousPath.posZ[t]));
                Files.write(OUT.resolve(label+"-same-angles-positions.csv"),positions,StandardCharsets.UTF_8);
            }
            double[] seed = null; int seeded = 0;
            if (scale != 0.0 && previousLambda != null && (!REPAIR || pass>=3 || scale==-2.0)) {
                seed = new double[reduced.walls.size()];
                for (int j = 0; j < authoredRows; j++) {
                    JumpLinearModel.Wall old = previousWalls.get(j), next = reduced.walls.get(j);
                    assertEquals(old.name,next.name); assertEquals(old.axis,next.axis); assertEquals(old.eq,next.eq);
                    seed[j] = previousLambda[j]*(scale<0.0?1.0:scale);
                    if (seed[j] != 0.0) seeded++;
                }
            }
            boolean preserved=false;
            if (scale<0.0 && seed!=null) {
                preserved=seedBandPrices(reduced,seed,authoredRows,previousGx,previousGz,lin,label,pass);
                if(!preserved) {
                    Arrays.fill(seed,authoredRows,seed.length,0.0);
                    SolverTrace.log("SEED", "case=%s pass=%d preservation declined; retain mapped position weights only",label,pass);
                }
                seeded=0;for(double v:seed)if(v!=0.0)seeded++;
            }
            CostateDualSolver solver = new CostateDualSolver(reduced.n,reduced.cx,reduced.cz,reduced.mMag,reduced.walls);
            double[] first = new double[3], last = new double[2];
            List<String> detailed = new ArrayList<>();
            detailed.add("iteration,dual,residual,fallback_ticks,min_radius_ratio,relaxed_position_violation,relaxed_band_violation,raw_fixed_violation,raw_continuous_violation,decoded_fixed_violation,decoded_continuous_violation,max_decoder_yaw_change,raw_final_z,decoded_final_z,shared_prefix_violation,shared_continuous_violation");
            List<String> tickDetail = new ArrayList<>();
            tickDetail.add("iteration,tick,gx,gz,norm,radius_ratio,raw_yaw,decoded_yaw,raw_assumed_vx,raw_assumed_vz");
            final double[] auditGx = DETAIL ? arrayOf(solver,"gx") : null;
            final double[] auditGz = DETAIL ? arrayOf(solver,"gz") : null;
            final double[] auditUx = DETAIL ? arrayOf(solver,"ux") : null;
            final double[] auditUz = DETAIL ? arrayOf(solver,"uz") : null;
            List<String> history = new ArrayList<>(); history.add("iteration,dual,residual,input_change,min_costate,fallback,final_sample");
            solver.iterationObserver = (it,value,residual,change,min,near,fallback,finalSample) -> {
                if (it==0) {first[0]=value;first[1]=residual;first[2]=min;}
                if (finalSample) {last[0]=min;last[1]=fallback;}
                if (DETAIL) auditIteration(it,value,residual,fallback,lin,pre,spec,reduced,authoredRows,auditGx,auditGz,auditUx,auditUz,detailed,tickDetail,exact);
                if (it%100==0 || finalSample) history.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%.17g,%.17g,%d,%s",it,value,residual,change,min,fallback,finalSample));
            };
            SolverTrace.log("SEED", "case=%s pass=%d pattern=%s seedRows=%d",label,pass,lin.patternDescription(),seeded);
            double[] initialGx = reduced.cx.clone(), initialGz = reduced.cz.clone();
            if (seed != null) for (int j=0;j<seed.length;j++) {
                JumpLinearModel.Wall w = reduced.walls.get(j); assertTrue(w.axis<2);
                for (int t=0;t<n;t++) {if(w.axis==0)initialGx[t]-=seed[j]*w.coef[t];else initialGz[t]-=seed[j]*w.coef[t];}
            }
            double[] initialAngles = pre.expand(lin,spec.objective,new CostateDualSolver.Result(initialGx,initialGz,seed==null?new double[walls.size()]:seed,Double.NaN,0.0,0.0));
            WindowCandidateValidation.Result initialCheck = new WindowCandidateValidation(exact,spec,9).evaluate(initialAngles);
            double yawChange = 0.0;
            if(previousAngles!=null)for(int t=0;t<n;t++)yawChange=Math.max(yawChange,Math.abs(Angles.wrap(initialAngles[t]-previousAngles[t])));
            if(preserved)assertTrue("verified costates preserve recovered angles",yawChange<1e-6);
            List<String> initial = new ArrayList<>();
            initial.add("continuous_violation,prefix_replay_violation,max_yaw_change_from_previous,preservation_verified");
            initial.add(String.format(Locale.ROOT,"%.17g,%.17g,%.17g,%s",initialCheck.continuousViolation,initialCheck.replayViolation,previousAngles==null?Double.NaN:yawChange,preserved));
            Files.write(OUT.resolve(label+"-pass"+pass+"-initial.csv"),initial,StandardCharsets.UTF_8);
            DualCandidateRetention retained = RETAIN ? new DualCandidateRetention(exact,spec,lin,pre,9,0.0) : null;
            solver.candidateObserver = retained;
            long t0 = System.nanoTime(); CostateDualSolver.Result r = solver.solve(.0003,seed,10000);
            double ms = (System.nanoTime()-t0)/1e6; solveMs += ms; passes++;
            if(DETAIL) {
                Files.write(OUT.resolve(label+"-pass"+pass+"-detailed.csv"),detailed,StandardCharsets.UTF_8);
                Files.write(OUT.resolve(label+"-pass"+pass+"-tick-detail.csv"),tickDetail,StandardCharsets.UTF_8);
            }
            if(r==null) {
                result.add(String.format(Locale.ROOT,"%d,Z/%s,%.2f,%d,\"%s\",%d,%d,%.17g,%.17g,%.17g,%d,%s,NaN,NaN,NaN,NaN,NaN,NaN,-1,%.3f",endJump,spec.objective.sense,scale,pass,lin.patternDescription(),walls.size(),seeded,first[0],first[1],first[2],solver.lastIters,solver.lastStopReason,ms));
                Files.write(OUT.resolve("passes.csv"),result,StandardCharsets.UTF_8);
                Files.write(OUT.resolve(label+"-pass"+pass+"-iterations.csv"),history,StandardCharsets.UTF_8);
                break;
            }
            double[] angles = pre.expand(lin,spec.objective,r);
            if(RETAIN) {
                WindowCandidateValidation.Result terminal=new WindowCandidateValidation(exact,spec,9).evaluate(angles,lin);
                boolean selected=retained.preferredTo(angles,terminal.maxViolation());
                List<String> saved=Arrays.asList("terminal_iteration,terminal_stop,terminal_violation,selected,retained_iteration,improvements,score,audit_ms",
                        String.format(Locale.ROOT,"%d,%s,%.17g,%s,%d,%d,%.17g,%.3f",solver.lastIters,solver.lastStopReason,terminal.maxViolation(),selected,retained.iteration,retained.improvements,retained.score,retained.evaluationNanos/1e6));
                Files.write(OUT.resolve(label+"-pass"+pass+"-retention.csv"),saved,StandardCharsets.UTF_8);
                if(selected) {
                    angles=retained.yaws;
                    List<String> savedWeights=new ArrayList<>();savedWeights.add("index,name,multiplier");
                    for(int j=0;j<retained.multipliers.length;j++)savedWeights.add(String.format(Locale.ROOT,"%d,%s,%.17g",j,reduced.walls.get(j).name,retained.multipliers[j]));
                    Files.write(OUT.resolve(label+"-pass"+pass+"-retained-multipliers.csv"),savedWeights,StandardCharsets.UTF_8);
                }
            }
            final WindowCandidateValidation.Diagnostics[] diagnostic = new WindowCandidateValidation.Diagnostics[1];
            WindowCandidateValidation.setDiagnosticObserver(d -> diagnostic[0]=d);
            WindowCandidateValidation.Result check;
            try {check = new WindowCandidateValidation(exact,spec,9).evaluate(angles,lin);}
            finally {WindowCandidateValidation.setDiagnosticObserver(null);}
            double assumed = JumpConstraintCompiler.compile(spec).maxViolation(angles,lin.forwardFixed(angles));
            int failed = 0;
            for (WindowCandidateValidation.InertiaCheck c : diagnostic[0].inertiaChecks) if (!c.assumptionSatisfied) failed++;
            result.add(String.format(Locale.ROOT,"%d,Z/%s,%.2f,%d,\"%s\",%d,%d,%.17g,%.17g,%.17g,%d,%s,%.17g,%.17g,%.0f,%.17g,%.17g,%.17g,%d,%.3f",endJump,spec.objective.sense,scale,pass,lin.patternDescription(),walls.size(),seeded,first[0],first[1],first[2],solver.lastIters,solver.lastStopReason,solver.lastFinalPgres,last[0],last[1],check.continuousViolation,check.replayViolation,assumed,failed,ms));
            Files.write(OUT.resolve("passes.csv"),result,StandardCharsets.UTF_8);
            Files.write(OUT.resolve(label+"-pass"+pass+"-iterations.csv"),history,StandardCharsets.UTF_8);
            saveDiagnostics(label,pass,diagnostic[0]);
            List<String> weights = new ArrayList<>();weights.add("index,name,axis,seed,final_lambda");
            for(int j=0;j<reduced.walls.size();j++)weights.add(String.format(Locale.ROOT,"%d,%s,%d,%.17g,%.17g",j,reduced.walls.get(j).name,reduced.walls.get(j).axis,seed==null?0.0:seed[j],r.lambda[j]));
            Files.write(OUT.resolve(label+"-pass"+pass+"-multipliers.csv"),weights,StandardCharsets.UTF_8);
            List<String> costates = new ArrayList<>();costates.add("tick,initial_gx,initial_gz,final_gx,final_gz,final_norm");
            for(int t=0;t<n;t++)costates.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%.17g,%.17g,%.17g",55+t,initialGx[t],initialGz[t],r.gx[t],r.gz[t],StrictMath.hypot(r.gx[t],r.gz[t])));
            Files.write(OUT.resolve(label+"-pass"+pass+"-costates.csv"),costates,StandardCharsets.UTF_8);
            boolean improved = check.maxViolation() < best;
            best = Math.min(best,check.maxViolation());
            if (check.maxViolation()==0.0) {accepted=true;break;}
            if (pass==0 && !POST_TURN) {
                double expected = endJump==9?.010383205522430217:endJump==10?.008981865496934915:.009402272;
                assertEquals("initial pass reproduces archived window",expected,check.continuousViolation,1e-9);
            }
            if (pass>0 && !improved) break;
            boolean[] nx = new boolean[n], nz = new boolean[n];
            new JumpLinearModel(spec.asScenario()).zeroingPattern(angles,exact.inertiaThreshold(),true,nx,nz);
            if (Arrays.equals(zx,nx) && Arrays.equals(zz,nz)) break;
            zx=nx;zz=nz;previousModel=lin;previousWalls=reduced.walls;previousLambda=r.lambda;previousAngles=angles;previousGx=r.gx;previousGz=r.gz;
        }
        outcomes.add(String.format(Locale.ROOT,"%d,Z/%s,%.2f,%s,%d,%.17g,%.3f",endJump,spec.objective.sense,scale,accepted,passes,best,solveMs));
        Files.write(OUT.resolve("outcomes.csv"),outcomes,StandardCharsets.UTF_8);
        System.out.println(outcomes.get(outcomes.size()-1));
    }
    private static void runProductionControl(ExactJumpModel exact,JumpSpec spec) throws Exception {
        List<String> rows=new ArrayList<>();rows.add("retain,accepted,continuous_violation,prefix_replay_violation,objective,elapsed_ms");
        for(boolean retain:new boolean[]{false,true}) {
            ClosedFormSolve.Config cfg=new ClosedFormSolve.Config();
            cfg.marginsRobust=new double[]{.0003};cfg.replayTickLimit=9;cfg.dualMaxIterations=10000;
            cfg.retainIntermediateCandidates=retain;
            long started=System.nanoTime();
            double[] yaws=ClosedFormSolve.optimizeRobust(exact,spec,0.0,new AtomicBoolean(false),cfg);
            double elapsed=(System.nanoTime()-started)/1e6;
            WindowCandidateValidation.Result check=yaws==null?null:new WindowCandidateValidation(exact,spec,9).evaluate(yaws);
            double objective=yaws==null?Double.NaN:spec.objective.evaluate(new JumpLinearModel(spec.asScenario()).forwardContinuous(yaws,.005,true));
            rows.add(String.format(Locale.ROOT,"%s,%s,%.17g,%.17g,%.17g,%.3f",retain,yaws!=null,check==null?Double.NaN:check.continuousViolation,check==null?Double.NaN:check.replayViolation,objective,elapsed));
            Files.write(OUT.resolve("production-control.csv"),rows,StandardCharsets.UTF_8);
            if(yaws!=null)assertEquals(0.0,check.maxViolation(),0.0);
        }
    }

    private static double[] arrayOf(Object instance,String name) throws Exception {
        java.lang.reflect.Field f=instance.getClass().getDeclaredField(name);
        f.setAccessible(true);
        return (double[])f.get(instance);
    }

    private static void auditIteration(int it,double value,double residual,int fallback,JumpLinearModel lin,
            FacingPrefold pre,JumpSpec spec,FacingPrefold.Reduced reduced,int authoredRows,
            double[] gx,double[] gz,double[] ux,double[] uz,List<String> detailed,List<String> ticks,ExactJumpModel exact) {
        int n=gx.length;
        double[] raw=new double[n],decoded=pre.expand(lin,spec.objective,gx,gz);
        double ratio=1.0,yawChange=0.0;
        for(int t=0;t<n;t++) {
            raw[t]=lin.recoverYawDeg(t,gx[t],gz[t]);
            double norm=StrictMath.hypot(gx[t],gz[t]);
            double r=norm/StrictMath.sqrt(norm*norm+1e-24);
            ratio=Math.min(ratio,r);
            yawChange=Math.max(yawChange,Math.abs(Angles.wrap(raw[t]-decoded[t])));
            ticks.add(String.format(Locale.ROOT,"%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",it,55+t,gx[t],gz[t],norm,r,raw[t],decoded[t],lin.velocityBeforeCheck(0,t,raw),lin.velocityBeforeCheck(1,t,raw)));
        }
        double pv=0.0,bv=0.0;
        for(int j=0;j<reduced.walls.size();j++) {
            JumpLinearModel.Wall w=reduced.walls.get(j);
            double dot=0.0;double[] u=w.axis==0?ux:uz;
            for(int t=0;t<n;t++)dot+=w.coef[t]*u[t];
            double violation=Math.max(0.0,dot-w.bPrime);
            if(j<authoredRows)pv=Math.max(pv,violation);else bv=Math.max(bv,violation);
        }
        JumpConstraintCompiler.Compiled compiled=JumpConstraintCompiler.compile(spec);
        JumpLinearModel physical=new JumpLinearModel(spec.asScenario());
        ForwardPath rp=physical.forwardContinuous(raw,.005,true),dp=physical.forwardContinuous(decoded,.005,true);
        final WindowCandidateValidation.Diagnostics[] audit=new WindowCandidateValidation.Diagnostics[1];
        WindowCandidateValidation.setDiagnosticObserver(d->audit[0]=d);
        WindowCandidateValidation.Result check;
        try {check=new WindowCandidateValidation(exact,spec,9).evaluate(decoded,lin);}
        finally {WindowCandidateValidation.setDiagnosticObserver(null);}
        if(it==5 || it==20 || it==50) {
            try {saveDiagnostics("snapshot-"+(lin.patternDescription().equals("free")?"free":"patterned")+"-iteration"+it,0,audit[0]);}
            catch(Exception e){throw new RuntimeException(e);}
        }
        detailed.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",it,value,residual,fallback,ratio,pv,bv,compiled.maxViolation(raw,lin.forwardFixed(raw)),compiled.maxViolation(raw,rp),compiled.maxViolation(decoded,lin.forwardFixed(decoded)),compiled.maxViolation(decoded,dp),yawChange,rp.posZ[n],dp.posZ[n],check.replayViolation,check.continuousViolation));
    }

    private static boolean seedBandPrices(FacingPrefold.Reduced reduced, double[] seed, int authoredRows,
                                       double[] oldGx, double[] oldGz, JumpLinearModel lin, String label, int pass) throws Exception {
        double[] dx=reduced.cx.clone(),dz=reduced.cz.clone();
        for(int j=0;j<authoredRows;j++) {
            JumpLinearModel.Wall w=reduced.walls.get(j);
            for(int t=0;t<reduced.n;t++){if(w.axis==0)dx[t]-=seed[j]*w.coef[t];else dz[t]-=seed[j]*w.coef[t];}
        }
        for(int t=0;t<reduced.n;t++){dx[t]-=oldGx[t];dz[t]-=oldGz[t];}
        for(int j=authoredRows;j+1<reduced.walls.size();j+=2) {
            JumpLinearModel.Wall hi=reduced.walls.get(j),lo=reduced.walls.get(j+1);
            assertEquals(hi.axis,lo.axis);
            double dot=0.0,norm2=0.0;double[] delta=hi.axis==0?dx:dz;
            for(int t=0;t<reduced.n;t++){assertEquals(hi.coef[t],-lo.coef[t],0.0);dot+=hi.coef[t]*delta[t];norm2+=hi.coef[t]*hi.coef[t];}
            double price=norm2==0.0?0.0:dot/norm2;
            seed[j]=Math.max(price,0.0);seed[j+1]=Math.max(-price,0.0);
            for(int t=0;t<reduced.n;t++)delta[t]-=price*hi.coef[t];
        }
        double[] actualX=reduced.cx.clone(),actualZ=reduced.cz.clone();
        for(int j=0;j<seed.length;j++) {
            if(!Double.isFinite(seed[j]) || seed[j]<0.0)return false;
            JumpLinearModel.Wall w=reduced.walls.get(j);
            for(int t=0;t<reduced.n;t++){if(w.axis==0)actualX[t]-=seed[j]*w.coef[t];else actualZ[t]-=seed[j]*w.coef[t];}
        }
        boolean verified=true;
        List<String> errors=new ArrayList<>();errors.add("tick,old_gx,old_gz,new_gx,new_gz,old_norm,error,allowed_error,raw_yaw_change");
        for(int t=0;t<reduced.n;t++) {
            double error=StrictMath.hypot(actualX[t]-oldGx[t],actualZ[t]-oldGz[t]);
            double norm=StrictMath.hypot(oldGx[t],oldGz[t]);
            double allowed=1e-8*Math.max(norm,1e-12);
            double change=Math.abs(Angles.wrap(lin.recoverYawDeg(t,actualX[t],actualZ[t])-lin.recoverYawDeg(t,oldGx[t],oldGz[t])));
            errors.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",55+t,oldGx[t],oldGz[t],actualX[t],actualZ[t],norm,error,allowed,change));
            if(error>allowed)verified=false;
        }
        if(REPAIR)Files.write(OUT.resolve(label+"-pass"+pass+"-seed-errors.csv"),errors,StandardCharsets.UTF_8);
        return verified;
    }
    private static void saveDiagnostics(String label, int pass, WindowCandidateValidation.Diagnostics d) throws Exception {
        List<String> margins = new ArrayList<>();margins.add("tick,name,comparison,continuous_margin,replay_margin,assumed_margin");
        for (WindowCandidateValidation.ConstraintMargin m:d.constraints) margins.add(String.format(Locale.ROOT,"%d,%s,%s,%.17g,%.17g,%.17g",55+m.constraint.t1,m.constraint.name,m.constraint.cmp,m.continuousMargin,m.replayMargin,m.assumedMargin));
        Files.write(OUT.resolve(label+"-pass"+pass+"-margins.csv"),margins,StandardCharsets.UTF_8);
        if(DETAIL || J14) {
            List<String> inertia=new ArrayList<>();inertia.add("tick,axis,assumed_zero,assumed_velocity,actual_velocity,actual_zero,assumption_satisfied");
            for(WindowCandidateValidation.InertiaCheck c:d.inertiaChecks)inertia.add(String.format(Locale.ROOT,"%d,%d,%s,%.17g,%.17g,%s,%s",55+c.tick,c.axis,c.assumedZero,c.assumedVelocity,c.continuousVelocity,c.continuousZero,c.assumptionSatisfied));
            Files.write(OUT.resolve(label+"-pass"+pass+"-inertia.csv"),inertia,StandardCharsets.UTF_8);
        }
        List<String> ticks = new ArrayList<>();ticks.add("tick,yaw,continuous_x,continuous_z,replay_x,replay_z,replay_vx,replay_vz");
        for (int t=0;t<d.continuousFacings.length;t++) ticks.add(String.format(Locale.ROOT,"%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",55+t,d.continuousFacings[t],d.continuousPath.posX[t],d.continuousPath.posZ[t],d.replayPath.posX[t],d.replayPath.posZ[t],d.replayPath.velX[t],d.replayPath.velZ[t]));
        Files.write(OUT.resolve(label+"-pass"+pass+"-path.csv"),ticks,StandardCharsets.UTF_8);
    }
}
