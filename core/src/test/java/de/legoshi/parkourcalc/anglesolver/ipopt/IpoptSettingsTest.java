package de.legoshi.parkourcalc.anglesolver.ipopt;

import de.legoshi.parkourcalc.core.anglesolver.AngleSolverState;
import de.legoshi.parkourcalc.core.save.SaveFile;
import de.legoshi.parkourcalc.core.save.SaveIO;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;
import de.legoshi.parkourcalc.core.ui.InputData;
import org.junit.Test;
import static org.junit.Assert.*;

public class IpoptSettingsTest {
    @Test public void settingsSurviveSaveLoadAndReset() {
        AngleSolverState state = new AngleSolverState();
        state.setBackend(AngleSolverState.Backend.IPOPT_LOCAL);
        state.setIpoptInertia(false);
        state.setIpoptRecordedWalls(false);
        state.setBudgetMs(5000);
        SaveFile saved = SaveIO.buildUndoSnapshot(new InputData(), Vec3dCore.ZERO, Vec3dCore.ZERO,
                0, 0, null, state);
        SaveIO.applyAngleSolverTo(SaveIO.parseSafe(SaveIO.saveJson(saved)), state);
        assertEquals(AngleSolverState.Backend.IPOPT_LOCAL, state.getBackend());
        assertFalse(state.isIpoptInertia());
        assertFalse(state.isIpoptRecordedWalls());
        assertEquals(5000, state.getBudgetMs());
        state.reset();
        assertEquals(AngleSolverState.Backend.PIPELINE, state.getBackend());
        assertTrue(state.isIpoptInertia());
        assertTrue(state.isIpoptRecordedWalls());
    }

    @Test public void legacyAndUnknownBackendUsePipeline() {
        SaveFile saved = new SaveFile();
        saved.angleSolver = new SaveFile.AngleSolver();
        AngleSolverState state = new AngleSolverState();
        state.setBackend(AngleSolverState.Backend.IPOPT_LOCAL);
        SaveIO.applyAngleSolverTo(saved, state);
        assertEquals(AngleSolverState.Backend.PIPELINE, state.getBackend());
        saved.angleSolver.backend = "future_backend";
        SaveIO.applyAngleSolverTo(saved, state);
        assertEquals(AngleSolverState.Backend.PIPELINE, state.getBackend());
    }
}
