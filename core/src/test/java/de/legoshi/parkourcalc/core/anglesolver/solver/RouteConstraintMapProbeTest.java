package de.legoshi.parkourcalc.core.anglesolver.solver;

import de.legoshi.parkourcalc.SlowSolverTests;
import de.legoshi.parkourcalc.anglesolver.harness.Fixtures;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverEngine;
import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.zip.GZIPInputStream;

@Category(SlowSolverTests.class)
public class RouteConstraintMapProbeTest {
    @Test public void exportCompiledRoute() throws Exception {
        org.junit.Assume.assumeTrue(Boolean.getBoolean("pkc.routeConstraintMapProbe"));
        Path root=Paths.get("/Users/arvindmurty/CS/ParkourCalculatorMod");
        Path out=root.resolve("docs/research/solver-traces/jump30-50-route-map-2026-10-24");Files.createDirectories(out);
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        try(InputStream in=new GZIPInputStream(Files.newInputStream(root.resolve("docs/research/solver-traces/jump39-baseline-handoff-2026-10-24/real11-input.json.gz")))) {
            byte[] buffer=new byte[8192];int count;while((count=in.read(buffer))!=-1)bytes.write(buffer,0,count);
        }
        SaveFile file=SaveIO.parseSafe(new String(bytes.toByteArray(),StandardCharsets.UTF_8));
        InputData input=new InputData();SaveIO.applyRowsTo(file,input);
        AngleSolverState state=new AngleSolverState();SaveIO.applyAngleSolverTo(file,state);state.setStartTick(0);
        JumpSpec spec=new AngleSolverEngine(state,Fixtures.buildBoxes(file),input,t->{},ExactJumpModel.forMcVersion(file.mcVersion)).debugBuildSpec();
        JumpPhysicsInputs sc=spec.asScenario();
        Method method=LongRunSolver.class.getDeclaredMethod("jumpBoundaries",JumpPhysicsInputs.class);method.setAccessible(true);
        int[] bounds=(int[])method.invoke(null,sc);
        List<String> jumps=new ArrayList<>();jumps.add("jump,start_tick,end_state,input_count");
        for(int j=0;j<bounds.length-1;j++)jumps.add((j+1)+","+bounds[j]+","+bounds[j+1]+","+(bounds[j+1]-bounds[j]));
        Files.write(out.resolve("jump-boundaries.csv"),jumps,StandardCharsets.UTF_8);
        List<String> constraints=new ArrayList<>();constraints.add("index,mode,t1,t2,op,comparison,rhs,name");
        for(int i=0;i<spec.constraints.size();i++) {
            JumpConstraint c=spec.constraints.get(i);
            constraints.add(i+","+c.mode+","+c.t1+","+(c.t2==null?"":c.t2)+","+c.op+","+c.cmp+","+c.rhs+","+c.name);
        }
        Files.write(out.resolve("compiled-constraints.csv"),constraints,StandardCharsets.UTF_8);
        List<String> physics=new ArrayList<>();physics.add("tick,slipperiness,jump,sprint,forward,strafe,surface,sneak,speed_amplifier");
        for(int t=0;t<sc.numTicks;t++)physics.add(t+","+sc.slipAt(t)+","+sc.jumpAt(t)+","+sc.sprintAt(t)+","+sc.forwardAt(t)+","+sc.strafeInputAt(t)+","+sc.surfaceAt(t)+","+sc.sneakAt(t)+","+sc.speedAmplifierAt(t));
        Files.write(out.resolve("physics-inputs.csv"),physics,StandardCharsets.UTF_8);
        org.junit.Assert.assertEquals(808,sc.numTicks);org.junit.Assert.assertEquals(76,bounds.length-1);org.junit.Assert.assertEquals(636,spec.constraints.size());
    }
}
