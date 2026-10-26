package de.legoshi.parkourcalc.forge.core.replay;

import com.google.gson.JsonObject;
import de.legoshi.parkourcalc.core.anglesolver.Medium;
import de.legoshi.parkourcalc.core.anglesolver.Slipperiness;
import de.legoshi.parkourcalc.core.anglesolver.solver.*;
import de.legoshi.parkourcalc.core.replay.DeclaredContacts;
import de.legoshi.parkourcalc.core.replay.IpoptReplay;
import de.legoshi.parkourcalc.core.replay.StaticScene;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.sim.AABB;
import de.legoshi.parkourcalc.core.sim.StartResumeState;
import de.legoshi.parkourcalc.core.sim.TickState;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import de.legoshi.parkourcalc.core.ui.InputRow;
import de.legoshi.parkourcalc.core.ui.MovementEffect;
import de.legoshi.parkourcalc.forge.core.sim.PlayerSprintMachine;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class LocalReplayOracle implements IpoptReplay.Oracle {
    private final JsonObject problem;
    private final StaticScene scene;
    private final ExactJumpModel exact = ExactJumpModel.forMcVersion("1.8.9");
    private java.util.Map<String, Double> violations = java.util.Collections.emptyMap();

    public LocalReplayOracle(JsonObject problem, StaticScene scene) {
        this.problem = problem;
        this.scene = scene;
    }

    @Override
    public String backend() { return scene == null ? "local-1.8.9-declared-contacts" : "local-1.8.9-static-scene"; }

    @Override
    public java.util.Map<String, Double> violations() { return violations; }

    @Override
    public List<TickState> replay(SaveFile file, boolean fresh) {
        if (!"1.8.9".equals(file.mcVersion)) throw new IllegalArgumentException("Local replay currently supports Minecraft 1.8.9");
        InputData inputs = new InputData();
        SaveIO.applyRowsTo(file, inputs);
        Vec3dCore position = SaveIO.posOf(file.start), velocity = SaveIO.velOf(file.start);
        AABB[] body = {scene == null ? null : scene.startBox(position)};
        StartResumeState resume = SaveIO.resumeOf(file.start);
        if (resume != null && resume.stuckMultiplier != null) throw new IllegalArgumentException("Web resume is not supported by this replay profile");
        boolean ground = resume != null ? resume.onGround : scene != null ? velocity.y <= 0 && scene.supported(body[0]) : groundAt(file, 0);
        boolean wall = resume != null && resume.wallContact;
        boolean incomingSprint = resume != null && resume.sprinting;
        if (resume != null && !Float.isNaN(resume.airSprintFactor)) {
            if (resume.airSprintFactor != Constants.AIR_SPEED_F && resume.airSprintFactor != Constants.AIR_SPEED_NO_SPRINT_F)
                throw new IllegalArgumentException("Unsupported resumed air movement factor");
            incomingSprint = resume.airSprintFactor == Constants.AIR_SPEED_F;
        }
        int cooldown = resume == null ? 0 : resume.jumpCooldown;
        PlayerSprintMachine.State sprint = sprintState(resume);
        float yaw = file.start.yaw;
        DeclaredContacts contacts = scene == null ? new DeclaredContacts(problem) : null;
        violations = contacts == null ? new java.util.LinkedHashMap<>() : contacts.violations;
        List<TickState> states = new ArrayList<>();
        states.add(state(position, velocity, ground, wall, yaw, sprint.isSprinting, false, Float.NaN, Float.NaN, null, Double.NaN));
        for (int tick = 0; tick < inputs.size(); tick++) {
            InputRow row = inputs.get(tick);
            validateRow(row);
            if (row.getYaw() != null) yaw = row.isYawLocked() ? row.getYaw() : yaw + row.getYaw();
            if (cooldown > 0) cooldown--;
            boolean jump = row.isKeyActive(InputRow.Key.JUMP);
            boolean sneak = row.isKeyActive(InputRow.Key.SNEAK);
            PlayerSprintMachine.Outputs input = PlayerSprintMachine.tick(new PlayerSprintMachine.Inputs(
                    row.isKeyActive(InputRow.Key.W), row.isKeyActive(InputRow.Key.S), row.isKeyActive(InputRow.Key.A), row.isKeyActive(InputRow.Key.D),
                    jump, sneak, row.isKeyActive(InputRow.Key.SPRINT), ground, false, false, wall, false, false, 20), sprint);
            boolean jumpFires = jump && ground && cooldown == 0;
            if (jumpFires) cooldown = 10;
            if (!jump) cooldown = 0;
            boolean ladder = scene != null ? scene.ladder(position) : contacts.ladder(tick);
            float slip = scene != null ? scene.slipperiness(position) : slipperiness(file, tick);
            float forward = input.moveForward * 0.98F, strafe = input.moveStrafe * 0.98F;
            JumpPhysicsInputs one = new JumpPhysicsInputs(1);
            one.startPos = position;
            one.initialVelocity = velocity;
            one.startYaw = yaw;
            one.yawLockedPerTick = new boolean[]{true};
            one.jumpPerTick = new boolean[]{jumpFires};
            one.slipPerTick = new double[]{ground ? slip : Double.NaN};
            one.surfacePerTick = new SurfaceKind[]{ladder ? SurfaceKind.LADDER : SurfaceKind.NORMAL};
            one.sneakPerTick = new boolean[]{sneak};
            one.sprintPerTick = new boolean[]{input.next.isSprinting};
            one.incomingSprint = incomingSprint;
            one.speedAmplifier = new int[]{row.getSpeedAmplifier()};
            one.slownessAmplifier = new int[]{row.getSlownessAmplifier()};
            one.forwardInputPerTick = new float[]{forward};
            one.strafeInputPerTick = new float[]{strafe};
            final int at = tick;
            final boolean wasGrounded = ground;
            StaticScene.Contact[] hit = {null};
            ForwardPath path = exact.forward(one, new double[]{yaw}, (ignored, start, intended) -> {
                if (jumpFires && row.getJumpBoostAmplifier() > 0)
                    intended = new Vec3dCore(intended.x, (double) 0.42F + (double) ((float) row.getJumpBoostAmplifier() * 0.1F), intended.z);
                if (scene == null) return contacts.move(at, start, intended, wasGrounded);
                hit[0] = scene.move(body[0], intended, wasGrounded, sneak);
                body[0] = hit[0].box;
                return new ExactJumpModel.Movement(hit[0].position, hit[0].velocity);
            });
            position = new Vec3dCore(path.posX[1], path.posY[1], path.posZ[1]);
            velocity = new Vec3dCore(path.velX[1], path.velY[1], path.velZ[1]);
            ground = scene == null ? contacts.onGround : hit[0].onGround;
            wall = scene == null ? contacts.horizontalCollision : hit[0].horizontalCollision;
            if (scene != null && tick >= problem.getAsJsonObject("source").get("startTick").getAsInt()) {
                int state = tick + 1 - problem.getAsJsonObject("source").get("startTick").getAsInt();
                boolean expectedX = false, expectedZ = false;
                if (problem.has("contacts")) for (com.google.gson.JsonElement e : problem.getAsJsonArray("contacts"))
                    if (e.getAsJsonObject().get("state").getAsInt() == state) {
                        if (e.getAsJsonObject().get("axis").getAsInt() == 0) expectedX = true;
                        else expectedZ = true;
                    }
                if (hit[0].blockedX != expectedX) violations.put("horizontalContactSchedule.X@" + state, 1.0);
                if (hit[0].blockedZ != expectedZ) violations.put("horizontalContactSchedule.Z@" + state, 1.0);
            }
            sprint = input.next;
            incomingSprint = sprint.isSprinting;
            states.add(state(position, velocity, ground, wall, yaw, sprint.isSprinting, sneak, forward, strafe,
                    ladder ? Medium.LADDER : Medium.NONE, one.contactAt(0) ? slip : Double.NaN));
        }
        boolean finalLadder = scene == null ? contacts.ladder(inputs.size()) : scene.ladder(position);
        states.add(state(position, velocity, ground, wall, yaw, sprint.isSprinting, false, 0, 0,
                finalLadder ? Medium.LADDER : Medium.NONE, ground ? scene == null ? slipperiness(file, inputs.size()) : scene.slipperiness(position) : Double.NaN));
        return states;
    }

    private static void validateRow(InputRow row) {
        if (row.isTeleportEnabled() || row.isKeyActive(InputRow.Key.LEFT_CLICK) || row.isKeyActive(InputRow.Key.RIGHT_CLICK))
            throw new IllegalArgumentException("Local replay requires static blocks and no teleports or clicks");
        for (MovementEffect effect : MovementEffect.all())
            if (row.getEffectLevel(effect) != 0 && effect != MovementEffect.SPEED && effect != MovementEffect.SLOWNESS && effect != MovementEffect.JUMP_BOOST)
                throw new IllegalArgumentException("Unsupported replay movement effect: " + effect);
    }

    private static PlayerSprintMachine.State sprintState(StartResumeState resume) {
        if (resume == null) return PlayerSprintMachine.State.initial();
        boolean sneak = resume.heldLastTick.contains(InputRow.Key.SNEAK);
        float forward = (resume.heldLastTick.contains(InputRow.Key.W) ? 1F : 0F) - (resume.heldLastTick.contains(InputRow.Key.S) ? 1F : 0F);
        if (sneak) forward *= 0.3F;
        return new PlayerSprintMachine.State(sneak, forward, resume.sprintWindow, resume.sprintTicksLeft, resume.sprinting);
    }

    private static boolean groundAt(SaveFile file, int tick) { return slipperiness(file, tick) < 1; }

    private static float slipperiness(SaveFile file, int tick) {
        String slip = file.angleSolver == null || file.angleSolver.defaultSlipperiness == null ? "AIR" : file.angleSolver.defaultSlipperiness;
        if (file.angleSolver != null && file.angleSolver.ticks != null) for (SaveFile.Tick t : file.angleSolver.ticks)
            if (t.tick == tick && t.override != null && t.override.slipperiness != null) slip = t.override.slipperiness;
        return (float) Slipperiness.valueOf(slip).slip;
    }

    private static TickState state(Vec3dCore pos, Vec3dCore vel, boolean ground, boolean wall, float yaw, boolean sprint,
                                   boolean sneak, float forward, float strafe, Medium medium, double slip) {
        return new TickState(pos, ground, sneak, wall, yaw, Collections.emptyList(), vel, false, Double.NaN, sprint, forward, strafe, medium, slip, 0);
    }
}
