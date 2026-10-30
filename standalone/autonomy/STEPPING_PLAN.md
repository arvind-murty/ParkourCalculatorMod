# Stepping plan grounded in the legacy movement code

Planning checkpoint, 2026-10-29. Target: the current static inert-box Minecraft 1.8.9 continuous model. The Forge simulator delegates movement to Minecraft; the shared `StaticScene` resolves the corresponding legacy collisions. Modern-version collision rules, fluids, moving blocks and special block callbacks are separate work.

The earlier two-mode migration plan was incomplete. Existing grounded-step and stair-boost profiles do not cover landing steps/blips or steps that finish without support. This plan includes those cases. No production stepping implementation changed during this investigation.

## What exists now

| Layer | Existing support | Limit |
| --- | --- | --- |
| Minecraft/mod simulator | Inherited step assist, including two step candidates and landing-triggered attempts | The simulator is not our route/schedule generator |
| Shared `StaticScene.move` | Ordinary Y-X-Z clipping, step eligibility, two raised alternatives, downward return, path competition, actual collision/ground/carry result | It is already the acceptance oracle; expose additional branch metadata rather than copy it |
| Automatic support graph | `stair_jump_profile`: two-input jump-start step cancellation, normally a +0.5 slab/stair boost; all four approach faces; ordinary jump remains an alternative | Starts from a grounded support at rest Y carry; allowed rise is above `0.42F` through `0.6F`; no automatic general landing-step or blip generation |
| Supplied contact/route contract | One-input non-jump `step`, finite step contact and simultaneous retained X/Z hits | Requires a selected final top surface, starts grounded in the current route adapter |
| Scalar Y helper | `step: true` sets the final plane and zeroes Y carry, accepting a positive rise through widened `0.6F` | Infers ground from negative incoming Y, rejects zero-net-rise steps and cannot represent every returned-Y/collision outcome |
| Typed movement generation | `normal`, `ladder_climb`, `ladder_fall` | No registered `stepping` type yet; stair proposals bypass normal dispatch |

Existing evidence includes a five-input staircase (two two-input boosts plus one run), one-input grounded stepping, ceiling-blocked steps, and a fresh IPOPT case with a step and retained X/Z contacts on the same input. These are specific accepted families, not full stepping coverage.

## Verified Minecraft branch

Checked against the cached mapped Minecraft/Forge 1.8.9 Entity movement bytecode and decompiled step section, the shared `StaticScene`, and executable probes in [experiments/stepping-mechanics](experiments/stepping-mechanics/README.md). The decompiler's mapped method name is `move`; it corresponds to legacy `moveEntity`.

1. Apply grounded Sneak edge prevention before the collision attempt.
2. Try ordinary movement by clipping Y, X, then Z. Preserve that result as a competing path.
3. A step attempt needs a positive configured step height, a horizontal clip in that ordinary path, and either incoming `onGround` or an actual downward Y clip on this input. Falling alone is insufficient. A jump-start input can qualify because it began grounded.
4. Reset to the original pre-movement box, not the ordinarily landed/moved box. The default upward allowance is the exact widened float `0.6F`.
5. Candidate A clips the upward allowance using the horizontal swept box. Candidate B clips it using the original box. Each then clips X and Z at its raised height.
6. Choose the candidate with greater squared X/Z progress; a tie chooses B. Move that candidate downward by its own upward allowance, clipped by the actual scene.
7. Adopt it only if its squared horizontal progress strictly exceeds the ordinary path. An ordinary/step tie retains the ordinary path.
8. Compute collision flags and velocity from the selected branch's returned movement values. Actual retained X/Z clipping affects momentum/sprint; discarded ordinary trigger hits do not.

Ceilings can reduce a candidate's upward allowance without necessarily blocking the step. Other ceilings block it. Actual collision boxes and the two path choices decide; neither a blanket no-ceiling rule nor an endpoint-only height check is sufficient. X/Z order and exact comparisons must remain shared with the existing resolver.

## Required movement cases

One physical step event occupies one input. The following modes describe why it is eligible, not separate Minecraft movement implementations.

