# Stepping terminology and cases

User definitions, 2026-10-29. These are the orchestration vocabulary and case descriptions. They supersede the earlier broad blip/grinding labels and the proposal to enumerate Minecraft's internal step branches as the primary movement types.

## Tick terminology

- **Landing tick L:** the last air position tick before the lower block's landing/contact would resolve.
- **Hit tick H:** the following position tick, H = L + 1. A stepping case can change the resulting height on H.
- **Jump tick J:** the position tick after applying the jump impulse. Usually feet Y is about 0.42 above the jumping block; step/ceiling cases can change that result.
- **Jump input:** the input applying the impulse. With the existing pre-input indexing, input t produces position state t+1, so J = t+1.

The position label does not change the input's movement constants. The input producing a normal jump tick begins grounded; the input producing a blip hit tick begins airborne.

Terminology conflicts to resolve in adapters/docs:

- Internal `JumpPhysicsInputs.jumpTick` names an impulse-applying input, not the user's resulting jump position tick. Preserve its API meaning and translate explicitly.
- Internal route event `landing` commonly denotes the resolved contact/end state H; the standard pre-landing support check is on L. Use explicit L/H names in the new case layer.
- Existing `hit`/`hitInputs` masks generally mean horizontal wall collisions, especially on ladders. Do not confuse them with the user's landing-related hit tick.
- The previous glossary's broad `Blip` included airborne/unsupported outcomes. The user distinguishes blip from blip-up by the landing/higher-height relation and hit result.
- The previous glossary used grinding for repeated jump cancels, including stair grinding. Here **grind** means the specified outside-to-under low-ceiling pattern. Use repeated boosts/cancels for the broader process.
- Jump cancel remains an umbrella description, not a synonym for only slab boost or only grind. The mirrored external reference retains its historical broad wording; the project definitions here are authoritative for this work.

## Accepted cases

Let a be the lower block's height, b the higher block's height, yL the landing-tick feet height, and yFreeH the next feet height from ordinary air Y movement without a Y obstacle. The lower block's usual landing-tick X/Z constraint remains present where landing is required.

### Normal stepping

Grounded movement rises by at most 0.6. All inputs in this movement are run inputs; there is no invented air tick or jump impulse. Use the appropriate before/after block constraints.

### Slab boost / stair boost

The position before the jump tick is on the lower level. The jump tick is on the higher level, with the specified rise approximately 0.42 through 0.6. The jump impulse and usual jump horizontal movement apply, while stepping changes the Y outcome. One air input follows the jump, producing the ordinary two-input airtime/cycle before subsequent movement.

### Blip

The ordinary air schedule lands on the lower level, and the resulting hit tick steps to the higher level. Require 0 <= b-yL <= 0.6. The landing tick satisfies the lower landing constraint; the hit tick additionally satisfies the higher block's X/Z condition and yH=b. The movement producing H is still air movement. Normal grounded movement/jumping follows from the higher level.

### Blip-up

Require yL>b and yFreeH<a, so the lower landing actually happens on this input rather than leaving another air tick. The hit tick moves into the higher-block region but retains the landing-tick height: yH=yL>b. It is grounded for subsequent movement despite being above the higher surface.

The blip-up case ends at that hit state. A jump taken from it starts at the elevated height; its resulting position is the next jump tick. With no jump, use the normal run-off continuation: one ground-regime input, then air as needed until another vertical landing/step. Do not extend a special blip-up mode across that continuation or invent a support at yH.

### Grind

The tick before the jump tick is outside a low ceiling; the jump tick is under it. The available head clearance is less than the nominal 0.42 jump rise. This stepping/ceiling combination cancels the rise entirely, so the jump tick has the same feet Y as the preceding tick, followed by one air input and ordinary continuation. With a flat floor that following input lands, giving the same two-input cycle as a stair boost. Different floor geometry can produce a longer ordinary continuation.

This differs from an ordinary ceiling hit: a ceiling hit can leave a positive rise up to the ceiling, and can occur while already under it or later in the jump. The grind's specified entry geometry and zero-rise result must be retained.

## Additional cases proposed for discussion

The following are observed shared-resolver outcomes, not newly accepted names or types. Descriptive probe labels are not claims about community terminology.

1. **Jump step-over without a ceiling.** Jump toward a raised obstruction and clear its corner or pass completely over it during the same input. The rise can be fully cancelled even though there is no ceiling, so this is outside the specified grind and outside a slab boost ending on the higher level. Probe `jump_step_over`: feet Y 1 -> 1 on a jump input, clears the corner and ends at the original level with an airborne ground flag even though a floor is present.
2. **Partial-rise jump cancellation.** A higher intermediate obstruction triggers stepping, but the input finishes on a lower destination surface. Probe `partial_rise_cancel`: feet Y 1 -> 1.25 despite the 0.42 jump impulse, ending airborne above a supported surface. This is below the specified slab-boost rise and above the zero-rise grind outcome. A broader jump-cancel profile could include it and the preceding zero-rise case.
3. **Landing step-over ending below the cleared obstacle's top.** Probe `landing_step_over`: lower floor 1, higher obstruction 1.5, yL=1.2 and yFreeH=0.8. The input clears the obstruction completely and ends at yH=1.2, grounded but unsupported. It is not blip (H is not on top of the higher block), and not blip-up (yL is below that block's top). Its subsequent jump/run-off options resemble the blip-up continuation, but its entry geometry differs.

Ordinary ceiling hits remain part of the movement/contact catalogue. They can stay shared ceiling events or become a separate reusable type; this decision does not require another stepping name. Repeated steps, axis directions, multiple collision boxes, and wall/ladder/ceiling combinations are variants/compositions unless they change the case outcome requirements above. Exact equality/collision bookkeeping is a boundary issue, not another movement type.

The nine shared-resolver probes and their geometry are saved in experiments/stepping-mechanics. They establish collision examples; they are not fresh full-route IPOPT or game-replay witnesses.
