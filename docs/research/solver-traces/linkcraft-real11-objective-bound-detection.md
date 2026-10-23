# Direct objective-bound detection

`ObjectiveBounds.directCap` now finds the tightest absolute X/Z wall at the scalar positional objective's state that caps the requested sense: MAX with LE, MIN with GE, or EQ for either sense. Opposite-sided, different-axis, different-tick and paired rows do not match. Custom-direction and motion objectives are deliberately outside this simple detector's scope. A null result means no recognized direct cap, not a proof of stability.

The helper accepts either a JumpSpec or an Objective plus constraints, so future whole-jump window selection can use it before building a solve. LongRun logs `LRS-PREFLIGHT` before primary/alternate objective calls, including the constraining row and RHS. This commit implements detection and diagnostic reporting; it does not select a different window or objective.

The first initial-window preflight correctly reports `Z/MAX@68 directCap=true constraint=Z@68 LE rhs=-1092.699999988079`, while alternate Z/MIN has no matching cap. Six unit tests plus the focused full fixed-margin 7-ladder probe pass. The capture still returns a miss at tick 90 before jump 14, as expected for a diagnostic-only change. All work used the original EPS2=1e-14. No full suite ran.

Evidence: [focused log](objective-bound-detection-2026-10-23/focused-tests.log), [preflight observations](objective-bound-detection-2026-10-23/preflight-observations.txt), [full trace](objective-bound-detection-2026-10-23/full-fixed-seven-trace.txt). This commit is kept separate from the subsequent smoothing-constant experiment.