| Mode | Entry/trigger | Typical continuation |
| --- | --- | --- |
| Grounded step | Starts grounded, no jump impulse; ordinary path is horizontally blocked | Usually ends grounded; may step again or jump on the next eligible input |
| Jump-start cancellation | Starts grounded and requests a jump; ordinary path is blocked after its initial Y movement | Usually ends not grounded even if the feet reach a support; recover/land, then jump again |
| Landing step / blip | Starts airborne, ordinary downward movement actually clips a floor, then X/Z is blocked | Can end higher, or remain at original Y with an unsupported `onGround` flag; subsequent movement uses that actual flag |

Repeated slab/stair grinding is composition of the jump-start cancellation and its recovery. A usual +0.5 boost has a two-input cycle, but that is not a universal step duration. Zero-net-rise and unsupported outcomes can occur in more than one mode. Do not assume every step ends on a higher block or that every jump-cancel lands on the next input.

The probes demonstrate:

| Probe | Feet Y before -> after | Ground flag | Actual support | Y carry before gravity |
| --- | --- | --- | --- | --- |
| Grounded step | 1 -> 1.5 | true | yes | 0 |
| Jump-start step | 1 -> 1.5 | false | yes | 0 |
| Landing step | 1.2 -> 1.5 | true | yes | 0 |
| Unsupported blip | 1.2 -> 1.2 | true | no | 0 |
| Unsupported jump cancel | 1 -> 1 | false | no | 0 |
| Return-leg equality case | 1.25 -> 1.5 | false | yes | incoming negative Y retained |

These are shared-resolver movement probes, not full route IPOPT or game-replay witnesses. Exact inputs and output are saved beside the probe.

## The representation must separate position, movement and ground

The selected step first moves up, then horizontally, then down. Its returned Y movement used by collision bookkeeping is the downward leg, not the net change in feet Y. Vertical collision is determined by comparing incoming requested Y with that returned leg. The ground flag additionally requires negative requested Y. On inert normal blocks, vertical collision zeroes Y carry before gravity; if those movement values are exactly equal, Y can be retained even while the feet rose. Post-movement ladder occupancy plus an actual retained horizontal clip can subsequently establish climb carry through the existing shared rule.

Therefore:

- Carry incoming Y/VY, `onGround`, jump/input history and complete X/Z/VXZ history. Do not replace them with a support top and rest velocity.
- Physical support and the ground flag are distinct. An open result may be grounded yet unsupported. Its next jump must not acquire an invented underfoot support requirement.
- A normal landing floor that triggers step eligibility is distinct from the optional final down-leg contact surface. The two can be different boxes or parts of one stair block. There may be no final down-leg contact at all.
- Describe actual collision volumes, not slab/stair block names. Partial boxes, stair orientation, carpets, beds and arbitrary low ledges are geometry cases. Preserve the existing surface/callback scope; do not infer normal friction or inert callbacks for unsupported special blocks.

## Proposed type and collision contract

Register `stepping` in the existing generator interface. Represent the physical collision input explicitly. Keep the existing two-input stair profile as a compatible composite of a step input and its normal recovery/landing input.

A generated step hypothesis needs:

- Its input index, mode, jump/Sneak choice and entry state requirements.
- The ordinary-path horizontal trigger and, for landing eligibility, its real downward floor clip.
- The selected step alternative A/B, its upward-clearance branch and upward allowance.
- The selected final X/Z contacts, plus a downward contact surface or explicit unobstructed full return.
- Derived final feet Y, returned Y leg, vertical collision, outgoing Y carry and `onGround`; physical support is optional and separate.
- Relative contact/clearance anchors for every stage, retimed with inserted run inputs. Include existing `jumpInputs` in generic retiming as well.

Extend the shared collision result with the missing step-stage metadata. Derive/check it with the same resolver. Extend the scalar step-event contract to replay the chosen outcome exactly, including zero net rise, unsupported results and returned-leg equality. Do not accept an arbitrary supplied ground flag as proof. Preserve legacy `step: true` contacts for saved cases where their restricted semantics apply.

A one-input step outcome is an open movement state; a landing block is not mandatory. The route orchestrator then expands normal movement, another step, a jump or ladder movement from its actual carried state. This generalizes open composition beyond the current airborne-only/real-support distinction. Existing support-graph shortcuts remain useful for ordinary grounded steps and the standard two-input boost.

## Constraint and discovery work

For each candidate input, consider steps when the incoming state is grounded, or when a falling normal phase can actually clip a floor. Generate the relevant finite horizontal triggers and nearby low obstacles from the route scene. For a landing step, use the current simulated original Y and the potential lower floor; do not measure every rise from a fixed source support. Enumerate the corresponding up/down contact or clearance choices from actual boxes.

Each selected structure needs ordinary-trigger, eligibility, raised-path clearance, finite face/down-leg conditions and correct timing/avoidance around the event. Distinguish rejected ordinary trigger collisions from actual retained step-path hits. A raised path can pass over an obstacle even when final feet Y returns to its original height, so ordinary endpoint-height blockage rules alone are not valid step clearance rules.

A/B selection and competition against the ordinary result are discrete scene decisions. Preserve exact tie behavior in acceptance. Where the NLP represents competing-path progress, use smooth squared-progress comparisons within a fixed clipping structure. Reuse shared branch diagnostics to derive/refine those structures; do not put a second min/max collision resolver inside the numerical objective. A candidate that loses the scene branch comparison remains unknown and can lead to another step structure or ordinary/avoidance continuation.

The support graph currently applies an ordinary maximum-jump-height gate before generating stair options, and its flight bounds assume the current profile family. Revisit those gates for landing steps and composite continuations: use bounds valid for the actual generated Y/contact history, durations and step outcomes. An ordinary jump-height or radius bound must not reject a route whose additional height or airtime comes from a valid step. Updating existing bounds for soundness is required type integration, separate from later aggressive pruning.

Reoptimize all facings and incoming X/Z momentum from the original route initial state. Enumerating a discrete collision structure must not pin a previous optimizer endpoint. Keep time before objective and retain explicit work/unknown reporting. Route-specific pruning remains deferred.

## Implementation sequence

1. **Migrate the working cases.** Register the stepping generator, route current stair and supplied step profiles through it, preserve one-input versus two-input control histories, legacy saves, competing ordinary jumps and run retiming. This stage alone is not full stepping completion.
2. **Generalize the physical event.** Add shared step-stage metadata and exact scalar outcome support. Cover actual trigger/retained contacts, both step candidates, ceilings, no-down-contact/zero-net-rise results and collision-derived ground/carry.
3. **Discover landing steps/blips.** Extend normal airborne schedule generation to floor-triggered step choices at any eligible input, using real incoming Y/VY and finite geometry. Permit open grounded-but-unsupported outcomes and continuation without a fictitious landing support.
4. **Compose the family.** Exercise repeated grounded steps, grinding, immediate eligible post-blip jumps, delayed jumps, and normal/ladder boundaries. Keep jump cooldown and release/repress semantics in the shared input compiler. Do not force one recovery tick for every structure.
5. **Validate before closing.** Run focused generator/physics tests and fresh full-route IPOPT plus scalar/shared-scene acceptance. Revalidate existing stair/step witnesses and ladder cases. Run the full orchestration gate; rerun numerical/shared-source suites when their implementation changes. Commit each feature separately in the isolated worktree.

## Completion tests

- Existing grounded step, +0.5 two-input boost, five-input staircase and simultaneous retained X/Z contact witnesses.
- Height boundaries below/at/above widened `0.6F`; jump-start cases that clear a low obstacle ordinarily; rising airborne movement without step eligibility.
- Landing step with a real ordinary floor trigger; identical geometry without that floor must not establish eligibility.
- Supported step, unsupported blip, unsupported jump cancel, zero-net-rise/full-return and returned-leg equality outcomes; independent checks of feet Y, Y carry, ground flag and support.
- Candidate A and B, exact path ties, ordinary-path wins, partial headroom and fully blocked headroom, all X/Z approach directions, finite multi-box stairs and absent final support.
- Before/after timing, extra/missing retained wall contacts, Y cancellation, sprint loss and carried air factor, Sneak edge prevention, run retiming and jump cooldown across short cycles.
- An immediately eligible post-blip jump without inventing a support block; repeated boosts and normal/ladder composition from original state.

Success remains a valid continuous solve with the correct complete movement/input/geometry constraints and independent acceptance. Replay/export stays with the other layer. Water and performance pruning remain separate work.
